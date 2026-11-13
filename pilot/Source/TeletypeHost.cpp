#include "TeletypeHost.h"
#include "Lang.h"
#include "PilotEngine.h"

extern "C"
{
#include "tt_host.h"
#include "pilot_io.h"
}

namespace sf
{
namespace
{
constexpr uint8_t kI2c2midi = 0x3F;
constexpr uint8_t kJf1 = 0x70, kJf2 = 0x75;   // Just Friends (JF1 / JF2)

std::string upperTrim (const std::string& s)
{
    std::string r;
    for (char c : s) r += (char) std::toupper ((unsigned char) c);
    const auto a = r.find_first_not_of (" \t\r\n");
    if (a == std::string::npos) return {};
    return r.substr (a, r.find_last_not_of (" \t\r\n") - a + 1);
}
} // namespace

TeletypeHost& TeletypeHost::get()
{
    static TeletypeHost h;
    return h;
}

uint32_t TeletypeHost::nowMs() const
{
    return (uint32_t) (juce::Time::getMillisecondCounterHiRes() - t0);
}

std::string TeletypeHost::sceneDir() const
{
    if (! sceneDirOverride.empty()) return sceneDirOverride;
    return sf::pilotDataDir().getChildFile ("Teletype")
        .getFullPathName().toStdString();
}

void TeletypeHost::ensureInit()
{
    if (initialized) return;
    initialized = true;
    t0 = juce::Time::getMillisecondCounterHiRes() - 1.0;
    const auto dir = sceneDir();
    juce::File (dir).createDirectory();
    tt_init (dir.c_str(), nowMs());
   #if JUCE_MAC || JUCE_LINUX
    if (sceneDirOverride.empty())
        virtualOut = juce::MidiOutput::createNewDevice ("Pilot Sealflake");
   #endif
}

void TeletypeHost::attach (PilotEngine* e)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    engines.push_back (e);
    if (owner.load() == nullptr) owner = e;
    ensureInit();
}

void TeletypeHost::detach (PilotEngine* e)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    engines.erase (std::remove (engines.begin(), engines.end(), e), engines.end());
    if (owner.load() == e)
    {
        owner = engines.empty() ? nullptr : engines.front();
        outFifo.reset();
        inFifo.reset();
    }
}

void TeletypeHost::tick()
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    if (! initialized) return;
    // MIDI in -> MI.* ops / MI.$ scripts
    {
        const auto scope = inFifo.read (inFifo.getNumReady());
        auto handle = [] (const juce::MidiMessage& m)
        {
            const uint8_t ch = (uint8_t) (m.getChannel() - 1);
            if (m.isNoteOn()) tt_midi_note_on (ch, (uint8_t) m.getNoteNumber(), m.getVelocity());
            else if (m.isNoteOff()) tt_midi_note_off (ch, (uint8_t) m.getNoteNumber(), m.getVelocity());
            else if (m.isController()) tt_midi_cc (ch, (uint8_t) m.getControllerNumber(), (uint8_t) m.getControllerValue());
            else if (m.isMidiClock()) tt_midi_clock();
            else if (m.isMidiStart()) tt_midi_start();
            else if (m.isMidiStop()) tt_midi_stop();
            else if (m.isMidiContinue()) tt_midi_continue();
        };
        for (int i = 0; i < scope.blockSize1; ++i) handle (inBuf[scope.startIndex1 + i]);
        for (int i = 0; i < scope.blockSize2; ++i) handle (inBuf[scope.startIndex2 + i]);
    }
    const uint32_t now = nowMs();
    tt_tick (now);
    midiTick (now);
}

void TeletypeHost::key (uint8_t hid, uint8_t mod, bool held)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    tt_key (hid, mod, held);
}

void TeletypeHost::keyRelease (uint8_t hid, uint8_t mod)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    tt_key_release (hid, mod);
}

bool TeletypeHost::refresh (uint8_t* frame)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    const bool changed = tt_refresh();
    std::memcpy (frame, tt_framebuffer(), (size_t) (kW * kH));
    return changed;
}

