// Offline test harness: measures channel levels, effect loudness, CPU and renders a demo.
#include "../Source/PilotEngine.h"
#include "../Source/Lang.h"
#include "../Source/TeletypeHost.h"
extern "C"
{
#include "tt_host.h"
}
#include <thread>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <chrono>
#include <cstdio>

using namespace sf;

static double SR = 48000.0;
static const int BLOCK = 256;

struct Stats { float peak = 0; double sumSq = 0; long n = 0; bool nan = false; };

static void renderSeconds (PilotEngine& e, double secs, Stats& st, std::vector<float>* outL = nullptr, std::vector<float>* outR = nullptr,
                           const std::function<void (double)>& onBlock = {})
{
    std::vector<float> L (BLOCK), R (BLOCK);
    const long total = (long) (secs * SR);
    for (long done = 0; done < total; done += BLOCK)
    {
        if (onBlock) onBlock (done / SR);
        e.process (L.data(), R.data(), BLOCK, -1.0);
        for (int i = 0; i < BLOCK; ++i)
        {
            if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) st.nan = true;
            st.peak = std::max (st.peak, std::max (std::fabs (L[i]), std::fabs (R[i])));
            st.sumSq += 0.5 * ((double) L[i] * L[i] + (double) R[i] * R[i]);
            ++st.n;
        }
        if (outL) { outL->insert (outL->end(), L.begin(), L.end()); outR->insert (outR->end(), R.begin(), R.end()); }
    }
}

static double rmsDb (const Stats& s) { return 10.0 * std::log10 (std::max (1e-12, s.sumSq / std::max (1L, s.n))); }

static void neutralFx (PilotEngine& e)
{
    for (int i = 0; i < kNumFx; ++i) { e.fxW[i] = 0.0f; e.fxV[i] = kFxInfo[i].defV; }
    e.fxW[FxEqu] = 8.0f / 15; e.fxV[FxEqu] = 8.0f / 15; e.fxW[FxVol] = 12.0f / 15; e.fxW[FxCom] = 0.0f;
}

