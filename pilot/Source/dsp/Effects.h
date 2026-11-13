// Pilot Sealflake — master effects chain (16 effects, Pilot-compatible codes)
//
// Every effect has the two legacy Pilot controls (wet "w" and value "v", 0..1, host-automatable)
// plus named parameters in real units (dB, ms, Hz, %). A named parameter is either a view onto
// w / v (kind 'W' / 'V') or an independent override slot (kind 'E') whose default is derived from
// w / v until it is set. The same table drives the audio code and the command language.
#pragma once
#include "DspCommon.h"
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <string>

namespace sf
{
struct FxInfo { const char* code; const char* name; const char* wLabel; const char* vLabel; float defW; float defV; };

enum FxId { FxBit, FxDis, FxChe, FxWah, FxPha, FxCho, FxVib, FxTre, FxFee, FxDel, FxRev, FxSte, FxEqu, FxCom, FxVol, FxLim, kNumFx };

static const FxInfo kFxInfo[kNumFx] = {
    { "bit", "BITCRUSH", "mix",   "crush",   0.0f,          7.0f / 15.0f },
    { "dis", "DISTORT",  "mix",   "drive",   0.0f,          0.0f },
    { "che", "CHEBYSHEV","mix",   "order",   0.0f,          7.0f / 15.0f },
    { "wah", "AUTOWAH",  "mix",   "sense",   0.0f,          1.0f },
    { "pha", "PHASER",   "mix",   "depth",   0.0f,          1.0f },
    { "cho", "CHORUS",   "mix",   "depth",   0.0f,          7.0f / 15.0f },
    { "vib", "VIBRATO",  "depth", "rate",    0.0f,          1.0f / 15.0f },
    { "tre", "TREMOLO",  "depth", "rate",    0.0f,          7.0f / 15.0f },
    { "fee", "FEEDBACK", "mix",   "time",    0.0f,          8.0f / 15.0f },
    { "del", "PINGPONG", "mix",   "fdbk",    0.0f,          6.0f / 15.0f },
    { "rev", "REVERB",   "mix",   "size",    2.0f / 15.0f,  9.0f / 15.0f },
    { "ste", "WIDENER",  "mix",   "width",   0.0f,          8.0f / 15.0f },
    { "equ", "EQ",       "low",   "high",    8.0f / 15.0f,  8.0f / 15.0f },
    { "com", "COMPRESS", "mix",   "amount",  1.0f,          3.0f / 15.0f },
    { "vol", "VOLUME",   "gain",  "warmth",  12.0f / 15.0f, 0.0f },
    { "lim", "LIMITER",  "drive", "release", 0.0f,          6.0f / 15.0f },
};

inline int fxIndexOf (const char* code3)
{
    for (int i = 0; i < kNumFx; ++i)
        if (std::tolower ((unsigned char) code3[0]) == kFxInfo[i].code[0]
            && std::tolower ((unsigned char) code3[1]) == kFxInfo[i].code[1]
            && std::tolower ((unsigned char) code3[2]) == kFxInfo[i].code[2]) return i;
    return -1;
}

struct FxContext { double sr; double bpm; };

inline double beatsToSec (double beats, double bpm) { return beats * 60.0 / std::max (bpm, 20.0); }

inline double divisionBeats (int idx)
{
    static const double d[16] = { 1.0 / 16, 1.0 / 8, 1.0 / 6, 1.0 / 4, 1.0 / 3, 3.0 / 8, 1.0 / 2, 2.0 / 3,
                                  3.0 / 4, 1.0, 4.0 / 3, 3.0 / 2, 2.0, 3.0, 4.0, 8.0 };
    return d[clamp (idx, 0, 15)];
}
inline double tremoloBeats (float v)
{
    static const double d[9] = { 4.0, 2.0, 1.0, 2.0 / 3, 0.5, 1.0 / 3, 0.25, 1.0 / 6, 0.125 };
    return d[clamp ((int) std::lround (v * 8.0f), 0, 8)];
}

//==============================================================================
// Named parameter table
constexpr float kUnset = -1.0e30f;
inline bool isSet (float x) { return x > -1.0e29f; }
constexpr int kMaxFxSlots = 8;

using DeriveFn = float (*) (float w, float v, const FxContext&);
using ToRawFn  = float (*) (float unit);

struct FxParamDef
{
    int fx; const char* name; char kind; int slot;
    DeriveFn derive;     // W/V: raw -> unit.  E: default value from w/v
    ToRawFn  toRaw;      // W/V only: unit -> raw 0..1
    float lo, hi;
    const char* unit;
    const char* help;
};

#define SF_D(expr) +[] (float w, float v, const FxContext& c) -> float { (void) w; (void) v; (void) c; return (float) (expr); }
#define SF_R(expr) +[] (float u) -> float { return clamp ((float) (expr), 0.0f, 1.0f); }

inline const std::vector<FxParamDef>& fxParamTable()
{
    static const std::vector<FxParamDef> t = {
        { FxBit, "MIX",   'W', 0, SF_D (w * 100), SF_R (u / 100), 0, 100, "%", "dry/wet" },
        { FxBit, "BITS",  'E', 0, SF_D (16 - 13 * v), nullptr, 1, 24, "bits", "bit depth" },
        { FxBit, "RATE",  'E', 1, SF_D (c.sr / (1 + v * v * 23)), nullptr, 50, 192000, "Hz", "sample-and-hold rate" },

        { FxDis, "MIX",   'W', 0, SF_D (w * 100), SF_R (u / 100), 0, 100, "%", "dry/wet" },
        { FxDis, "DRIVE", 'E', 0, SF_D (v * 38), nullptr, 0, 60, "dB", "input drive" },
        { FxDis, "BIAS",  'E', 1, SF_D (18), nullptr, 0, 100, "%", "asymmetry (even harmonics)" },
        { FxDis, "TONE",  'E', 2, SF_D (20000), nullptr, 200, 20000, "Hz", "post lowpass" },
        { FxDis, "OUT",   'E', 3, SF_D (0), nullptr, -48, 24, "dB", "output trim" },

        { FxChe, "MIX",   'W', 0, SF_D (w * 100), SF_R (u / 100), 0, 100, "%", "dry/wet" },
        { FxChe, "ORDER", 'E', 0, SF_D (1 + 15 * v), nullptr, 1, 16, "", "harmonic order (fractional ok)" },
        { FxChe, "DRIVE", 'E', 1, SF_D (5.1), nullptr, -24, 36, "dB", "input drive" },

        { FxWah, "MIX",   'W', 0, SF_D (w * 100), SF_R (u / 100), 0, 100, "%", "dry/wet" },
        { FxWah, "SENS",  'E', 0, SF_D (v * 100), nullptr, 0, 100, "%", "envelope sensitivity" },
        { FxWah, "LO",    'E', 1, SF_D (280), nullptr, 40, 5000, "Hz", "sweep bottom" },
        { FxWah, "HI",    'E', 2, SF_D (5513), nullptr, 200, 16000, "Hz", "sweep top" },
        { FxWah, "Q",     'E', 3, SF_D (3), nullptr, 0.5f, 20, "", "resonance" },
        { FxWah, "ATK",   'E', 4, SF_D (4), nullptr, 0.1f, 500, "ms", "follower attack" },
        { FxWah, "REL",   'E', 5, SF_D (120), nullptr, 1, 3000, "ms", "follower release" },

        { FxPha, "MIX",   'W', 0, SF_D (w * 100), SF_R (u / 100), 0, 100, "%", "dry/wet (100 = deepest notches)" },
        { FxPha, "RATE",  'E', 0, SF_D (0.35), nullptr, 0.01f, 20, "Hz", "LFO rate" },
        { FxPha, "DEPTH", 'E', 1, SF_D (1 + 4 * v), nullptr, 0, 8, "oct", "sweep range" },
        { FxPha, "FB",    'E', 2, SF_D ((0.15 + 0.55 * v) * 100), nullptr, -95, 95, "%", "feedback" },
        { FxPha, "STAGES",'E', 3, SF_D (8), nullptr, 2, 12, "", "allpass stages" },
        { FxPha, "FREQ",  'E', 4, SF_D (180), nullptr, 20, 5000, "Hz", "sweep bottom" },

        { FxCho, "MIX",   'W', 0, SF_D (w * 100), SF_R (u / 100), 0, 100, "%", "dry/wet" },
        { FxCho, "RATE",  'E', 0, SF_D (0.31 * (0.6 + 0.8 * (1 - v))), nullptr, 0.01f, 10, "Hz", "LFO rate (voice 1)" },
        { FxCho, "DEPTH", 'E', 1, SF_D (0.6 + 4.2 * v), nullptr, 0, 15, "ms", "modulation depth" },
        { FxCho, "TIME",  'E', 2, SF_D (7.5), nullptr, 1, 30, "ms", "base delay" },

        { FxVib, "DEPTH", 'W', 0, SF_D (w * 4.5), SF_R (u / 4.5), 0, 4.5f, "ms", "pitch depth" },
        { FxVib, "RATE",  'V', 0, SF_D (0.6 * std::pow (12.0, (double) v)), SF_R (std::log (std::max (u, 0.6f) / 0.6) / std::log (12.0)), 0.6f, 7.2f, "Hz", "rate" },

        { FxTre, "DEPTH", 'W', 0, SF_D (w * 100), SF_R (u / 100), 0, 100, "%", "depth" },
        { FxTre, "SYNC",  'E', 0, SF_D (tremoloBeats (v)), nullptr, 0.03125f, 16, "beats", "period in beats (clears RATE)" },
        { FxTre, "RATE",  'E', 1, SF_D (1.0 / beatsToSec (tremoloBeats (v), c.bpm)), nullptr, 0.05f, 40, "Hz", "free rate (overrides SYNC)" },
        { FxTre, "SHAPE", 'E', 2, SF_D (27), nullptr, 0, 100, "%", "0 sine .. 100 square" },

        { FxFee, "MIX",   'W', 0, SF_D (w * 100), SF_R (u / 100), 0, 100, "%", "dry/wet" },
        { FxFee, "SYNC",  'E', 0, SF_D (divisionBeats ((int) std::lround (v * 15))), nullptr, 0.0625f, 16, "beats", "delay in beats (clears TIME)" },
        { FxFee, "TIME",  'E', 1, SF_D (1000.0 * beatsToSec (divisionBeats ((int) std::lround (v * 15)), c.bpm)), nullptr, 1, 8000, "ms", "free delay (overrides SYNC)" },
        { FxFee, "FB",    'E', 2, SF_D (55), nullptr, 0, 110, "%", "feedback" },
        { FxFee, "HI",    'E', 3, SF_D (5200), nullptr, 500, 20000, "Hz", "feedback lowpass" },
        { FxFee, "LO",    'E', 4, SF_D (110), nullptr, 10, 2000, "Hz", "feedback highpass" },
        { FxFee, "WOW",   'E', 5, SF_D (100), nullptr, 0, 500, "%", "tape wow" },

        { FxDel, "MIX",   'W', 0, SF_D (w * 100), SF_R (u / 100), 0, 100, "%", "dry/wet" },
        { FxDel, "FB",    'V', 0, SF_D ((0.05 + 0.85 * v) * 100), SF_R ((u / 100 - 0.05) / 0.85), 5, 90, "%", "feedback" },
        { FxDel, "SYNC",  'E', 0, SF_D (0.75), nullptr, 0.0625f, 8, "beats", "delay in beats (clears TIME)" },
        { FxDel, "TIME",  'E', 1, SF_D (1000.0 * beatsToSec (0.75, c.bpm)), nullptr, 1, 4000, "ms", "free delay (overrides SYNC)" },
        { FxDel, "HI",    'E', 2, SF_D (6000), nullptr, 500, 20000, "Hz", "feedback lowpass" },
        { FxDel, "LO",    'E', 3, SF_D (150), nullptr, 10, 2000, "Hz", "feedback highpass" },

        { FxRev, "MIX",   'W', 0, SF_D (w * 100), SF_R (u / 100), 0, 100, "%", "dry/wet" },
        { FxRev, "DECAY", 'V', 0, SF_D (0.35 * std::pow (40.0, (double) v)), SF_R (std::log (std::max (u, 0.35f) / 0.35) / std::log (40.0)), 0.35f, 14, "s", "RT60" },
        { FxRev, "PRE",   'E', 0, SF_D (6 + 26 * v), nullptr, 0, 250, "ms", "predelay" },
        { FxRev, "DAMP",  'E', 1, SF_D (9500 - 5000 * v), nullptr, 500, 20000, "Hz", "high damping" },
        { FxRev, "LO",    'E', 2, SF_D (140), nullptr, 10, 2000, "Hz", "input low cut" },
        { FxRev, "MOD",   'E', 3, SF_D (100), nullptr, 0, 400, "%", "line modulation" },

        { FxSte, "MIX",   'W', 0, SF_D (w * 100), SF_R (u / 100), 0, 100, "%", "dry/wet" },
        { FxSte, "WIDTH", 'E', 0, SF_D (100 + 160 * v), nullptr, 0, 300, "%", "stereo width (100 = unchanged)" },
        { FxSte, "XOVER", 'E', 1, SF_D (160), nullptr, 20, 1000, "Hz", "bass kept mono below" },

        { FxEqu, "LO",    'W', 0, SF_D ((w * 15 - 8) * 1.6), SF_R ((u / 1.6 + 8) / 15), -12.8f, 11.2f, "dB", "low shelf gain" },
        { FxEqu, "HI",    'V', 0, SF_D ((v * 15 - 8) * 1.6), SF_R ((u / 1.6 + 8) / 15), -12.8f, 11.2f, "dB", "high shelf gain" },
        { FxEqu, "MID",   'E', 0, SF_D (0), nullptr, -18, 18, "dB", "mid peak gain" },
        { FxEqu, "LOF",   'E', 1, SF_D (110), nullptr, 20, 1000, "Hz", "low shelf freq" },
        { FxEqu, "MIDF",  'E', 2, SF_D (1000), nullptr, 100, 10000, "Hz", "mid freq" },
        { FxEqu, "HIF",   'E', 3, SF_D (7500), nullptr, 1000, 20000, "Hz", "high shelf freq" },
        { FxEqu, "Q",     'E', 4, SF_D (0.7), nullptr, 0.1f, 10, "", "mid Q" },

        { FxCom, "MIX",   'W', 0, SF_D (w * 100), SF_R (u / 100), 0, 100, "%", "parallel mix" },
        { FxCom, "THR",   'E', 0, SF_D (-32 * v), nullptr, -60, 0, "dB", "threshold" },
        { FxCom, "RATIO", 'E', 1, SF_D (1.5 + 6.5 * v), nullptr, 1, 50, ":1", "ratio" },
        { FxCom, "ATK",   'E', 2, SF_D (6), nullptr, 0.05f, 500, "ms", "attack" },
        { FxCom, "REL",   'E', 3, SF_D (90), nullptr, 5, 5000, "ms", "release" },
        { FxCom, "KNEE",  'E', 4, SF_D (8), nullptr, 0, 24, "dB", "soft knee width" },
        { FxCom, "GAIN",  'E', 5, SF_D (32 * v * (1 - 1 / (1.5 + 6.5 * v)) * 0.36), nullptr, -24, 36, "dB", "makeup (default auto)" },

        { FxVol, "GAIN",  'W', 0, SF_D (w < 0.02f ? -96.0 : (w - 0.8) * 60), SF_R (u <= -95 ? 0.0 : u / 60 + 0.8), -96, 12, "dB", "output gain" },
        { FxVol, "WARM",  'V', 0, SF_D (v * 100), SF_R (u / 100), 0, 100, "%", "tape warmth" },

        { FxLim, "DRIVE", 'W', 0, SF_D (w * 18), SF_R (u / 18), 0, 18, "dB", "input drive" },
        { FxLim, "REL",   'V', 0, SF_D ((0.01 + 0.4 * v * v) * 1000), SF_R (std::sqrt (std::max (0.0f, u / 1000 - 0.01f) / 0.4f)), 10, 410, "ms", "release" },
        { FxLim, "CEIL",  'E', 0, SF_D (-0.3), nullptr, -24, 0, "dBTP", "true-peak ceiling" },
        { FxLim, "TP",    'E', 1, SF_D (1), nullptr, 0, 1, "", "true-peak detection on/off" },
    };
    return t;
}
#undef SF_D
#undef SF_R

inline const FxParamDef* findFxParam (int fx, const std::string& name)
{
    for (auto& p : fxParamTable()) if (p.fx == fx && name == p.name) return &p;
    return nullptr;
}
inline const FxParamDef* findFxSlot (int fx, int slot)
{
    for (auto& p : fxParamTable()) if (p.fx == fx && p.kind == 'E' && p.slot == slot) return &p;
    return nullptr;
}

//==============================================================================
class Effect
{
public:
    virtual ~Effect() = default;
    virtual void prepare (double sampleRate, int maxBlock) { sr = sampleRate; wet.setLength (sr, 0.03); val.setLength (sr, 0.05); wet.reset (0); val.reset (0); (void) maxBlock; }
    virtual void reset() {}
    virtual void process (float* L, float* R, int n, const FxContext& ctx) = 0;
    virtual bool alwaysOn() const { return false; }

