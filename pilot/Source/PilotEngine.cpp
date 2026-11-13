#include "PilotEngine.h"
#include "Lang.h"
#include "TeletypeHost.h"
#include <cctype>
#include <chrono>
#include <cstdio>
#include <sstream>

namespace sf
{
namespace
{
// Original Pilot transpose table: letter -> (note, octave offset)
bool transposeNote (char n, char& outNote, int& outOct)
{
    static const char* table[][2] = {
        { "A", "A0" }, { "a", "a0" }, { "B", "B0" }, { "C", "C0" }, { "c", "c0" }, { "D", "D0" }, { "d", "d0" },
        { "E", "E0" }, { "F", "F0" }, { "f", "f0" }, { "G", "G0" }, { "g", "g0" }, { "H", "A0" }, { "h", "a0" },
        { "I", "B0" }, { "J", "C1" }, { "j", "c1" }, { "K", "D1" }, { "k", "d1" }, { "L", "E1" }, { "M", "F1" },
        { "m", "f1" }, { "N", "G1" }, { "n", "g1" }, { "O", "A1" }, { "o", "a1" }, { "P", "B1" }, { "Q", "C2" },
        { "q", "c2" }, { "R", "D2" }, { "r", "d2" }, { "S", "E2" }, { "T", "F2" }, { "t", "f2" }, { "U", "G2" },
        { "u", "g2" }, { "V", "A2" }, { "v", "a2" }, { "W", "B2" }, { "X", "C3" }, { "x", "c3" }, { "Y", "D3" },
        { "y", "d3" }, { "Z", "E3" }, { "e", "F0" }, { "l", "F1" }, { "s", "F2" }, { "z", "F3" }, { "b", "C1" },
        { "i", "C1" }, { "p", "C2" }, { "w", "C3" } };
    for (auto& t : table)
        if (t[0][0] == n) { outNote = t[1][0]; outOct = t[1][1] - '0'; return true; }
    return false;
}

int noteIndex (char n)
{
    static const char order[12] = { 'C', 'c', 'D', 'd', 'E', 'F', 'f', 'G', 'g', 'A', 'a', 'B' };
    for (int i = 0; i < 12; ++i) if (order[i] == n) return i;
    return -1;
}

std::string lower (std::string s) { for (auto& c : s) c = (char) std::tolower ((unsigned char) c); return s; }

std::string fmtNum (double v)
{
    char b[32];
    std::snprintf (b, sizeof (b), "%g", v);
    return b;
}

// Default channel presets — Pilot Sealflake voicing
const char* kPresetCommands =
    "0ENGa;0OSC4wsw;0ENV0866;0FIL748;0MOD35;"
    "1ENGa;1OSC2qsq;1ENV0604;1FIL56a;1MOD54;"
    "2ENGa;2OSC8w4r;2ENV6acc;2FIL624;2MOD36;"
    "3ENGa;3OSCsqsw;3ENV0563;3FIL489;3MOD18;"
    "4ENGm;4OSCsisq;4ENV0a09;4FILf00;4MOD4a;"
    "5ENGm;5OSCtrsi;5ENV16c5;5FILf00;5MOD58;"
    "6ENGf;6OSCsisi;6ENV0b37;6FILf00;6MOD36;"
    "7ENGf;7OSCsisi;7ENV0c0c;7FILf00;7MOD88;"
    "8ENGf;8OSCsisi;8ENV0643;8FILf00;8MOD3a;"
    "9ENGf;9OSC2isi;9ENV0928;9FILf00;9MOD6b;"
    "aENGp;aOSCsi--;aENV0a08;aFILa00;aMOD30;"
    "bENGa;bOSCsw--;bENV0623;bFIL3cc;bMOD30;"
    "cENGk;cOSCsi--;cENV0800;cFILf00;cMOD39;"
    "dENGs;dOSCsi--;dENV0700;dFILf00;dMOD0a;"
    "eENGh;eOSCsq--;eENV0500;eFILf00;eMOD06;"
    "fENGt;fOSCsi--;fENV0900;fFILf00;fMOD46";
} // namespace

PilotEngine::PilotEngine()
{
    for (int i = 0; i < kNumFx; ++i) { fxW[i] = kFxInfo[i].defW; fxV[i] = kFxInfo[i].defV; }
    for (auto& row : noteMap) for (auto& x : row) x = -1;
    language = std::make_unique<Lang> (*this);
    for (auto* p = kPresetCommands; *p;)
    {
        const char* e = std::strchr (p, ';');
        const std::string part = e ? std::string (p, (size_t) (e - p)) : std::string (p);
        runLegacy (part);
        if (! e) break;
        p = e + 1;
    }
    TeletypeHost::get().attach (this);
    scheduler = std::thread ([this] { schedulerLoop(); });
}

PilotEngine::~PilotEngine()
{
    schedulerRun = false;
    if (scheduler.joinable()) scheduler.join();
    TeletypeHost::get().detach (this);
}

// Teletype's clock (metro, DEL, TR pulses, MIDI out timing): 1 ms on a dedicated thread, never the audio thread
void PilotEngine::schedulerLoop()
{
    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    auto& tt = TeletypeHost::get();
    while (schedulerRun.load())
    {
        next += std::chrono::milliseconds (1);
        std::this_thread::sleep_until (next);
        if (tt.isOwner (this)) tt.tick();
        if (clock::now() - next > std::chrono::milliseconds (50)) next = clock::now();
    }
}

void PilotEngine::log (const std::string& s)
{
    if (quiet) return;
    std::lock_guard<std::mutex> lk (mirrorLock);
    logLines.push_back (s);
    while (logLines.size() > 12) logLines.pop_front();
}

std::deque<std::string> PilotEngine::recentLog() const
{
    std::lock_guard<std::mutex> lk (mirrorLock);
    return logLines;
}

ChannelSettings PilotEngine::getSettings (int ch) const
{
    std::lock_guard<std::mutex> lk (mirrorLock);
    return mirror[clamp (ch, 0, kChannels - 1)];
}

void PilotEngine::editChannel (int ch, const std::function<void (ChannelSettings&)>& fn)
{
    if (ch < 0 || ch >= kChannels) return;
    { std::lock_guard<std::mutex> lk (mirrorLock); fn (mirror[ch]); }
    pushSettings (ch);
}

void PilotEngine::pushSettings (int ch)
{
    Cmd c; c.type = Cmd::Settings; c.ch = (int8_t) ch;
    {
        std::lock_guard<std::mutex> lk (mirrorLock);
        c.settings = mirror[ch];
    }
    queue.push (c);
}

void PilotEngine::playNote (int ch, int note, float vel, float lengthMs)
{
    if (ch < 0 || ch >= kChannels) return;
    Cmd c; c.type = Cmd::Note; c.ch = (int8_t) ch;
    c.note = (int16_t) clamp (note, 0, 127);
    c.vel = clamp (vel, 0.0f, 1.0f);
    c.lengthBeats = -1.0f;
    c.lengthMs = lengthMs;
    queue.push (c);
}

void PilotEngine::jfCommand (int ch, int op, int idx, int a, int b)
{
    if (ch < 0 || ch >= kChannels) return;
    Cmd c; c.type = Cmd::Jf; c.ch = (int8_t) ch; c.jfOp = (uint8_t) op; c.jfIdx = (int8_t) idx; c.a = (int16_t) a; c.b = (int16_t) b;
    queue.push (c);
}

std::vector<int> PilotEngine::jfChannels() const
{
    std::vector<int> r;
    std::lock_guard<std::mutex> lk (mirrorLock);
    for (int ch = 0; ch < kChannels; ++ch) if (mirror[ch].engine == EngJF) r.push_back (ch);
    return r;
}

void PilotEngine::releaseNote (int ch, int note)
{
    if (ch < 0 || ch >= kChannels) return;
    Cmd c; c.type = Cmd::NoteOff; c.ch = (int8_t) ch; c.note = (int16_t) clamp (note, 0, 127);
    queue.push (c);
}

void PilotEngine::resetChannels()
{
    {
        std::lock_guard<std::mutex> lk (mirrorLock);
        for (auto& m : mirror) m.resetExtended();
    }
    const bool q = quiet.exchange (true);
    for (auto* p = kPresetCommands; *p;)
    {
        const char* e = std::strchr (p, ';');
        runLegacy (e ? std::string (p, (size_t) (e - p)) : std::string (p));
        if (! e) break;
        p = e + 1;
    }
    quiet = q;
}

void PilotEngine::setFxRaw (int f, float w, float v)
{
    if (f < 0 || f >= kNumFx) return;
    if (onFxCommand) onFxCommand (f, w, v);
    if (w >= 0) fxW[f] = clamp (w, 0.0f, 1.0f);   // immediate mirror so getters see the new value
    if (v >= 0) fxV[f] = clamp (v, 0.0f, 1.0f);
}

void PilotEngine::resetEffects()
{
    for (int i = 0; i < kNumFx; ++i)
    {
        setFxRaw (i, kFxInfo[i].defW, kFxInfo[i].defV);
        clearFxOverrides (i);
    }
}

void PilotEngine::clearFxOverrides (int f)
{
    if (f < 0 || f >= kNumFx) return;
    for (auto& e : fx.ext[f]) e.store (kUnset);
}

float PilotEngine::getFxParam (const FxParamDef& d) const
{
    const auto ctx = fxContext();
    const float w = fxW[d.fx].load(), v = fxV[d.fx].load();
    if (d.kind == 'E')
    {
        const float o = fx.ext[d.fx][d.slot].load();
        if (isSet (o)) return o;
    }
    return d.derive (w, v, ctx);
}

void PilotEngine::setFxParam (const FxParamDef& d, float unit)
{
    unit = clamp (unit, d.lo, d.hi);
    if (d.kind == 'W') { setFxRaw (d.fx, d.toRaw (unit), -1.0f); return; }
    if (d.kind == 'V') { setFxRaw (d.fx, -1.0f, d.toRaw (unit)); return; }
    fx.ext[d.fx][d.slot].store (unit);
    // a tempo-synced setting clears the matching free-time override
    const std::string n = d.name;
    if (n == "SYNC")
        if (auto* t = findFxParam (d.fx, d.fx == FxTre ? "RATE" : "TIME"))
            if (t->kind == 'E') fx.ext[d.fx][t->slot].store (kUnset);
}

std::string PilotEngine::serializeState() const
{
    std::string out;
    const ChannelSettings def;
    for (int ch = 0; ch < kChannels; ++ch)
    {
        ChannelSettings s;
        { std::lock_guard<std::mutex> lk (mirrorLock); s = mirror[ch]; }
        const char id = str36 (ch);
        const std::string n = std::to_string (ch);
        out += id; out += "ENG"; out += s.engine; out += ';';
        if (s.engine == EngJF)
        {
            out += "CH.TIME " + n + " " + fmtNum (s.jf.time * 100.0) + ";CH.INTONE " + n + " " + fmtNum (s.jf.intone * 100.0)
                 + ";CH.RAMP " + n + " " + fmtNum (s.jf.ramp * 100.0) + ";CH.CURVE " + n + " " + fmtNum (s.jf.curve * 100.0)
                 + ";CH.FM " + n + " " + fmtNum (s.jf.fm * 100.0) + ";CH.RANGE " + n + " " + std::to_string (s.jf.range)
                 + ";CH.MODE " + n + " " + std::to_string (s.jf.mode) + ";";
        }
        out += id; out += "OSC";
        out += s.wave1 >= 0 ? kWaveCodes[s.wave1] : "--";
        out += s.wave2 >= 0 ? kWaveCodes[s.wave2] : "--";
        out += ';';
        // continuous values are stored exactly via the language (legacy hex is 16-step)
        out += "CH.ATKN " + n + " " + fmtNum (s.env[0]) + ";CH.DECN " + n + " " + fmtNum (s.env[1])
             + ";CH.SUSN " + n + " " + fmtNum (s.env[2]) + ";CH.RELN " + n + " " + fmtNum (s.env[3]) + ";";
        out += "CH.CUTN " + n + " " + fmtNum (s.fil[0]) + ";CH.RESN " + n + " " + fmtNum (s.fil[1])
             + ";CH.FENVN " + n + " " + fmtNum (s.fil[2]) + ";";
        out += "CH.RATION " + n + " " + fmtNum (s.mod[0]) + ";CH.INDEXN " + n + " " + fmtNum (s.mod[1]) + ";";
        auto addIf = [&] (const char* op, double v, double d) { if (v != d) out += std::string (op) + " " + n + " " + fmtNum (v) + ";"; };
        addIf ("CH.VOICES", s.voices, def.voices);
        addIf ("CH.GLIDE", s.glideMs, def.glideMs);
        addIf ("CH.LEGATO", s.legato ? 1 : 0, def.legato ? 1 : 0);
        addIf ("CH.PAN", s.pan, def.pan);
        addIf ("CH.LEVEL", s.levelDb, def.levelDb);
        addIf ("CH.DETUNE", s.detuneCents, def.detuneCents);
        addIf ("CH.WIDTH", s.width, def.width);
        addIf ("CH.BEND", s.bendRange, def.bendRange);
        addIf ("CH.TRANS", s.transpose, def.transpose);
        addIf ("CH.FINE", s.fineCents, def.fineCents);
        addIf ("CH.VSENS", s.velSens, def.velSens);
        addIf ("CH.KEYT", s.keyTrack, def.keyTrack);
        addIf ("CH.DRIFT", s.driftCents, def.driftCents);
    }
    for (int f = 0; f < kNumFx; ++f)
        for (int sl = 0; sl < kMaxFxSlots; ++sl)
        {
            const float o = fx.ext[f][sl].load();
            if (! isSet (o)) continue;
            if (auto* d = findFxSlot (f, sl))
                out += std::string (kFxInfo[f].code) + "." + d->name + " " + fmtNum (o) + ";";
        }
    out += "MIDI.MODE " + std::to_string (midiMode.load()) + ";MIDI.CH " + std::to_string (midiTarget.load())
         + ";MIDI.MASK " + std::to_string (rrMask.load()) + ";";
    return out;
}

// Pilot's command line and UDP (ORCA). In order:
//   1. classic Pilot commands, alone or joined with ';'   04C   0ENV056f;REV8c   bpm140
//   2. a Teletype command, run like Teletype's live mode    PI.N 0 60 100   DEL 500: PI.T 12   M 250
//   3. Pilot's extended commands (no Teletype equivalent)   CH.VOICES 0 4   COM.RATIO 1.4
std::string PilotEngine::submit (const std::string& text, bool logIt)
{
    const bool prevQuiet = quiet.exchange (! logIt);
    std::string r;
    auto trim = [] (const std::string& x)
    {
        const auto a = x.find_first_not_of (" \t\r\n");
        return a == std::string::npos ? std::string() : x.substr (a, x.find_last_not_of (" \t\r\n") - a + 1);
    };
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : text) { if (c == '\n' || c == '\r') { lines.push_back (cur); cur.clear(); } else cur += c; }
        lines.push_back (cur);
    }
    for (auto& raw : lines)
    {
        const std::string line = trim (raw);
        if (line.empty()) continue;

        std::vector<std::string> parts;
        bool allLegacy = true;
        {
            std::string cur;
            for (char c : line + ";")
            {
                if (c != ';') { cur += c; continue; }
                const auto t = trim (cur);
                cur.clear();
                if (t.empty()) continue;
                parts.push_back (t);
                if (t.find (' ') != std::string::npos || ! isLegacyCommand (t)) allLegacy = false;
            }
        }
        if (allLegacy && ! parts.empty()) { for (auto& p : parts) runLegacy (p); r.clear(); continue; }

        bool ok = false;
        const auto tt = TeletypeHost::get().run (line, ok);
        if (ok) { r = tt; if (logIt) log (line); continue; }
        // not Teletype: Pilot's extended commands (and saved sessions) when the first word isn't a Teletype op
        const auto first = line.substr (0, line.find_first_of (" ;:"));
        if (! TeletypeHost::get().isKnownOp (first) && language->validate (line).empty()) { r = language->run (line); continue; }
        r = tt;   // the Teletype error
    }
    quiet = prevQuiet;
    return r;
}

