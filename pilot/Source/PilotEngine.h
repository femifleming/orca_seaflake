// Pilot Sealflake — engine: thread-safe command queue, synth + fx, language host
#pragma once
#include "dsp/Synth.h"
#include "dsp/Effects.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <deque>
#include <memory>
#include <thread>
#include <vector>

namespace sf
{
class Lang;

struct Cmd
{
    enum Type : uint8_t { Note, NoteOff, Settings, AllOff, Kill, Jf };
    Type type = Note;
    int8_t ch = 0;
    int16_t note = 60;
    float vel = 0.66f;
    float lengthBeats = 0.25f;  // > 0: length in beats (tempo-relative)
    float lengthMs = -1.0f;     // > 0: length in ms; both <= 0: held until NoteOff
    ChannelSettings settings;   // for Settings
    uint8_t jfOp = 0; int8_t jfIdx = 0; int16_t a = 0, b = 0;   // for Jf (Just Type)
};

// Single-consumer ring; producers serialized by a mutex (never the audio thread)
class CmdQueue
{
public:
    static constexpr int kSize = 4096;
    bool push (const Cmd& c)
    {
        std::lock_guard<std::mutex> lk (prodLock);
        const int w = write.load (std::memory_order_relaxed);
        const int next = (w + 1) % kSize;
        if (next == read.load (std::memory_order_acquire)) return false;
        buf[w] = c;
        write.store (next, std::memory_order_release);
        return true;
    }
    bool pop (Cmd& c)
    {
        const int r = read.load (std::memory_order_relaxed);
        if (r == write.load (std::memory_order_acquire)) return false;
        c = buf[r];
        read.store ((r + 1) % kSize, std::memory_order_release);
        return true;
    }
private:
    Cmd buf[kSize];
    std::atomic<int> read { 0 }, write { 0 };
    std::mutex prodLock;
};

// MIDI routing modes (plugin / standalone MIDI input)
enum MidiMode : int { MidiMulti = 0, MidiOmni = 1, MidiRoundRobin = 2, MidiRoundRobinFree = 3 };

class PilotEngine
{
public:
    static constexpr int kChannels = Synth::kChannels;

    PilotEngine();
    ~PilotEngine();

    // ---- producer side (UDP thread / message thread / scheduler) ----
    // Runs a line of Pilot language (Teletype-style ops, or legacy Pilot commands).
    // Returns a short result / error string for display.
    std::string submit (const std::string& text, bool logIt = true);
    void runLegacy (const std::string& cmd);
    bool setQuiet (bool q) { return quiet.exchange (q); }   // returns the previous value
    static bool isLegacyCommand (const std::string& cmd);
    void resetChannels();                 // presets + default voice settings
    void resetEffects();                  // defaults + clear named overrides
    std::string serializeState() const;   // channels + fx overrides + midi routing, as commands
    ChannelSettings getSettings (int ch) const;
    void editChannel (int ch, const std::function<void (ChannelSettings&)>& fn);
    std::deque<std::string> recentLog() const;
    void panic() { Cmd c; c.type = Cmd::AllOff; queue.push (c); }
    void playNote (int ch, int note, float vel, float lengthMs);   // lengthMs <= 0: hold
    void releaseNote (int ch, int note);
    // Just Friends channels (engine 'j'): a Just Type trigger / note (JustFriends::Op)
    void jfCommand (int ch, int op, int idx, int a, int b);
    std::vector<int> jfChannels() const;   // channels in JF mode, lowest first (JF1, JF2 on i2c)

    // effects: raw legacy controls (0..1, < 0 = unchanged) and named parameters (real units)
    void setFxRaw (int fx, float w, float v);
    float getFxParam (const FxParamDef& d) const;
    void setFxParam (const FxParamDef& d, float unit);
    void clearFxOverrides (int fx);
    FxContext fxContext() const { return { sr, curBpm.load() }; }

    // Called for effect changes from the command language. If unset, the engine
    // applies them directly to its own fx parameter mirror.
    std::function<void (int fx, float w, float v)> onFxCommand;   // w or v may be < 0 = unchanged
    std::function<void (int port)> onPortCommand;
    std::function<void (bool on)> onRecord;   // PI.REC
    std::atomic<float> fxW[kNumFx], fxV[kNumFx];