    void setParams (float w, float v) { wet.set (w); val.set (v); }
    void initParams (float w, float v) { wet.reset (w); val.reset (v); }
    void bind (int fxId, const std::atomic<float>* extSlots)
    {
        id = fxId; ext = extSlots;
        for (int s = 0; s < kMaxFxSlots; ++s) slotDef[s] = findFxSlot (id, s);
    }

    void run (float* L, float* R, int n, const FxContext& ctx)
    {
        const bool silent = ! alwaysOn() && wet.target <= 0.0f && wet.settled();
        if (silent) { bypassed = true; return; }
        if (bypassed) { bypassed = false; reset(); }
        process (L, R, n, ctx);
    }

protected:
    // Named parameter in real units: the override if set, else its default derived from w/v
    float P (int slot, const FxContext& ctx) const
    {
        const float o = ext ? ext[slot].load (std::memory_order_relaxed) : kUnset;
        if (isSet (o)) return o;
        return slotDef[slot] ? slotDef[slot]->derive (wet.target, val.target, ctx) : 0.0f;
    }
    bool overridden (int slot) const { return ext && isSet (ext[slot].load (std::memory_order_relaxed)); }

    double sr = 48000.0;
    Smooth wet, val;
    bool bypassed = true;
    int id = 0;
    const std::atomic<float>* ext = nullptr;
    const FxParamDef* slotDef[kMaxFxSlots] {};
};

//==============================================================================
// BIT — bit depth + sample-rate reduction with fractional (non-integer) decimation
class Bitcrusher : public Effect
{
public:
    void reset() override { holdL = holdR = 0; phase = 1.0; }
    void process (float* L, float* R, int n, const FxContext& ctx) override
    {
        const bool bitsSet = overridden (0), rateSet = overridden (1);
        const float bitsO = P (0, ctx), rateO = P (1, ctx);
        for (int i = 0; i < n; ++i)
        {
            const float w = wet.next(), v = val.next();
            const float bits = bitsSet ? clamp (bitsO, 1.0f, 24.0f) : 16.0f - 13.0f * v;
            const float q = std::exp2 (bits - 1.0f);
            const double step = rateSet ? clamp ((double) rateO / sr, 1.0e-4, 1.0) : 1.0 / (1.0 + v * v * 23.0);
            phase += step;
            if (phase >= 1.0)
            {
                phase -= 1.0;
                holdL = std::round (L[i] * q) / q;
                holdR = std::round (R[i] * q) / q;
            }
            L[i] += w * (holdL - L[i]);
            R[i] += w * (holdR - R[i]);
        }
    }
private:
    float holdL = 0, holdR = 0; double phase = 1.0;
};

//==============================================================================
// Base for oversampled waveshapers (4x polyphase IIR half-band)
class OversampledShaper : public Effect
{
public:
    void prepare (double sampleRate, int maxBlock) override
    {
        Effect::prepare (sampleRate, maxBlock);
        os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        os->initProcessing ((size_t) maxBlock);
        dryL.assign ((size_t) maxBlock, 0.0f); dryR.assign ((size_t) maxBlock, 0.0f);
        wv.assign ((size_t) maxBlock, 0.0f); vv.assign ((size_t) maxBlock, 0.0f);
        for (auto& d : dcs) { d.set ((float) sr); d.reset(); }
        for (auto& t : tone) t.reset();
    }
    void reset() override { if (os) os->reset(); for (auto& d : dcs) d.reset(); for (auto& t : tone) t.reset(); }