void TeletypeHost::screenLine (int line, void* segs, int max, int& n, uint8_t& bg)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    n = tt_screen_segments (line, (tt_seg_t*) segs, max);
    bg = tt_screen_line_bg (line);
}

const uint8_t* TeletypeHost::textMask()
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    std::memcpy (mask, tt_text_mask(), sizeof (mask));
    return mask;
}

int TeletypeHost::liveSubmode()
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    return tt_live_submode();
}

int TeletypeHost::mode()
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    return tt_mode();
}

std::string TeletypeHost::run (const std::string& text, bool& ok)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    int16_t v = 0;
    char err[64] {};
    const int r = tt_run_command (text.c_str(), &v, err);
    ok = r >= 0;
    if (r == 1) return std::to_string (v);
    if (r < 0) return err;
    return {};
}

bool TeletypeHost::isKnownOp (const std::string& word)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    return tt_is_known_op (upperTrim (word).c_str());
}

std::string TeletypeHost::sceneText()
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    std::vector<char> buf (64 * 1024);
    const size_t n = tt_scene_serialize (buf.data(), buf.size());
    return std::string (buf.data(), n);
}

void TeletypeHost::loadSceneText (const std::string& text, bool runInit)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    tt_scene_deserialize (text.c_str(), runInit);
}

int TeletypeHost::sceneNumber()
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    return tt_scene_number();
}

bool TeletypeHost::metroActive()
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    return tt_metro_active();
}

//==============================================================================
// MIDI
void TeletypeHost::midiIn (const juce::MidiMessage& m)
{
    if (! (m.isNoteOnOrOff() || m.isController() || m.isMidiClock() || m.isMidiStart() || m.isMidiStop() || m.isMidiContinue()))
        return;
    const auto scope = inFifo.write (1);
    if (scope.blockSize1 > 0) inBuf[scope.startIndex1] = m;
    else if (scope.blockSize2 > 0) inBuf[scope.startIndex2] = m;
}

void TeletypeHost::drainMidiOut (juce::MidiBuffer& out, int sample)
{
    const auto scope = outFifo.read (outFifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i) out.addEvent (outBuf[scope.startIndex1 + i], sample);
    for (int i = 0; i < scope.blockSize2; ++i) out.addEvent (outBuf[scope.startIndex2 + i], sample);
}

void TeletypeHost::sendMidi (const juce::MidiMessage& m)
{
    if (virtualOut) virtualOut->sendMessageNow (m);
    const auto scope = outFifo.write (1);
    if (scope.blockSize1 > 0) outBuf[scope.startIndex1] = m;
    else if (scope.blockSize2 > 0) outBuf[scope.startIndex2] = m;
}

void TeletypeHost::midiTick (uint32_t now)
{
    if (! pendingOffs.empty())
    {
        for (auto it = pendingOffs.begin(); it != pendingOffs.end();)
        {
            if ((int32_t) (now - it->due) >= 0)
            {
                sendMidi (juce::MidiMessage::noteOff (it->ch + 1, it->note));
                it = pendingOffs.erase (it);
            }
            else ++it;
        }
    }
    for (auto it = slewing.begin(); it != slewing.end();)
    {
        auto& s = cc[it->first][it->second];
        const uint32_t dt = now - s.last;
        if (dt == 0) { ++it; continue; }
        s.last = now;
        const double before = std::round (s.cur);
        s.cur += s.step * dt;
        const bool done = (s.step >= 0 && s.cur >= s.target) || (s.step < 0 && s.cur <= s.target);
        if (done) s.cur = s.target;
        if (std::round (s.cur) != before)
            sendMidi (juce::MidiMessage::controllerEvent (it->first + 1, it->second, juce::jlimit (0, 127, (int) std::round (s.cur))));
        if (done) it = slewing.erase (it);
        else ++it;
    }
}

