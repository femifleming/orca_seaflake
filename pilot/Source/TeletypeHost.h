// Pilot Sealflake — the Teletype (monome's firmware, teletype/) running inside Pilot.
//
// There is one Teletype per process, like one module in a rack: every Pilot
// instance shows the same Teletype screen, and PI.* ops play the instance that
// owns it (the first one opened). All calls into the firmware go through one lock.
//
//   PI.*    play / change the Pilot synth          (teletype/src/ops/pilot.c)
//   I2M.*   i2c2midi, sent as real MIDI out:       virtual port "Pilot Sealflake",
//           the plugin's MIDI output, and the standalone's MIDI output device
//   MI.*    MIDI in (from the plugin's / standalone's MIDI input)
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sf
{
class PilotEngine;

class TeletypeHost
{
public:
    static TeletypeHost& get();
    static inline std::string sceneDirOverride;   // tests: keep the user's scenes untouched

    static constexpr int kW = 128, kH = 64;

    void attach (PilotEngine*);
    void detach (PilotEngine*);
    bool isOwner (const PilotEngine* e) const { return owner.load() == e; }

    void tick();   // the owner's scheduler thread, every millisecond

    // UI thread
    void key (uint8_t hid, uint8_t mod, bool held);
    void keyRelease (uint8_t hid, uint8_t mod);
    bool refresh (uint8_t* frame);   // copies the 128x64 OLED (0-15 per pixel); true when changed
    int mode();
    int liveSubmode();
    // the screen as text (tt_seg_t, tt_host.h): copied under the lock after refresh()
    void screenLine (int line, void* segs, int max, int& n, uint8_t& bg);
    const uint8_t* textMask();

    // A live command (Pilot's command line / UDP). ok = false on a Teletype error.
    std::string run (const std::string& text, bool& ok);
    bool isKnownOp (const std::string& word);

    std::string sceneText();
    void loadSceneText (const std::string&, bool runInit);
    int sceneNumber();
    bool metroActive();

    // MIDI in from the audio thread (lock-free; handled on the next tick)
    void midiIn (const juce::MidiMessage&);
    // MIDI out to the owner's audio thread (lock-free)
    void drainMidiOut (juce::MidiBuffer& out, int sample);

    // ---- called from the firmware (under the lock) ----
    void iiTx (uint8_t addr, const uint8_t* data, uint8_t len);
    void iiRx (uint8_t addr, uint8_t* data, uint8_t len);
    void jfTx (uint8_t addr, const uint8_t* data, uint8_t len);
    void kill();
    PilotEngine* engine() const { return owner.load(); }
    uint32_t nowMs() const;

private:
    TeletypeHost() = default;
    void ensureInit();
    void sendMidi (const juce::MidiMessage&);
    void midiTick (uint32_t now);
    std::string sceneDir() const;

    mutable std::recursive_mutex mtx;
    std::atomic<PilotEngine*> owner { nullptr };
    std::vector<PilotEngine*> engines;
    bool initialized = false;
    double t0 = 0.0;

    // MIDI out
    std::unique_ptr<juce::MidiOutput> virtualOut;
    juce::AbstractFifo outFifo { 1024 };
    juce::MidiMessage outBuf[1024];

    // MIDI in
    juce::AbstractFifo inFifo { 1024 };
    juce::MidiMessage inBuf[1024];

    // i2c2midi emulation (32 channels: 1-16 and 17-32 both go out on MIDI channels 1-16)
    struct I2mChannel { int16_t time = 100, shift = 0, rep = 1, rat = 1; int8_t minN = 0, maxN = 127; };
    I2mChannel i2m[32];
    struct PendingOff { uint32_t due; uint8_t ch, note; };
    std::vector<PendingOff> pendingOffs;
    struct CcState { int16_t offset = 0, slew = 0; double cur = -1, target = 0, step = 0; uint32_t last = 0; };
    CcState cc[16][128];
    std::vector<std::pair<uint8_t, uint8_t>> slewing;
    uint8_t rxReply[4] {};
    uint8_t mask[kW * kH] {};
    int rxLen = 0;
    uint8_t rxAddr = 0;
    bool jfSwap = false;   // JF.ADDR
};
} // namespace sf
