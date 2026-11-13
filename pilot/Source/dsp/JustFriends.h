// Pilot Sealflake — Just Friends (Mannequins / Whimsical Raps) as a channel engine ('j').
//
// Six slopes IDENTITY..6N, like the module: TIME, INTONE, RAMP, CURVE, FM, the shape/sound
// RANGE switch, the transient/sustain/cycle MODE switch and the RUN jack (SHIFT, STRATA,
// VOLLEY, SPILL, PLUME, FLOOM). The Teletype's JF.* ops arrive as Just Type commands
// (command(): JF.TR, JF.VTR, JF.NOTE, JF.VOX, JF.PITCH, JF.TICK); RMODE, RUN, SHIFT, MODE,
// GOD, TUNE and QT are part of JfSettings. Pilot notes on a JF channel play like JF.NOTE.
//
// Voltages follow the module: 1 V/oct (Teletype: 1638.4 per volt), RUN -5..+5 V,
// velocity 5 V = a normal trigger. In synthesis mode N 0 is the TIME pitch; with TIME at
// noon that is C4 (261.63 Hz), the same pitch as Pilot note 60.
#pragma once
#include "DspCommon.h"
#include <atomic>
#include <cmath>

namespace sf
{
struct JfSettings
{
    // panel
    float time = 0.5f, intone = 0.5f, ramp = 0.5f, curve = 0.5f, fm = 0.5f;   // knobs 0..1 (noon = 0.5)
    int8_t range = 1;    // 0 shape, 1 sound
    int8_t mode = 1;     // 0 transient, 1 sustain, 2 cycle
    // Just Type (Teletype) state
    bool runOn = false;  // JF.RMODE: as if a cable were patched into RUN
    float run = 0.0f;    // JF.RUN volts
    float shift = 0.0f;  // JF.SHIFT volts (V/oct transpose of TIME)
    bool synth = false;  // JF.MODE 1: synthesis (sound) / Geode (shape)
    bool god = false;    // JF.GOD: A = 432 Hz
    int16_t qt = 0;      // JF.QT: Geode quantize (divisions of the measure, 0 = off)
    int16_t tuneN[6] { 0, 0, 0, 0, 0, 0 }, tuneD[6] { 0, 0, 0, 0, 0, 0 };   // JF.TUNE (0 = INTONE)
};

inline const char* jfRangeCode (int r) { return r == 0 ? "sh" : "sn"; }
inline const char* jfModeCode (int m) { static const char* c[3] = { "tr", "su", "cy" }; return c[clamp (m, 0, 2)]; }

// CURVE: 12:00 linear; CW log -> cosine; CCW expo -> trapezoid -> square
inline float jfShape (float x, float c)
{
    x = clamp (x, 0.0f, 1.0f);
    auto lerp = [] (float a, float b, float t) { return a + (b - a) * t; };
    if (c >= 0.5f)
    {
        const float k = (c - 0.5f) * 2.0f;
        const float lg = 1.0f - (1.0f - x) * (1.0f - x);
        const float cs = 0.5f - 0.5f * std::cos ((float) kPi * x);
        return k < 0.5f ? lerp (x, lg, k * 2.0f) : lerp (lg, cs, k * 2.0f - 1.0f);
    }
    const float k = (0.5f - c) * 2.0f;
    const float ex = x * x * x;
    if (k < 0.5f) return lerp (x, ex, k * 2.0f);
    const float m = k * 2.0f - 1.0f;
    const float tr = clamp (x * (1.0f + 40.0f * m * m), 0.0f, 1.0f);
    return lerp (ex, tr, std::min (1.0f, m * 3.0f));
}

inline double jfBaseHz (float time, int range, bool god)
{
    if (range == 0) return 0.25 * std::pow (640.0, (double) clamp (time, 0.0f, 1.0f));               // 4 s .. 160 Hz
    return 261.6255653 * std::pow (2.0, ((double) clamp (time, 0.0f, 1.0f) - 0.5) * 9.4) * (god ? 432.0 / 440.0 : 1.0);   // ~10 Hz .. 6.8 kHz
}

class JustFriends
{
public:
    static constexpr int kN = 6, kOs = 4;
    enum Op : uint8_t { OpTr = 1, OpVtr = 5, OpTick = 7, OpVox = 8, OpNote = 9, OpPitch = 13, OpRelease = 100 };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        osr = sr * kOs;
        for (auto* b : { &aa1, &aa2 }) setLowpass (*b, sr * 0.45, osr, 0.707);
        dc.set ((float) sr);
        reset();
    }

