// Pilot Sealflake — polyphonic 16-channel synthesis engine
#pragma once
#include "DspCommon.h"
#include "JustFriends.h"
#include <atomic>

namespace sf
{
// Waveform codes (compatible with original Pilot OSC command)
// index: 0..3 = si tr sq sw (1 voice), 4..7 = 2x unison, 8..11 = 4x, 12..15 = 8x
static const char* const kWaveCodes[16] = { "si", "tr", "sq", "sw", "2i", "2r", "2q", "2w",
                                            "4i", "4r", "4q", "4w", "8i", "8r", "8q", "8w" };

inline int waveCodeIndex (const char* two)
{
    char a = (char) std::tolower ((unsigned char) two[0]), b = (char) std::tolower ((unsigned char) two[1]);
    for (int i = 0; i < 16; ++i) if (kWaveCodes[i][0] == a && kWaveCodes[i][1] == b) return i;
    return -2; // unknown
}
inline Wave waveOf (int code)  { return (Wave) (code & 3); }
inline int unisonOf (int code) { static const int u[4] = { 1, 2, 4, 8 }; return code < 0 ? 0 : u[(code >> 2) & 3]; }

// Engines
enum Engine : char { EngAnalog = 'a', EngFM = 'f', EngAM = 'm', EngPluck = 'p',
                     EngKick = 'k', EngSnare = 's', EngHat = 'h', EngTom = 't', EngJF = 'j' };

inline bool isValidEngine (char c)
{
    return c == 'a' || c == 'f' || c == 'm' || c == 'p' || c == 'k' || c == 's' || c == 'h' || c == 't' || c == 'j';
}
inline bool isDrum (char e) { return e == EngKick || e == EngSnare || e == EngHat || e == EngTom; }
inline const char* engineName (char e)
{
    switch (e)
    {
        case EngAnalog: return "ANALOG"; case EngFM: return "FM"; case EngAM: return "AM";
        case EngPluck: return "PLUCK";   case EngKick: return "KICK"; case EngSnare: return "SNARE";
        case EngHat: return "HAT";       case EngTom: return "TOM"; case EngJF: return "JUST FRIENDS"; default: return "?";
    }
}

struct ChannelSettings
{
    char  engine = EngAnalog;
    int   wave1 = 3, wave2 = -1;                // wave code index, -1 = off
    float env[4] = { 0.0f, 0.5f, 0.6f, 0.4f };  // A D S R (normalized 0..1)
    float fil[3] = { 1.0f, 0.0f, 0.0f };        // cutoff, resonance, env amount
    float mod[2] = { 0.2f, 0.5f };              // ratio (table index), index/depth/level

    // extended voice settings — only reachable via "<ch>:" commands
    int   voices      = 8;      // v  polyphony 1..16 (1 = mono)
    float glideMs     = 0.0f;   // g  portamento time
    bool  legato      = false;  // l  mono legato (no envelope retrigger)
    float pan         = 0.0f;   // p  -100..100
    float levelDb     = 0.0f;   // a  channel level
    float detuneCents = -1.0f;  // d  unison spread (<0 = automatic)
    float width       = 100.0f; // w  unison stereo width %
    float bendRange   = 2.0f;   // b  pitch bend range (semitones)
    float transpose   = 0.0f;   // t  semitones
    float fineCents   = 0.0f;   // n  fine tune
    float velSens     = 100.0f; // s  velocity sensitivity %
    float keyTrack    = 40.0f;  // k  filter key tracking %
    float driftCents  = 3.0f;   // r  analog pitch drift

    JfSettings jf;              // engine 'j' (Just Friends) panel + Just Type state

    void resetExtended() { const ChannelSettings d; voices = d.voices; glideMs = d.glideMs; legato = d.legato; pan = d.pan; levelDb = d.levelDb;
                           detuneCents = d.detuneCents; width = d.width; bendRange = d.bendRange; transpose = d.transpose; fineCents = d.fineCents;
                           velSens = d.velSens; keyTrack = d.keyTrack; driftCents = d.driftCents; }
};

inline float envTime (float x) { return 0.0015f + 4.0f * std::pow (clamp (x, 0.0f, 1.0f), 2.2f); }
inline float modRatio (float x)   // table points at legacy hex steps, linear in between
{
    static const float t[16] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 11.0f, 14.0f };
    const float p = clamp (x, 0.0f, 1.0f) * 15.0f;
    const int i = std::min ((int) p, 14);
    return t[i] + (p - (float) i) * (t[i + 1] - t[i]);
}
inline double midiToHz (double n) { return 440.0 * std::pow (2.0, (n - 69.0) / 12.0); }