    std::atomic<double> bpm { 120.0 };
    std::atomic<bool> useHostTempo { true };
    std::atomic<int> udpPort { 49161 };

    // MIDI routing (read on the audio thread by the processor)
    std::atomic<int> midiMode { MidiMulti }, midiTarget { 0 };
    std::atomic<int> rrMask { 0xFFFF };

    // ---- audio side ----
    void prepare (double sampleRate, int maxBlock);
    void process (float* L, float* R, int n, double hostBpm);
    void midiNoteOn (int ch, int note, float vel)  { synth.noteOn (ch, note, vel, -1); }
    void midiNoteOff (int ch, int note)            { synth.noteOff (ch, note); }
    void midiBend (int ch, float norm)             { synth.setBend (ch, norm); }
    void midiSustain (int ch, bool down)           { synth.setSustain (ch, down); }
    void midiAllOff()                              { synth.allNotesOff(); }
    int  gatedVoices (int ch) const                { return synth.gatedVoices (ch); }
    void handleMidi (const juce::MidiMessage& m);   // routes by midiMode (multi / omni / round robin)
    int  latency() const { return fx.latency(); }
    uint64_t limiterClips() const { return fx.limiterClips(); }

    const ScopeTap& channelTap (int ch) const { return synth.channels[ch].tap; }
    const ScopeTap& fxTap (int i) const      { return fx.taps[i]; }
    uint32_t channelNotes (int ch) const     { return synth.channels[ch].notes.load (std::memory_order_relaxed); }
    float channelMeter (int ch) { return synth.channels[ch].meter.exchange (0.0f); }
    int   channelVoices (int ch) const { return synth.channels[ch].active.load(); }
    float limiterGain() { return limGain.exchange (1.0f); }
    float lastLimiterGain() const { return lastLimGain.load(); }
    double effectiveBpm() const { return curBpm.load(); }
    double tempo() const { return (useHostTempo.load() && hostTempoSeen.load()) ? curBpm.load() : bpm.load(); }   // for the language
    double sampleRate() const { return sr; }

    Lang& lang() { return *language; }

    // scope ring (mono, decimated) for UI
    static constexpr int kScope = 2048;
    float scope[kScope] {};
    std::atomic<int> scopeWrite { 0 };

private:
    std::atomic<bool> quiet { false };
    void pushSettings (int ch);
    void log (const std::string& s);
    void schedulerLoop();

    mutable std::mutex mirrorLock;
    ChannelSettings mirror[kChannels];
    std::deque<std::string> logLines;
    Rng rng { 0xC0FFEEu };

    CmdQueue queue;
    Synth synth;
    FxChain fx;
    double sr = 48000.0;
    int scopeDecim = 0;
    std::atomic<float> limGain { 1.0f }, lastLimGain { 1.0f };
    std::atomic<double> curBpm { 120.0 };
    std::atomic<bool> hostTempoSeen { false };

    int8_t noteMap[16][128];          // MIDI (src channel, note) -> Pilot channel it went to
    int lastMidiMode = 0, rrLast = 15;

    std::unique_ptr<Lang> language;
    std::atomic<bool> schedulerRun { true };
    std::thread scheduler;
};

// Pilot's data folder: ~/Library/Application Support/Pilot Sealflake (macOS), %APPDATA%\Pilot Sealflake (Windows),
// ~/.config/Pilot Sealflake (Linux)
inline juce::File pilotDataDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    d = d.getChildFile ("Application Support");
   #endif
    return d.getChildFile ("Pilot Sealflake");
}

// base-36 helpers (Pilot / Orca style)
inline int int36 (char c)
{
    c = (char) std::tolower ((unsigned char) c);
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    return -1;
}
inline char str36 (int v) { static const char* t = "0123456789abcdefghijklmnopqrstuvwxyz"; return t[clamp (v, 0, 35)]; }
inline float from16 (char c) { const int v = int36 (c); return v < 0 ? -1.0f : std::min (1.0f, (float) v / 15.0f); }
inline char to16 (float x) { return str36 ((int) std::lround (clamp (x, 0.0f, 1.0f) * 15.0f)); }
} // namespace sf