    void process (float* L, float* R, int n, const FxContext& ctx) override
    {
        block (ctx);
        std::copy (L, L + n, dryL.begin()); std::copy (R, R + n, dryR.begin());
        for (int i = 0; i < n; ++i) { wv[(size_t) i] = wet.next(); vv[(size_t) i] = val.next(); }
        float* chans[2] = { L, R };
        juce::dsp::AudioBlock<float> blk (chans, 2, (size_t) n);
        auto up = os->processSamplesUp (blk);
        const int factor = (int) os->getOversamplingFactor();
        for (int c = 0; c < 2; ++c)
        {
            float* d = up.getChannelPointer ((size_t) c);
            for (int i = 0; i < n * factor; ++i)
                d[i] = shape (d[i], vv[(size_t) (i / factor)], c);
        }
        os->processSamplesDown (blk);
        const bool useTone = toneHz < 19000.0f;
        if (useTone) for (auto& t : tone) t.set (toneHz, (float) sr);
        for (int i = 0; i < n; ++i)
        {
            const float w = wv[(size_t) i];
            float yl = dcs[0].process (L[i]) * outGain, yr = dcs[1].process (R[i]) * outGain;
            if (useTone) { yl = tone[0].process (yl); yr = tone[1].process (yr); }
            L[i] = dryL[(size_t) i] + w * (yl - dryL[(size_t) i]);
            R[i] = dryR[(size_t) i] + w * (yr - dryR[(size_t) i]);
        }
    }
protected:
    virtual void block (const FxContext&) {}
    virtual float shape (float x, float v, int ch) = 0;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    std::vector<float> dryL, dryR, wv, vv;
    DCBlocker dcs[2];
    OnePoleLP tone[2];
    float toneHz = 20000.0f, outGain = 1.0f;
};

// DIS — asymmetric tube-style saturation, auto level compensated
class Distortion : public OversampledShaper
{
    void block (const FxContext& ctx) override
    {
        driveSet = overridden (0);
        driveDb = P (0, ctx);
        bias = clamp (P (1, ctx), 0.0f, 100.0f) / 100.0f;
        toneHz = P (2, ctx);
        outGain = dbToGain (P (3, ctx));
    }
    float shape (float x, float v, int) override
    {
        const float db = driveSet ? driveDb : v * 38.0f;
        const float drive = dbToGain (db);
        const float vEq = db / 38.0f;
        const float y = fastTanh (x * drive + bias) - fastTanh (bias);
        const float comp = 0.3f / fastTanh (0.3f * drive) / (1.0f + 1.2f * vEq);
        return y * comp;
    }
    bool driveSet = false; float driveDb = 0.0f, bias = 0.18f;
};

// CHE — Chebyshev polynomial harmonic generator (fractional order crossfade)
class Chebyshev : public OversampledShaper
{
    void block (const FxContext& ctx) override
    {
        orderSet = overridden (0);
        orderO = clamp (P (0, ctx), 1.0f, 16.0f);
        pre = dbToGain (P (1, ctx));
    }
    float shape (float x, float v, int) override
    {
        const float order = orderSet ? orderO : 1.0f + v * 15.0f;
        const int lo = (int) order;
        const float frac = order - (float) lo;
        const float xs = fastTanh (x * pre);
        // T_n via recurrence; subtract T_n(0) so silence stays silent (no DC for even orders)
        float t0 = 1.0f, t1 = xs, tLo = xs, tHi = xs;
        for (int k = 2; k <= lo + 1; ++k)
        {
            const float t2 = 2.0f * xs * t1 - t0;
            t0 = t1; t1 = t2;
            if (k == lo) tLo = t2;
            if (k == lo + 1) tHi = t2;
        }
        auto t0of = [] (int n) { return (n & 1) ? 0.0f : ((n / 2) & 1 ? -1.0f : 1.0f); };
        tLo -= lo == 1 ? 0.0f : t0of (lo);
        tHi -= t0of (lo + 1);
        return (tLo + frac * (tHi - tLo)) * 0.5f / (1.0f + 0.08f * order);
    }
    bool orderSet = false; float orderO = 1.0f, pre = 1.8f;
};

//==============================================================================
// WAH — envelope-following resonant filter
class AutoWah : public Effect
{
public:
    void reset() override { env = 0; f[0].reset(); f[1].reset(); }
    void process (float* L, float* R, int n, const FxContext& ctx) override
    {
        const bool sensSet = overridden (0);
        const float sensO = P (0, ctx) / 100.0f;
        const float lo = std::max (20.0f, P (1, ctx)), hi = std::max (P (2, ctx), lo * 1.01f), q = std::max (0.5f, P (3, ctx));
        const float atk = std::exp (-1.0f / (std::max (0.0001f, P (4, ctx) * 0.001f) * (float) sr));
        const float rel = std::exp (-1.0f / (std::max (0.001f, P (5, ctx) * 0.001f) * (float) sr));
        const float octs = std::log2 (hi / lo);
        const float norm = 1.1f * std::sqrt (3.0f / q);
        for (int i = 0; i < n; ++i)
        {
            const float w = wet.next(), v = val.next();
            const float sens = sensSet ? sensO : v;
            const float in = std::max (std::fabs (L[i]), std::fabs (R[i]));
            env = in > env ? atk * env + (1 - atk) * in : rel * env + (1 - rel) * in;
            if ((i & 7) == 0)
            {
                const float e = clamp (env * (1.0f + sens * 10.0f), 0.0f, 1.0f);
                const float fc = lo * std::exp2 (e * octs);
                f[0].set (fc, q, (float) sr); f[1].set (fc, q, (float) sr);
            }
            f[0].tick (L[i]); f[1].tick (R[i]);
            const float yl = (f[0].lp * 0.6f + f[0].bp) * norm, yr = (f[1].lp * 0.6f + f[1].bp) * norm;
            L[i] += w * (yl - L[i]); R[i] += w * (yr - R[i]);
        }
    }
private:
    float env = 0; SVF f[2];
};

//==============================================================================
// PHA — stereo phaser with feedback (quadrature LFO), 2..12 stages
class Phaser : public Effect
{
public:
    void reset() override { for (auto& c : z) for (auto& s : c) s = 0; fb[0] = fb[1] = 0; }
    void process (float* L, float* R, int n, const FxContext& ctx) override
    {
        float* io[2] = { L, R };
        const float rate = P (0, ctx), depthO = P (1, ctx), fbO = P (2, ctx) / 100.0f, base = P (4, ctx);
        const bool depthSet = overridden (1), fbSet = overridden (2);
        const int stages = clamp ((int) std::lround (P (3, ctx)), 2, 12);
        for (int i = 0; i < n; ++i)
        {
            const float w = wet.next(), v = val.next();
            if ((i & 3) == 0)
            {
                lfo += rate / sr * 4.0; if (lfo >= 1.0) lfo -= 1.0;
                const float depth = depthSet ? depthO : 1.0f + 4.0f * v;
                for (int c = 0; c < 2; ++c)
                {
                    const double ph = lfo + c * 0.25;
                    const float s = 0.5f + 0.5f * (float) std::sin (kTwoPi * ph);
                    const float fc = base * std::exp2 (s * depth);
                    const float t = std::tan ((float) kPi * std::min (fc, (float) sr * 0.45f) / (float) sr);
                    aCur[c] = (t - 1.0f) / (t + 1.0f);
                }
            }
            const float feedback = clamp (fbSet ? fbO : 0.15f + 0.55f * v, -0.95f, 0.95f);
            for (int c = 0; c < 2; ++c)
            {
                float x = io[c][i] + fb[c] * feedback;
                for (int s = 0; s < stages; ++s)
                {
                    const float y = aCur[c] * x + z[c][s];
                    z[c][s] = flushDenormal (x - aCur[c] * y);
                    x = y;
                }
                fb[c] = fastTanh (x);
                io[c][i] = io[c][i] * (1.0f - 0.5f * w) + x * 0.5f * w;
            }
        }
    }
private:
    double lfo = 0; float z[2][12] {}; float fb[2] {}; float aCur[2] {};
};

//==============================================================================
// CHO — 3-voice-per-side stereo ensemble chorus
class Chorus : public Effect
{
public:
    void prepare (double s, int mb) override { Effect::prepare (s, mb); for (auto& d : dl) d.prepare ((int) (sr * 0.06)); }
    void reset() override { for (auto& d : dl) d.clear(); }
    void process (float* L, float* R, int n, const FxContext& ctx) override
    {
        static const double rel[3] = { 1.0, 0.47 / 0.31, 0.63 / 0.31 };
        const bool rateSet = overridden (0), depthSet = overridden (1);
        const float rateO = P (0, ctx), depthO = P (1, ctx), baseMs = P (2, ctx);
        for (int i = 0; i < n; ++i)
        {
            const float w = wet.next(), v = val.next();
            dl[0].push (L[i]); dl[1].push (R[i]);
            const float depthMs = depthSet ? depthO : 0.6f + 4.2f * v;
            const double r1 = rateSet ? rateO : 0.31 * (0.6 + 0.8 * (1.0 - v));
            float out[2] = { 0, 0 };
            for (int k = 0; k < 3; ++k)
            {
                ph[k] += r1 * rel[k] / sr; if (ph[k] >= 1.0) ph[k] -= 1.0;
                for (int c = 0; c < 2; ++c)
                {
                    const double p = ph[k] + c * 0.5 + k * (1.0 / 3.0);
                    const float m = (float) std::sin (kTwoPi * p);
                    const float d = std::max (2.0f, (baseMs + k * 2.3f + depthMs * 0.5f * (1.0f + m)) * 0.001f * (float) sr);
                    out[c] += dl[c ^ (k & 1)].readHermite (d);
                }
            }
            L[i] = L[i] * (1.0f - 0.45f * w) + out[0] * (1.0f / 3.0f) * 0.85f * w;
            R[i] = R[i] * (1.0f - 0.45f * w) + out[1] * (1.0f / 3.0f) * 0.85f * w;
        }
    }
private:
    DelayLine dl[2]; double ph[3] { 0.0, 0.33, 0.66 };
};

//==============================================================================
// VIB — true pitch vibrato (modulated delay, Hermite interpolation)
class Vibrato : public Effect
{
public:
    void prepare (double s, int mb) override { Effect::prepare (s, mb); for (auto& d : dl) d.prepare ((int) (sr * 0.03)); }
    void reset() override { for (auto& d : dl) d.clear(); }
    void process (float* L, float* R, int n, const FxContext&) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float w = wet.next(), v = val.next();
            const double rate = 0.6 * std::pow (12.0, (double) v);
            ph += rate / sr; if (ph >= 1.0) ph -= 1.0;
            dl[0].push (L[i]); dl[1].push (R[i]);
            const float depth = w * 0.0045f * (float) sr * (float) std::min (1.0, 4.0 / rate + 0.25);
            const float d = 3.0f + depth * 0.5f * (1.0f + (float) std::sin (kTwoPi * ph));
            const float yl = dl[0].readHermite (d), yr = dl[1].readHermite (d);
            const float mix = std::min (1.0f, w * 4.0f);
            L[i] += mix * (yl - L[i]); R[i] += mix * (yr - R[i]);
        }
    }