//==============================================================================
class Voice
{
public:
    static constexpr int kMaxUni = 8;
    static constexpr int kBlock = 16;
    static constexpr int kFade = 96;

    struct StartArgs { int note; float vel; int len; uint32_t seed; double glideFrom = -1.0; };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        amp.setSampleRate (sr); filEnv.setSampleRate (sr); modEnv.setSampleRate (sr);
        pbuf.assign ((size_t) (sr / 16.0) + 16, 0.0f);
        dc.set ((float) sr);
        active = false;
    }

    bool isActive() const  { return active; }
    bool isGateOn() const  { return active && ! released && fadeLeft == 0; }
    int  getNote() const   { return note; }
    float level() const    { return amp.value(); }
    uint64_t age = 0;
    bool sustainedByPedal = false;

    void trigger (const StartArgs& a, const ChannelSettings& cs)
    {
        if (fadeLeft > 0 && hasPending) { pending = a; pendingSettings = cs; return; }
        if (active && fadeLeft == 0 && amp.value() > 0.002f && a.note != note)
        {
            pending = a; hasPending = true; fadeLeft = kFade; pendingSettings = cs;
            return;
        }
        begin (a, cs, active && a.note == note);
    }

    void release()
    {
        if (! active) return;
        if (hasPending) { hasPending = false; }
        released = true;
        if (! isDrum (engine)) { amp.gateOff(); filEnv.gateOff(); modEnv.gateOff(); }
    }

    void kill() { active = false; hasPending = false; fadeLeft = 0; amp.reset(); }

    // Mono mode: move this voice to a new note without fading (glides if glide > 0)
    void retarget (const StartArgs& a, const ChannelSettings& cs, bool retriggerEnv)
    {
        if (! active || isDrum (engine) || engine == EngPluck || cs.engine != engine) { begin (a, cs, active); return; }
        note = a.note; vel = clamp (a.vel, 0.0f, 1.0f);
        const float sens = clamp (cs.velSens, 0.0f, 100.0f) / 100.0f;
        velGain = (1.0f - sens) + sens * std::pow (vel, 1.4f);
        lenLeft = a.len; released = false; sustainedByPedal = false;
        pitchTarget = note + cs.transpose + cs.fineCents / 100.0;
        if (cs.glideMs <= 0.0f) pitchNow = pitchTarget;
        if (retriggerEnv) { amp.gateOn(); filEnv.gateOn(); modEnv.gateOn(); }
        hasPending = false; fadeLeft = 0;
    }
    double currentPitch() const { return pitchNow; }

    // Accumulates into L/R. Returns true while sounding.
    bool render (float* L, float* R, int n, const ChannelSettings& cs, float bendSemis)
    {
        if (! active) return false;
        int i = 0;
        while (i < n)
        {
            const int m = std::min (kBlock, n - i);
            control (cs, bendSemis, m);
            for (int j = 0; j < m; ++j)
            {
                float l, r;
                tick (l, r);
                if (fadeLeft > 0)
                {
                    const float g = (float) fadeLeft / (float) kFade;
                    l *= g; r *= g;
                    if (--fadeLeft == 0)
                    {
                        if (hasPending) { hasPending = false; begin (pending, pendingSettings, false); }
                        else { active = false; return false; }
                    }
                }
                L[i + j] += l; R[i + j] += r;
            }
            i += m;
            if (lenLeft > 0) { lenLeft -= m; if (lenLeft <= 0) release(); }
            if (amp.finished() && fadeLeft == 0) { active = false; return false; }
            if (engine == EngPluck) { if (pluckPeak < 2.0e-5f && fadeLeft == 0) { active = false; return false; } pluckPeak = 0.0f; }
        }
        return true;
    }