    void reset()
    {
        for (auto& s : sl) s = Slope();
        aa1.reset(); aa2.reset(); dc.reset();
        measurePos = 0;
    }

    // ---- control (audio thread, from the engine's command queue) ----
    // idx: 0 = all six, 1..6 = one slope. a/b: Teletype values (16-bit)
    void command (int op, int idx, int a, int b, const JfSettings& js)
    {
        auto each = [&] (auto&& fn) { if (idx <= 0) for (int i = 0; i < kN; ++i) fn (i); else if (idx <= kN) fn (idx - 1); };
        switch (op)
        {
            case OpTr:  each ([&] (int i) { setGate (i, a != 0, 1.0f, js); }); break;
            case OpVtr: each ([&] (int i) { setGate (i, true, velFromTT (a), js); sl[i].autoOff = (int) (0.010 * sr); }); break;
            case OpPitch: each ([&] (int i) { sl[i].pitchV = (float) a / 1638.4f; sl[i].pitched = true; }); break;
            case OpVox:
                if (! js.synth) break;
                if (js.range == 0) each ([&] (int i) { startGeode (i, a, b, js); });
                else each ([&] (int i) { if (b == 0) voiceOff (i); else voiceOn (i, (float) a / 1638.4f, velFromTT (b), -1, js); });
                break;
            case OpNote:
                if (! js.synth) break;
                if (js.range == 0) { const int i = nextSlot(); startGeode (i, a, b, js); }
                else if (b == 0) { for (int i = 0; i < kN; ++i) if (sl[i].voice && std::fabs (sl[i].pitchV - (float) a / 1638.4f) < 1.0e-4f) voiceOff (i); }
                else playPitch ((float) a / 1638.4f, velFromTT (b), -1, js);
                break;
            case OpTick:
                if (a > 48) { tickBpm = (double) a; tickPerMeasure = 0; }
                else if (a > 0)
                {
                    if (lastTickAt >= 0 && samplesSinceTick > 0) tickBpm = 60.0 * sr / ((double) samplesSinceTick * a / 4.0);
                    tickPerMeasure = a; lastTickAt = 0; samplesSinceTick = 0;
                }
                break;
            default: break;
        }
    }

    // Pilot notes (MIDI, UDP, PI.N): a polyphonic note, like JF.NOTE in synthesis mode
    void noteOn (double pitchVolts, float vel, int lenSamples, const JfSettings& js)
    {
        playPitch ((float) pitchVolts, 0.3f + 1.4f * clamp (vel, 0.0f, 1.0f), lenSamples, js, true);
    }
    void noteOff (double pitchVolts)
    {
        for (int i = 0; i < kN; ++i)
            if (sl[i].voice && sl[i].fromPilot && sl[i].gate && std::fabs (sl[i].pitchV - (float) pitchVolts) < 1.0e-4f) voiceOff (i);
    }
    void allOff() { for (int i = 0; i < kN; ++i) { sl[i].gate = false; sl[i].geode = false; if (sl[i].voice) voiceOff (i); } }
    void kill() { reset(); }

    int gated() const { int c = 0; for (auto& s : sl) if (s.gate || s.geode) ++c; return c; }
    bool busy() const { for (auto& s : sl) if (s.seg != Idle || s.env > 1.0e-4f || s.geode || s.gate) return true; return false; }
    float out (int i) const { return sl[clamp (i, 0, kN - 1)].lastOut; }   // tests / UI