private:
    DelayLine dl[2]; double ph = 0;
};

//==============================================================================
// TRE — tempo-synced (or free) stereo tremolo, quadrature, variable shape
class Tremolo : public Effect
{
public:
    void process (float* L, float* R, int n, const FxContext& ctx) override
    {
        const bool rateSet = overridden (1), syncSet = overridden (0);
        const float rateO = P (1, ctx), syncO = P (0, ctx);
        const float drive = 0.2f + clamp (P (2, ctx), 0.0f, 100.0f) / 100.0f * 6.0f;
        const float norm = fastTanh (drive);
        for (int i = 0; i < n; ++i)
        {
            const float w = wet.next(), v = val.next();
            const double hz = rateSet ? (double) rateO : 1.0 / beatsToSec (syncSet ? (double) syncO : tremoloBeats (v), ctx.bpm);
            ph += hz / sr; if (ph >= 1.0) ph -= 1.0;
            const float sl = (float) std::sin (kTwoPi * ph), sr2 = (float) std::sin (kTwoPi * (ph + 0.25));
            auto shape = [drive, norm] (float s) { return 0.5f + 0.5f * fastTanh (s * drive) / norm; };
            L[i] *= 1.0f - w * (1.0f - shape (sl));
            R[i] *= 1.0f - w * (1.0f - shape (sr2));
        }
    }
private:
    double ph = 0;
};