private:
    void begin (const StartArgs& a, const ChannelSettings& cs, bool retrig)
    {
        const bool wasActive = active;
        rng = Rng (a.seed);
        note = a.note; vel = clamp (a.vel, 0.0f, 1.0f);
        {
            const float sens = clamp (cs.velSens, 0.0f, 100.0f) / 100.0f;
            velGain = (1.0f - sens) + sens * std::pow (vel, 1.4f);
        }
        lenLeft = a.len;
        released = false; sustainedByPedal = false;
        engine = cs.engine;
        pitchTarget = note + cs.transpose + cs.fineCents / 100.0;
        pitchNow = (cs.glideMs > 0.0f && a.glideFrom >= 0.0) ? a.glideFrom : pitchTarget;
        if (isDrum (engine) || engine == EngPluck) pitchNow = pitchTarget;
        baseHz = midiToHz (pitchNow);

        const int w1 = cs.wave1 < 0 ? 0 : cs.wave1;
        uni1 = isDrum (engine) || engine == EngPluck ? 1 : unisonOf (w1);
        uni2 = (engine == EngAnalog || engine == EngAM) && cs.wave2 >= 0 ? unisonOf (cs.wave2) : (engine == EngFM ? 1 : 0);
        if (engine == EngAM && uni2 > 1) uni2 = 1;
        if (cs.wave1 < 0 && (engine == EngAnalog)) uni1 = 0;
        if (uni1 == 0 && uni2 == 0) uni1 = 1;

        setupUnison (uni1, detune1, panL1, panR1, false, cs);
        setupUnison (uni2, detune2, panL2, panR2, true, cs);
        stereo = uni1 > 1 || uni2 > 1;

        if (! retrig || ! wasActive)
        {
            for (int u = 0; u < kMaxUni; ++u)
            {
                osc1[u].phase = uni1 > 1 ? rng.unipolar() : 0.0;
                osc2[u].phase = uni2 > 1 ? rng.unipolar() : 0.0;
            }
            ladL.reset(); ladR.reset(); dc.reset();
            amp.reset(); filEnv.reset(); modEnv.reset();
            drift = 0.0f; firstControl = true;
            for (auto& s : svfs) s.reset();
            hp1.reset(); hp2.reset();
        }

        if (isDrum (engine))
        {
            for (auto& o : osc1) o.phase = 0.0;
            osc2[0].phase = 0.0;
            pitchEnv = 1.0f; clickEnv = 1.0f; toneEnv = 1.0f;
            const float d = envTime (cs.env[1]) * 0.6f + 0.03f;
            float decay = d;
            if (engine == EngHat) decay = d * 0.35f + 0.015f;
            amp.setParams (0.0005f, decay, 0.0f, decay);
            const float tau = engine == EngKick ? 0.006f + cs.mod[0] * 0.08f
                            : engine == EngTom  ? 0.02f + cs.mod[0] * 0.12f
                            : 0.012f;
            pitchCoef = std::exp (-1.0f / (tau * (float) sr));
            clickCoef = std::exp (-1.0f / (0.0035f * (float) sr));
            toneCoef  = std::exp (-1.0f / (d * 0.35f * (float) sr));
            hp1.set (engine == EngHat ? 7000.0f : 1500.0f, (float) sr);
            hp2.set (engine == EngHat ? 7000.0f : 1500.0f, (float) sr);
            svfs[0].set (engine == EngHat ? 9500.0f : 7000.0f, engine == EngHat ? 1.2f : 0.7f, (float) sr);
        }
        else if (engine == EngPluck)
        {
            exciteString (cs);
        }

        amp.gateOn(); filEnv.gateOn(); modEnv.gateOn();
        active = true; fadeLeft = 0; hasPending = false;
    }

    void setupUnison (int n, double* det, float* pl, float* pr, bool flip, const ChannelSettings& cs)
    {
        static const float autoCents[9] = { 0, 0, 7.0f, 0, 12.0f, 0, 0, 0, 17.0f };
        const float spread = cs.detuneCents >= 0.0f ? cs.detuneCents : autoCents[clamp (n, 0, 8)];
        const float widthK = clamp (cs.width, 0.0f, 200.0f) / 100.0f;
        for (int u = 0; u < kMaxUni; ++u) { det[u] = 1.0; pl[u] = pr[u] = 0.7071f; }
        if (n <= 1) return;
        for (int u = 0; u < n; ++u)
        {
            float x = 2.0f * (float) u / (float) (n - 1) - 1.0f;   // -1..1
            const float cents = spread * x * (0.6f + 0.4f * std::fabs (x));
            det[u] = std::pow (2.0, cents / 1200.0);
            float p = clamp ((u & 1 ? -1.0f : 1.0f) * std::fabs (x) * 0.85f * widthK, -1.0f, 1.0f);
            if (flip) p = -p;
            const float ang = (p + 1.0f) * (float) kPi * 0.25f;
            pl[u] = std::cos (ang); pr[u] = std::sin (ang);
        }
        const float norm = 1.0f / std::sqrt ((float) n);
        for (int u = 0; u < n; ++u) { pl[u] *= norm; pr[u] *= norm; }
    }

    void exciteString (const ChannelSettings& cs)
    {
        const double P = sr / baseHz;
        const float bright = clamp (cs.fil[0], 0.0f, 1.0f);
        loopC = 0.18f + 0.8f * bright * (0.55f + 0.45f * vel);
        const double lpDelay = (1.0 - loopC) / loopC;
        double total = std::max (3.0, P - lpDelay);
        pN = std::min ((int) std::floor (total - 0.25), (int) pbuf.size() - 1);
        pN = std::max (pN, 2);
        const double d = total - pN;
        apA = (float) ((1.0 - d) / (1.0 + d));
        apX1 = apY1 = 0.0f; loopZ = 0.0f; ppos = 0;

        const float T60 = envTime (cs.env[1]) * 1.6f + 0.25f;
        loopFb = (float) std::pow (10.0, -3.0 * P / (T60 * sr));
        loopFb = std::min (loopFb, 0.9995f);

        // excitation: velocity-shaped noise burst with pick-position comb
        OnePoleLP lp; lp.set (800.0f + 14000.0f * vel * (0.3f + 0.7f * bright), (float) sr);
        float mean = 0.0f;
        for (int i = 0; i < pN; ++i) { pbuf[(size_t) i] = lp.process (rng.bipolar()); mean += pbuf[(size_t) i]; }
        mean /= (float) pN;
        const int pick = std::max (1, (int) (pN * 0.13f));
        for (int i = pN - 1; i >= 0; --i)
        {
            const float x = pbuf[(size_t) i] - mean;
            const float xd = i - pick >= 0 ? pbuf[(size_t) (i - pick)] - mean : 0.0f;
            pbuf[(size_t) i] = (x - 0.6f * xd) * 0.9f;
        }
    }

    void control (const ChannelSettings& cs, float bendSemis, int m)
    {
        // analog drift (random walk, ±~3 cents)
        const float dr = std::max (0.0f, cs.driftCents);
        drift = clamp (drift * 0.995f + rng.bipolar() * 0.08f * (dr / 3.0f), -dr, dr);
        if (pitchNow != pitchTarget && ! isDrum (engine) && engine != EngPluck)
        {
            const double k = cs.glideMs > 0.0f ? 1.0 - std::exp (-(double) m / (cs.glideMs * 0.001 * sr / 4.0)) : 1.0;
            pitchNow += (pitchTarget - pitchNow) * k;
            if (std::fabs (pitchTarget - pitchNow) < 1.0e-4) pitchNow = pitchTarget;
            baseHz = midiToHz (pitchNow);
        }
        const double pmul = std::pow (2.0, (bendSemis + (isDrum (engine) ? 0.0f : drift * 0.01f)) / 12.0);
        const double f = baseHz * pmul;
        const Wave w1 = waveOf (cs.wave1 < 0 ? 0 : cs.wave1);
        const Wave w2 = waveOf (cs.wave2 < 0 ? 0 : cs.wave2);
        curHz = f;

        if (! isDrum (engine) && engine != EngPluck)
        {
            amp.setParams (envTime (cs.env[0]), envTime (cs.env[1]), cs.env[2], envTime (cs.env[3]));
            filEnv.setParams (envTime (cs.env[0]), envTime (cs.env[1]) * 0.8f, cs.env[2] * 0.5f, envTime (cs.env[3]));
            modEnv.setParams (envTime (cs.env[0]) * 0.5f, envTime (cs.env[1]) * 0.7f, cs.env[2] * 0.6f, envTime (cs.env[3]) * 0.8f);
        }
        else if (engine == EngPluck)
        {
            amp.setParams (0.001f, 0.01f, 1.0f, envTime (cs.env[3]) * 0.5f + 0.01f);
        }

        const double ratio = modRatio (cs.mod[0]);
        switch (engine)
        {
            case EngAnalog:
                for (int u = 0; u < uni1; ++u) osc1[u].set (w1, f * detune1[u], sr);
                for (int u = 0; u < uni2; ++u) osc2[u].set (w2, f * ratio * detune2[u] * 1.0035, sr);
                osc2Level = cs.mod[1];
                break;
            case EngFM:
                for (int u = 0; u < uni1; ++u) osc1[u].set (w1, f * detune1[u], sr);
                osc2[0].set (cs.wave2 < 0 ? Wave::Sine : w2, f * ratio, sr);
                fmIndex = cs.mod[1] * 1.6f * (0.35f + 0.65f * vel);
                break;
            case EngAM:
                for (int u = 0; u < uni1; ++u) osc1[u].set (w1, f * detune1[u], sr);
                osc2[0].set (cs.wave2 < 0 ? Wave::Sine : w2, f * ratio, sr);
                amDepth = cs.mod[1];
                break;
            case EngHat:
            {
                static const double hr[6] = { 1.0, 1.4826, 1.8003, 2.5461, 2.6303, 3.8968 };
                for (int u = 0; u < 6; ++u) osc1[u].set (Wave::Square, f * 0.8 * hr[u], sr);
                break;
            }
            default: break;
        }

        // filter cutoff (per control block, linearly ramped coefficient)
        filterOn = engine == EngAnalog || cs.fil[0] < 0.985f || cs.fil[2] > 0.01f;
        if (filterOn)
        {
            const float fe = engine == EngAnalog || engine == EngFM || engine == EngAM ? filEnv.value() : amp.value();
            const float key = (float) std::pow (f / 261.63, clamp (cs.keyTrack, 0.0f, 200.0f) / 100.0f);
            const float oct = cs.fil[2] * (7.0f * fe + 1.2f * vel);
            const float hz = 20.0f * std::pow (2.0f, 10.0f * cs.fil[0] + oct) * key;
            const float target = Ladder::computeG (hz, (float) sr);
            if (firstControl) { gCur = target; gStep = 0.0f; firstControl = false; }
            else gStep = (target - gCur) / (float) m;
            ladL.k = ladR.k = cs.fil[1] * 3.85f;
        }
    }

    inline void tick (float& outL, float& outR)
    {
        float l = 0.0f, r = 0.0f;
        const float a = amp.tick();
        switch (engine)
        {
            case EngAnalog:
            {
                for (int u = 0; u < uni1; ++u) { const float s = osc1[u].tick(); l += s * panL1[u]; r += s * panR1[u]; }
                for (int u = 0; u < uni2; ++u) { const float s = osc2[u].tick() * osc2Level; l += s * panL2[u]; r += s * panR2[u]; }
                filEnv.tick();
                break;
            }
            case EngFM:
            {
                const float me = modEnv.tick();
                filEnv.tick();
                const float mod = osc2[0].tickPM (fmFb * 0.12);
                fmFb = mod;
                const double pm = (double) (mod * fmIndex * me);
                for (int u = 0; u < uni1; ++u) { const float s = osc1[u].tickPM (pm); l += s * panL1[u]; r += s * panR1[u]; }
                break;
            }
            case EngAM:
            {
                filEnv.tick();
                const float mod = osc2[0].tick();
                const float g = 1.0f - amDepth * 0.5f * (1.0f - mod);
                for (int u = 0; u < uni1; ++u) { const float s = osc1[u].tick() * g; l += s * panL1[u]; r += s * panR1[u]; }
                break;
            }
            case EngPluck:
            {
                const float x = pbuf[(size_t) ppos];
                const float ap = apA * x + apX1 - apA * apY1;
                apX1 = x; apY1 = flushDenormal (ap);
                loopZ = flushDenormal (loopZ + loopC * (ap - loopZ));
                pbuf[(size_t) ppos] = loopZ * loopFb;
                if (++ppos >= pN) ppos = 0;
                l = r = dc.process (x) * 0.7071f * 1.6f;
                pluckPeak = std::max (pluckPeak, std::fabs (x));
                break;
            }
            case EngKick:
            {
                pitchEnv *= pitchCoef; clickEnv *= clickCoef;
                const float sweep = 1.0f + 5.0f * pitchModDepth();
                osc1[0].inc = baseHz * std::exp2 (sweep * pitchEnv) / sr;
                const float body = readTable (WaveTables::get().sineTable(), osc1[0].phase);
                osc1[0].phase += osc1[0].inc; if (osc1[0].phase >= 1.0) osc1[0].phase -= 1.0;
                const float click = hp1.process (rng.bipolar()) * clickEnv * 0.5f;
                const float y = fastTanh (1.7f * (body + click) * a) * 0.9f;
                l = r = y * 0.7071f;
                break;
            }
            case EngTom:
            {
                pitchEnv *= pitchCoef; clickEnv *= clickCoef; toneEnv *= toneCoef;
                const double fhz = baseHz * std::exp2 (1.2f * pitchEnv * (0.4f + pitchModDepth()));
                osc1[0].inc = fhz / sr; osc1[1].inc = fhz * 1.593 / sr;
                const float* st = WaveTables::get().sineTable();
                const float b0 = readTable (st, osc1[0].phase), b1 = readTable (st, osc1[1].phase);
                for (int u = 0; u < 2; ++u) { osc1[u].phase += osc1[u].inc; if (osc1[u].phase >= 1.0) osc1[u].phase -= 1.0; }
                const float click = hp1.process (rng.bipolar()) * clickEnv * 0.25f;
                const float y = fastTanh (1.4f * ((b0 + 0.3f * b1 * toneEnv) * a + click * a)) * 0.9f;
                l = r = y * 0.7071f;
                break;
            }
            case EngSnare:
            {
                pitchEnv *= pitchCoef; toneEnv *= toneCoef;
                osc1[0].inc = baseHz * std::exp2 (0.7f * pitchEnv) / sr;
                const float body = readTable (WaveTables::get().sineTable(), osc1[0].phase);
                osc1[0].phase += osc1[0].inc; if (osc1[0].phase >= 1.0) osc1[0].phase -= 1.0;
                float nz = hp2.process (hp1.process (rng.bipolar()));
                svfs[0].tick (nz); nz = svfs[0].lp;
                const float nAmt = 0.35f + 0.65f * noiseAmt;
                const float y = fastTanh (1.3f * (body * toneEnv * (1.0f - 0.45f * noiseAmt) + nz * nAmt * 1.6f) * a) * 0.9f;
                l = r = y * 0.7071f;
                break;
            }
            case EngHat:
            {
                float m = 0.0f;
                for (int u = 0; u < 6; ++u) m += osc1[u].tick();
                m *= (1.0f / 6.0f);
                const float nz = rng.bipolar();
                const float mix = m * (1.0f - noiseAmt * 0.7f) + nz * noiseAmt * 0.8f;
                svfs[0].tick (mix);
                const float y = hp2.process (hp1.process (svfs[0].bp * 1.6f + svfs[0].hp * 0.4f)) * a * 3.0f;
                l = r = y * 0.7071f;
                break;
            }
            default: break;
        }

        if (filterOn)
        {
            gCur += gStep;
            ladL.G = gCur;
            if (stereo) { ladR.G = gCur; l = ladL.process (l * 0.35f, 1.0f) * 2.857f; r = ladR.process (r * 0.35f, 1.0f) * 2.857f; }
            else        { l = ladL.process (l * 0.35f, 1.0f) * 2.857f; r = l; }
        }

        const float g = (isDrum (engine) ? 1.0f : a) * velGain;
        outL = l * g; outR = r * g;
    }

    float pitchModDepth() const { return noiseAmt; }