    // Renders the MIX output (mono, added to `buf`). bpm: Pilot's tempo (Geode default clock)
    void render (float* buf, int n, const JfSettings& js, double bendVolts, double bpm)
    {
        if (js.synth != lastSynth) { if (! js.synth) for (int i = 0; i < kN; ++i) { sl[i].geode = false; if (sl[i].voice) voiceOff (i); } lastSynth = js.synth; }

        lastBpm = bpm;
        const bool sound = js.range == 1;
        const float r = clamp (js.ramp, 0.004f, 0.996f);
        const float runV = js.runOn ? clamp (js.run, -5.0f, 5.0f) : 0.0f;
        const double base = jfBaseHz (js.time, js.range, js.god) * std::pow (2.0, (double) js.shift);
        const float fmAmt = (js.fm - 0.5f) * 2.0f;   // < 0: INTONE side (detune with no input), > 0: TIME side
        double rate[kN];
        for (int i = 0; i < kN; ++i)
        {
            const int k = i + 1;
            double ratio = std::pow ((double) k, 2.0 * (double) js.intone - 1.0);
            if (js.tuneN[i] > 0 && js.tuneD[i] > 0) ratio = (double) js.tuneN[i] / (double) js.tuneD[i];
            if (fmAmt < 0.0f) ratio *= std::pow (2.0, (double) (-fmAmt) * 0.25 * (double) i / 12.0);   // up to 1/4 semitone per OUT step
            const auto& s = sl[i];
            double hz = s.pitched || s.voice ? jfBaseHz (js.time, js.range, js.god) * std::pow (2.0, (double) s.pitchV + js.shift + bendVolts)
                                             : base * ratio;
            rate[i] = clamp (hz, 0.005, sr * 0.45);
        }

        // Geode clock
        const double measureSec = 4.0 * 60.0 / clamp (tickBpm > 0 ? tickBpm : bpm, 10.0, 999.0);
        const double measureLen = measureSec * sr;
        const float shiftTh = runV < 0 ? r * (1.0f + runV / 5.0f) : r + (1.0f - r) * runV / 5.0f;   // SHIFT / SPILL receptiveness
        const float strataY = clamp (0.5f + runV / 10.0f, 0.0f, 1.0f);
        const float floomRatio = (float) std::pow (2.0, runV / 5.0);
        const float floomIndex = 0.4f + std::max (0.0f, fmAmt) * 2.6f;
        const int mode = clamp ((int) js.mode, 0, 2);
        const bool shift = js.runOn && ! sound && mode == 0, strata = js.runOn && ! sound && mode == 1, volley = js.runOn && ! sound && mode == 2;
        const bool spill = js.runOn && sound && mode == 0, plume = js.runOn && sound && mode == 1, floom = js.runOn && sound && mode == 2;
        const float plumeTau = 0.25f * std::pow (2.0f, -runV / 1.25f);   // RUN 0 ~ vactrol, -5 V slow, +5 V blips

        for (int t = 0; t < n; ++t)
        {
            ++samplesSinceTick;
            measurePos += 1.0; if (measurePos >= measureLen) measurePos -= measureLen;
            // Geode: rhythmic re-triggers
            for (int i = 0; i < kN; ++i)
            {
                auto& s = sl[i];
                if (! s.geode) continue;
                if (s.geodeWait > 0) { --s.geodeWait; continue; }
                trigger (i, s.amp, shift, shiftTh, r);
                if (s.geodeLeft == 0) { s.geode = false; continue; }
                if (s.geodeLeft > 0) --s.geodeLeft;
                s.geodeWait = std::max (1, (int) (measureLen / std::max (1, s.geodeDivs) / std::pow ((double) (i + 1), 2.0 * js.intone - 1.0)));
            }
            // envelopes for voices / PLUME (per output sample)
            for (int i = 0; i < kN; ++i)
            {
                auto& s = sl[i];
                if (s.autoOff > 0 && --s.autoOff == 0) s.gate = false;
                if (s.lenLeft > 0 && --s.lenLeft == 0) voiceOff (i);
                if (s.voice)
                {
                    float target = 0.0f, tau = 0.12f;
                    if (mode == 0)      { target = s.strike ? 1.0f : 0.0f; tau = s.strike ? 0.002f : (plume ? plumeTau : 0.45f); }
                    else if (mode == 1) { target = s.gate ? 1.0f : 0.0f; tau = s.gate ? 0.004f : (plume ? plumeTau : 0.12f); }
                    else                { target = s.gate ? 1.0f : 0.0f; tau = s.gate ? 0.02f : 0.5f; }
                    s.env += (target - s.env) * (1.0f - std::exp (-1.0f / (tau * (float) sr)));
                    if (s.strike && s.env > 0.97f) s.strike = false;
                }
                else if (plume)
                {
                    const float target = s.strike || s.gate ? 1.0f : 0.0f;
                    const float tau = target > 0 ? 0.0015f : s.plumeTau;
                    s.env += (target - s.env) * (1.0f - std::exp (-1.0f / (tau * (float) sr)));
                    if (s.strike && s.env > 0.97f) s.strike = false;
                }
            }

            float mixOs = 0.0f;
            for (int os = 0; os < kOs; ++os)
            {
                float mix = 0.0f;
                for (int i = 0; i < kN; ++i)
                {
                    auto& s = sl[i];
                    const double fr = rate[i] / osr;
                    float y;
                    if (s.voice || plume)   // free-running oscillator (voices, PLUME's oscillators) gated by an envelope
                    {
                        s.ph += fr; if (s.ph >= 1.0) s.ph -= std::floor (s.ph);
                        float p = (float) s.ph;
                        if (floom) p = wrap01 (p + floomIndex * 0.16f * std::sin ((float) kTwoPi * wrap01 ((float) s.ph * floomRatio)));
                        y = cycShape (p, r, js.curve) * s.env;
                        if (plume) y = s.lpg.process (y, 200.0f + s.env * s.env * 14000.0f, (float) osr);
                    }
                    else
                    {
                        const bool free = (mode == 2 && ! volley && ! s.geode && ! (js.synth && ! sound)) || (spill && i == 0);
                        y = runSlope (s, i, fr, r, js.curve, mode, free, strata, strataY, volley, floom, floomRatio, floomIndex);
                        if (floom && s.seg != Idle)   // FLOOM: each slope phase-modulated by its own cosine (ratio from RUN)
                        {
                            const float p = cyclePos (s, r);
                            y = cycShape (wrap01 (p + floomIndex * 0.16f * std::cos ((float) kTwoPi * wrap01 (p * floomRatio))), r, js.curve);
                        }
                        if (spill && i == 0 && s.wrapped)
                            for (int j = 1; j < kN; ++j) { auto& o = sl[j]; if (o.seg == Idle || cyclePos (o, r) >= shiftTh) { o.seg = Rise; o.x = 0; o.amp = 1.0f; } }
                    }
                    y *= s.amp;
                    if (os == kOs - 1) s.lastOut = y;
                    if (sound) mix += y * (2.0f / 3.0f);
                    else mix = std::max (mix, y / (float) (i + 1));
                }
                mixOs = aa2.process (aa1.process (mix));
            }
            buf[t] += dc.process (mixOs);
        }
    }

private:
    enum Seg : uint8_t { Idle, Rise, Fall };
    struct Lpg   // one-pole lowpass with moving cutoff (PLUME's low-pass-gate colour)
    {
        float z = 0;
        float process (float x, float fc, float fs) { const float g = 1.0f - std::exp (-(float) kTwoPi * std::min (fc, fs * 0.45f) / fs); z += g * (x - z); return z; }
    };
    struct Slope
    {
        Seg seg = Idle; float x = 0; bool held = false, gate = false, wrapped = false;
        float amp = 1.0f, floorY = 0.0f, lastOut = 0.0f;
        int volleyLeft = 0, autoOff = 0, lenLeft = 0;
        // voices (synthesis)
        bool voice = false, pitched = false, strike = false, fromPilot = false; float pitchV = 0, env = 0, plumeTau = 0.25f; double ph = 0; uint64_t age = 0;
        Lpg lpg;
        // Geode
        bool geode = false; int geodeDivs = 1, geodeLeft = 0, geodeWait = 0;
    };