// i2c2midi: the I2M.* ops address an i2c2midi module (0x3F); Pilot answers as one.
void TeletypeHost::iiTx (uint8_t addr, const uint8_t* d, uint8_t len)
{
    rxLen = 0; rxAddr = addr;
    if ((addr == kJf1 || addr == kJf2) && len > 0) { jfTx (addr, d, len); return; }
    if (addr != kI2c2midi || len == 0) return;
    const uint32_t now = nowMs();
    auto chIdx = [] (int c) { return juce::jlimit (0, 31, c); };          // 0-based channel byte
    auto midiCh = [] (int c) { return (juce::jlimit (0, 31, c) & 15) + 1; };
    auto s16 = [] (uint8_t hi, uint8_t lo) { return (int16_t) ((hi << 8) | lo); };
    auto replyS16 = [this] (int16_t v) { rxReply[0] = (uint8_t) ((v >> 8) & 0xff); rxReply[1] = (uint8_t) (v & 0xff); rxLen = 2; };
    auto noteOn = [&] (int c, int note, int vel, int dur)
    {
        auto& st = i2m[chIdx (c)];
        note = note + st.shift;
        if (note < st.minN || note > st.maxN || note < 0 || note > 127) return;
        const int ch = midiCh (c);
        if (vel <= 0) { sendMidi (juce::MidiMessage::noteOff (ch, note)); return; }
        sendMidi (juce::MidiMessage::noteOn (ch, note, (uint8_t) juce::jlimit (1, 127, vel)));
        if (dur > 0) pendingOffs.push_back ({ now + (uint32_t) dur, (uint8_t) (ch - 1), (uint8_t) note });
    };
    auto setCc = [&] (int c, int ctl, int value7, bool useSlew)
    {
        const int ch = midiCh (c) - 1;
        auto& s = cc[ch][ctl & 127];
        const int target = juce::jlimit (0, 127, value7 + s.offset);
        if (useSlew && s.slew > 0 && s.cur >= 0)
        {
            s.target = target;
            s.step = (target - s.cur) / (double) s.slew;
            s.last = now;
            const std::pair<uint8_t, uint8_t> key { (uint8_t) ch, (uint8_t) (ctl & 127) };
            if (std::find (slewing.begin(), slewing.end(), key) == slewing.end()) slewing.push_back (key);
            return;
        }
        s.cur = s.target = target;
        slewing.erase (std::remove (slewing.begin(), slewing.end(), std::pair<uint8_t, uint8_t> ((uint8_t) ch, (uint8_t) (ctl & 127))), slewing.end());
        sendMidi (juce::MidiMessage::controllerEvent (ch + 1, ctl & 127, target));
    };

    switch (d[0])
    {
        case 1: if (len >= 2) replyS16 (i2m[chIdx (d[1] - 1)].time); break;                         // TIME get (1-based)
        case 2: if (len >= 4) i2m[chIdx (d[1] - 1)].time = s16 (d[2], d[3]); break;                 // TIME set
        case 3: if (len >= 2) { rxReply[0] = (uint8_t) i2m[chIdx (d[1] - 1)].shift; rxLen = 1; } break;
        case 4: if (len >= 3) i2m[chIdx (d[1] - 1)].shift = (int8_t) d[2]; break;
        case 5: if (len >= 2) replyS16 (i2m[chIdx (d[1] - 1)].rep); break;
        case 6: if (len >= 4) i2m[chIdx (d[1] - 1)].rep = s16 (d[2], d[3]); break;
        case 7: if (len >= 2) replyS16 (i2m[chIdx (d[1] - 1)].rat); break;
        case 8: if (len >= 4) i2m[chIdx (d[1] - 1)].rat = s16 (d[2], d[3]); break;
        case 10: if (len >= 3) i2m[chIdx (d[1] - 1)].minN = (int8_t) d[2]; break;
        case 12: if (len >= 3) i2m[chIdx (d[1] - 1)].maxN = (int8_t) d[2]; break;
        case 20: if (len >= 4) noteOn (d[1], d[2], d[3], i2m[chIdx (d[1])].time); break;            // NOTE (0-based)
        case 21: if (len >= 3) sendMidi (juce::MidiMessage::noteOff (midiCh (d[1]), juce::jlimit (0, 127, d[2] + i2m[chIdx (d[1])].shift))); break;
        case 22:                                                                                     // PANIC
            pendingOffs.clear();
            slewing.clear();
            for (int c = 1; c <= 16; ++c)
            {
                sendMidi (juce::MidiMessage::allNotesOff (c));
                sendMidi (juce::MidiMessage::allSoundOff (c));
            }
            for (auto& st : i2m) st = I2mChannel();
            break;
        case 23: if (len >= 6) noteOn (d[1], d[2], d[3], s16 (d[4], d[5])); break;                  // NT
        case 40: if (len >= 5) setCc (d[1], d[2], ((d[3] << 7) | d[4]) >> 7, true); break;          // CC (14-bit)
        case 41: if (len >= 3) replyS16 (cc[midiCh (d[1]) - 1][d[2] & 127].offset); break;
        case 42: if (len >= 5) cc[midiCh (d[1]) - 1][d[2] & 127].offset = s16 (d[3], d[4]); break;
        case 43: if (len >= 3) replyS16 (cc[midiCh (d[1]) - 1][d[2] & 127].slew); break;
        case 44: if (len >= 5) cc[midiCh (d[1]) - 1][d[2] & 127].slew = s16 (d[3], d[4]); break;
        case 45: if (len >= 5) setCc (d[1], d[2], ((d[3] << 7) | d[4]) >> 7, false); break;         // CC.SET
        case 50: case 55:                                                                            // NRPN / NRPN.SET
            if (len >= 6)
            {
                const int ch = midiCh (d[1]);
                const int v = juce::jlimit (0, 16383, (int) ((d[4] << 7) | d[5]));
                sendMidi (juce::MidiMessage::controllerEvent (ch, 99, d[2] & 127));
                sendMidi (juce::MidiMessage::controllerEvent (ch, 98, d[3] & 127));
                sendMidi (juce::MidiMessage::controllerEvent (ch, 6, v >> 7));
                sendMidi (juce::MidiMessage::controllerEvent (ch, 38, v & 127));
            }
            break;
        case 60: if (len >= 3) sendMidi (juce::MidiMessage::programChange (midiCh (d[1]), d[2] & 127)); break;
        case 61: if (len >= 4) sendMidi (juce::MidiMessage::pitchWheel (midiCh (d[1]), juce::jlimit (0, 16383, s16 (d[2], d[3]) + 8192))); break;
        case 62: if (len >= 3) sendMidi (juce::MidiMessage::channelPressureChange (midiCh (d[1]), d[2] & 127)); break;
        case 63: sendMidi (juce::MidiMessage::midiClock()); break;
        case 64: sendMidi (juce::MidiMessage::midiStart()); break;
        case 65: sendMidi (juce::MidiMessage::midiStop()); break;
        case 66: sendMidi (juce::MidiMessage::midiContinue()); break;
        default: break;   // chords, buffer, mute/solo, queries: not emulated
    }
}