//==============================================================================
// FEE — tempo-synced (or free) tape-style stereo feedback delay
class FeedbackDelay : public Effect
{
public:
    void prepare (double s, int mb) override
    {
        Effect::prepare (s, mb);
        for (auto& d : dl) d.prepare ((int) (sr * 8.5));
        time.setLength (sr, 0.25);
    }
    void reset() override { for (auto& d : dl) d.clear(); for (int c = 0; c < 2; ++c) { lp[c].reset(); hp[c].reset(); } first = true; }
    void process (float* L, float* R, int n, const FxContext& ctx) override
    {
        float* io[2] = { L, R };
        const bool timeSet = overridden (1), syncSet = overridden (0);
        const float timeO = P (1, ctx), syncO = P (0, ctx);
        const float fb = clamp (P (2, ctx), 0.0f, 110.0f) / 100.0f;
        for (int c = 0; c < 2; ++c) { lp[c].set (P (3, ctx), (float) sr); hp[c].set (P (4, ctx), (float) sr); }
        const float wowAmt = P (5, ctx) / 100.0f;
        for (int i = 0; i < n; ++i)
        {
            const float w = wet.next(), v = val.next();
            const double sec = timeSet ? timeO * 0.001 : beatsToSec (syncSet ? (double) syncO : divisionBeats ((int) std::lround (v * 15.0f)), ctx.bpm);
            const float target = (float) clamp (sec * sr, 4.0, sr * 8.0);
            if (first) { time.reset (target); first = false; } else time.set (target);
            const float t = time.next();
            wow += 0.27 / sr; if (wow >= 1.0) wow -= 1.0;
            const float mod = (float) std::sin (kTwoPi * wow) * 0.0006f * (float) sr * wowAmt;
            const float wg = std::sqrt (w), dg = std::sqrt (1.0f - w);
            for (int c = 0; c < 2; ++c)
            {
                const float d = std::max (4.0f, t * (c ? 1.012f : 1.0f) + mod);
                const float y = dl[c].readHermite (d);
                const float fbk = fastTanh (hp[c].process (lp[c].process (y)) * fb * 1.1f) / 1.1f;
                dl[c].push (io[c][i] + fbk);
                io[c][i] = io[c][i] * dg + y * wg;
            }
        }
    }
private:
    DelayLine dl[2]; OnePoleLP lp[2]; OnePoleHP hp[2]; Smooth time; double wow = 0; bool first = true;
};

// DEL — tempo-synced (dotted 1/8 default) ping-pong delay, value = feedback
class PingPong : public Effect
{
public:
    void prepare (double s, int mb) override
    {
        Effect::prepare (s, mb);
        for (auto& d : dl) d.prepare ((int) (sr * 4.5));
        time.setLength (sr, 0.3);
    }
    void reset() override { for (auto& d : dl) d.clear(); for (int c = 0; c < 2; ++c) { lp[c].reset(); hp[c].reset(); } first = true; }
    void process (float* L, float* R, int n, const FxContext& ctx) override
    {
        const bool timeSet = overridden (1);
        const double sec = timeSet ? P (1, ctx) * 0.001 : beatsToSec (P (0, ctx), ctx.bpm);
        for (int c = 0; c < 2; ++c) { lp[c].set (P (2, ctx), (float) sr); hp[c].set (P (3, ctx), (float) sr); }
        const float target = (float) clamp (sec * sr, 4.0, sr * 4.0);
        for (int i = 0; i < n; ++i)
        {
            const float w = wet.next(), v = val.next();
            if (first) { time.reset (target); first = false; } else time.set (target);
            const float t = std::max (4.0f, time.next());
            const float yl = dl[0].readHermite (t), yr = dl[1].readHermite (t);
            const float fb = 0.05f + 0.85f * v;
            const float in = (L[i] + R[i]) * 0.5f;
            dl[0].push (in + hp[0].process (lp[0].process (yr)) * fb);
            dl[1].push (hp[1].process (lp[1].process (yl)) * fb);
            const float wg = std::sqrt (w), dg = std::sqrt (1.0f - w);
            L[i] = L[i] * dg + yl * wg; R[i] = R[i] * dg + yr * wg;
        }
    }
private:
    DelayLine dl[2]; OnePoleLP lp[2]; OnePoleHP hp[2]; Smooth time; bool first = true;
};