    static float wrap01 (float p) { return p - std::floor (p); }
    static float cycShape (float p, float r, float c) { return p < r ? jfShape (p / r, c) : jfShape (1.0f - (p - r) / (1.0f - r), c); }
    static float cyclePos (const Slope& s, float r) { return s.seg == Rise ? s.x * r : s.seg == Fall ? r + s.x * (1.0f - r) : 1.0f; }
    float velFromTT (int v) const { return clamp ((float) v / 8192.0f, 0.0f, 2.0f); }

    // TRIGGER input (rising edge) for a slope, honouring MODE
    void trigger (int i, float amp, bool shift, float shiftTh, float r)
    {
        auto& s = sl[i];
        s.amp = amp;
        if (s.seg == Idle || (shift && cyclePos (s, r) >= shiftTh)) { s.seg = Rise; s.x = 0; s.held = false; }
        s.strike = true;
        s.plumeTau = curPlumeTau;
    }

    void setGate (int i, bool on, float amp, const JfSettings& js)
    {
        auto& s = sl[i];
        const bool rising = on && ! s.gate;
        s.gate = on;
        if (! rising) return;
        s.amp = amp;
        const float r = clamp (js.ramp, 0.004f, 0.996f);
        const float runV = js.runOn ? clamp (js.run, -5.0f, 5.0f) : 0.0f;
        curPlumeTau = 0.25f * std::pow (2.0f, -runV / 1.25f);
        s.plumeTau = curPlumeTau;
        s.strike = true;
        if (s.voice) return;
        const int mode = clamp ((int) js.mode, 0, 2);
        const bool sound = js.range == 1;
        if (mode == 0)
        {
            const bool shift = js.runOn && ! sound;
            const float th = runV < 0 ? r * (1.0f + runV / 5.0f) : r + (1.0f - r) * runV / 5.0f;
            if (s.seg == Idle || (shift && cyclePos (s, r) >= th)) { s.seg = Rise; s.x = 0; }
        }
        else if (mode == 1)
        {
            if (s.seg == Fall) { s.seg = Rise; s.x = 1.0f - s.x; } else if (s.seg == Idle) { s.seg = Rise; s.x = 0; }
            s.held = false; s.floorY = 0;
        }
        else
        {
            s.seg = Rise; s.x = 0; s.held = false;
            if (js.runOn && ! sound)   // VOLLEY: number of cycles from RUN when the trigger arrives
                s.volleyLeft = runV >= 0 ? (int) std::lround (6.0 + runV / 5.0 * 30.0) : (int) std::lround (std::max (0.0, 6.0 * (1.0 + runV / 4.0)));
            if (js.runOn && ! sound && s.volleyLeft == 0) s.seg = Idle;
        }
    }