// Original Pilot command set (single token, no spaces) — kept for Orca compatibility
// True when a single word is a classic Pilot command (as sent by ORCA / orca-seaflake).
// These always win over the stack language so ORCA patches keep working unchanged.
bool PilotEngine::isLegacyCommand (const std::string& cmd)
{
    if (cmd.size() < 2 || cmd.find ('.') != std::string::npos) return false;
    const std::string lc = lower (cmd);
    if (cmd.size() >= 4 && fxIndexOf (cmd.c_str()) >= 0 && from16 (cmd[3]) >= 0) return true;              // REVff
    if (lc.size() > 3 && lc.rfind ("bpm", 0) == 0 && std::isdigit ((unsigned char) lc[3])) return true;   // bpm140
    for (const char* sp : { "renv", "rosc", "refx", "reset" }) if (lc.rfind (sp, 0) == 0) return true;
    const int ch = int36 (cmd[0]);
    if (ch < 0 || ch >= kChannels) return false;
    const std::string rest = lc.substr (1);
    for (const char* op : { "env", "fil", "mod", "eng", "osc" }) if (rest.size() > 3 && rest.rfind (op, 0) == 0) return true;   // 0ENV056f
    char nn; int off;
    return cmd.size() >= 3 && std::isdigit ((unsigned char) cmd[1]) && transposeNote (cmd[2], nn, off);   // 04C, 34Cf8
}