//==============================================================================
// REV — 16-line feedback delay network reverb with input diffusion,
// modulated delay lines, frequency-dependent decay and predelay.
class FdnReverb : public Effect
{
public:
    static constexpr int N = 16;
    void prepare (double s, int mb) override
    {
        Effect::prepare (s, mb);
        static const float ms[N] = { 29.7f, 34.1f, 38.9f, 43.3f, 47.9f, 53.1f, 57.7f, 62.3f,
                                     67.9f, 71.3f, 77.1f, 82.7f, 87.1f, 93.7f, 99.1f, 106.3f };
        for (int i = 0; i < N; ++i)
        {
            len[i] = (float) (ms[i] * 0.001 * sr);
            lines[i].prepare ((int) len[i] + (int) (sr * 0.004) + 64);
            damp[i].reset();
        }
        static const float apMs[2][4] = { { 4.77f, 3.59f, 12.73f, 9.31f }, { 5.13f, 3.91f, 11.87f, 8.71f } };
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k) diff[c][k].prepare ((int) (apMs[c][k] * 0.001 * sr), k < 2 ? 0.72f : 0.62f);
        pre.prepare ((int) (sr * 0.26));
        for (int c = 0; c < 2; ++c) outLow[c].setLowShelf (220.0, -3.0, sr);
        for (int i = 0; i < N; ++i) modPh[i] = (double) i / N;
        lastRt = -1.0f;
    }
    void reset() override
    {
        for (auto& l : lines) l.clear();
        for (auto& d : damp) d.reset();
        for (auto& c : diff) for (auto& a : c) a.clear();
        pre.clear();
        for (int c = 0; c < 2; ++c) { inHP[c].reset(); outLow[c].reset(); }
    }
    void process (float* L, float* R, int n, const FxContext& ctx) override
    {
        const float preMs = P (0, ctx), dampHz = P (1, ctx), lowCut = P (2, ctx), modAmt = P (3, ctx) / 100.0f;
        for (int c = 0; c < 2; ++c) inHP[c].set (lowCut, (float) sr);
        for (int i = 0; i < n; ++i)
        {
            const float w = wet.next(), v = val.next();
            const float rt = 0.35f * std::pow (40.0f, v);
            if ((i & 31) == 0 && (std::fabs (rt - lastRt) > 1.0e-3f || dampHz != lastDamp)) updateDecay (rt, dampHz);

            pre.push ((L[i] + R[i]) * 0.5f);
            const float side = (L[i] - R[i]) * 0.5f;
            const float pd = pre.readHermite (std::max (2.0f, preMs * 0.001f * (float) sr));
            float inL = inHP[0].process (pd + side), inR = inHP[1].process (pd - side);
            for (int k = 0; k < 4; ++k) { inL = diff[0][k].process (inL); inR = diff[1][k].process (inR); }

            float x[N];
            for (int k = 0; k < N; ++k)
            {
                float d = len[k];
                if (k & 1)
                {
                    modPh[k] += (0.11 + 0.037 * k) / sr; if (modPh[k] >= 1.0) modPh[k] -= 1.0;
                    d += (float) std::sin (kTwoPi * modPh[k]) * 0.0009f * modAmt * (float) sr;
                }
                x[k] = lines[k].readHermite (std::max (2.0f, d));
            }
            float outL = 0, outR = 0;
            for (int k = 0; k < N; ++k) { const float s = (k & 2) ? -1.0f : 1.0f; if (k & 1) outR += x[k] * s; else outL += x[k] * s; }

            hadamard16 (x);
            for (int k = 0; k < N; ++k)
            {
                const float in = (k & 1) ? inR : inL;
                const float y = damp[k].process (x[k] * gain[k]);
                lines[k].push (y + in * 0.35f * ((k & 4) ? -1.0f : 1.0f));
            }
            outL = outLow[0].process (outL * 0.6f); outR = outLow[1].process (outR * 0.6f);
            const float wg = std::sqrt (w), dg = std::sqrt (1.0f - w);
            L[i] = L[i] * dg + outL * wg;
            R[i] = R[i] * dg + outR * wg;
        }
    }
private:
    void updateDecay (float rt, float dampHz)
    {
        lastRt = rt; lastDamp = dampHz;
        for (int k = 0; k < N; ++k)
        {
            gain[k] = std::pow (10.0f, -3.0f * len[k] / (rt * (float) sr));
            damp[k].set (dampHz, (float) sr);
        }
    }
    static void hadamard16 (float* x)
    {
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += h << 1)
                for (int j = i; j < i + h; ++j) { const float a = x[j], b = x[j + h]; x[j] = a + b; x[j + h] = a - b; }
        for (int i = 0; i < N; ++i) x[i] *= 0.25f;
    }
    DelayLine lines[N], pre;
    OnePoleLP damp[N];
    AllpassDiffuser diff[2][4];
    OnePoleHP inHP[2];
    Biquad outLow[2];
    float len[N] {}, gain[N] {}; double modPh[N] {};
    float lastRt = -1.0f, lastDamp = -1.0f;
};

//==============================================================================
// STE — M/S widener with mono-safe bass and allpass decorrelation
class Widener : public Effect
{
public:
    void prepare (double s, int mb) override
    {
        Effect::prepare (s, mb);
        static const float a[3] = { 3.1f, 4.7f, 7.3f }, b[3] = { 3.7f, 5.3f, 6.1f };
        for (int k = 0; k < 3; ++k) { ap1[k].prepare ((int) (a[k] * 0.001 * sr), 0.5f); ap2[k].prepare ((int) (b[k] * 0.001 * sr), 0.5f); }
    }
    void reset() override { sideHP.reset(); for (auto& a : ap1) a.clear(); for (auto& a : ap2) a.clear(); }
    void process (float* L, float* R, int n, const FxContext& ctx) override
    {
        const bool widthSet = overridden (0);
        const float widthO = clamp (P (0, ctx), 0.0f, 300.0f) / 100.0f;
        sideHP.set (P (1, ctx), (float) sr);
        for (int i = 0; i < n; ++i)
        {
            const float w = wet.next(), v = val.next();
            const float width = widthSet ? widthO : 1.0f + 1.6f * v;
            const float decor = clamp ((width - 1.0f) / 1.6f, 0.0f, 1.25f);
            const float m = (L[i] + R[i]) * 0.5f, s = (L[i] - R[i]) * 0.5f;
            float d1 = m, d2 = m;
            for (int k = 0; k < 3; ++k) { d1 = ap1[k].process (d1); d2 = ap2[k].process (d2); }
            const float extra = (d1 - d2) * 0.5f * decor * 0.5f;
            const float s2 = sideHP.process (s * width + extra);
            const float sOut = s + w * (s2 - s);
            L[i] = m + sOut; R[i] = m - sOut;
        }
    }
private:
    OnePoleHP sideHP; AllpassDiffuser ap1[3], ap2[3];
};