public:
    // per-drum extra parameter (mod index), updated per block by Channel
    float noiseAmt = 0.5f;

private:
    double sr = 48000.0, baseHz = 440.0, curHz = 440.0, pitchNow = 69.0, pitchTarget = 69.0;
    bool active = false, released = false, stereo = false, filterOn = false;
    int note = -1, lenLeft = -1, uni1 = 1, uni2 = 0, fadeLeft = 0;
    char engine = EngAnalog;
    float vel = 0.6f, velGain = 0.5f, drift = 0.0f;
    TableOsc osc1[kMaxUni], osc2[kMaxUni];
    double detune1[kMaxUni] {}, detune2[kMaxUni] {};
    float panL1[kMaxUni] {}, panR1[kMaxUni] {}, panL2[kMaxUni] {}, panR2[kMaxUni] {};
    float osc2Level = 0.5f, fmIndex = 0.0f, fmFb = 0.0f, amDepth = 0.0f;
    ADSR amp, filEnv, modEnv;
    Ladder ladL, ladR;
    float gCur = 0.5f, gStep = 0.0f, pluckPeak = 0.0f;
    bool firstControl = true;
    // drums
    float pitchEnv = 1, pitchCoef = 0.99f, clickEnv = 0, clickCoef = 0.9f, toneEnv = 0, toneCoef = 0.99f;
    SVF svfs[2];
    OnePoleHP hp1, hp2;
    // pluck
    std::vector<float> pbuf;
    int pN = 2, ppos = 0;
    float apA = 0, apX1 = 0, apY1 = 0, loopZ = 0, loopC = 0.5f, loopFb = 0.99f;
    DCBlocker dc;
    Rng rng;
    StartArgs pending {};
    ChannelSettings pendingSettings;
    bool hasPending = false;
};