void PilotEngine::runLegacy (const std::string& cmd)
{
    if (cmd.empty()) return;
    const std::string lc = lower (cmd);

    // ---- global effects: XXXwv (like Pilot: first 3 chars pick the effect, next 2 are the values) ----
    if (cmd.size() >= 4)
    {
        const int fxi = fxIndexOf (cmd.c_str());
        if (fxi >= 0)
        {
            const float w = from16 (cmd[3]);
            const float v = cmd.size() >= 5 ? from16 (cmd[4]) : -1.0f;
            if (w < 0.0f) return;
            setFxRaw (fxi, w, v);
            log (cmd);
            return;
        }
    }

    // ---- specials ----
    if (lc.rfind ("bpm", 0) == 0)
    {
        const int b = std::atoi (lc.c_str() + 3);
        if (b >= 30 && b <= 400) { bpm = (double) b; useHostTempo = false; log (cmd); }
        return;
    }
    if (lc.rfind ("renv", 0) == 0 || lc.rfind ("rosc", 0) == 0)
    {
        for (int ch = 0; ch < kChannels; ++ch) runLegacy (std::string (1, str36 (ch)) + (lc[1] == 'e' ? "ren" : "ros"));
        return;
    }
    if (lc.rfind ("refx", 0) == 0)
    {
        for (int i = 0; i < FxEqu; ++i) setFxRaw (i, rng.unipolar() * 0.5f, rng.unipolar());
        log (cmd);
        return;
    }
    if (lc.rfind ("reset", 0) == 0) { resetChannels(); resetEffects(); log (cmd); return; }
    if (lc == "stop" || lc == "panic") { panic(); log (cmd); return; }
    if (lc.size() > 4 && lc.rfind ("port", 0) == 0 && lc.find_first_not_of ("0123456789", 4) == std::string::npos)
    {
        const int p = clamp (std::atoi (lc.c_str() + 4), 1, 65535);
        udpPort = p;
        if (onPortCommand) onPortCommand (p);
        return;
    }
    if (lc.rfind ("midi", 0) == 0)
    {
        const std::string r = lc.substr (4);
        if (r.empty()) { midiMode = MidiMulti; return; }
        if (r.size() == 1 && int36 (r[0]) >= 0 && int36 (r[0]) < 16) { midiMode = MidiOmni; midiTarget = int36 (r[0]); return; }
        if (r.rfind ("rr", 0) == 0 || r.rfind ("rf", 0) == 0)
        {
            int mask = 0;
            const std::string list = r.substr (2);
            for (size_t i = 0; i < list.size(); ++i)
            {
                const int a = int36 (list[i]);
                if (a < 0 || a > 15) continue;
                if (i + 2 < list.size() && list[i + 1] == '-' && int36 (list[i + 2]) >= 0)
                {
                    const int b = std::min (15, int36 (list[i + 2]));
                    for (int k = std::min (a, b); k <= std::max (a, b); ++k) mask |= 1 << k;
                    i += 2;
                }
                else mask |= 1 << a;
            }
            rrMask = mask == 0 ? 0xFFFF : mask;
            midiMode = r[1] == 'f' ? MidiRoundRobinFree : MidiRoundRobin;
        }
        return;
    }

    // ---- channel commands ----
    const int ch = int36 (cmd[0]);
    if (ch < 0 || ch >= kChannels || cmd.size() < 3) return;
    const std::string rest = cmd.substr (1);
    const std::string op = lower (rest.substr (0, 3));
    const std::string val = rest.size() > 3 ? rest.substr (3) : std::string();

    if (op == "env")
    {
        editChannel (ch, [&val] (ChannelSettings& s)
        {
            if (s.engine == EngJF)   // JF: TIME INTONE RAMP CURVE
            {
                float* k[4] = { &s.jf.time, &s.jf.intone, &s.jf.ramp, &s.jf.curve };
                for (size_t i = 0; i < 4 && i < val.size(); ++i) { const float x = from16 (val[i]); if (x >= 0) *k[i] = x; }
                return;
            }
            for (size_t i = 0; i < 4 && i < val.size(); ++i) { const float x = from16 (val[i]); if (x >= 0) s.env[i] = x; }
        });
        log (cmd); return;
    }
    if (op == "fil")
    {
        editChannel (ch, [&val] (ChannelSettings& s) { for (size_t i = 0; i < 3 && i < val.size(); ++i) { const float x = from16 (val[i]); if (x >= 0) s.fil[i] = x; } });
        log (cmd); return;
    }
    if (op == "mod")
    {
        editChannel (ch, [&val] (ChannelSettings& s)
        {
            if (s.engine == EngJF) { const float x = val.empty() ? -1.0f : from16 (val[0]); if (x >= 0) s.jf.fm = x; return; }   // JF: FM
            for (size_t i = 0; i < 2 && i < val.size(); ++i) { const float x = from16 (val[i]); if (x >= 0) s.mod[i] = x; }
        });
        log (cmd); return;
    }
    if (op == "eng")
    {
        if (val.empty() || ! isValidEngine ((char) std::tolower ((unsigned char) val[0]))) return;
        editChannel (ch, [&val] (ChannelSettings& s) { s.engine = (char) std::tolower ((unsigned char) val[0]); });
        log (cmd); return;
    }
    if (op == "osc")
    {
        if (val.size() < 2) return;
        editChannel (ch, [&val] (ChannelSettings& s)
        {
            if (s.engine == EngJF)   // JF: range (sh/sn) + mode (tr/su/cy), e.g. 0OSCsncy
            {
                const std::string v = lower (val);
                int r = -1;
                for (int k = 0; k < 2; ++k) if (v.compare (0, 2, jfRangeCode (k)) == 0) r = k;
                if (r >= 0)
                {
                    s.jf.range = (int8_t) r;
                    for (int m = 0; m < 3; ++m) if (v.size() >= 4 && v.compare (2, 2, jfModeCode (m)) == 0) s.jf.mode = (int8_t) m;
                    return;
                }
            }
            const int a = val.substr (0, 2) == "--" ? -1 : waveCodeIndex (val.c_str());
            if (a >= -1) s.wave1 = a;
            if (val.size() >= 4)
            {
                const int b = val.substr (2, 2) == "--" ? -1 : waveCodeIndex (val.c_str() + 2);
                if (b >= -1) s.wave2 = b;
            }
        });
        log (cmd); return;
    }
    if (op == "ren")
    {
        editChannel (ch, [this] (ChannelSettings& s)
        {
            if (s.engine == EngJF)
            {
                s.jf.time = std::round ((0.25f + rng.unipolar() * 0.5f) * 15.0f) / 15.0f;
                s.jf.intone = std::round (rng.unipolar() * 15.0f) / 15.0f;
                s.jf.ramp = std::round (rng.unipolar() * 15.0f) / 15.0f;
                s.jf.curve = std::round (rng.unipolar() * 15.0f) / 15.0f;
                return;
            }
            s.env[0] = std::round (rng.unipolar() * 0.25f * 15.0f) / 15.0f;
            s.env[1] = std::round (rng.unipolar() * 0.75f * 15.0f) / 15.0f;
            s.env[2] = std::round (rng.unipolar() * 0.5f * 15.0f) / 15.0f;
            s.env[3] = std::round (rng.unipolar() * 15.0f) / 15.0f;
        });
        return;
    }
    if (op == "ros")
    {
        editChannel (ch, [this] (ChannelSettings& s)
        {
            if (isDrum (s.engine)) return;
            if (s.engine == EngJF) { s.jf.mode = (int8_t) (rng.next() % 3); return; }
            s.wave1 = (int) (rng.next() % 16);
            s.wave2 = (int) (rng.next() % 16);
        });
        return;
    }

    // ---- note: <ch><octave><note>[velocity][length] ----
    if (rest.size() < 2 || ! std::isdigit ((unsigned char) rest[0])) return;
    char nn; int off;
    if (! transposeNote (rest[1], nn, off)) return;
    const int octave = clamp ((rest[0] - '0') + off, 0, 8);
    const int idx = noteIndex (nn);
    if (idx < 0) return;
    Cmd c;
    c.type = Cmd::Note;
    c.ch = (int8_t) ch;
    c.note = (int16_t) clamp ((octave + 1) * 12 + idx, 0, 127);
    c.vel = rest.size() >= 3 ? from16 (rest[2]) : 0.66f;
    if (c.vel < 0) c.vel = 0.66f;
    const float lv = rest.size() >= 4 ? from16 (rest[3]) : 0.0f;
    c.lengthBeats = 4.0f * ((lv < 0 ? 0.0f : lv * 15.0f) + 1.0f) / 16.0f;   // 1/16 bar .. 1 bar
    queue.push (c);
}