//==============================================================================
// EQU — low shelf + mid peak + high shelf. Legacy: w = low gain, v = high gain (8 = flat)
class Equalizer : public Effect
{
public:
    bool alwaysOn() const override { return true; }
    void reset() override { for (auto& b : lo) b.reset(); for (auto& b : hi) b.reset(); for (auto& b : mid) b.reset(); }
    void process (float* L, float* R, int n, const FxContext& ctx) override
    {
        const float midDb = P (0, ctx), lof = P (1, ctx), midf = P (2, ctx), hif = P (3, ctx), q = P (4, ctx);
        for (int i = 0; i < n; ++i)
        {
            const float w = wet.next(), v = val.next();
            if ((i & 31) == 0)
            {
                const float gl = (w * 15.0f - 8.0f) * 1.6f, gh = (v * 15.0f - 8.0f) * 1.6f;
                if (gl != lastL || lof != lastLf) { for (auto& b : lo) b.setLowShelf (lof, gl, sr); lastL = gl; lastLf = lof; }
                if (gh != lastH || hif != lastHf) { for (auto& b : hi) b.setHighShelf (hif, gh, sr); lastH = gh; lastHf = hif; }
                if (midDb != lastM || midf != lastMf || q != lastQ) { for (auto& b : mid) b.setPeak (midf, midDb, std::max (0.1f, q), sr); lastM = midDb; lastMf = midf; lastQ = q; }
                flatLH = std::fabs (gl) < 0.01f && std::fabs (gh) < 0.01f;
                flatM = std::fabs (midDb) < 0.01f;
            }
            if (! flatLH) { L[i] = hi[0].process (lo[0].process (L[i])); R[i] = hi[1].process (lo[1].process (R[i])); }
            if (! flatM)  { L[i] = mid[0].process (L[i]); R[i] = mid[1].process (R[i]); }
        }
    }
private:
    Biquad lo[2], hi[2], mid[2];
    float lastL = 1e9f, lastH = 1e9f, lastM = 1e9f, lastLf = -1, lastHf = -1, lastMf = -1, lastQ = -1;
    bool flatLH = true, flatM = true;
};

//==============================================================================
// COM — stereo-linked feed-forward compressor, soft knee, program-dependent
// release, auto makeup; wet = parallel mix.
class Compressor : public Effect
{
public:
    void reset() override { envDb = 0; slow = 0; }
    void process (float* L, float* R, int n, const FxContext& ctx) override
    {
        const bool thrSet = overridden (0), ratioSet = overridden (1), gainSet = overridden (5);
        const float thrO = P (0, ctx), ratioO = std::max (1.0f, P (1, ctx)), gainO = P (5, ctx);
        const float atk = std::exp (-1.0f / (std::max (0.00005f, P (2, ctx) * 0.001f) * (float) sr));
        const float relMs = std::max (1.0f, P (3, ctx));
        const float relF = std::exp (-1.0f / (relMs * 0.001f * (float) sr)), relS = std::exp (-1.0f / (relMs * 0.0067f * (float) sr));
        const float knee = std::max (0.0f, P (4, ctx));
        for (int i = 0; i < n; ++i)
        {
            const float w = wet.next(), v = val.next();
            const float thr = thrSet ? thrO : -32.0f * v;
            const float ratio = ratioSet ? ratioO : 1.5f + 6.5f * v;
            const float x = gainToDb (std::max (std::fabs (L[i]), std::fabs (R[i])));
            const float over = x - thr;
            float red = 0.0f;
            if (knee <= 0.0f) { if (over > 0.0f) red = over * (1.0f - 1.0f / ratio); }
            else if (over > knee * 0.5f) red = over * (1.0f - 1.0f / ratio);
            else if (over > -knee * 0.5f) { const float t = over + knee * 0.5f; red = (1.0f - 1.0f / ratio) * t * t / (2.0f * knee); }
            if (red > envDb) envDb = atk * envDb + (1 - atk) * red;
            else envDb = relF * envDb + (1 - relF) * std::max (red, slow);
            slow = relS * slow + (1 - relS) * red;
            const float makeup = gainSet ? gainO : -thr * (1.0f - 1.0f / ratio) * 0.36f;
            const float g = dbToGain (makeup - envDb);
            L[i] = L[i] * (1.0f - w) + L[i] * g * w;
            R[i] = R[i] * (1.0f - w) + R[i] * g * w;
        }
    }
    float getReductionDb() const { return envDb; }
private:
    float envDb = 0, slow = 0;
};

//==============================================================================
// VOL — output gain (c ≈ 0 dB) and tape-style warmth saturation
class Volume : public Effect
{
public:
    bool alwaysOn() const override { return true; }
    void process (float* L, float* R, int n, const FxContext&) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float w = wet.next(), v = val.next();
            const float g = w < 0.02f ? 0.0f : dbToGain ((w - 0.8f) * 60.0f);
            float l = L[i] * g, r = R[i] * g;
            if (v > 0.001f)
            {
                const float d = 1.0f + v * 4.0f;
                l += v * (fastTanh (l * d) / d - l); r += v * (fastTanh (r * d) / d - r);
                l *= 1.0f + v * 0.6f; r *= 1.0f + v * 0.6f;
            }
            L[i] = l; R[i] = r;
        }
    }
};