// Just Friends: JF.* ops (Just Type) play the channels in JF mode (engine 'j'):
// the lowest one answers at 0x70 (JF1), the next at 0x75 (JF2); JF.ADDR swaps them.
void TeletypeHost::jfTx (uint8_t addr, const uint8_t* d, uint8_t len)
{
    auto* e = engine();
    if (e == nullptr) return;
    const auto chs = e->jfChannels();
    const int unit = (addr == kJf1 ? 0 : 1) ^ (jfSwap ? 1 : 0);
    if (unit >= (int) chs.size()) return;
    const int ch = chs[(size_t) unit];
    auto s16 = [] (uint8_t hi, uint8_t lo) { return (int) (int16_t) ((hi << 8) | lo); };
    auto edit = [&] (const std::function<void (JfSettings&)>& fn) { e->editChannel (ch, [&fn] (ChannelSettings& s) { fn (s.jf); }); };
    auto reply = [this] (int v, int bytes)
    {
        if (bytes == 1) { rxReply[0] = (uint8_t) v; rxLen = 1; return; }
        rxReply[0] = (uint8_t) ((v >> 8) & 0xff); rxReply[1] = (uint8_t) (v & 0xff); rxLen = 2;
    };
    const auto js = e->getSettings (ch).jf;
    auto knob = [] (float k) { return juce::jlimit (0, 16383, (int) std::lround (k * 16383.0f)); };
    const int idx = len >= 2 ? juce::jlimit (0, 6, (int) d[1]) : 0;
    switch (d[0])
    {
        case 1:  if (len >= 3) e->jfCommand (ch, JustFriends::OpTr, idx, d[2], 0); break;                           // JF.TR
        case 2:  if (len >= 2) edit ([&] (JfSettings& j) { j.runOn = d[1] != 0; }); break;                          // JF.RMODE
        case 3:  if (len >= 3) { const float v = (float) s16 (d[1], d[2]) / 1638.4f; edit ([v] (JfSettings& j) { j.run = v; }); } break;     // JF.RUN
        case 4:  if (len >= 3) { const float v = (float) s16 (d[1], d[2]) / 1638.4f; edit ([v] (JfSettings& j) { j.shift = v; }); } break;   // JF.SHIFT
        case 5:  if (len >= 4) e->jfCommand (ch, JustFriends::OpVtr, idx, s16 (d[2], d[3]), 0); break;             // JF.VTR
        case 6:  if (len >= 2) edit ([&] (JfSettings& j) { j.synth = d[1] != 0; }); break;                          // JF.MODE
        case 7:  if (len >= 2) e->jfCommand (ch, JustFriends::OpTick, 0, d[1], 0); break;                           // JF.TICK
        case 8:  if (len >= 6) e->jfCommand (ch, JustFriends::OpVox, idx, s16 (d[2], d[3]), s16 (d[4], d[5])); break;   // JF.VOX
        case 9:  if (len >= 5) e->jfCommand (ch, JustFriends::OpNote, 0, s16 (d[1], d[2]), s16 (d[3], d[4])); break;   // JF.NOTE / JF.POLY
        case 10: if (len >= 2) edit ([&] (JfSettings& j) { j.god = d[1] != 0; }); break;                            // JF.GOD
        case 11:                                                                                                       // JF.TUNE ch num den
            if (len >= 4)
                edit ([&] (JfSettings& j)
                {
                    for (int i = 0; i < 6; ++i)
                        if (idx == 0 || idx == i + 1) { j.tuneN[i] = (int16_t) (int8_t) d[2]; j.tuneD[i] = (int16_t) (int8_t) d[3]; }
                });
            break;
        case 12: if (len >= 2) edit ([&] (JfSettings& j) { j.qt = (int16_t) (int8_t) d[1]; }); break;              // JF.QT
        case 13: if (len >= 4) e->jfCommand (ch, JustFriends::OpPitch, idx, s16 (d[2], d[3]), 0); break;           // JF.PITCH
        case 14: if (len >= 2) jfSwap = (d[1] == 2) == (addr == kJf1) ? ! jfSwap : jfSwap; break;                  // JF.ADDR
        case 15 | 0x80: reply (js.range, 1); break;                                                                    // JF.SPEED
        case 16 | 0x80: reply (js.mode, 1); break;                                                                     // JF.TSC
        case 17 | 0x80: reply (knob (js.ramp), 2); break;
        case 18 | 0x80: reply (knob (js.curve), 2); break;
        case 19 | 0x80: reply (knob (js.fm), 2); break;
        case 20 | 0x80: reply (knob (js.time), 2); break;
        case 21 | 0x80: reply (knob (js.intone), 2); break;
        default: break;
    }
}