//==============================================================================
// MIDI routing (audio thread)
void PilotEngine::handleMidi (const juce::MidiMessage& m)
{
    const int mode = midiMode.load (std::memory_order_relaxed);
    if (mode != lastMidiMode)
    {
        for (int c = 0; c < 16; ++c) for (int k = 0; k < 128; ++k) if (noteMap[c][k] >= 0) { synth.noteOff (noteMap[c][k], k); noteMap[c][k] = -1; }
        lastMidiMode = mode;
    }
    const int target = midiTarget.load (std::memory_order_relaxed);
    const int mask = rrMask.load (std::memory_order_relaxed) & 0xFFFF;
    const int src = clamp (m.getChannel() - 1, 0, 15);

    // next channel for round robin: strict order, or (mode 3) the next channel with no held notes
    auto pickRoundRobin = [&] () -> int
    {
        int first = -1;
        for (int step = 1; step <= 16; ++step)
        {
            const int c = (rrLast + step) & 15;
            if (! (mask & (1 << c))) continue;
            if (first < 0) first = c;
            if (mode == MidiRoundRobin) break;
            if (synth.gatedVoices (c) == 0) { first = c; break; }
        }
        if (first < 0) first = target;
        rrLast = first;
        return first;
    };
    auto forTargets = [&] (auto&& fn)
    {
        if (mode == MidiMulti) fn (src);
        else if (mode == MidiOmni) fn (target);
        else for (int c = 0; c < 16; ++c) if (mask & (1 << c)) fn (c);
    };

    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber();
        if (noteMap[src][note] >= 0) synth.noteOff (noteMap[src][note], note);   // repeated note-on
        const int ch = mode == MidiMulti ? src : mode == MidiOmni ? target : pickRoundRobin();
        noteMap[src][note] = (int8_t) ch;
        synth.noteOn (ch, note, m.getFloatVelocity(), -1);
    }
    else if (m.isNoteOff())
    {
        const int note = m.getNoteNumber();
        const int ch = noteMap[src][note];
        if (ch >= 0) { synth.noteOff (ch, note); noteMap[src][note] = -1; }
    }
    else if (m.isPitchWheel())
    {
        const float b = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        forTargets ([&] (int c) { synth.setBend (c, b); });
    }
    else if (m.isSustainPedalOn())  forTargets ([&] (int c) { synth.setSustain (c, true); });
    else if (m.isSustainPedalOff()) forTargets ([&] (int c) { synth.setSustain (c, false); });
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        synth.allNotesOff();
        for (auto& row : noteMap) for (auto& x : row) x = -1;
    }
}