int main (int argc, char** argv)
{
    Lang::libraryPathOverride = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pilot-render-library.xml").getFullPathName().toStdString();
    {
        auto ttDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pilot-render-teletype");
        ttDir.deleteRecursively();
        TeletypeHost::sceneDirOverride = ttDir.getFullPathName().toStdString();
    }

    const std::string mode = argc > 1 ? argv[1] : "all";
    if (const char* e = std::getenv ("PILOT_SR")) SR = std::atof (e);
    const std::string extra = std::getenv ("PILOT_EXTRA") ? std::getenv ("PILOT_EXTRA") : "";

    if (mode == "limiter")
    {
        // brutal overload: everything playing, distortion, +12 dB volume, +18 dB limiter drive
        PilotEngine e; e.prepare (SR, BLOCK);
        e.submit ("DIS.MIX 100; DIS.DRIVE 40; VOL.GAIN 12; LIM.DRIVE 18; COM.MIX 0; REV.MIX 30; EQU.LO 11; EQU.HI 11");
        e.submit (extra);
        std::string s;
        for (int ch = 0; ch < 16; ++ch)
            for (int k = 0; k < 6; ++k) s += std::string (1, str36 (ch)) + std::to_string (1 + k % 6) + "CEGBDF"[k] + "ff;";
        std::vector<float> L, R; Stats st;
        renderSeconds (e, 8.0, st, &L, &R, [&] (double t) { if (std::fmod (t, 0.5) < BLOCK / SR) e.submit (s); });
        juce::File out (argc > 2 ? argv[2] : "limiter.wav"); out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (out.createOutputStream().release());
        auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (SR).withNumChannels (2).withBitsPerSample (32).withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        const float* ch[2] = { L.data(), R.data() };
        writer->writeFromFloatArrays (ch, 2, (int) L.size());
        std::printf ("limiter stress: sample peak %.3f dBFS, rms %.1f dB, safety-clamp hits %llu, ceiling %s dBTP %s\n",
                     gainToDb (st.peak), rmsDb (st), (unsigned long long) e.limiterClips(), e.submit ("LIM.CEIL").c_str(), st.nan ? "NaN!" : "");
        return 0;
    }

    if (mode == "jf")
    {
        // Just Friends: the DSP alone, then engine 'j' driven by the Teletype's JF.* ops over (emulated) i2c
        int fails = 0;
        auto ok = [&] (const std::string& what, bool good) { if (! good) ++fails; std::printf ("%-52s %s\n", what.c_str(), good ? "ok" : "FAIL"); };
        auto near = [] (double a, double b, double tol) { return std::fabs (a - b) <= tol * b; };
        // rising crossings of the signal's mean = frequency
        auto freqOf = [] (const std::vector<float>& x, double sr)
        {
            double m = 0; for (float v : x) m += v; m /= std::max<size_t> (1, x.size());
            int c = 0; long first = -1, last = -1;
            for (size_t i = 1; i < x.size(); ++i) if (x[i - 1] < m && x[i] >= m) { ++c; if (first < 0) first = (long) i; last = (long) i; }
            return c > 1 ? (c - 1) * sr / (double) (last - first) : 0.0;
        };

        std::printf ("-- DSP\n");
        auto run = [&] (JustFriends& jf, const JfSettings& js, double secs, int slope, std::vector<float>* mix = nullptr)
        {
            std::vector<float> o; std::vector<float> b (BLOCK);
            for (long d = 0; d < (long) (secs * SR); d += BLOCK)
            {
                std::fill (b.begin(), b.end(), 0.0f);
                // one sample at a time so the per-slope output can be recorded
                for (int i = 0; i < BLOCK; ++i) { float one = 0; jf.render (&one, 1, js, 0.0, 120.0); b[(size_t) i] = one; o.push_back (jf.out (slope)); }
                if (mix) mix->insert (mix->end(), b.begin(), b.end());
            }
            return o;
        };
        {
            JustFriends jf; jf.prepare (SR); JfSettings js; js.mode = 2;   // sound / cycle: six free-running oscillators
            auto o1 = run (jf, js, 1.0, 0);
            ok ("cycle, TIME noon: IDENTITY = C4 (261.6 Hz)", near (freqOf (o1, SR), 261.63, 0.01));
            js.intone = 1.0f; jf.reset();
            auto o2 = run (jf, js, 1.0, 1), o6 = run (jf, js, 1.0, 5);
            std::printf ("  2N %.2f 6N %.2f\n", freqOf (o2, SR), freqOf (o6, SR)); ok ("INTONE full CW: 2N = 2x", near (freqOf (o2, SR), 523.25, 0.01));
            ok ("INTONE full CW: 6N = 6x", near (freqOf (o6, SR), 1569.8, 0.01));
            js.intone = 0.0f; jf.reset();
            auto o3 = run (jf, js, 2.0, 2);
            ok ("INTONE full CCW: 3N = 1/3", near (freqOf (o3, SR), 87.21, 0.01));
            js.intone = 0.5f; js.time = 1.0f; jf.reset();
            ok ("TIME full CW > 6 kHz", freqOf (run (jf, js, 0.5, 0), SR) > 6000.0);
            js.time = 0.5f; js.shift = 1.0f; jf.reset();
            { const double f = freqOf (run (jf, js, 1.0, 0), SR); std::printf ("  shift %.2f\n", f); ok ("JF.SHIFT 1 V = octave up", near (f, 523.25, 0.01)); }
            js.shift = 0; js.god = true; jf.reset();
            ok ("JF.GOD: A = 432 Hz tuning", near (freqOf (run (jf, js, 1.0, 0), SR), 261.63 * 432.0 / 440.0, 0.01));
            js.god = false; js.tuneN[3] = 5; js.tuneD[3] = 4; jf.reset();
            ok ("JF.TUNE 4 5 4: 4N = 5/4", near (freqOf (run (jf, js, 1.0, 3), SR), 261.63 * 1.25, 0.01));
        }
        {
            JustFriends jf; jf.prepare (SR); JfSettings js; js.range = 0; js.mode = 1; js.time = 0.3f;   // shape / sustain
            jf.command (JustFriends::OpTr, 1, 1, 0, js);
            auto a = run (jf, js, 2.0, 0);
            std::printf ("  hold %.4f\n", a.back()); ok ("shape/sustain: gate high rises and holds at the top", a.back() > 0.999f);
            jf.command (JustFriends::OpTr, 1, 0, 0, js);
            auto b = run (jf, js, 3.0, 0);
            ok ("shape/sustain: gate low falls to zero", b.back() < 1.0e-4f);
            js.runOn = true; js.run = 0.0f;   // STRATA
            jf.command (JustFriends::OpTr, 1, 1, 0, js);
            auto c = run (jf, js, 3.0, 0);
            ok ("STRATA (RUN 0 V): plateau at half way", std::fabs (c.back() - 0.5f) < 0.02f);
        }
        auto countCycles = [] (const std::vector<float>& x) { int c = 0; bool up = false; for (float v : x) { if (! up && v > 0.9f) { ++c; up = true; } if (v < 0.1f) up = false; } return c; };
        {
            JustFriends jf; jf.prepare (SR); JfSettings js; js.range = 0; js.mode = 0; js.time = 0.6f;   // shape / transient
            jf.command (JustFriends::OpTr, 1, 1, 0, js); jf.command (JustFriends::OpTr, 1, 0, 0, js);
            auto a = run (jf, js, 2.0, 0);
            std::printf ("  cycles %d last %.4f\n", countCycles (a), a.back()); ok ("shape/transient: one AR shape per trigger", countCycles (a) == 1 && a.back() < 1.0e-4f);
            js.mode = 2; js.runOn = true; js.run = 0.0f;   // VOLLEY
            jf.reset();
            jf.command (JustFriends::OpTr, 1, 1, 0, js);
            auto v = run (jf, js, 6.0, 0);
            std::printf ("  volley %d\n", countCycles (v)); ok ("VOLLEY (RUN 0 V): six cycles per trigger", countCycles (v) == 6);
            js.run = -5.0f; jf.reset(); jf.command (JustFriends::OpTr, 1, 0, 0, js); jf.command (JustFriends::OpTr, 1, 1, 0, js);
            ok ("VOLLEY (RUN -5 V): none", countCycles (run (jf, js, 2.0, 0)) == 0);
            jf.command (JustFriends::OpVtr, 1, 4096, 0, js);
            js.runOn = false; js.mode = 0; jf.reset(); jf.command (JustFriends::OpVtr, 1, 4096, 0, js);
            auto h = run (jf, js, 2.0, 0);
            ok ("JF.VTR V 2.5: half-height shape", std::fabs (*std::max_element (h.begin(), h.end()) - 0.5f) < 0.02f);
        }
        {
            auto rmsOf = [] (const std::vector<float>& x, size_t a, size_t b) { double q = 0; for (size_t i = a; i < b; ++i) q += (double) x[i] * x[i]; return std::sqrt (q / std::max<size_t> (1, b - a)); };
            JustFriends jf; jf.prepare (SR); JfSettings js; js.runOn = true; js.run = 0.0f;
            js.mode = 0;   // SPILL: IDENTITY runs and re-triggers the others
            std::vector<float> m; run (jf, js, 0.5, 0, &m);
            ok ("SPILL: IDENTITY free-runs and triggers 2N-6N", rmsOf (m, 4800, m.size()) > 0.05 && freqOf (run (jf, js, 0.5, 2), SR) > 50.0);
            js.mode = 1; jf.reset();   // PLUME: low pass gate pinged by triggers
            jf.command (JustFriends::OpTr, 0, 1, 0, js); jf.command (JustFriends::OpTr, 0, 0, 0, js);
            std::vector<float> p; run (jf, js, 2.0, 0, &p);
            ok ("PLUME: trigger pings, then decays", rmsOf (p, 0, 4800) > 0.05 && rmsOf (p, p.size() - 4800, p.size()) < 0.005);
            js.mode = 2; jf.reset();   // FLOOM vs plain cycle
            std::vector<float> f1; run (jf, js, 0.5, 0, &f1);
            js.runOn = false; jf.reset();
            std::vector<float> f0; run (jf, js, 0.5, 0, &f0);
            double d = 0; for (size_t i = 4800; i < f0.size(); ++i) d += std::fabs (f1[i] - f0[i]);
            std::printf ("  floom d %.4f f %.2f\n", d / (double) (f0.size() - 4800), freqOf (f1, SR));
            ok ("FLOOM: changes the timbre, keeps the pitch", d / (double) (f0.size() - 4800) > 0.01 && near (freqOf (f1, SR), 261.63, 0.02));
        }
        {
            JustFriends jf; jf.prepare (SR); JfSettings js; js.synth = true; js.range = 0; js.mode = 0; js.time = 0.9f;   // Geode
            jf.command (JustFriends::OpVox, 1, 4, 3, js);   // 4 divisions, 3 repeats
            auto g = run (jf, js, 3.0, 0);
            ok ("Geode: JF.VOX 1 4 3 = 4 envelopes, then stops", countCycles (g) == 4);
        }
        {
            JustFriends jf; jf.prepare (SR); JfSettings js; js.synth = true;   // synthesis
            jf.command (JustFriends::OpNote, 0, 1638, 8192, js);   // N 12
            std::vector<float> mix; run (jf, js, 1.0, 0, &mix);
            ok ("synthesis: JF.NOTE N 12 V 5 = C5", near (freqOf (std::vector<float> (mix.begin() + 4800, mix.end()), SR), 523.25, 0.01));
            ok ("synthesis: one voice gated", jf.gated() == 1);
            for (int k = 0; k < 8; ++k) jf.command (JustFriends::OpNote, 0, 1638 * k / 12, 8192, js);
            ok ("synthesis: 8 notes -> 6 voices", jf.gated() == 6);
            js.synth = false; std::vector<float> m2; run (jf, js, 0.1, 0, &m2);
            ok ("JF.MODE 0 releases the voices", jf.gated() == 0);
        }

        std::printf ("-- engine 'j' + Teletype JF.* (i2c 0x70 / 0x75)\n");
        PilotEngine e; e.prepare (SR, BLOCK);
        neutralFx (e);
        auto tt = [&] (const std::string& cmd, const std::string& expect = "")
        {
            bool good = false; const auto r = TeletypeHost::get().run (cmd, good);
            const bool pass = good && (expect == "*" || r == expect);
            if (! pass) ++fails;
            std::printf ("%-52s %s%s\n", cmd.c_str(), r.empty() ? "(ok)" : r.c_str(), pass ? "" : ("  EXPECTED " + expect).c_str());
        };
        tt ("JF.TR 1 1");   // no JF channel yet: like an empty i2c bus
        tt ("JF.TIME", "0");
        e.submit ("0ENGj");
        ok ("0ENGj selects Just Friends", e.getSettings (0).engine == EngJF);
        tt ("PI.ENG 0", "8");
        tt ("JF.SPEED", "1"); tt ("JF.TSC", "1"); tt ("JF.TIME", "8192");
        e.submit ("0OSCsncy"); tt ("JF.TSC", "2");
        e.submit ("0OSCsnsu");
        e.submit ("0ENV8800"); tt ("JF.RAMP", "0"); tt ("JF.TIME", "8738");
        e.submit ("CH.TIME 0 50; CH.RAMP 0 50; CH.CURVE 0 50");
        tt ("JF.CURVE", "8192");
        auto renderMono = [&] (double secs) { std::vector<float> L, R; Stats st; renderSeconds (e, secs, st, &L, &R); return L; };
        tt ("JF.MODE 1"); tt ("JF.NOTE N 0 V 5");
        { auto x = renderMono (0.6); ok ("JF.NOTE N 0 V 5 on JF1 = C4", near (freqOf (std::vector<float> (x.begin() + 4800, x.end()), SR), 261.63, 0.02)); }
        ok ("JF.NOTE gates a voice on channel 0", e.gatedVoices (0) == 1);
        tt ("JF.NOTE N 0 0");
        renderMono (0.05);
        ok ("JF.NOTE N 0 0 releases it", e.gatedVoices (0) == 0);
        renderMono (1.0);
        e.submit ("PI.N# 0 69 100");
        {
            auto x = renderMono (0.6);
            ok ("Pilot note 69 on a JF channel = 440 Hz", near (freqOf (std::vector<float> (x.begin() + 4800, x.end()), SR), 440.0, 0.02));
        }
        e.submit ("PI.NO# 0 69"); renderMono (0.05);
        ok ("Pilot note off releases", e.gatedVoices (0) == 0);
        tt ("JF.MODE 0"); e.submit ("0OSCsncy");
        {
            Stats st; renderSeconds (e, 0.5, st);
            ok ("JF.MODE 0 + cycle: free-running (audible)", rmsDb (st) > -40.0);
        }
        e.submit ("0OSCsnsu");
        tt ("JF.RMODE 1"); tt ("JF.RUN V 2"); tt ("JF.SHIFT V 1"); tt ("JF.GOD 1"); tt ("JF.QT 4"); tt ("JF.TUNE 2 3 2");
        {
            const auto j = e.getSettings (0).jf;
            ok ("RMODE / RUN / SHIFT / GOD / QT / TUNE reach channel 0", j.runOn && std::fabs (j.run - 2.0f) < 0.01f && std::fabs (j.shift - 1.0f) < 0.01f && j.god && j.qt == 4 && j.tuneN[1] == 3 && j.tuneD[1] == 2);
        }
        tt ("JF.RMODE 0"); tt ("JF.SHIFT 0"); tt ("JF.GOD 0");
        e.submit ("2ENGj");
        tt ("JF.SEL 2; JF.MODE 1");
        { const auto n = e.channelNotes (2); tt ("JF.NOTE N 7 V 5"); renderMono (0.05); ok ("JF.SEL 2: JF.NOTE plays the second JF channel (2)", e.channelNotes (2) == n + 1 && e.gatedVoices (2) == 1); }
        tt ("JF.SEL 1");
        { const auto n = e.channelNotes (2); tt ("JF2: JF.VOX 1 N 0 V 5"); renderMono (0.05); ok ("JF2: mod addresses channel 2", e.channelNotes (2) == n + 1); }
        { const auto n0 = e.channelNotes (0), n2 = e.channelNotes (2); tt ("JF0: JF.TR 0 1"); renderMono (0.01); ok ("JF0: both units", e.channelNotes (0) == n0 + 1 && e.channelNotes (2) == n2 + 1); }
        tt ("JF.TR 0 0");
        tt ("JF.ADDR 2");   // JF1 moves to 0x75: the two swap
        { const auto n = e.channelNotes (2); tt ("JF.VOX 1 N 0 V 5"); renderMono (0.01); ok ("JF.ADDR 2 swaps JF1 / JF2", e.channelNotes (2) == n + 1); }
        tt ("JF.SEL 2; JF.ADDR 1"); tt ("JF.SEL 1");   // the module now at 0x75 goes back to 1
        tt ("JF.POLY.RESET");
        {
            const auto n0 = e.channelNotes (0), n2 = e.channelNotes (2);
            for (int k = 0; k < 8; ++k) tt ("JF.POLY N " + std::to_string (k) + " V 5");
            renderMono (0.01);
            ok ("JF.POLY: 6 notes on JF1, then JF2", e.channelNotes (0) == n0 + 6 && e.channelNotes (2) == n2 + 2);
        }
        {
            Stats st; renderSeconds (e, 1.0, st);
            ok ("no NaN / overload", ! st.nan && st.peak < 4.0f);
        }
        e.submit ("RENV; ROSC");
        ok ("renv / rosc keep the JF channel valid", e.getSettings (0).engine == EngJF);
        ok ("serialized state restores the JF panel", e.serializeState().find ("CH.TIME 0") != std::string::npos);
        std::printf ("%d failures\n", fails);
        return fails == 0 ? 0 : 1;
    }

    if (mode == "pi")
    {
        // the real Teletype firmware + PI.* ops (teletype/src/ops/pilot.c) + I2M.* -> MIDI out
        PilotEngine e; e.prepare (SR, BLOCK);
        int fails = 0;
        auto check = [&] (const std::string& cmd, const std::string& expect)
        {
            const auto r = e.submit (cmd);
            const bool ok = expect == "*" || r == expect;
            if (! ok) ++fails;
            std::printf ("%-40s -> %-24s %s\n", cmd.c_str(), r.empty() ? "(ok)" : r.c_str(), ok ? "" : ("  EXPECTED " + expect).c_str());
        };
        auto ok = [&] (const std::string& what, bool good) { if (! good) ++fails; std::printf ("%-40s    %s\n", what.c_str(), good ? "ok" : "FAIL"); };
        auto notesOn = [&] (int ch) { Stats st; renderSeconds (e, 0.02, st); return e.gatedVoices (ch); };
        auto midiOut = [&]
        {
            juce::MidiBuffer b;
            TeletypeHost::get().drainMidiOut (b, 0);
            std::vector<juce::MidiMessage> v;
            for (const auto m : b) v.push_back (m.getMessage());
            return v;
        };
        midiOut();

        std::printf ("-- Teletype language (real firmware)\n");
        check ("ADD 1 2", "3");
        check ("X ADD 2 3; X", "5");
        check ("Y 0", ""); check ("IF GT 3 2: Y 7", ""); check ("Y", "7");
        check ("P.N 0; P.L 0", ""); check ("L 1 4: P.PUSH MUL I 10", ""); check ("P.L", "4"); check ("P 2", "30");
        check ("N 12", "1638"); check ("BPM 120", "500");
        check ("FOO 1", "UNKNOWN WORD: FOO");
        check ("ADD 1", "NOT ENOUGH PARAMS: ADD");
        check ("PI.N 0 60", "NOT ENOUGH PARAMS: PI.N");
        check ("T 0", ""); check ("DEL 30: T 9", ""); check ("X 0; DEL 30: X 1", "BAD PRE SEPARATOR"); check ("T", "0");
        std::this_thread::sleep_for (std::chrono::milliseconds (80));
        check ("T", "9");
        check ("A 0", ""); check ("DEL.X 3 10: A ADD A 1", "");
        std::this_thread::sleep_for (std::chrono::milliseconds (80));
        check ("A", "3");
        check ("J 5; J", "5");   // J and K are local to each script, like Teletype

        std::printf ("-- PI.* notes\n");
        check ("PI.CH 7", ""); check ("PI.CH", "7"); check ("PI.# 4", ""); check ("PI.#", "4"); check ("PI.CH 7", "");
        check ("PI.NOTE 60 100", ""); ok ("PI.NOTE holds on PI.CH", notesOn (7) == 1);
        check ("PI.NO 60", ""); ok ("PI.NO releases", notesOn (7) == 0);
        check ("PI.N 3 60 100", ""); ok ("PI.N plays on ch 3", notesOn (3) == 1);
        check ("PI.LEN 3 30", ""); check ("PI.LEN 3", "30");
        check ("PI.N 3 64 100", ""); { Stats st; renderSeconds (e, 0.3, st); } ok ("PI.N releases after PI.LEN", e.gatedVoices (3) == 0);
        check ("PI.LEN 3 0", "");
        check ("PI.NT 2 60 100 30", ""); ok ("PI.NT plays", notesOn (2) == 1); { Stats st; renderSeconds (e, 0.1, st); } ok ("PI.NT stops after 30 ms", e.gatedVoices (2) == 0);
        check ("PI.N# 5 67 90", ""); ok ("PI.N# holds", notesOn (5) == 1);
        check ("PI.NO# 5 67", ""); ok ("PI.NO# releases", notesOn (5) == 0);
        check ("PI.N# 5 N 60 90", ""); ok ("Teletype pitch N 60 = note 60", notesOn (5) == 1);
        check ("PI.NO# 5 60", ""); ok ("...released as note 60", notesOn (5) == 0);
        check ("PI.N# 4 60 100; PI.AO", ""); ok ("PI.AO", notesOn (4) == 0);
        check ("PI.VOX 6 48 100", ""); ok ("PI.VOX", notesOn (6) == 1);
        check ("PI.VOX.P 6 55", ""); ok ("PI.VOX.P replaces the note", notesOn (6) == 1);
        check ("PI.VO 6", ""); ok ("PI.VO", notesOn (6) == 0);
        { const auto b = e.channelNotes (12); check ("PI.T 12", ""); notesOn (12); ok ("PI.T", e.channelNotes (12) != b); }
        { const auto b = e.channelNotes (13); check ("PI.TV 13 127", ""); notesOn (13); ok ("PI.TV", e.channelNotes (13) != b); }

        std::printf ("-- N.B (scale degrees) with PI.N\n");
        check ("N.B 1", "0"); check ("N.B 2", "273"); check ("N.B 3", "546"); check ("N.B 8", "1638");
        auto playsNote = [&] (const std::string& cmd, int ch, int midi)
        {
            check (cmd, "");
            const bool on = notesOn (ch) == 1;
            e.submit ("PI.NO# " + std::to_string (ch) + " " + std::to_string (midi));
            const bool off = notesOn (ch) == 0;
            ok (cmd + " = note " + std::to_string (midi), on && off);
            e.submit ("PI.AO");
        };
        playsNote ("PI.N# 5 ADD N 48 N.B 3 100", 5, 52);
        playsNote ("PI.N# 5 ADD N 60 N.B 8 100", 5, 72);
        check ("N.B 48 0", "");                                     // root C3, scale preset 0
        playsNote ("PI.N# 5 N.B 1 100", 5, 48);
        playsNote ("PI.N# 5 N.B 5 100", 5, 55);
        playsNote ("PI.N# 5 N.B 8 100", 5, 60);
        check ("N.B 48 -3", "");                                    // another preset scale
        { bool good = false; TeletypeHost::get().run ("PI.N 0 N.B 3 100", good); ok ("PI.N 0 N.B 3 100 runs", good); }
        check ("N.B 0 0", "");
        check ("N.BX 0 60 0", ""); check ("N.BX 0 1", "*");
        e.submit ("PI.AO");

        std::printf ("-- PI.* parameters\n");
        check ("PI.CH 2; PI.ALG 1", ""); check ("PI.A", "1"); check ("PI.ENG 2", "1");
        check ("PI.ENG 2 4", ""); check ("PI.ENG 2", "4"); ok ("PI.ENG sets the engine", e.getSettings (2).engine == 'k');
        check ("PI.P 12 6", ""); check ("PI.P 12", "6"); check ("PI.P# 2 12", "6");
        check ("PI.P# 3 12 4", ""); check ("PI.P# 3 12", "4");
        check ("PI.MIN 15", "-100"); check ("PI.MAX 15", "100");
        check ("PI.PV 15 16384", ""); check ("PI.P 15", "100");
        check ("PI.P 10 140", ""); check ("PI.P 10", "140"); check ("PI.MAX 10", "1400");
        check ("PI.P 233 -24", ""); check ("PI.P 233", "-24");
        check ("PI.P 230 9", ""); check ("PI.P 230", "9");
        check ("PI.P 7 1000", ""); check ("PI.P 7", "1000");
        check ("PI.ENV 4 1 4 5 10", "");
        { const auto st = e.getSettings (4); ok ("PI.ENV 1-10: 1 = min, 10 = max", std::abs (st.env[0]) < 1e-3 && std::abs (st.env[1] - 3 / 9.f) < 1e-3
                                                                                   && std::abs (st.env[2] - 4 / 9.f) < 1e-3 && std::abs (st.env[3] - 1.f) < 1e-3); }
        check ("PI.ENV 4 0 99 -5 11", "");
        { const auto st = e.getSettings (4); ok ("PI.ENV clamps to 1-10", std::abs (st.env[0]) < 1e-3 && std::abs (st.env[1] - 1.f) < 1e-3 && std::abs (st.env[2]) < 1e-3 && std::abs (st.env[3] - 1.f) < 1e-3); }
        check ("PI.FIL 4 8 15 2", ""); ok ("PI.FIL = 4FIL8f2", std::abs (e.getSettings (4).fil[1] - 1.f) < 1e-4);
        check ("PI.MOD 4 3 6", ""); ok ("PI.MOD = 4MOD36", std::abs (e.getSettings (4).mod[0] - 3 / 15.f) < 1e-4);
        check ("PI.OSC 4 1 -1", ""); ok ("PI.OSC", e.getSettings (4).wave1 == 1 && e.getSettings (4).wave2 == -1);
        check ("PI.FX 10 8 12", ""); ok ("PI.FX 10 = REV8c", std::abs (e.fxW[FxRev].load() - 8 / 15.f) < 1e-4 && std::abs (e.fxV[FxRev].load() - 12 / 15.f) < 1e-4);
        check ("PI.BPM 133", ""); check ("PI.BPM", "133");
        for (const char* op : { "RENV", "ROSC", "REFX", "PI.RENV", "PI.ROSC", "PI.REFX" })
        {
            const auto s5 = e.getSettings (5); const auto w0 = e.getSettings (0).wave1; const auto r0 = e.fxW[0].load();
            bool changed = false;
            for (int k = 0; k < 8 && ! changed; ++k)
            {
                bool good = false; TeletypeHost::get().run (op, good); notesOn (0);
                const auto n5 = e.getSettings (5);
                for (int j = 0; j < 4; ++j) changed |= n5.env[j] != s5.env[j];
                changed |= e.getSettings (0).wave1 != w0 || e.fxW[0].load() != r0;
                if (! good) break;
            }
            ok (std::string ("Teletype ") + op + " randomizes", changed);
        }
        check ("PI.P# 2 12 6; PI.SAVE 31", ""); check ("PI.P# 2 12 1", ""); check ("PI.PRESET 31", ""); check ("PI.P# 2 12", "6");
        check ("PI.RESET", ""); check ("PI.P# 2 12", "*");

        std::printf ("-- classic Pilot commands still win\n");
        check ("0ENGk", ""); check ("PI.ENG 0", "4");
        check ("REV8c;0ENVffff", ""); check ("PI.P 100", "*");

        std::printf ("-- I2M.* -> MIDI out\n");
        midiOut();
        check ("I2M.CH 3; I2M.T 40; I2M.N 60 100", "");
        {
            auto v = midiOut();
            ok ("I2M.N sends note on ch 3", v.size() == 1 && v[0].isNoteOn() && v[0].getChannel() == 3 && v[0].getNoteNumber() == 60 && v[0].getVelocity() == 100);
            std::this_thread::sleep_for (std::chrono::milliseconds (90));
            v = midiOut();
            ok ("...and its note off after I2M.T 40", v.size() == 1 && v[0].isNoteOff() && v[0].getNoteNumber() == 60);
        }
        check ("I2M.T", "40");
        check ("I2M.CC 7 100", "");
        { auto v = midiOut(); ok ("I2M.CC sends CC 7 = 100", v.size() == 1 && v[0].isController() && v[0].getControllerNumber() == 7 && v[0].getControllerValue() == 100 && v[0].getChannel() == 3); }
        check ("I2M.CC# 16 1 64", "");
        { auto v = midiOut(); ok ("I2M.CC# on channel 16", v.size() == 1 && v[0].getChannel() == 16 && v[0].getControllerValue() == 64); }
        check ("I2M.NT 62 90 20", "");
        { auto v = midiOut(); ok ("I2M.NT", v.size() == 1 && v[0].isNoteOn() && v[0].getNoteNumber() == 62); }
        check ("I2M.PB 8191; I2M.PRG 5", "");
        { auto v = midiOut(); ok ("I2M.PB + I2M.PRG", v.size() == 2 && v[0].isPitchWheel() && v[0].getPitchWheelValue() == 16383 && v[1].isProgramChange() && v[1].getProgramChangeNumber() == 5); }
        check ("I2M.CC.SLEW 1 50; I2M.CC 1 0; I2M.CC 1 127", "");
        std::this_thread::sleep_for (std::chrono::milliseconds (120));
        { auto v = midiOut(); ok ("I2M.CC.SLEW ramps", v.size() > 5 && v.back().getControllerValue() == 127); }

        std::printf ("-- MIDI in -> MI.* ops\n");
        TeletypeHost::get().midiIn (juce::MidiMessage::noteOn (2, 64, (juce::uint8) 99));
        std::this_thread::sleep_for (std::chrono::milliseconds (40));
        check ("MI.LN", "64"); check ("MI.LV", "*"); check ("MI.LCH", "*");

        std::printf ("-- documentation examples parse and run\n");
        for (const char* line : { "DEL RRND 200 2000: $ $", "PI.N 0 ADD 48 RND 24 100", "PI.N 0 N.S 60 1 P.NEXT 100", "EVERY 4: PI.T 12",
                                  "I2M.N ADD 36 P.HERE 90", "M 125; PI.LEN 0 80; PI.ENG 12 4; I2M.CH 2", "PI.CH 2; PI.ALG 1; PI.P 10 350",
                                  "PI.PV 7 PRM", "PI.ENV 3 1 4 5 10; PI.FX 10 8 12", "I2M.CC 74 RND 127", "PI.SAVE 4", "PI.PRESET 4",
                                  "PI.PV 7 V 5", "PI.N 0 N 60 100", "DEL 500: PI.T 12", "M 250", "X ADD X 1" })
        {
            bool good = false;
            const auto r = TeletypeHost::get().run (line, good);
            ok (std::string (line) + (good ? "" : "  -> " + r), good);
        }
        e.submit ("M.ACT 0; PI.AO; PI.LEN 0 0; I2M.CH 3; DEL.CLR");
        midiOut();

        std::printf ("-- scenes / state\n");
        check ("Z 0", "");
        TeletypeHost::get().loadSceneText ("TEST\n\n#1\nZ 42\n\n#I\nZ 7\n", true);
        check ("Z", "7"); check ("SCRIPT 1; Z", "42");
        ok ("scene text round-trips", TeletypeHost::get().sceneText().find ("Z 42") != std::string::npos);
        TeletypeHost::get().loadSceneText ("#1\nDEL 30: $ $\nC ADD C 1\nPI.N 0 60 100\n", false);
        check ("C 0", "");
        const auto loopStart = std::chrono::steady_clock::now();
        check ("$ 1", "");
        std::this_thread::sleep_for (std::chrono::milliseconds (200));
        {   // one run every 30 ms, measured against the time that really passed (CI machines oversleep)
            const int c = std::atoi (e.submit ("C").c_str());
            const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - loopStart).count();
            const int expect = 1 + (int) (ms / 30.0);
            ok ("DEL 30: $ $ loops script 1 (" + std::to_string (c) + " runs in " + std::to_string ((int) ms) + " ms)", std::abs (c - expect) <= 1);
        }
        check ("DEL.CLR", "");
        check ("M.ACT 1; M 50; M.RESET", "");
        TeletypeHost::get().loadSceneText ("#M\nB ADD B 1\n", false);
        check ("B 0", "");
        const auto t0 = std::chrono::steady_clock::now();
        std::this_thread::sleep_for (std::chrono::milliseconds (1000));
        const int k = std::atoi (e.submit ("B").c_str());
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        check ("M.ACT 0", "");
        const double expect = ms / 50.0;
        std::printf ("metro ticks in %.0f ms at M 50: %d (expected %.1f) %s\n", ms, k, expect, std::abs (k - expect) <= 2.0 ? "" : "  UNEXPECTED");
        if (std::abs (k - expect) > 2.0) ++fails;

        std::printf ("%d failures\n", fails);
        return fails == 0 ? 0 : 1;
    }

    if (mode == "ttshot" && argc >= 3)
    {
        // Teletype screens -> PNGs (keys sent as USB HID codes, like a keyboard on the module)
        PilotEngine e; e.prepare (SR, BLOCK);
        auto& tt = TeletypeHost::get();
        const juce::File dir (argv[2]);
        dir.createDirectory();
        auto shot = [&] (const char* name)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (40));
            uint8_t fb[128 * 64];
            tt.refresh (fb); tt.refresh (fb);
            juce::Image img (juce::Image::RGB, 128 * 3, 64 * 3, true);
            for (int y = 0; y < 64 * 3; ++y)
                for (int x = 0; x < 128 * 3; ++x)
                {
                    const int l = fb[(y / 3) * 128 + x / 3];
                    const auto v = (juce::uint8) (l == 0 ? 0 : 25 + l * 230 / 15);
                    img.setPixelAt (x, y, juce::Colour (v, v, v));
                }
            auto f = dir.getChildFile (juce::String (name) + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
        };
        auto type = [&] (const char* text)
        {
            for (const char* c = text; *c; ++c)
            {
                if (*c == '\n') { tt.key (0x28, 0, false); continue; }
                const char ch = *c;
                uint8_t hid = 0, mod = 0;
                if (ch >= 'A' && ch <= 'Z') hid = (uint8_t) (0x04 + ch - 'A');
                else if (ch >= '1' && ch <= '9') hid = (uint8_t) (0x1E + ch - '1');
                else if (ch == '0') hid = 0x27;
                else if (ch == ' ') hid = 0x2C;
                else if (ch == '.') hid = 0x37;
                else if (ch == ':') { hid = 0x33; mod = 0x02; }
                else if (ch == ';') hid = 0x33;
                else if (ch == '#') { hid = 0x20; mod = 0x02; }
                tt.key (hid, mod, false);
            }
        };
        e.submit ("PI.N 0 60 100");
        type ("X ADD 2 3\nX\n");
        shot ("1-live");
        tt.key (0x2B, 0, false);                       // tab -> edit
        type ("DEL 250: PI.N 0 60 100\nPI.ENV 3 0 5 6 15\nI2M.N 60 100\n");
        shot ("2-edit");
        tt.key (0x2B, 0, false);                       // tab -> pattern
        type ("12\n24\n36\n");
        shot ("3-pattern");
        tt.key (0x2B, 0, false);                       // tab -> live
        tt.key (0x0B, 0x04, false);                    // alt-h -> help
        for (int i = 0; i < 17; ++i) tt.key (0x30, 0, false);   // ] x17 -> page 18
        shot ("4-help-pilot");
        tt.key (0x0B, 0x04, false);
        tt.key (0x29, 0, false);                       // esc -> scene read
        shot ("5-scenes");
        tt.key (0x29, 0, false);
        tt.key (0x35, 0x02, false);                    // ~ vars
        shot ("6-live-vars");
        return 0;
    }

    if (mode == "midiports")
    {
        for (auto& d : juce::MidiInput::getAvailableDevices()) std::printf ("in:  %s\n", d.name.toRawUTF8());
        return 0;
    }

    if (mode == "ttedit")
    {
        // type into the script editor and print the screen text, line by line
        PilotEngine e; e.prepare (SR, BLOCK);
        auto& tt = TeletypeHost::get();
        auto dump = [&] (const char* title)
        {
            uint8_t fb[128 * 64]; tt.refresh (fb); tt.refresh (fb);
            std::printf ("== %s (mode %d)\n", title, tt.mode());
            for (int l = 0; l < 8; ++l)
            {
                tt_seg_t segs[48]; int n = 0; uint8_t bg = 0;
                tt.screenLine (l, segs, 48, n, bg);
                std::printf ("%d bg%d:", l, bg);
                for (int i = 0; i < n; ++i) std::printf (" [x%d fg%d hi%d '%s']", segs[i].x, segs[i].fg, segs[i].hi, segs[i].text);
                std::printf ("\n");
            }
        };
        tt.key (0x2B, 0, false);   // tab -> edit
        dump ("edit, empty");
        tt.key (0x1B, 0x02, false); tt.key (0x2C, 0, false); tt.key (0x1E, 0, false);   // "X 1"
        dump ("edit, typed X 1");
        tt.key (0x28, 0, false);   // enter
        dump ("edit, after enter");
        tt.key (0x2B, 0, false); tt.key (0x1E, 0, false); tt.key (0x1F, 0, false);   // tab -> pattern, type 12
        dump ("pattern, typing 12");
        tt.key (0x2B, 0, false); tt.key (0x1B, 0x02, false);                          // tab -> live, type X
        dump ("live, typed X");
        tt.key (0x28, 0, false);
        dump ("live, after enter");
        tt.key (0x0B, 0x04, false);
        dump ("help");
        tt.key (0x0B, 0x04, false); tt.key (0x29, 0, false);
        dump ("scene read");
        tt.key (0x29, 0, false); tt.key (0x29, 0x04, false);
        dump ("scene write");
        return 0;
    }

    if (mode == "legacy")
    {
        // every classic Pilot command must keep working exactly (ORCA / orca-seaflake send these)
        PilotEngine e; e.prepare (SR, BLOCK);
        int fails = 0;
        auto ok = [&] (const std::string& what, bool good) { if (! good) ++fails; std::printf ("%-28s %s\n", what.c_str(), good ? "ok" : "FAIL"); };
        auto plays = [&] (const std::string& cmd, int ch)
        {
            const auto before = e.channelNotes (ch);
            e.submit (cmd); Stats st; renderSeconds (e, 0.02, st);
            ok (cmd + " plays ch " + std::to_string (ch), e.channelNotes (ch) > before);
        };
        for (const char* c : { "04C", "04c", "14D", "a4Cf8", "f2E", "12e5", "33Gf", "54A", "75B9", "C4C", "E4F", "B3E", "D0Af0" })
            plays (c, int36 (c[0]));
        plays ("04C;14E;24G", 2);
        plays ("_04C_", 0);

        e.submit ("0ENV056f");
        auto s0 = e.getSettings (0);
        ok ("0ENV056f", std::abs (s0.env[0] - 0) < 1e-4 && std::abs (s0.env[1] - 5 / 15.f) < 1e-4 && std::abs (s0.env[2] - 6 / 15.f) < 1e-4 && std::abs (s0.env[3] - 1) < 1e-4);
        e.submit ("1OSCsisq");
        auto s1 = e.getSettings (1);
        ok ("1OSCsisq", s1.wave1 == waveCodeIndex ("si") && s1.wave2 == waveCodeIndex ("sq"));
        for (const char* w : { "si","2i","4i","8i","tr","2r","4r","8r","sq","2q","4q","8q","sw","2w","4w","8w" })
        {
            e.submit (std::string ("2OSC") + w + "--");
            ok (std::string ("2OSC") + w + "--", e.getSettings (2).wave1 == waveCodeIndex (w) && e.getSettings (2).wave2 == -1);
        }
        e.submit ("fenv1234"); ok ("fenv1234", std::abs (e.getSettings (15).env[3] - 4 / 15.f) < 1e-4);
        e.submit ("3FIL8f2"); ok ("3FIL8f2", std::abs (e.getSettings (3).fil[1] - 1) < 1e-4);

        for (const char* fx : { "BIT","DIS","WAH","CHE","FEE","DEL","TRE","REV","PHA","VIB","CHO","STE","EQU","COM","VOL","LIM" })
        {
            const int i = fxIndexOf (fx);
            e.submit (std::string (fx) + "ff"); bool g = e.fxW[i].load() == 1.f && e.fxV[i].load() == 1.f;
            e.submit (std::string (fx) + "3a"); g = g && std::abs (e.fxW[i].load() - 3 / 15.f) < 1e-4 && std::abs (e.fxV[i].load() - 10 / 15.f) < 1e-4;
            std::string lc (fx); for (auto& ch : lc) ch = (char) std::tolower (ch);
            e.submit (lc + "07"); g = g && e.fxW[i].load() == 0.f && std::abs (e.fxV[i].load() - 7 / 15.f) < 1e-4;
            e.submit (std::string (fx) + "5"); g = g && std::abs (e.fxW[i].load() - 5 / 15.f) < 1e-4;
            ok (std::string (fx) + "ff / 3a / lowercase / 1 digit", g);
        }
        e.submit ("bpm140"); ok ("bpm140", e.bpm.load() == 140.0);
        e.submit ("BPM90"); ok ("BPM90", e.bpm.load() == 90.0);
        const auto s5 = e.getSettings (5);
        bool changed = false;
        for (int k = 0; k < 5 && ! changed; ++k) { e.submit ("renv"); const auto n = e.getSettings (5); for (int j = 0; j < 4; ++j) changed |= n.env[j] != s5.env[j]; }
        ok ("renv", changed);
        auto w0 = e.getSettings (0).wave1; changed = false;
        for (int k = 0; k < 5 && ! changed; ++k) { e.submit ("rosc"); changed = e.getSettings (0).wave1 != w0; }
        ok ("rosc", changed);
        float r0 = e.fxW[0].load(); changed = false;
        for (int k = 0; k < 5 && ! changed; ++k) { e.submit ("refx"); changed = e.fxW[0].load() != r0; }
        ok ("refx", changed);
        e.submit ("REV8c;0ENVffff"); e.submit ("reset");
        PilotEngine fresh; fresh.prepare (SR, BLOCK);
        ok ("reset", e.serializeState() == fresh.serializeState());
        std::printf ("%d failures\n", fails);
        return fails == 0 ? 0 : 1;
    }

    if (mode == "lang")
    {
        // Pilot's extended commands: no Teletype equivalent, still accepted on the command line / UDP
        PilotEngine e; e.prepare (SR, BLOCK);
        int fails = 0;
        auto check = [&] (const std::string& cmd, const std::string& expect)
        {
            const auto r = e.submit (cmd);
            const bool ok = expect == "*" || r == expect;
            if (! ok) ++fails;
            std::printf ("%-42s -> %-14s %s\n", cmd.c_str(), r.empty() ? "(ok)" : r.c_str(), ok ? "" : ("  EXPECTED " + expect).c_str());
        };
        check ("COM.RATIO 1.4; COM.THR -20", "");
        check ("COM.RATIO", "1.4");
        check ("COM.THR", "-20");
        check ("COM.CLR; COM.THR", "*");
        check ("REV.MIX 50", ""); check ("REV.MIX", "50");
        check ("REV8c", ""); check ("REV.W", "8");
        check ("LIM.CEIL -1", ""); check ("LIM.CEIL", "-1");
        check ("CH.VOICES 0 4", ""); check ("CH.VOICES 0", "4");
        check ("CH.ATK 3 100", ""); check ("ROUND CH.ATK 3", "100");
        check ("CH.RATIO 6 1.4", ""); check ("CH.RATIO 6", "1.4");
        check ("CH.ENG 5 1", ""); check ("CH.ENG 5", "1");
        check ("0ENGk", ""); check ("CH.ENG 0", "4");
        check ("MIDI.MODE 2; MIDI.RR 0 7; MIDI.MASK", "255");
        check ("MIDI.MODE 0", "");
        check ("HELLO 1", "UNKNOWN WORD: HELLO");
        // a saved session restores through the same path
        const auto saved = e.serializeState();
        PilotEngine f; f.prepare (SR, BLOCK);
        f.submit (saved, false);
        if (f.serializeState() != saved) { ++fails; std::printf ("session restore: MISMATCH\n"); }
        else std::printf ("session restore: ok\n");
        std::printf ("\n%s (%d failures)\n", fails == 0 ? "LANG TEST PASSED" : "LANG TEST FAILED", fails);
        return fails == 0 ? 0 : 1;
    }

    if (mode == "rr")
    {
        int fails = 0;
        auto run = [&] (const char* setup, const char* name, std::function<bool (PilotEngine&)> ok)
        {
            PilotEngine e; e.prepare (SR, BLOCK);
            e.submit (setup);
            std::vector<float> L (BLOCK), R (BLOCK);
            e.process (L.data(), R.data(), BLOCK, -1);
            const int notes[6] = { 60, 64, 67, 71, 74, 77 };
            for (int i = 0; i < 6; ++i)
            {
                e.handleMidi (juce::MidiMessage::noteOn (1, notes[i], (juce::uint8) 100));
                e.process (L.data(), R.data(), BLOCK, -1);
            }
            const bool good = ok (e);
            std::printf ("%-48s %s\n", name, good ? "ok" : "FAILED");
            if (! good) ++fails;
            for (int i = 0; i < 6; ++i) e.handleMidi (juce::MidiMessage::noteOff (1, notes[i]));
            for (int b = 0; b < 4; ++b) e.process (L.data(), R.data(), BLOCK, -1);
        };
        run ("MIDI.MODE 2; MIDI.RR 0 15", "round robin: 6 notes -> channels 0..5, 1 each", [] (PilotEngine& e)
        { for (int c = 0; c < 16; ++c) if (e.gatedVoices (c) != (c < 6 ? 1 : 0)) return false; return true; });
        run ("MIDI.MODE 2; MIDI.RR 4 7", "round robin 4-7: wraps, 4&5 get 2 notes", [] (PilotEngine& e)
        { return e.gatedVoices (4) == 2 && e.gatedVoices (5) == 2 && e.gatedVoices (6) == 1 && e.gatedVoices (7) == 1 && e.gatedVoices (0) == 0; });
        run ("MIDI.MODE 1; MIDI.CH 9; CH.VOICES 9 16", "omni -> ch 9: one channel, 6 voices", [] (PilotEngine& e)
        { for (int c = 0; c < 16; ++c) if (e.gatedVoices (c) != (c == 9 ? 6 : 0)) return false; return true; });
        run ("MIDI.MODE 1; MIDI.CH 2; CH.VOICES 2 4", "omni -> ch 2 with 4 voices: steals", [] (PilotEngine& e)
        { return e.gatedVoices (2) == 4; });
        run ("MIDI.MODE 0", "multi: MIDI ch1 -> Pilot ch0 poly", [] (PilotEngine& e) { return e.gatedVoices (0) == 6; });
        {
            PilotEngine e; e.prepare (SR, BLOCK);
            e.submit ("MIDI.MODE 2; MIDI.RR 0 3");
            std::vector<float> L (BLOCK), R (BLOCK);
            for (int i = 0; i < 8; ++i) { e.handleMidi (juce::MidiMessage::noteOn (1, 60 + i, (juce::uint8) 100)); e.handleMidi (juce::MidiMessage::noteOff (1, 60 + i)); }
            e.process (L.data(), R.data(), BLOCK, -1);
            bool good = true;
            for (int c = 0; c < 16; ++c) good &= (int) e.channelNotes (c) == (c < 4 ? 2 : 0);
            std::printf ("%-48s %s\n", "round robin note counts 0-3 after 8 notes = 2 each", good ? "ok" : "FAILED");
            if (! good) ++fails;
        }
        std::printf ("\n%s\n", fails == 0 ? "RR TEST PASSED" : "RR TEST FAILED");
        return fails;
    }

    if (mode == "seq" && argc >= 5)
    {
        // PilotRender seq "t1|cmd;cmd/t2|cmd..." seconds out.wav   (commands fire at render time t)
        PilotEngine e; e.prepare (SR, BLOCK); neutralFx (e);
        std::vector<std::pair<double, std::string>> ev;
        juce::StringArray parts; parts.addTokens (argv[2], "/", "");
        for (auto& p : parts) ev.push_back ({ p.upToFirstOccurrenceOf ("|", false, false).getDoubleValue(), p.fromFirstOccurrenceOf ("|", false, false).toStdString() });
        size_t next = 0;
        std::vector<float> L, R; Stats st;
        renderSeconds (e, std::atof (argv[3]), st, &L, &R, [&] (double t) { while (next < ev.size() && ev[next].first <= t) e.submit (ev[next++].second); });
        juce::File out (argv[4]); out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (out.createOutputStream().release());
        auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (SR).withNumChannels (2).withBitsPerSample (32).withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        const float* ch[2] = { L.data(), R.data() };
        writer->writeFromFloatArrays (ch, 2, (int) L.size());
        std::printf ("%s peak %.2f %s\n", argv[4], gainToDb (st.peak), st.nan ? "NaN!" : "");
        return 0;
    }

    if (mode == "cmd" && argc >= 5)
    {
        // PilotRender cmd "<commands>" <seconds> <out.wav> [neutral]
        PilotEngine e; e.prepare (SR, BLOCK);
        if (argc < 6 || std::string (argv[5]) != "fx") neutralFx (e);
        e.submit (argv[2]);
        std::vector<float> L, R; Stats st;
        renderSeconds (e, std::atof (argv[3]), st, &L, &R);
        juce::File out (argv[4]); out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (out.createOutputStream().release());
        auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (SR).withNumChannels (2).withBitsPerSample (32).withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        const float* ch[2] = { L.data(), R.data() };
        writer->writeFromFloatArrays (ch, 2, (int) L.size());
        std::printf ("%s peak %.2f rms %.1f %s\n", argv[4], gainToDb (st.peak), rmsDb (st), st.nan ? "NaN!" : "");
        return 0;
    }

    if (mode == "piparams") { PilotEngine e; std::printf ("%s", e.lang().piParamReference().c_str()); return 0; }
    if (mode == "ref")
    {
        PilotEngine e;
        std::printf ("%s", e.lang().reference().c_str());
        return 0;
    }

    if (mode == "all" || mode == "levels")
    {
        std::printf ("== Channel levels (C3 chord C-E-G, vel f, 1 bar @120) ==\n");
        for (int ch = 0; ch < 16; ++ch)
        {
            PilotEngine e; e.prepare (SR, BLOCK); neutralFx (e);
            const char id = str36 (ch);
            std::string notes = std::string (1, id) + "3Cff";
            if (ch < 12) notes += std::string (";") + id + "3Eff;" + id + "3Gff";
            if (ch >= 12) notes = std::string (1, id) + (ch == 12 ? "2Cf" : ch == 14 ? "5Cf" : "3Cf");
            e.submit (notes);
            Stats st; renderSeconds (e, 3.0, st);
            std::printf ("ch %c %-7s peak %6.2f dBFS  rms %6.1f dB %s\n", id, engineName (e.getSettings (ch).engine),
                         gainToDb (st.peak), rmsDb (st), st.nan ? "NaN!" : "");
        }
    }

    if (mode == "all" || mode == "fx")
    {
        std::printf ("\n== Effects at full wet vs dry (ch0 chord, ch2 pad) ==\n");
        Stats dry;
        {
            PilotEngine e; e.prepare (SR, BLOCK); neutralFx (e);
            e.submit ("03Cff;03Eff;03Gff;23Aff;24Cff");
            renderSeconds (e, 3.0, dry);
        }
        std::printf ("dry           rms %6.1f dB peak %6.2f\n", rmsDb (dry), gainToDb (dry.peak));
        for (int f = 0; f < kNumFx; ++f)
        {
            for (float v : { 0.0f, 0.5f, 1.0f })
            {
                PilotEngine e; e.prepare (SR, BLOCK); neutralFx (e);
                if (f == FxEqu) { e.fxW[f] = 1.0f; e.fxV[f] = v; }
                else if (f == FxVol) { e.fxW[f] = 12.0f / 15; e.fxV[f] = v; }
                else { e.fxW[f] = 1.0f; e.fxV[f] = v; }
                e.submit ("03Cff;03Eff;03Gff;23Aff;24Cff");
                Stats st; renderSeconds (e, 3.0, st);
                std::printf ("%s w=f v=%.1f  rms %6.1f dB (%+5.1f) peak %6.2f %s\n", kFxInfo[f].code, v, rmsDb (st), rmsDb (st) - rmsDb (dry),
                             gainToDb (st.peak), st.nan ? "NaN!" : "");
            }
        }
    }

    if (mode == "all" || mode == "cpu")
    {
        std::printf ("\n== CPU stress: all 16 channels x 8 voices, all effects on ==\n");
        PilotEngine e; e.prepare (SR, BLOCK);
        for (int i = 0; i < kNumFx; ++i) { if (i < FxEqu) e.fxW[i] = 0.5f; e.fxV[i] = 0.5f; }
        std::string s;
        for (int ch = 0; ch < 16; ++ch)
            for (int k = 0; k < 8; ++k) s += std::string (1, str36 (ch)) + std::to_string (2 + k % 4) + "CDEFGAB"[k % 7] + "ff;";
        e.submit ("bpm60");
        Stats st;
        auto t0 = std::chrono::high_resolution_clock::now();
        renderSeconds (e, 10.0, st, nullptr, nullptr, [&] (double t) { if (std::fmod (t, 1.0) < BLOCK / SR) e.submit (s); });
        auto t1 = std::chrono::high_resolution_clock::now();
        const double el = std::chrono::duration<double> (t1 - t0).count();
        std::printf ("10 s rendered in %.3f s  -> %.1f%% of one core (worst case)  peak %.2f dBFS %s\n", el, el * 10.0, gainToDb (st.peak), st.nan ? "NaN!" : "");
    }

    if (mode == "all" || mode == "demo")
    {
        PilotEngine e; e.prepare (SR, BLOCK);
        std::vector<float> L, R; Stats st;
        e.submit ("bpm118;REV48;CHO46;DEL38;COMf4;STEf9");
        int step = 0;
        const double stepSec = 60.0 / 118.0 / 4.0;
        double next = 0.0;
        const char* bass = "CCgCCCfC";
        renderSeconds (e, 24.0, st, &L, &R, [&] (double t)
        {
            while (t >= next)
            {
                const int s = step % 16;
                std::string c;
                if (s % 4 == 0) c += "c2Cf;";
                if (s % 8 == 4) c += "d3Cd;";
                if (s % 2 == 1) c += "e5C8;";
                if (s == 14) c += "f2G9;";
                c += std::string ("b2") + bass[s % 8] + "b;";
                const int bar = (step / 16) % 4;
                static const char* chords[4][3] = { { "C", "E", "G" }, { "a", "C", "f" }, { "F", "A", "C" }, { "G", "B", "D" } };
                if (s == 0) for (int k = 0; k < 3; ++k) c += std::string ("2") + (k == 2 && bar == 1 ? "4" : "3") + chords[bar][k] + "9f;";
                if (step >= 64 && (s == 2 || s == 7 || s == 10)) c += std::string ("6") + "4" + chords[bar][s % 3] + "a6;";
                if (step >= 32 && s % 3 == 0) c += std::string ("a5") + chords[bar][(s / 3) % 3] + "84;";
                if (step >= 96 && s == 8) c += "74Eb8;";
                e.submit (c);
                ++step; next += stepSec;
            }
        });
        juce::File out (argc > 2 ? argv[2] : "demo.wav");
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (out.createOutputStream().release());
        auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (SR).withNumChannels (2).withBitsPerSample (24);
        auto writer = wav.createWriterFor (os, opts);
        const float* ch[2] = { L.data(), R.data() };
        writer->writeFromFloatArrays (ch, 2, (int) L.size());
        writer.reset();
        std::printf ("\ndemo: %s  peak %.2f dBFS rms %.1f dB %s\n", out.getFullPathName().toRawUTF8(), gainToDb (st.peak), rmsDb (st), st.nan ? "NaN!" : "");
    }
    return 0;
}