void TeletypeHost::iiRx (uint8_t addr, uint8_t* data, uint8_t len)
{
    for (int i = 0; i < len; ++i) data[i] = (addr == rxAddr && i < rxLen) ? rxReply[i] : 0;
    rxLen = 0;
}

void TeletypeHost::kill()
{
    if (auto* e = engine()) e->panic();
    for (const auto& p : pendingOffs) sendMidi (juce::MidiMessage::noteOff (p.ch + 1, p.note));
    pendingOffs.clear();
}
} // namespace sf

//==============================================================================
// C side: hardware hooks (tt_host.h) and the PI.* ops (pilot_io.h)
using sf::TeletypeHost;

extern "C"
{
uint32_t tt_hw_ticks (void) { return TeletypeHost::get().nowMs(); }
void tt_hw_ii_tx (uint8_t addr, const uint8_t* data, uint8_t l) { TeletypeHost::get().iiTx (addr, data, l); }
void tt_hw_ii_rx (uint8_t addr, uint8_t* data, uint8_t l) { TeletypeHost::get().iiRx (addr, data, l); }
void tt_hw_kill (void) { TeletypeHost::get().kill(); }

static sf::PilotEngine* eng() { return TeletypeHost::get().engine(); }

static int16_t toI16 (double v) { return (int16_t) juce::jlimit (-32768.0, 32767.0, std::round (v)); }

void pilot_io_note (int16_t ch, int16_t note, int16_t vel, int16_t len_ms)
{
    if (auto* e = eng())
    {
        const float len = len_ms < 0 ? -1.0f : len_ms == 0 ? (float) e->lang().noteLengthMs (ch) : (float) len_ms;
        e->playNote (ch, note, (float) vel / 127.0f, len);
    }
}
void pilot_io_note_off (int16_t ch, int16_t note) { if (auto* e = eng()) e->releaseNote (ch, note); }
void pilot_io_all_off (void) { if (auto* e = eng()) e->panic(); }
void pilot_io_command (const char* cmd) { if (auto* e = eng()) e->runLegacy (cmd); }

int16_t pilot_io_param_get (int16_t ch, int16_t num)
{
    auto* e = eng();
    double lo, hi; int scale;
    if (e == nullptr || ! e->lang().piInfo (num, lo, hi, scale)) return 0;
    return toI16 (e->lang().piGetCh (ch, num) * scale);
}
void pilot_io_param_set (int16_t ch, int16_t num, int16_t value)
{
    auto* e = eng();
    double lo, hi; int scale;
    if (e == nullptr || ! e->lang().piInfo (num, lo, hi, scale)) return;
    e->lang().piSetCh (ch, num, (double) value / scale);
}
void pilot_io_param_pv (int16_t ch, int16_t num, int16_t value)
{
    if (auto* e = eng()) e->lang().piSetNormCh (ch, num, (double) value / 16384.0);
}
int16_t pilot_io_param_min (int16_t num)
{
    auto* e = eng();
    double lo, hi; int scale;
    return (e != nullptr && e->lang().piInfo (num, lo, hi, scale)) ? toI16 (lo * scale) : 0;
}
int16_t pilot_io_param_max (int16_t num)
{
    auto* e = eng();
    double lo, hi; int scale;
    return (e != nullptr && e->lang().piInfo (num, lo, hi, scale)) ? toI16 (hi * scale) : 0;
}

int16_t pilot_io_preset_get (void) { auto* e = eng(); return e ? (int16_t) std::atoi (e->lang().run ("PI.PRESET").c_str()) : 0; }
void pilot_io_preset_load (int16_t n) { if (auto* e = eng()) e->lang().run ("PI.PRESET " + std::to_string (juce::jlimit (0, 31, (int) n))); }
void pilot_io_preset_save (int16_t n) { if (auto* e = eng()) e->lang().run ("PI.SAVE " + std::to_string (juce::jlimit (0, 31, (int) n))); }
void pilot_io_reset (void) { if (auto* e = eng()) { e->resetChannels(); e->resetEffects(); } }
void pilot_io_rec (int16_t on) { if (auto* e = eng()) if (e->onRecord) e->onRecord (on != 0); }
int16_t pilot_io_bpm_get (void) { auto* e = eng(); return e ? toI16 (e->tempo()) : 120; }
void pilot_io_bpm_set (int16_t bpm)
{
    if (auto* e = eng())
    {
        e->bpm = (double) juce::jlimit (30, 400, (int) bpm);
        e->useHostTempo = false;
    }
}
}
