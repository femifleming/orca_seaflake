// Pilot Sealflake — shared DSP building blocks
#pragma once
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include <algorithm>
#include <atomic>

namespace sf
{
constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

template <typename T> inline T clamp (T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

// Smooth rational tanh approximation (exact limits, C1 continuous at clip point)
inline float fastTanh (float x)
{
    if (x >  3.0f) return  1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline float flushDenormal (float x) { return std::fabs (x) < 1.0e-15f ? 0.0f : x; }

//==============================================================================
// Small fast RNG (xorshift32)
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    explicit Rng (uint32_t seed = 0x9E3779B9u) : s (seed ? seed : 1u) {}
    inline uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float bipolar() { return (float) ((int32_t) next()) * (1.0f / 2147483648.0f); }
    inline float unipolar() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
};

//==============================================================================
// Band-limited mip-mapped wavetables. Each level holds 2^n harmonics.
// Level selection lets partials extend up to (sr - 18 kHz) so that any
// foldover lands above 18 kHz, while keeping the full audible bandwidth.
enum class Wave : int { Sine = 0, Triangle = 1, Square = 2, Saw = 3 };

class WaveTables
{
public:
    static constexpr int kSize   = 4096;
    static constexpr int kMask   = kSize - 1;
    static constexpr int kLevels = 11;          // 1024, 512 ... 1 harmonics

    static const WaveTables& get() { static const WaveTables t; return t; }

    const float* table (Wave w, double freqHz, double sr) const
    {
        if (w == Wave::Sine) return sine.data();
        const double limit = sr > 36000.0 ? sr - 18000.0 : sr * 0.5;
        const double kmax  = limit / std::max (freqHz, 0.1);
        int level = 0;
        while (level < kLevels - 1 && (1024 >> level) > kmax) ++level;
        return tables[(int) w - 1][(size_t) level].data();
    }

    const float* sineTable() const { return sine.data(); }

private:
    WaveTables()
    {
        std::vector<float> sinT ((size_t) kSize);
        for (int n = 0; n < kSize; ++n) sinT[(size_t) n] = (float) std::sin (kTwoPi * n / kSize);

        sine.assign ((size_t) kSize + 4, 0.0f);
        for (int n = 0; n < kSize + 4; ++n) sine[(size_t) n] = sinT[(size_t) (n & kMask)];

        std::vector<double> acc ((size_t) kSize);
        for (int w = 0; w < 3; ++w)
        {
            for (int level = 0; level < kLevels; ++level)
            {
                const int H = 1024 >> level;
                std::fill (acc.begin(), acc.end(), 0.0);
                for (int k = 1; k <= H; ++k)
                {
                    double amp = 0.0;
                    if (w == 0)      { if (k & 1) amp = (8.0 / (kPi * kPi)) * ((((k - 1) / 2) & 1) ? -1.0 : 1.0) / (double) (k * k); } // triangle
                    else if (w == 1) { if (k & 1) amp = (4.0 / kPi) / (double) k; }                                                       // square
                    else             { amp = (2.0 / kPi) * ((k & 1) ? 1.0 : -1.0) / (double) k; }                                         // saw
                    if (amp == 0.0) continue;
                    for (int n = 0; n < kSize; ++n)
                        acc[(size_t) n] += amp * sinT[(size_t) ((k * n) & kMask)];
                }
                auto& t = tables[w][(size_t) level];
                t.assign ((size_t) kSize + 4, 0.0f);
                for (int n = 0; n < kSize + 4; ++n) t[(size_t) n] = (float) acc[(size_t) (n & kMask)];
            }
        }
    }

    std::vector<float> sine;
    std::array<std::array<std::vector<float>, kLevels>, 3> tables;
};

// Linear interpolated table read, phase in [0,1)
inline float readTable (const float* t, double phase)
{
    const double p = phase * WaveTables::kSize;
    const int i = (int) p;
    const float f = (float) (p - i);
    const float a = t[i], b = t[i + 1];
    return a + f * (b - a);
}

inline double wrap01 (double p) { return p - std::floor (p); }

struct TableOsc
{
    double phase = 0.0, inc = 0.0;
    const float* tab = nullptr;

    void set (Wave w, double freq, double sr)
    {
        inc = freq / sr;
        tab = WaveTables::get().table (w, freq, sr);
    }
    inline float tick()
    {
        const float y = readTable (tab, phase);
        phase += inc;
        if (phase >= 1.0) phase -= 1.0;
        return y;
    }
    inline float tickPM (double pm)   // pm in cycles
    {
        const float y = readTable (tab, wrap01 (phase + pm));
        phase += inc;
        if (phase >= 1.0) phase -= 1.0;
        return y;
    }
};

//==============================================================================
// Analog-style exponential ADSR (RC-curve segments)
class ADSR
{
public:
    enum Stage { Idle, Attack, Decay, Sustain, Release };

    void setSampleRate (double s) { sr = s; }

    void setParams (float a, float d, float s, float r)
    {
        sustain = clamp (s, 0.0f, 1.0f);
        attackCoef  = coef (a * (float) sr, kRatioA);
        attackBase  = (1.0f + kRatioA) * (1.0f - attackCoef);
        decayCoef   = coef (d * (float) sr, kRatioDR);
        decayBase   = (sustain - kRatioDR) * (1.0f - decayCoef);
        releaseCoef = coef (r * (float) sr, kRatioDR);
        releaseBase = -kRatioDR * (1.0f - releaseCoef);
    }

    void gateOn()  { stage = Attack; }          // restarts from current level (click-free retrigger)
    void gateOff() { if (stage != Idle) stage = Release; }
    void reset()   { stage = Idle; out = 0.0f; }

    inline float tick()
    {
        switch (stage)
        {
            case Attack:
                out = attackBase + out * attackCoef;
                if (out >= 1.0f) { out = 1.0f; stage = Decay; }
                break;
            case Decay:
                out = decayBase + out * decayCoef;
                if (out <= sustain) { out = sustain; stage = Sustain; }
                break;
            case Sustain: out = sustain; break;
            case Release:
                out = releaseBase + out * releaseCoef;
                if (out <= 0.0f) { out = 0.0f; stage = Idle; }
                break;
            case Idle: default: break;
        }
        return out;
    }

    bool isIdle() const       { return stage == Idle; }
    bool isReleasing() const  { return stage == Release; }
    bool finished() const     { return stage == Idle || ((stage == Sustain || stage == Decay) && sustain <= 0.0f && out < 1.0e-4f); }
    float value() const       { return out; }
    Stage getStage() const    { return stage; }

private:
    static float coef (float rate, float ratio)
    {
        return rate <= 1.0f ? 0.0f : std::exp (-std::log ((1.0f + ratio) / ratio) / rate);
    }
    static constexpr float kRatioA  = 0.3f;
    static constexpr float kRatioDR = 0.0001f;

    double sr = 48000.0;
    Stage stage = Idle;
    float out = 0.0f, sustain = 1.0f;
    float attackCoef = 0, attackBase = 0, decayCoef = 0, decayBase = 0, releaseCoef = 0, releaseBase = 0;
};

//==============================================================================
// Zero-delay-feedback 4-pole transistor-ladder style lowpass (TPT, Zavalishin)
// with nonlinear input stage and passband gain compensation.
struct Ladder
{
    float s[4] = { 0, 0, 0, 0 };
    float G = 0.5f, k = 0.0f;

    void reset() { s[0] = s[1] = s[2] = s[3] = 0.0f; }

    static float computeG (float fc, float sr)
    {
        const float g = std::tan ((float) kPi * clamp (fc, 10.0f, sr * 0.46f) / sr);
        return g / (1.0f + g);
    }

    inline float process (float x, float drive)
    {
        const float G2 = G * G, G3 = G2 * G, G4 = G3 * G;
        const float om = 1.0f - G;
        const float sigma = G3 * om * s[0] + G2 * om * s[1] + G * om * s[2] + om * s[3];
        const float y4est = (G4 * x + sigma) / (1.0f + k * G4);
        float u = fastTanh ((x - k * y4est) * drive) / drive;
        for (int i = 0; i < 4; ++i)
        {
            const float v = (u - s[i]) * G;
            const float y = v + s[i];
            s[i] = flushDenormal (y + v);
            u = y;
        }
        return u * (1.0f + k * 0.42f);
    }
};

//==============================================================================
// Simper / Cytomic state-variable filter (TPT)
struct SVF
{
    float g = 0, kq = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    float lp = 0, bp = 0, hp = 0;

    void reset() { ic1 = ic2 = 0.0f; }
    void set (float fc, float Q, float sr)
    {
        g  = std::tan ((float) kPi * clamp (fc, 5.0f, sr * 0.48f) / sr);
        kq = 1.0f / std::max (Q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + kq));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline void tick (float v0)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushDenormal (2.0f * v1 - ic1);
        ic2 = flushDenormal (2.0f * v2 - ic2);
        lp = v2; bp = v1; hp = v0 - kq * v1 - v2;
    }
};

//==============================================================================
struct OnePoleLP
{
    float a = 0.0f, z = 0.0f;
    void set (float fc, float sr) { a = std::exp (-(float) kTwoPi * clamp (fc, 1.0f, sr * 0.49f) / sr); }
    inline float process (float x) { z = flushDenormal (x + a * (z - x)); return z; }
    void reset() { z = 0; }
};

struct OnePoleHP
{
    OnePoleLP lp;
    void set (float fc, float sr) { lp.set (fc, sr); }
    inline float process (float x) { return x - lp.process (x); }
    void reset() { lp.reset(); }
};

struct DCBlocker
{
    float x1 = 0, y1 = 0, R = 0.9995f;
    void set (float sr) { R = 1.0f - (float) (kTwoPi * 10.0 / sr); }
    inline float process (float x) { const float y = x - x1 + R * y1; x1 = x; y1 = flushDenormal (y); return y; }
    void reset() { x1 = y1 = 0; }
};

// RBJ biquad, transposed direct form II, double precision state
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float process (float xin)
    {
        const double x = xin;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    void setLowShelf (double fc, double gainDb, double sr, double S = 0.8)  { shelf (fc, gainDb, sr, S, true); }
    void setHighShelf (double fc, double gainDb, double sr, double S = 0.8) { shelf (fc, gainDb, sr, S, false); }
    void setPeak (double fc, double gainDb, double Q, double sr)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = kTwoPi * fc / sr;
        const double alpha = std::sin (w) / (2.0 * Q), c = std::cos (w);
        const double a0 = 1 + alpha / A;
        b0 = (1 + alpha * A) / a0; b1 = -2 * c / a0; b2 = (1 - alpha * A) / a0;
        a1 = -2 * c / a0; a2 = (1 - alpha / A) / a0;
    }
private:
    void shelf (double fc, double gainDb, double sr, double S, bool low)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = kTwoPi * fc / sr;
        const double c = std::cos (w), sn = std::sin (w);
        const double alpha = sn / 2.0 * std::sqrt ((A + 1 / A) * (1 / S - 1) + 2);
        const double sq = 2 * std::sqrt (A) * alpha;
        double a0;
        if (low)
        {
            a0 = (A + 1) + (A - 1) * c + sq;
            b0 = A * ((A + 1) - (A - 1) * c + sq) / a0;
            b1 = 2 * A * ((A - 1) - (A + 1) * c) / a0;
            b2 = A * ((A + 1) - (A - 1) * c - sq) / a0;
            a1 = -2 * ((A - 1) + (A + 1) * c) / a0;
            a2 = ((A + 1) + (A - 1) * c - sq) / a0;
        }
        else
        {
            a0 = (A + 1) - (A - 1) * c + sq;
            b0 = A * ((A + 1) + (A - 1) * c + sq) / a0;
            b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0;
            b2 = A * ((A + 1) + (A - 1) * c - sq) / a0;
            a1 = 2 * ((A - 1) - (A + 1) * c) / a0;
            a2 = ((A + 1) - (A - 1) * c - sq) / a0;
        }
    }
};