//==============================================================================
void PilotEngine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    synth.prepare (sr, maxBlock);
    fx.prepare (sr, maxBlock);
    for (int i = 0; i < kNumFx; ++i) fx.initParams (i, fxW[i].load(), fxV[i].load());
    for (int ch = 0; ch < kChannels; ++ch)
    {
        std::lock_guard<std::mutex> lk (mirrorLock);
        synth.channels[ch].settings = mirror[ch];
    }
}

void PilotEngine::process (float* L, float* R, int n, double hostBpm)
{
    const double tempo = (hostBpm > 0.0 && useHostTempo.load()) ? hostBpm : bpm.load();
    curBpm = tempo;
    hostTempoSeen.store (hostBpm > 0.0, std::memory_order_relaxed);

    Cmd c;
    int guard = 0;
    while (guard++ < 1024 && queue.pop (c))
    {
        switch (c.type)
        {
            case Cmd::Note:
            {
                int len = -1;
                if (c.lengthBeats > 0.0f) len = std::max (16, (int) (beatsToSec (c.lengthBeats, tempo) * sr));
                else if (c.lengthMs > 0.0f) len = std::max (16, (int) (c.lengthMs * 0.001 * sr));
                synth.noteOn (c.ch, c.note, c.vel, len);
                break;
            }
            case Cmd::NoteOff:  synth.noteOff (c.ch, c.note); break;
            case Cmd::Settings: synth.channels[c.ch].settings = c.settings; break;
            case Cmd::AllOff:   synth.killAll(); break;
            case Cmd::Kill:     synth.killAll(); break;
            case Cmd::Jf:       synth.jfCommand (c.ch, c.jfOp, c.jfIdx, c.a, c.b); break;
        }
    }

    for (int i = 0; i < kNumFx; ++i) fx.setParams (i, fxW[i].load (std::memory_order_relaxed), fxV[i].load (std::memory_order_relaxed));

    synth.render (L, R, n, tempo);
    FxContext ctx { sr, tempo };
    fx.process (L, R, n, ctx);

    const float g = fx.takeLimiterGain();
    limGain.store (std::min (g, limGain.load()));
    lastLimGain.store (g);

    int w = scopeWrite.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        if (++scopeDecim >= 2) { scopeDecim = 0; scope[w] = 0.5f * (L[i] + R[i]); w = (w + 1) & (kScope - 1); }
    }
    scopeWrite.store (w, std::memory_order_release);
}
} // namespace sf