    // one oversampled step of a slope; returns 0..1
    float runSlope (Slope& s, int, double fr, float r, float c, int mode, bool free, bool strata, float strataY,
                    bool volley, bool, float, float)
    {
        s.wrapped = false;
        if (s.seg == Idle && free) { s.seg = Rise; s.x = 0; }
        if (s.seg == Rise)
        {
            if (! s.held) s.x += (float) (fr / r);
            if (s.x >= 1.0f)
            {
                const float over = (s.x - 1.0f) * r / (1.0f - r);   // carry the overshoot into the fall (keeps pitch exact)
                s.x = 1.0f;
                if (mode == 1 && s.gate && ! strata) s.held = true;
                else { s.seg = Fall; s.x = std::min (over, 0.999f); s.held = false; s.floorY = (mode == 1 && strata && s.gate) ? strataY : 0.0f; }
                return jfShape (1.0f, c);
            }
            return jfShape (s.x, c);
        }
        if (s.seg == Fall)
        {
            if (mode == 1 && ! s.gate) s.floorY = 0.0f;
            const float y = jfShape (1.0f - s.x, c);
            if (s.floorY > 0.0f && y <= s.floorY) return y;   // STRATA plateau while the gate is high
            s.x += (float) (fr / (1.0f - r));
            if (s.x >= 1.0f)
            {
                s.x = std::min ((s.x - 1.0f) * (1.0f - r) / r, 0.999f); s.wrapped = true;
                if (free) { s.seg = Rise; }
                else if (volley && mode == 2 && --s.volleyLeft > 0) { s.seg = Rise; }
                else { s.seg = Idle; s.x = 0.0f; }
            }
            return y;
        }
        return 0.0f;
    }