//==============================================================================
// Power-of-two circular delay line with 4-point Hermite interpolation
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // d = delay in samples relative to the most recently pushed sample (d >= 2 for hermite)
    inline float readHermite (float d) const
    {
        const float pos = (float) (w - 1) - d;
        const float fl = std::floor (pos);
        const int i = (int) fl;
        const float f = pos - fl;
        const float ym1 = buf[(size_t) ((i - 1) & mask)];
        const float y0  = buf[(size_t) (i & mask)];
        const float y1  = buf[(size_t) ((i + 1) & mask)];
        const float y2  = buf[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    }
    inline float readInt (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }
    int capacity() const { return mask + 1; }
private:
    std::vector<float> buf;
    int mask = 0, w = 0;
};

// Schroeder allpass diffuser
struct AllpassDiffuser
{
    DelayLine d; int len = 1; float g = 0.6f;
    void prepare (int samples, float gain) { len = std::max (1, samples); d.prepare (len + 2); g = gain; }
    inline float process (float x)
    {
        const float delayed = d.readInt (len - 1);
        const float v = x + g * delayed;
        d.push (v);
        return delayed - g * v;
    }
    void clear() { d.clear(); }
};

// Linear parameter smoother (no allocation, audio-thread safe)
struct Smooth
{
    float cur = 0, target = 0, step = 0; int left = 0, len = 1;
    void setLength (double sr, double seconds) { len = std::max (1, (int) (sr * seconds)); }
    void reset (float v) { cur = target = v; left = 0; step = 0; }
    void set (float v) { if (v != target) { target = v; left = len; step = (target - cur) / (float) len; } }
    inline float next() { if (left > 0) { cur += step; if (--left == 0) cur = target; } return cur; }
    bool settled() const { return left == 0; }
};
// Lock-free mono waveform tap for the UI (like Tone.Waveform(256))
struct ScopeTap
{
    static constexpr int N = 512;
    float buf[N] {};
    std::atomic<int> w { 0 };
    void push (const float* L, const float* R, int n, float gain = 1.0f)
    {
        int ww = w.load (std::memory_order_relaxed);
        const int start = n > N ? n - N : 0;
        for (int i = start; i < n; ++i) { buf[ww] = 0.5f * gain * (L[i] + R[i]); ww = (ww + 1) & (N - 1); }
        w.store (ww, std::memory_order_release);
    }
    void pushSilence (int n)
    {
        int ww = w.load (std::memory_order_relaxed);
        for (int i = 0; i < std::min (n, N); ++i) { buf[ww] = 0.0f; ww = (ww + 1) & (N - 1); }
        w.store (ww, std::memory_order_release);
    }
    void read (float* out, int count) const
    {
        const int ww = w.load (std::memory_order_acquire);
        for (int i = 0; i < count; ++i) out[i] = buf[(ww - count + i) & (N - 1)];
    }
};
} // namespace sf