//==============================================================================
class Channel
{
public:
    static constexpr int kVoices = 16;

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        for (auto& v : voices) v.prepare (sr);
        jf.prepare (sr);
        bufL.assign ((size_t) maxBlock, 0.0f);
        bufR.assign ((size_t) maxBlock, 0.0f);
    }

    int polyphony() const { return clamp (settings.voices, 1, kVoices); }

    double jfPitch (int note) const { return (note + settings.transpose + settings.fineCents / 100.0 - 60.0) / 12.0; }

    void noteOn (int note, float vel, int lenSamples, uint64_t& clock, uint32_t seed)
    {
        if (settings.engine == EngJF) { notes.fetch_add (1, std::memory_order_relaxed); jf.noteOn (jfPitch (note), vel, lenSamples, settings.jf); return; }
        Voice::StartArgs a { note, vel, lenSamples, seed, lastPitch };
        notes.fetch_add (1, std::memory_order_relaxed);
        lastPitch = note + settings.transpose + settings.fineCents / 100.0;
        const int n = polyphony();

        if (n == 1)   // mono: one voice, last-note priority, optional legato + glide
        {
            auto& v = voices[0];
            if (lenSamples < 0) pushHeld (note);
            v.age = ++clock;
            if (v.isActive() && ! isDrum (settings.engine))
                v.retarget (a, settings, ! (settings.legato && v.isGateOn()));
            else
                v.trigger (a, settings);
            return;
        }

        for (int i = 0; i < n; ++i)   // retrigger same note
            if (voices[i].isActive() && voices[i].getNote() == note) { voices[i].age = ++clock; voices[i].trigger (a, settings); return; }
        for (int i = 0; i < n; ++i)
            if (! voices[i].isActive()) { voices[i].age = ++clock; voices[i].trigger (a, settings); return; }
        Voice* best = nullptr;   // steal: quietest released voice, otherwise oldest
        for (int i = 0; i < n; ++i)
            if (! voices[i].isGateOn() && (best == nullptr || voices[i].level() < best->level())) best = &voices[i];
        if (best == nullptr)
            for (int i = 0; i < n; ++i)
                if (best == nullptr || voices[i].age < best->age) best = &voices[i];
        best->age = ++clock;
        best->trigger (a, settings);
    }

    void noteOff (int note)
    {
        if (settings.engine == EngJF) { jf.noteOff (jfPitch (note)); return; }
        if (polyphony() == 1)
        {
            removeHeld (note);
            auto& v = voices[0];
            if (! v.isGateOn() || v.getNote() != note) return;
            if (heldCount > 0)   // fall back to the previous held key
            {
                Voice::StartArgs a { held[heldCount - 1], lastVel, -1, 1u, -1.0 };
                v.retarget (a, settings, ! settings.legato);
                return;
            }
            if (sustainPedal) v.sustainedByPedal = true; else v.release();
            return;
        }
        for (auto& v : voices)
            if (v.isGateOn() && v.getNote() == note)
            {
                if (sustainPedal) v.sustainedByPedal = true;
                else v.release();
            }
    }

    void setSustain (bool down)
    {
        sustainPedal = down;
        if (! down)
            for (auto& v : voices)
                if (v.sustainedByPedal) { v.sustainedByPedal = false; v.release(); }
    }

    int gatedVoices() const { if (settings.engine == EngJF) return jf.gated(); int c = 0; for (auto& v : voices) if (v.isGateOn()) ++c; return c; }
    void allOff() { heldCount = 0; for (auto& v : voices) v.release(); jf.allOff(); }
    void kill()   { heldCount = 0; for (auto& v : voices) v.kill(); jf.kill(); }
    void jfCommand (int op, int idx, int a, int b) { if (settings.engine == EngJF) { if (op == JustFriends::OpVtr || op == JustFriends::OpTr || op == JustFriends::OpNote || op == JustFriends::OpVox) notes.fetch_add (1, std::memory_order_relaxed); jf.command (op, idx, a, b, settings.jf); } }

    void render (float* L, float* R, int n)
    {
        std::fill (bufL.begin(), bufL.begin() + n, 0.0f);
        std::fill (bufR.begin(), bufR.begin() + n, 0.0f);
        int count = 0;
        const int poly = polyphony();
        const float bendSemis = bend * settings.bendRange;
        if (settings.engine == EngJF)
        {
            if (lastEngine != EngJF) for (auto& v : voices) v.kill();
            jf.render (bufL.data(), n, settings.jf, bend * settings.bendRange / 12.0, bpm);
            std::copy (bufL.begin(), bufL.begin() + n, bufR.begin());
            if (jf.busy()) ++count;
        }
        else if (lastEngine == EngJF) jf.kill();
        lastEngine = settings.engine;
        for (int i = 0; i < kVoices; ++i)
        {
            if (settings.engine == EngJF) break;
            auto& v = voices[i];
            if (i >= poly && v.isGateOn()) v.release();   // polyphony was reduced
            v.noiseAmt = settings.mod[1];
            if (v.render (bufL.data(), bufR.data(), n, settings, bendSemis)) ++count;
        }
        active.store (count, std::memory_order_relaxed);
        if (count == 0 && peak < 1.0e-6f) { meter.store (0.0f, std::memory_order_relaxed); tap.pushSilence (n); return; }

        const float g = gainFor (settings.engine) * dbToGain (clamp (settings.levelDb, -60.0f, 24.0f));
        const float p = clamp (settings.pan, -100.0f, 100.0f) / 100.0f;
        const float gl = g * std::min (1.0f, 1.0f - p), gr = g * std::min (1.0f, 1.0f + p);
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float l = bufL[(size_t) i] * gl, r = bufR[(size_t) i] * gr;
            L[i] += l; R[i] += r;
            pk = std::max (pk, std::max (std::fabs (l), std::fabs (r)));
        }
        peak = pk;
        tap.push (bufL.data(), bufR.data(), n, g);
        meter.store (std::max (pk, meter.load (std::memory_order_relaxed)), std::memory_order_relaxed);
    }

    static float gainFor (char e)
    {
        switch (e)
        {
            case EngAnalog: return 0.30f; case EngFM: return 0.36f; case EngAM: return 0.36f;
            case EngPluck:  return 0.85f; case EngKick: return 0.80f; case EngSnare: return 0.55f;
            case EngHat:    return 1.20f; case EngTom: return 0.65f; case EngJF: return 0.42f; default: return 0.3f;
        }
    }

    ChannelSettings settings;
    float bend = 0.0f;    // normalized -1..1 (scaled by settings.bendRange)
    double bpm = 120.0;   // for Just Friends' Geode clock
    JustFriends jf;
    float lastVel = 0.66f;
    std::atomic<float> meter { 0.0f };
    std::atomic<int> active { 0 };
    std::atomic<uint32_t> notes { 0 };
    ScopeTap tap;