    int nextSlot()
    {
        int best = 0;
        for (int i = 0; i < kN; ++i) if (! sl[i].gate && sl[i].env < sl[best].env) best = i;
        for (int i = 0; i < kN; ++i) if (! sl[i].gate && sl[i].env < 1.0e-3f && ! sl[i].geode) { best = i; break; }
        bool allBusy = true; for (auto& s : sl) if (! s.gate) allBusy = false;
        if (allBusy) { best = 0; for (int i = 1; i < kN; ++i) if (sl[i].age < sl[best].age) best = i; }
        return best;
    }

    void playPitch (float volts, float amp, int lenSamples, const JfSettings& js, bool pilot = false)
    {
        for (int i = 0; i < kN; ++i)   // same pitch retriggers its voice
            if (sl[i].voice && sl[i].gate && std::fabs (sl[i].pitchV - volts) < 1.0e-4f) { voiceOn (i, volts, amp, lenSamples, js, pilot); return; }
        voiceOn (nextSlot(), volts, amp, lenSamples, js, pilot);
    }

    void voiceOn (int i, float volts, float amp, int lenSamples, const JfSettings& js, bool pilot = false)
    {
        auto& s = sl[i];
        if (! s.voice) { s.ph = 0; s.env = 0; }
        s.voice = true; s.pitched = true; s.pitchV = volts; s.amp = amp; s.gate = true; s.strike = true; s.fromPilot = pilot;
        s.lenLeft = lenSamples > 0 ? lenSamples : 0;
        s.age = ++clock;
        const float runV = js.runOn ? clamp (js.run, -5.0f, 5.0f) : 0.0f;
        s.plumeTau = 0.25f * std::pow (2.0f, -runV / 1.25f);
    }
    void voiceOff (int i) { sl[i].gate = false; sl[i].lenLeft = 0; }

    void startGeode (int i, int divs, int repeats, const JfSettings& js)
    {
        auto& s = sl[i];
        s.voice = false; s.pitched = false;
        if (divs <= 0) { s.geode = false; return; }
        s.geode = true; s.geodeDivs = divs; s.geodeLeft = repeats < 0 ? -1 : repeats; s.amp = 1.0f; s.age = ++clock;
        s.geodeWait = 0;
        if (js.qt > 0)
        {
            const double measureLen = 4.0 * 60.0 / clamp (tickBpm > 0 ? tickBpm : lastBpm, 10.0, 999.0) * sr;
            const double grid = measureLen / js.qt;
            s.geodeWait = (int) (grid - std::fmod (measurePos, grid));
        }
    }

    static void setLowpass (Biquad& b, double fc, double fs, double q)
    {
        const double w = kTwoPi * fc / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q), a0 = 1 + alpha;
        b.b0 = (1 - c) / 2 / a0; b.b1 = (1 - c) / a0; b.b2 = (1 - c) / 2 / a0; b.a1 = -2 * c / a0; b.a2 = (1 - alpha) / a0;
    }

    double sr = 48000.0, osr = 192000.0;
    Slope sl[kN];
    Biquad aa1, aa2;
    DCBlocker dc;
    uint64_t clock = 0;
    bool lastSynth = false;
    float curPlumeTau = 0.25f;
    double tickBpm = 0.0, lastBpm = 120.0, measurePos = 0.0;
    int tickPerMeasure = 0; long lastTickAt = -1, samplesSinceTick = 0;
};
} // namespace sf