//==============================================================================
// LIM — true-peak lookahead brickwall limiter, always on.
//  * peaks are detected on the 8x-interpolated signal (inter-sample peaks), so the
//    reconstructed analog output stays under the ceiling, not just the samples
//  * gain = box-filtered running minimum over the lookahead window: it reaches the
//    required reduction exactly when the peak leaves the delay line (no overshoot)
//  * a final ceiling clamp exists only as a guard; clipCount() reports if it ever acts
class Limiter : public Effect
{
public:
    static constexpr int kTaps = 48, kCenter = 23, kPhases = 8;
    bool alwaysOn() const override { return true; }
    void prepare (double s, int mb) override
    {
        Effect::prepare (s, mb);
        la = std::max (8, (int) (sr * 0.0015));
        dl[0].prepare (la + 4); dl[1].prepare (la + 4);
        hold.assign ((size_t) la, 1.0f); box.assign ((size_t) la, 1.0f);
        for (auto& d : dcs) d.set ((float) sr);
        // inaudible band-limit below Nyquist so inter-sample peaks are measurable with a finite filter
        {
            const double fc = std::min (20000.0, 0.42 * sr);
            const double qs[2] = { 0.5412, 1.3066 };   // 4th-order Butterworth
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < 2; ++k)
                {
                    auto& b = nyq[c][k];
                    const double w = kTwoPi * fc / sr, al = std::sin (w) / (2.0 * qs[k]), cw = std::cos (w), a0 = 1.0 + al;
                    b.b0 = (1.0 - cw) / 2.0 / a0; b.b1 = (1.0 - cw) / a0; b.b2 = b.b0;
                    b.a1 = -2.0 * cw / a0; b.a2 = (1.0 - al) / a0;
                    b.reset();
                }
        }
        // 8x polyphase interpolator (48-tap Kaiser-windowed sinc), phases 1..7 between taps 23 and 24
        auto bessel0 = [] (double x) { double sum = 1.0, term = 1.0; for (int k = 1; k < 30; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; } return sum; };
        const double beta = 8.0, halfLen = kTaps / 2.0;
        for (int p = 1; p < kPhases; ++p)
        {
            const double t = kCenter + (double) p / kPhases;
            double c[kTaps], sum = 0.0;
            for (int m = 0; m < kTaps; ++m)
            {
                const double x = t - m;
                const double sinc = std::fabs (x) < 1e-9 ? 1.0 : std::sin (kPi * x) / (kPi * x);
                const double r = x / halfLen;
                const double wn = std::fabs (r) < 1.0 ? bessel0 (beta * std::sqrt (1.0 - r * r)) / bessel0 (beta) : 0.0;
                c[m] = sinc * wn;
                sum += c[m];
            }
            for (int m = 0; m < kTaps; ++m) coef[p - 1][m] = (float) (c[m] / sum);
        }
        reset();
    }
    // total delay: interpolator centre + lookahead line
    int latency() const { return (kTaps - 1 - kCenter) + (la - 1); }
    void reset() override
    {
        dl[0].clear(); dl[1].clear();
        std::fill (hold.begin(), hold.end(), 1.0f); std::fill (box.begin(), box.end(), 1.0f);
        boxSum = (double) la; hp = 0; relEnv = 1.0f; prevReq = 1.0f;
        for (auto& h : hist) for (auto& x : h) x = 0.0f;
        for (auto& d : dcs) d.reset();
        for (auto& c : nyq) for (auto& b : c) b.reset();
    }
    void process (float* L, float* R, int n, const FxContext& ctx) override
    {
        const float ceiling = dbToGain (clamp (P (0, ctx), -24.0f, 0.0f));
        const bool truePeak = P (1, ctx) > 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const float w = wet.next(), v = val.next();
            const float drive = dbToGain (w * 18.0f);
            const float rel = std::exp (-1.0f / ((0.01f + 0.4f * v * v) * (float) sr));
            float in[2] = { dcs[0].process (L[i]) * drive, dcs[1].process (R[i]) * drive };
            if (truePeak) for (int c = 0; c < 2; ++c) in[c] = nyq[c][1].process (nyq[c][0].process (in[c]));
            float pk = 0.0f, cur[2];
            for (int c = 0; c < 2; ++c)
            {
                float* h = hist[c];
                std::memmove (h, h + 1, sizeof (float) * (kTaps - 1));
                h[kTaps - 1] = in[c];
                cur[c] = h[kCenter];
                pk = std::max (pk, std::fabs (cur[c]));
                if (truePeak)
                    for (int p = 0; p < kPhases - 1; ++p)
                    {
                        float y = 0.0f;
                        for (int m = 0; m < kTaps; ++m) y += coef[p][m] * h[m];
                        pk = std::max (pk, std::fabs (y));
                    }
            }
            if (truePeak) pk *= 1.02f;   // guard band: max error between 8x phases (~0.17 dB)
            // a peak between samples n and n+1 must hold down both of them
            const float reqHere = pk > ceiling ? ceiling / pk : 1.0f;
            const float req = std::min (reqHere, prevReq);
            prevReq = reqHere;
            relEnv = req < relEnv ? req : rel * relEnv + (1 - rel) * req;
            hold[(size_t) hp] = relEnv;
            float mn = 1.0f;
            for (float hv : hold) mn = std::min (mn, hv);
            boxSum += mn - box[(size_t) hp];
            box[(size_t) hp] = mn;
            hp = (hp + 1) % la;
            const float g = std::min (1.0f, (float) (boxSum / la));
            dl[0].push (cur[0]); dl[1].push (cur[1]);
            float ol = dl[0].readInt (la - 1) * g, orr = dl[1].readInt (la - 1) * g;
            if (std::fabs (ol) > ceiling || std::fabs (orr) > ceiling)
            {
                ++clips;
                ol = clamp (ol, -ceiling, ceiling); orr = clamp (orr, -ceiling, ceiling);
            }
            L[i] = ol; R[i] = orr;
            grMin = std::min (grMin, g);
        }
    }
    float takeGainReduction() { const float g = grMin; grMin = 1.0f; return g; }
    uint64_t clipCount() const { return clips; }
private:
    DelayLine dl[2]; std::vector<float> hold, box; double boxSum = 0; int la = 64, hp = 0;
    float relEnv = 1.0f, grMin = 1.0f; DCBlocker dcs[2];
    float hist[2][kTaps] {}; float coef[kPhases - 1][kTaps] {}; float prevReq = 1.0f; Biquad nyq[2][2];
    uint64_t clips = 0;
};

//==============================================================================
class FxChain
{
public:
    FxChain()
    {
        fx[FxBit] = std::make_unique<Bitcrusher>();
        fx[FxDis] = std::make_unique<Distortion>();
        fx[FxChe] = std::make_unique<Chebyshev>();
        fx[FxWah] = std::make_unique<AutoWah>();
        fx[FxPha] = std::make_unique<Phaser>();
        fx[FxCho] = std::make_unique<Chorus>();
        fx[FxVib] = std::make_unique<Vibrato>();
        fx[FxTre] = std::make_unique<Tremolo>();
        fx[FxFee] = std::make_unique<FeedbackDelay>();
        fx[FxDel] = std::make_unique<PingPong>();
        fx[FxRev] = std::make_unique<FdnReverb>();
        fx[FxSte] = std::make_unique<Widener>();
        fx[FxEqu] = std::make_unique<Equalizer>();
        fx[FxCom] = std::make_unique<Compressor>();
        fx[FxVol] = std::make_unique<Volume>();
        fx[FxLim] = std::make_unique<Limiter>();
        for (int i = 0; i < kNumFx; ++i)
        {
            for (auto& e : ext[i]) e.store (kUnset);
            fx[i]->bind (i, ext[i]);
        }
    }
    void prepare (double sr, int maxBlock)
    {
        for (int i = 0; i < kNumFx; ++i) { fx[i]->prepare (sr, maxBlock); fx[i]->initParams (kFxInfo[i].defW, kFxInfo[i].defV); fx[i]->reset(); }
    }
    void setParams (int i, float w, float v) { fx[i]->setParams (w, v); }
    void initParams (int i, float w, float v) { fx[i]->initParams (w, v); }
    void process (float* L, float* R, int n, const FxContext& ctx)
    {
        for (int i = 0; i < kNumFx; ++i) { fx[i]->run (L, R, n, ctx); taps[i].push (L, R, n); }
    }
    int latency() const { return static_cast<const Limiter*> (fx[FxLim].get())->latency(); }
    float takeLimiterGain() { return static_cast<Limiter*> (fx[FxLim].get())->takeGainReduction(); }
    uint64_t limiterClips() const { return static_cast<const Limiter*> (fx[FxLim].get())->clipCount(); }
    float compressorReductionDb() const { return static_cast<const Compressor*> (fx[FxCom].get())->getReductionDb(); }

    std::atomic<float> ext[kNumFx][kMaxFxSlots];   // named-parameter overrides (kUnset = derived)
    ScopeTap taps[kNumFx];
private:
    std::unique_ptr<Effect> fx[kNumFx];
};
} // namespace sf