private:
    void pushHeld (int note) { removeHeld (note); if (heldCount < 32) held[heldCount++] = note; }
    void removeHeld (int note)
    {
        int j = 0;
        for (int i = 0; i < heldCount; ++i) if (held[i] != note) held[j++] = held[i];
        heldCount = j;
    }

    double sr = 48000.0;
    Voice voices[kVoices];
    std::vector<float> bufL, bufR;
    bool sustainPedal = false;
    float peak = 0.0f;
    double lastPitch = -1.0;
    char lastEngine = EngAnalog;
    int held[32] {}; int heldCount = 0;
};

//==============================================================================
class Synth
{
public:
    static constexpr int kChannels = 16;

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        (void) WaveTables::get();
        for (auto& c : channels) c.prepare (sr, maxBlock);
    }

    void noteOn (int ch, int note, float vel, int lenSamples)
    {
        if (ch < 0 || ch >= kChannels || note < 0 || note > 127) return;
        channels[ch].lastVel = vel;
        channels[ch].noteOn (note, vel, lenSamples, clock, rng.next());
    }
    void noteOff (int ch, int note) { if (ch >= 0 && ch < kChannels) channels[ch].noteOff (note); }
    void jfCommand (int ch, int op, int idx, int a, int b) { if (ch >= 0 && ch < kChannels) channels[ch].jfCommand (op, idx, a, b); }
    void setBend (int ch, float norm) { if (ch >= 0 && ch < kChannels) channels[ch].bend = norm; }
    int  gatedVoices (int ch) const { return (ch >= 0 && ch < kChannels) ? channels[ch].gatedVoices() : 0; }
    void setSustain (int ch, bool d) { if (ch >= 0 && ch < kChannels) channels[ch].setSustain (d); }
    void allNotesOff() { for (auto& c : channels) c.allOff(); }
    void killAll() { for (auto& c : channels) c.kill(); }

    void render (float* L, float* R, int n, double bpm = 120.0)
    {
        std::fill (L, L + n, 0.0f);
        std::fill (R, R + n, 0.0f);
        for (auto& c : channels) { c.bpm = bpm; c.render (L, R, n); }
    }

    Channel channels[kChannels];

private:
    double sr = 48000.0;
    uint64_t clock = 0;
    Rng rng { 12345u };
};
} // namespace sf
