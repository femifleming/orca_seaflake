#include "Lang.h"
#include "PilotEngine.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace sf
{
namespace
{
std::string upper (std::string s) { for (auto& c : s) c = (char) std::toupper ((unsigned char) c); return s; }
std::string trim (const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace ((unsigned char) s[a])) ++a;
    while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
    return s.substr (a, b - a);
}
std::string opKey (const std::string& tok) { return upper (tok); }
std::string dotless (const std::string& tok)
{
    std::string k;
    for (char c : upper (tok)) if (c != '.') k += c;
    return k;
}
std::vector<std::string> tokenize (const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (std::isspace ((unsigned char) c) || c == '_') { if (! cur.empty()) { out.push_back (cur); cur.clear(); } }
        else cur += c;
    }
    if (! cur.empty()) out.push_back (cur);
    return out;
}
bool parseNumber (const std::string& t, double& v)
{
    if (t.empty()) return false;
    const char c0 = t[0];
    if (! (std::isdigit ((unsigned char) c0) || ((c0 == '-' || c0 == '+' || c0 == '.') && t.size() > 1))) return false;
    char* end = nullptr;
    v = std::strtod (t.c_str(), &end);
    return end != nullptr && *end == '\0';
}
// note literal: C4, F#2, BB3 (b = flat), C-1
bool parseNote (const std::string& tok, double& v)
{
    const std::string t = upper (tok);
    if (t.size() < 2 || t.size() > 4) return false;
    static const int pcs[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    if (t[0] < 'A' || t[0] > 'G') return false;
    int pc = pcs[t[0] - 'A'];
    size_t i = 1;
    if (i < t.size() && (t[i] == '#' || t[i] == 'B') && i + 1 < t.size()) { pc += t[i] == '#' ? 1 : -1; ++i; }
    const std::string oct = t.substr (i);
    if (oct.empty()) return false;
    for (size_t k = 0; k < oct.size(); ++k) if (! (std::isdigit ((unsigned char) oct[k]) || (k == 0 && oct[k] == '-'))) return false;
    v = (std::atoi (oct.c_str()) + 1) * 12 + pc;
    return true;
}
std::string fmt (double v)
{
    char b[48];
    if (std::fabs (v - std::round (v)) < 1e-9 && std::fabs (v) < 1e15) std::snprintf (b, sizeof (b), "%lld", (long long) std::llround (v));
    else std::snprintf (b, sizeof (b), "%.4g", v);
    return b;
}
std::vector<std::string> splitTop (const std::string& s, char sep)
{
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == sep) { out.push_back (cur); cur.clear(); } else cur += c; }
    out.push_back (cur);
    return out;
}

// conversions between stored (normalized) channel values and real units
double envMs (float x) { return envTime (x) * 1000.0; }
float envFromMs (double ms) { return clamp ((float) std::pow (std::max (0.0, ms / 1000.0 - 0.0015) / 4.0, 1.0 / 2.2), 0.0f, 1.0f); }
double cutHz (float x) { return 20.0 * std::pow (2.0, 10.0 * x); }
float cutFromHz (double hz) { return clamp ((float) (std::log2 (std::max (20.0, hz) / 20.0) / 10.0), 0.0f, 1.0f); }
float ratioFromValue (double r)
{
    // inverse of the piecewise-linear modRatio()
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 40; ++i) { const float m = 0.5f * (lo + hi); if (modRatio (m) < r) lo = m; else hi = m; }
    return 0.5f * (lo + hi);
}
const char* kEngines = "afmpkshtj";
int engineIndex (char e) { for (int i = 0; i < 9; ++i) if (kEngines[i] == e) return i; return 0; }

const int kScales[9][7] = {
    { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 },
    { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 4, 5, 7, 9, 10 }, { 0, 1, 3, 5, 6, 8, 10 }, { 0, 2, 4, 7, 9, -1, -1 }, { 0, 3, 5, 7, 10, -1, -1 } };
juce::File libraryFile()
{
    if (! sf::Lang::libraryPathOverride.empty()) return juce::File (sf::Lang::libraryPathOverride);
    return sf::pilotDataDir().getChildFile ("teletype-library.xml");
}

void writePattern (juce::XmlElement& e, const double* v, int len, int idx, int start, int end, bool wrap)
{
    e.setAttribute ("len", len); e.setAttribute ("idx", idx); e.setAttribute ("start", start); e.setAttribute ("end", end); e.setAttribute ("wrap", wrap);
    juce::String vals;
    for (int i = 0; i < 64; ++i) vals << fmt (v[i]) << " ";
    e.setAttribute ("values", vals.trim());
}

} // namespace

//==============================================================================
Lang::Lang (PilotEngine& e) : eng (e)
{
    for (auto& v : chVel) v = 0.66;
    for (auto& l : chLenMs) l = 0.0;
    for (auto& v : piVel) v = 100.0;
    for (auto& n : voxNote) n = -1;
    t0 = now();
    defineAll();
    loadLibrary();
}

double Lang::now() const { return juce::Time::getMillisecondCounterHiRes(); }

void Lang::define (const std::string& name, int args, bool ret, int setArgs,
                   std::function<double (const double*)> get, std::function<void (const double*)> set, const std::string& doc)
{
    Op op; op.args = args; op.ret = ret; op.setArgs = setArgs; op.get = std::move (get); op.set = std::move (set); op.doc = doc;
    const auto key = opKey (name);
    ops[key] = std::move (op);
    docs.emplace_back (name, doc);
    const auto dl = dotless (name);
    if (dl != key)
    {
        auto it = aliases.find (dl);
        if (it == aliases.end()) aliases[dl] = key;
        else if (it->second != key) it->second.clear();   // ambiguous without dots
    }
}

const Lang::Op* Lang::findOp (const std::string& token) const
{
    auto it = ops.find (opKey (token));
    if (it != ops.end()) return &it->second;
    auto al = aliases.find (dotless (token));   // Orca can't send '.', so dots are optional
    if (al == aliases.end() || al->second.empty()) return nullptr;
    it = ops.find (al->second);
    return it == ops.end() ? nullptr : &it->second;
}

bool Lang::isWord (const std::string& t) const
{
    double v;
    return parseNumber (t, v) || parseNote (t, v) || findOp (t) != nullptr;
}

//==============================================================================
std::string Lang::run (const std::string& text)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    std::string out;
    for (auto& line : splitTop (text, '\n')) runLine (line, liveCtx, out);   // IF/ELSE state persists like a script
    return out;
}

void Lang::runLine (const std::string& line, Ctx& ctx, std::string& out)
{
    const auto segs = splitTop (line, ';');
    for (size_t i = 0; i < segs.size(); ++i)
    {
        const std::string seg = trim (segs[i]);
        if (seg.empty()) continue;
        const size_t colon = seg.find (':');
        if (colon != std::string::npos)
        {
            std::string body = seg.substr (colon + 1);
            for (size_t k = i + 1; k < segs.size(); ++k) body += ";" + segs[k];
            runPre (trim (seg.substr (0, colon)), trim (body), ctx, out);
            return;
        }
        runCommand (seg, ctx, out);
    }
}

void Lang::runCommand (const std::string& cmd, Ctx& ctx, std::string& out)
{
    curScript = ctx.script;
    const auto toks = tokenize (cmd);
    if (toks.empty()) return;
    // classic Pilot commands first (04C, 0ENV056f, 1OSCsisq, REVff, bpm140, renv, rosc, refx, reset)
    if (toks.size() == 1 && (PilotEngine::isLegacyCommand (toks[0]) || ! isWord (toks[0]))) { eng.runLegacy (toks[0]); return; }

    std::vector<double> st;
    std::string err;
    const Op* first = findOp (toks[0]);
    if (first != nullptr && first->setArgs >= 0)
    {
        if (! evalRange (toks, 1, st, err)) { out = "ERR " + err; return; }
        if ((int) st.size() == first->setArgs)
        {
            std::vector<double> a ((size_t) first->setArgs);
            for (int k = 0; k < first->setArgs; ++k) { a[(size_t) k] = st.back(); st.pop_back(); }
            first->set (a.data());
            return;
        }
        if (! applyOp (*first, toks[0], st, err)) { out = "ERR " + err; return; }
    }
    else if (! evalRange (toks, 0, st, err)) { out = "ERR " + err; return; }

    if (st.size() == 1) out = fmt (st.back());
    else if (st.size() > 1) out = "ERR extra values";
}

bool Lang::evalRange (const std::vector<std::string>& toks, size_t from, std::vector<double>& st, std::string& err)
{
    for (size_t i = toks.size(); i-- > from;)
    {
        double v;
        if (parseNumber (toks[i], v) || parseNote (toks[i], v)) { st.push_back (v); continue; }
        const Op* op = findOp (toks[i]);
        if (op == nullptr) { err = "unknown " + upper (toks[i]); return false; }
        if (! applyOp (*op, toks[i], st, err)) return false;
    }
    return true;
}

bool Lang::applyOp (const Op& op, const std::string& name, std::vector<double>& st, std::string& err)
{
    if ((int) st.size() < op.args) { err = upper (name) + " needs " + std::to_string (op.args); return false; }
    double a[8] {};
    for (int k = 0; k < op.args; ++k) { a[k] = st.back(); st.pop_back(); }
    const double r = op.get (a);
    if (op.ret) st.push_back (std::isfinite (r) ? r : 0.0);
    return true;
}

void Lang::runPre (const std::string& pre, const std::string& body, Ctx& ctx, std::string& out)
{
    auto toks = tokenize (pre);
    if (toks.empty()) { out = "ERR empty PRE"; return; }
    const std::string name = opKey (toks[0]);
    std::vector<std::string> argToks (toks.begin() + 1, toks.end());
    auto args = [&] (std::vector<double>& st) -> bool
    {
        std::string err;
        if (! evalRange (argToks, 0, st, err)) { out = "ERR " + err; return false; }
        return true;
    };
    auto runBody = [&] { runLine (body, ctx, out); };
    std::vector<double> st;

    if (name == "IF" || name == "ELIF")
    {
        if (! args (st) || st.empty()) { if (out.empty()) out = "ERR " + name + " needs 1"; return; }
        const bool c = st.back() != 0.0;
        if (name == "IF") { ctx.inIf = true; ctx.ifFired = c; if (c) runBody(); }
        else if (ctx.inIf && ! ctx.ifFired && c) { ctx.ifFired = true; runBody(); }
        return;
    }
    if (name == "ELSE") { if (ctx.inIf && ! ctx.ifFired) runBody(); ctx.inIf = false; return; }
    if (name == "L")
    {
        if (! args (st) || st.size() < 2) { if (out.empty()) out = "ERR L needs 2"; return; }
        const long a = std::lround (st[st.size() - 1]), b = std::lround (st[st.size() - 2]);
        const long step = b >= a ? 1 : -1;
        int guard = 0;
        for (long i = a; guard++ < 1024; i += step) { vars[8] = (double) i; runBody(); if (i == b) break; }
        return;
    }
    if (name == "W")
    {
        for (int guard = 0; guard < 1000; ++guard)
        {
            st.clear();
            if (! args (st) || st.empty() || st.back() == 0.0) break;
            runBody();
        }
        return;
    }
    if (name == "EVERY" || name == "SKIP")
    {
        if (! args (st) || st.empty()) return;
        const long n = std::max (1L, std::lround (st.back()));
        const long c = ++everyCount[std::to_string (ctx.script) + "|" + pre + "|" + body];
        const bool fire = name == "EVERY" ? (c % n == 0) : (c % n != 0);
        ctx.lastFired = fire;
        if (fire) runBody();
        return;
    }
    if (name == "OTHER") { if (! ctx.lastFired) runBody(); return; }
    if (name == "PROB")
    {
        if (! args (st) || st.empty()) return;
        if (std::uniform_real_distribution<double> (0.0, 100.0) (rng) < st.back()) runBody();
        return;
    }
    if (name == "DEL")
    {
        if (! args (st) || st.empty()) return;
        if (delayed.size() < 256) delayed.push_back ({ now() + std::max (0.0, st.back()), ctx.script, body });
        return;
    }
    // Teletype: DEL.X n ms: queue n copies, each ms after the previous (at ms, 2ms .. n*ms)
    //           DEL.R n ms: run now, then n-1 more copies ms apart (at 0, ms .. (n-1)*ms)
    if (name == "DEL.X" || name == "DEL.R")
    {
        if (! args (st) || st.size() < 2) { if (out.empty()) out = "ERR " + name + " needs 2"; return; }
        const long n = std::lround (st[st.size() - 1]);
        const double ms = std::max (0.0, st[st.size() - 2]);
        if (n < 1) return;
        const bool now0 = name == "DEL.R";
        const double start = now();
        for (long k = now0 ? 1 : 0; k < n && delayed.size() < 256; ++k)
            delayed.push_back ({ start + ms * (double) (now0 ? k : k + 1), ctx.script, body });
        if (now0) runBody();
        return;
    }
    if (name == "DEF")
    {
        if (! args (st) || st.empty()) return;
        const int n = (int) std::lround (st.back());
        if (n < 1 || n > kScripts) { out = "ERR script 1-10"; return; }
        if (scripts[n].size() < 64) scripts[n].push_back (body);
        return;
    }
    out = "ERR unknown PRE " + name;
}

void Lang::runScript (int n, int depth, bool force)
{
    if (n < 1 || n > kScripts || depth > 8 || scriptNest >= 8) return;   // like Teletype: bare "$ $" stops instead of recursing forever
    if (muted[n] && ! force) return;
    lastRun[n] = now();
    Ctx c; c.script = n; c.depth = depth;
    std::string out;
    const auto lines = scripts[n];   // copy: a script may redefine itself
    const int caller = curScript;
    ++scriptNest;
    for (auto& l : lines)
        if (! trim (l).empty() && trim (l)[0] != '#') runLine (l, c, out);   // '#' = commented out
    --scriptNest;
    curScript = caller;
}

void Lang::runInit()
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    runScript (10, 1);
}

void Lang::tick (double)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    const double t = now();
    lastNow = t;
    if (metroOn)
    {
        if (nextMetro <= 0.0) nextMetro = t;
        if (t >= nextMetro)
        {
            nextMetro += std::max (10.0, metroMs);
            if (t - nextMetro > metroMs) nextMetro = t + metroMs;   // fell behind: resync
            ++metroCount;
            runScript (9, 1);
        }
    }
    if (! delayed.empty())
    {
        std::vector<Delayed> due;
        for (auto it = delayed.begin(); it != delayed.end();)
            if (it->due <= t) { due.push_back (*it); it = delayed.erase (it); } else ++it;
        for (auto& d : due) { Ctx c; c.script = d.script; std::string out; runLine (d.body, c, out); }
    }
}

//==============================================================================
void Lang::defineAll()
{
    auto& E = eng;
    auto rnd = [this] (double lo, double hi) { if (hi < lo) std::swap (lo, hi); return (double) std::uniform_int_distribution<long> ((long) std::floor (lo), (long) std::floor (hi)) (rng); };

    // ---------------- variables ----------------
    const char* names[11] = { "A", "B", "C", "D", "X", "Y", "Z", "T", "I", "J", "K" };
    for (int i = 0; i < 11; ++i)
        define (names[i], 0, true, 1, [this, i] (const double*) { return vars[i]; }, [this, i] (const double* a) { vars[i] = a[0]; },
                "variable (get / set). I is the L loop counter");

    // ---------------- maths ----------------
    auto bin = [this] (const char* n, const char* alias, std::function<double (double, double)> f, const char* doc)
    {
        define (n, 2, true, -1, [f] (const double* a) { return f (a[0], a[1]); }, nullptr, doc);
        if (alias) define (alias, 2, true, -1, [f] (const double* a) { return f (a[0], a[1]); }, nullptr, doc);
    };
    bin ("ADD", "+", [] (double a, double b) { return a + b; }, "a + b");
    bin ("SUB", "-", [] (double a, double b) { return a - b; }, "a - b");
    bin ("MUL", "*", [] (double a, double b) { return a * b; }, "a * b");
    bin ("DIV", "/", [] (double a, double b) { return b == 0 ? 0.0 : a / b; }, "a / b (0 if b = 0)");
    bin ("MOD", "%", [] (double a, double b) { if (b == 0) return 0.0; const double m = std::fmod (a, b); return m < 0 ? m + std::fabs (b) : m; }, "a mod b");
    bin ("MIN", nullptr, [] (double a, double b) { return std::min (a, b); }, "smaller");
    bin ("MAX", nullptr, [] (double a, double b) { return std::max (a, b); }, "larger");
    bin ("AVG", nullptr, [] (double a, double b) { return (a + b) / 2; }, "average");
    bin ("EQ", "==", [] (double a, double b) { return a == b ? 1.0 : 0.0; }, "1 if a = b");
    bin ("NE", "!=", [] (double a, double b) { return a != b ? 1.0 : 0.0; }, "1 if a != b");
    bin ("LT", "<", [] (double a, double b) { return a < b ? 1.0 : 0.0; }, "1 if a < b");
    bin ("GT", ">", [] (double a, double b) { return a > b ? 1.0 : 0.0; }, "1 if a > b");
    bin ("LTE", "<=", [] (double a, double b) { return a <= b ? 1.0 : 0.0; }, "1 if a <= b");
    bin ("GTE", ">=", [] (double a, double b) { return a >= b ? 1.0 : 0.0; }, "1 if a >= b");
    bin ("AND", "&&", [] (double a, double b) { return (a != 0 && b != 0) ? 1.0 : 0.0; }, "logical and");
    bin ("OR", "||", [] (double a, double b) { return (a != 0 || b != 0) ? 1.0 : 0.0; }, "logical or");
    bin ("XOR", nullptr, [] (double a, double b) { return ((a != 0) != (b != 0)) ? 1.0 : 0.0; }, "logical xor");
    bin ("QT", nullptr, [] (double a, double b) { return b == 0 ? a : std::round (a / b) * b; }, "quantize a to multiple of b");
    define ("NOT", 1, true, -1, [] (const double* a) { return a[0] == 0 ? 1.0 : 0.0; }, nullptr, "logical not");
    define ("!", 1, true, -1, [] (const double* a) { return a[0] == 0 ? 1.0 : 0.0; }, nullptr, "logical not");
    define ("EZ", 1, true, -1, [] (const double* a) { return a[0] == 0 ? 1.0 : 0.0; }, nullptr, "1 if zero");
    define ("NZ", 1, true, -1, [] (const double* a) { return a[0] != 0 ? 1.0 : 0.0; }, nullptr, "1 if not zero");
    define ("ABS", 1, true, -1, [] (const double* a) { return std::fabs (a[0]); }, nullptr, "absolute value");
    define ("NEG", 1, true, -1, [] (const double* a) { return -a[0]; }, nullptr, "negate");
    define ("FLOOR", 1, true, -1, [] (const double* a) { return std::floor (a[0]); }, nullptr, "round down");
    define ("ROUND", 1, true, -1, [] (const double* a) { return std::round (a[0]); }, nullptr, "round");
    define ("LIM", 3, true, -1, [] (const double* a) { return clamp (a[0], std::min (a[1], a[2]), std::max (a[1], a[2])); }, nullptr, "x lo hi: clamp");
    define ("WRAP", 3, true, -1, [] (const double* a)
    {
        const double lo = std::min (a[1], a[2]), hi = std::max (a[1], a[2]), r = hi - lo + 1;
        if (r <= 0) return lo;
        double m = std::fmod (a[0] - lo, r); if (m < 0) m += r; return lo + m;
    }, nullptr, "x lo hi: wrap into range (inclusive)");
    define ("SCALE", 5, true, -1, [] (const double* a)
    {
        return a[1] == a[0] ? a[2] : a[2] + (a[4] - a[0]) * (a[3] - a[2]) / (a[1] - a[0]);
    }, nullptr, "a b x y v: map v from a..b to x..y");
    define ("RAND", 1, true, -1, [rnd] (const double* a) { return rnd (0, a[0]); }, nullptr, "random 0..x");
    define ("RRAND", 2, true, -1, [rnd] (const double* a) { return rnd (a[0], a[1]); }, nullptr, "random a..b");
    define ("TOSS", 0, true, -1, [rnd] (const double*) { return rnd (0, 1); }, nullptr, "random 0 or 1");
    define ("N.S", 3, true, -1, [] (const double* a)
    {
        const int sc = clamp ((int) std::lround (a[1]), 0, 8);
        int n = 0; while (n < 7 && kScales[sc][n] >= 0) ++n;
        long d = std::lround (a[2]);
        long oct = d >= 0 ? d / n : -((-d + n - 1) / n);
        long idx = d - oct * n;
        return a[0] + 12.0 * (double) oct + kScales[sc][idx];
    }, nullptr, "root scale degree: note in scale (0 maj 1 min 2 dor 3 phr 4 lyd 5 mix 6 loc 7 maj-pent 8 min-pent)");

    // ---------------- time / scripts ----------------
    define ("BPM", 0, true, 1, [&E] (const double*) { return E.tempo(); },
            [&E] (const double* a) { E.bpm = clamp (a[0], 20.0, 999.0); E.useHostTempo = false; }, "tempo (get / set). Plugin follows host until set");
    define ("BPM.HOST", 0, true, 1, [&E] (const double*) { return E.useHostTempo.load() ? 1.0 : 0.0; },
            [&E] (const double* a) { E.useHostTempo = a[0] != 0; }, "1 = follow host tempo");
    define ("BT", 1, true, -1, [&E] (const double* a) { return a[0] * 60000.0 / std::max (20.0, E.tempo()); }, nullptr, "beats -> ms at current tempo");
    define ("TIME", 0, true, 1, [this] (const double*) { return now() - t0; }, [this] (const double* a) { t0 = now() - a[0]; }, "ms counter (get / set)");
    define ("M", 0, true, 1, [this] (const double*) { return metroMs; }, [this] (const double* a) { metroMs = std::max (10.0, a[0]); }, "metro period ms (runs script 9)");
    define ("M.ACT", 0, true, 1, [this] (const double*) { return metroOn ? 1.0 : 0.0; },
            [this] (const double* a) { metroOn = a[0] != 0; nextMetro = 0.0; }, "metro on / off");
    define ("M.RESET", 0, false, -1, [this] (const double*) { nextMetro = 0.0; return 0.0; }, nullptr, "restart metro phase");
    define ("DEL.CLR", 0, false, -1, [this] (const double*) { delayed.clear(); return 0.0; }, nullptr, "cancel pending DEL commands");
    // Teletype: SCRIPT / $ alone = this script's number, SCRIPT n = run script n  ("DEL 500: $ $" loops the script)
    define ("$", 0, true, 1, [this] (const double*) { return (double) curScript; }, [this] (const double* a) { runScript ((int) std::lround (a[0]), 1); }, "run script n (1-10); alone: this script's number");
    define ("SCRIPT", 0, true, 1, [this] (const double*) { return (double) curScript; }, [this] (const double* a) { runScript ((int) std::lround (a[0]), 1); }, "run script n (1-10); alone: this script's number");
    define ("DEF.CLR", 1, false, -1, [this] (const double* a)
    {
        const int n = (int) std::lround (a[0]);
        if (n >= 1 && n <= kScripts) scripts[n].clear();
        return 0.0;
    }, nullptr, "erase script n");
    define ("DEF.LEN", 1, true, -1, [this] (const double* a)
    {
        const int n = (int) std::lround (a[0]);
        return (n >= 1 && n <= kScripts) ? (double) scripts[n].size() : 0.0;
    }, nullptr, "number of lines in script n");

    // ---------------- patterns (Teletype semantics: length, index, start, end, wrap) ----------------
    define ("P.N", 0, true, 1, [this] (const double*) { return (double) patN; }, [this] (const double* a) { patN = clamp ((int) std::lround (a[0]), 0, 3); }, "current pattern 0-3");
    auto ix = [] (double v) { return clamp ((int) std::lround (v), 0, 63); };
    auto patOp = [this] (const std::string& suffix, int n, bool ret, bool settable,
                         std::function<double (Pattern&, const double*)> g, std::function<void (Pattern&, const double*)> st, const std::string& doc)
    {
        std::function<void (const double*)> s1, s2;
        if (settable)
        {
            s1 = [this, st] (const double* a) { st (pat[clamp (patN, 0, 3)], a); };
            s2 = [this, st] (const double* a) { st (pat[clamp ((int) std::lround (a[0]), 0, 3)], a + 1); };
        }
        define ("P" + suffix, n, ret, settable ? n + 1 : -1, [this, g] (const double* a) { return g (pat[clamp (patN, 0, 3)], a); }, s1, doc);
        define ("PN" + suffix, n + 1, ret, settable ? n + 2 : -1, [this, g] (const double* a) { return g (pat[clamp ((int) std::lround (a[0]), 0, 3)], a + 1); }, s2,
                "p " + doc + " (pattern p)");
    };
    patOp ("", 1, true, true, [ix] (Pattern& p, const double* a) { return p.v[ix (a[0])]; }, [ix] (Pattern& p, const double* a) { p.v[ix (a[0])] = a[1]; }, "i (x): value at index i");
    patOp (".L", 0, true, true, [] (Pattern& p, const double*) { return (double) p.len; }, [] (Pattern& p, const double* a) { p.len = clamp ((int) std::lround (a[0]), 0, 64); }, "(x): length");
    patOp (".I", 0, true, true, [] (Pattern& p, const double*) { return (double) p.idx; }, [ix] (Pattern& p, const double* a) { p.idx = ix (a[0]); }, "(x): play index");
    patOp (".HERE", 0, true, true, [] (Pattern& p, const double*) { return p.v[p.idx]; }, [] (Pattern& p, const double* a) { p.v[p.idx] = a[0]; }, "(x): value at the play index");
    patOp (".START", 0, true, true, [] (Pattern& p, const double*) { return (double) p.start; }, [ix] (Pattern& p, const double* a) { p.start = ix (a[0]); }, "(x): loop start");
    patOp (".END", 0, true, true, [] (Pattern& p, const double*) { return (double) p.end; }, [ix] (Pattern& p, const double* a) { p.end = ix (a[0]); }, "(x): loop end");
    patOp (".WRAP", 0, true, true, [] (Pattern& p, const double*) { return p.wrap ? 1.0 : 0.0; }, [] (Pattern& p, const double* a) { p.wrap = a[0] != 0; }, "(0/1): wrap between start and end");
    patOp (".NEXT", 0, true, false, [] (Pattern& p, const double*)
    {
        int i = p.idx;
        if (i == p.len - 1 || i == p.end) { if (p.wrap) i = p.start; }
        else ++i;
        if (i > p.len) i = 0;
        p.idx = clamp (i, 0, 63);
        return p.v[p.idx];
    }, nullptr, "advance the index (start/end/wrap) and read");
    patOp (".PREV", 0, true, false, [] (Pattern& p, const double*)
    {
        int i = p.idx;
        if (i == 0 || i == p.start) { if (p.wrap) i = p.end < p.len ? p.end : p.len - 1; }
        else --i;
        p.idx = clamp (i, 0, 63);
        return p.v[p.idx];
    }, nullptr, "step the index back and read");
    patOp (".PUSH", 1, false, false, [] (Pattern& p, const double* a) { if (p.len < 64) p.v[p.len++] = a[0]; return 0.0; }, nullptr, "x: append value");
    patOp (".POP", 0, true, false, [] (Pattern& p, const double*) { return p.len > 0 ? p.v[--p.len] : 0.0; }, nullptr, "remove + return last value");
    patOp (".INS", 2, false, false, [ix] (Pattern& p, const double* a)
    {
        const int i = std::min (ix (a[0]), p.len);
        if (p.len >= 64) return 0.0;
        for (int k = p.len; k > i; --k) p.v[k] = p.v[k - 1];
        p.v[i] = a[1]; ++p.len; return 0.0;
    }, nullptr, "i x: insert value, shifting later values down");
    patOp (".RM", 1, true, false, [ix] (Pattern& p, const double* a)
    {
        const int i = ix (a[0]);
        if (i >= p.len) return 0.0;
        const double v = p.v[i];
        for (int k = i; k < 63; ++k) p.v[k] = p.v[k + 1];
        --p.len; return v;
    }, nullptr, "i: remove + return value, shifting later values up");
    patOp (".RND", 0, true, false, [this] (Pattern& p, const double*)
    {
        const int lo = std::min (p.start, std::max (0, p.len - 1)), hi = std::min (p.end, std::max (0, p.len - 1));
        return p.len > 0 ? p.v[std::uniform_int_distribution<int> (std::min (lo, hi), std::max (lo, hi)) (rng)] : 0.0;
    }, nullptr, "random value between start and end");

    // ---------------- more Teletype ----------------
    define ("?", 3, true, -1, [] (const double* a) { return a[0] != 0 ? a[1] : a[2]; }, nullptr, "x a b: a if x is true, else b");
    define ("RND", 1, true, -1, [rnd] (const double* a) { return rnd (0, a[0]); }, nullptr, "random 0..x (alias of RAND)");
    define ("RRND", 2, true, -1, [rnd] (const double* a) { return rnd (a[0], a[1]); }, nullptr, "random a..b (alias of RRAND)");
    define ("ER", 3, true, -1, [] (const double* a)
    {
        const long fill = std::lround (a[0]), len = std::lround (a[1]);
        if (len <= 0 || fill <= 0) return 0.0;
        long step = std::lround (a[2]) % len; if (step < 0) step += len;
        return ((step * fill) % len) < fill ? 1.0 : 0.0;
    }, nullptr, "fill len step: euclidean rhythm, 1 if step is a hit");
    define ("DRUNK", 0, true, 1, [this, rnd] (const double*) { drunk += rnd (-1, 1); return drunk; }, [this] (const double* a) { drunk = a[0]; },
            "random walk: changes by -1, 0 or +1 each read (get / set)");
    define ("O", 0, true, 1, [this] (const double*) { const double v = counterO; counterO = counterO >= 63 ? 0 : counterO + 1; return v; },
            [this] (const double* a) { counterO = a[0]; }, "counter: returns then increments (wraps 0-63)");
    define ("MUTE", 1, true, 2, [this] (const double* a) { const int n = (int) std::lround (a[0]); return (n >= 1 && n <= kScripts && muted[n]) ? 1.0 : 0.0; },
            [this] (const double* a) { const int n = (int) std::lround (a[0]); if (n >= 1 && n <= kScripts) muted[n] = a[1] != 0; }, "n (0/1): script muted");
    define ("LAST", 1, true, -1, [this] (const double* a) { const int n = (int) std::lround (a[0]); return (n >= 1 && n <= kScripts && lastRun[n] > 0) ? now() - lastRun[n] : 0.0; },
            nullptr, "n: ms since script n last ran");
    define ("SCENE", 0, true, 1, [this] (const double*) { return (double) scene; }, [this] (const double* a) { readScene (clamp ((int) std::lround (a[0]), 0, kScenes - 1), false); },
            "load scene x (scripts, patterns, metro; doesn't run I)");

    // ---------------- playing ----------------
    auto lenFor = [this, &E] (int ch) { const double l = chLenMs[clamp (ch, 0, 15)]; return (float) (l > 0 ? l : 0.25 * 60000.0 / std::max (20.0, E.tempo())); };
    define ("NOTE", 2, false, -1, [this, &E, lenFor] (const double* a)
    {
        const int ch = (int) std::lround (a[0]);
        E.playNote (ch, (int) std::lround (a[1]), (float) chVel[clamp (ch, 0, 15)], lenFor (ch)); return 0.0;
    }, nullptr, "ch note: play (channel's CH.VEL / CH.LEN)");
    define ("NOTE.V", 3, false, -1, [&E, lenFor] (const double* a)
    {
        const int ch = (int) std::lround (a[0]);
        E.playNote (ch, (int) std::lround (a[1]), (float) (a[2] / 127.0), lenFor (ch)); return 0.0;
    }, nullptr, "ch note vel(0-127)");
    define ("NOTE.VL", 4, false, -1, [&E] (const double* a)
    {
        E.playNote ((int) std::lround (a[0]), (int) std::lround (a[1]), (float) (a[2] / 127.0), (float) std::max (1.0, a[3])); return 0.0;
    }, nullptr, "ch note vel ms");
    define ("ON", 3, false, -1, [&E] (const double* a) { E.playNote ((int) std::lround (a[0]), (int) std::lround (a[1]), (float) (a[2] / 127.0), -1.0f); return 0.0; },
            nullptr, "ch note vel: note on (held)");
    define ("OFF", 2, false, -1, [&E] (const double* a) { E.releaseNote ((int) std::lround (a[0]), (int) std::lround (a[1])); return 0.0; }, nullptr, "ch note: note off");
    define ("STOP", 0, false, -1, [&E] (const double*) { E.panic(); return 0.0; }, nullptr, "silence everything");
    define ("RESET", 0, false, -1, [this, &E] (const double*) { E.resetChannels(); E.resetEffects(); runScript (10, 1); return 0.0; }, nullptr, "channel + effect defaults, then run script 10");
    define ("RENV", 0, false, -1, [&E] (const double*) { E.runLegacy ("renv"); return 0.0; }, nullptr, "randomize envelopes");
    define ("ROSC", 0, false, -1, [&E] (const double*) { E.runLegacy ("rosc"); return 0.0; }, nullptr, "randomize oscillators");
    define ("REFX", 0, false, -1, [&E] (const double*) { E.runLegacy ("refx"); return 0.0; }, nullptr, "randomize effects I-III");

    // ---------------- channels ----------------
    auto chOp = [this, &E] (const std::string& name, std::function<double (const ChannelSettings&)> g,
                            std::function<void (ChannelSettings&, double)> s, const std::string& doc)
    {
        define (name, 1, true, 2,
                [&E, g] (const double* a) { return g (E.getSettings (clamp ((int) std::lround (a[0]), 0, 15))); },
                [&E, s] (const double* a) { const double v = a[1]; E.editChannel ((int) std::lround (a[0]), [s, v] (ChannelSettings& cs) { s (cs, v); }); },
                doc);
    };
    chOp ("CH.ENG", [] (const ChannelSettings& s) { return (double) engineIndex (s.engine); },
          [] (ChannelSettings& s, double v) { s.engine = kEngines[clamp ((int) std::lround (v), 0, 8)]; }, "ch (e): 0 analog 1 fm 2 am 3 pluck 4 kick 5 snare 6 hat 7 tom 8 just friends");
    // Just Friends (engine 8 / 'j') panel
    chOp ("CH.TIME", [] (const ChannelSettings& s) { return s.jf.time * 100.0; }, [] (ChannelSettings& s, double v) { s.jf.time = clamp ((float) v / 100.0f, 0.0f, 1.0f); }, "ch (%): JF TIME (50 = noon: C4 in sound, ~6 Hz in shape)");
    chOp ("CH.INTONE", [] (const ChannelSettings& s) { return s.jf.intone * 100.0; }, [] (ChannelSettings& s, double v) { s.jf.intone = clamp ((float) v / 100.0f, 0.0f, 1.0f); }, "ch (%): JF INTONE (50 = unison, 100 = harmonic, 0 = subharmonic)");
    chOp ("CH.RAMP", [] (const ChannelSettings& s) { return s.jf.ramp * 100.0; }, [] (ChannelSettings& s, double v) { s.jf.ramp = clamp ((float) v / 100.0f, 0.0f, 1.0f); }, "ch (%): JF RAMP (0 saw, 50 triangle, 100 ramp)");
    chOp ("CH.CURVE", [] (const ChannelSettings& s) { return s.jf.curve * 100.0; }, [] (ChannelSettings& s, double v) { s.jf.curve = clamp ((float) v / 100.0f, 0.0f, 1.0f); }, "ch (%): JF CURVE (0 square, 25 expo, 50 linear, 100 cosine)");
    chOp ("CH.FM", [] (const ChannelSettings& s) { return s.jf.fm * 100.0; }, [] (ChannelSettings& s, double v) { s.jf.fm = clamp ((float) v / 100.0f, 0.0f, 1.0f); }, "ch (%): JF FM (below 50 = INTONE-side detune, above = FLOOM index)");
    chOp ("CH.RANGE", [] (const ChannelSettings& s) { return (double) s.jf.range; }, [] (ChannelSettings& s, double v) { s.jf.range = (int8_t) clamp ((int) std::lround (v), 0, 1); }, "ch (0/1): JF range: 0 shape, 1 sound");
    chOp ("CH.MODE", [] (const ChannelSettings& s) { return (double) s.jf.mode; }, [] (ChannelSettings& s, double v) { s.jf.mode = (int8_t) clamp ((int) std::lround (v), 0, 2); }, "ch (0-2): JF mode: 0 transient, 1 sustain, 2 cycle");
    chOp ("CH.OSC1", [] (const ChannelSettings& s) { return (double) s.wave1; },
          [] (ChannelSettings& s, double v) { s.wave1 = clamp ((int) std::lround (v), -1, 15); }, "ch (w): -1 off, 0-15 = si tr sq sw, +4 per unison step (2/4/8)");
    chOp ("CH.OSC2", [] (const ChannelSettings& s) { return (double) s.wave2; },
          [] (ChannelSettings& s, double v) { s.wave2 = clamp ((int) std::lround (v), -1, 15); }, "ch (w): second oscillator / modulator wave");
    chOp ("CH.ATK", [] (const ChannelSettings& s) { return envMs (s.env[0]); }, [] (ChannelSettings& s, double v) { s.env[0] = envFromMs (v); }, "ch (ms): attack");
    chOp ("CH.DEC", [] (const ChannelSettings& s) { return envMs (s.env[1]); }, [] (ChannelSettings& s, double v) { s.env[1] = envFromMs (v); }, "ch (ms): decay");
    chOp ("CH.SUS", [] (const ChannelSettings& s) { return s.env[2] * 100.0; }, [] (ChannelSettings& s, double v) { s.env[2] = clamp ((float) v / 100.0f, 0.0f, 1.0f); }, "ch (%): sustain");
    chOp ("CH.REL", [] (const ChannelSettings& s) { return envMs (s.env[3]); }, [] (ChannelSettings& s, double v) { s.env[3] = envFromMs (v); }, "ch (ms): release");
    chOp ("CH.CUT", [] (const ChannelSettings& s) { return cutHz (s.fil[0]); }, [] (ChannelSettings& s, double v) { s.fil[0] = cutFromHz (v); }, "ch (Hz): filter cutoff");
    chOp ("CH.RES", [] (const ChannelSettings& s) { return s.fil[1] * 100.0; }, [] (ChannelSettings& s, double v) { s.fil[1] = clamp ((float) v / 100.0f, 0.0f, 1.0f); }, "ch (%): resonance");
    chOp ("CH.FENV", [] (const ChannelSettings& s) { return s.fil[2] * 100.0; }, [] (ChannelSettings& s, double v) { s.fil[2] = clamp ((float) v / 100.0f, 0.0f, 1.0f); }, "ch (%): filter envelope amount");
    chOp ("CH.RATIO", [] (const ChannelSettings& s) { return modRatio (s.mod[0]); }, [] (ChannelSettings& s, double v) { s.mod[0] = ratioFromValue (v); }, "ch (r): FM/AM/osc2 frequency ratio 0.25-14");
    chOp ("CH.INDEX", [] (const ChannelSettings& s) { return s.mod[1] * 100.0; }, [] (ChannelSettings& s, double v) { s.mod[1] = clamp ((float) v / 100.0f, 0.0f, 1.0f); }, "ch (%): FM index / AM depth / osc2 level / drum tone");
    // normalized (0-1) forms used for exact state recall
    const char* nn[9] = { "CH.ATKN", "CH.DECN", "CH.SUSN", "CH.RELN", "CH.CUTN", "CH.RESN", "CH.FENVN", "CH.RATION", "CH.INDEXN" };
    for (int k = 0; k < 9; ++k)
        chOp (nn[k], [k] (const ChannelSettings& s) { return (double) (k < 4 ? s.env[k] : k < 7 ? s.fil[k - 4] : s.mod[k - 7]); },
              [k] (ChannelSettings& s, double v) { float& r = k < 4 ? s.env[k] : k < 7 ? s.fil[k - 4] : s.mod[k - 7]; r = clamp ((float) v, 0.0f, 1.0f); },
              "ch (0-1): raw normalized value");
    chOp ("CH.VOICES", [] (const ChannelSettings& s) { return (double) s.voices; }, [] (ChannelSettings& s, double v) { s.voices = clamp ((int) std::lround (v), 1, 16); }, "ch (n): polyphony 1-16 (1 = mono)");
    chOp ("CH.GLIDE", [] (const ChannelSettings& s) { return (double) s.glideMs; }, [] (ChannelSettings& s, double v) { s.glideMs = clamp ((float) v, 0.0f, 10000.0f); }, "ch (ms): portamento");
    chOp ("CH.LEGATO", [] (const ChannelSettings& s) { return s.legato ? 1.0 : 0.0; }, [] (ChannelSettings& s, double v) { s.legato = v != 0; }, "ch (0/1): mono legato (no retrigger)");
    chOp ("CH.PAN", [] (const ChannelSettings& s) { return (double) s.pan; }, [] (ChannelSettings& s, double v) { s.pan = clamp ((float) v, -100.0f, 100.0f); }, "ch (-100..100): pan");
    chOp ("CH.LEVEL", [] (const ChannelSettings& s) { return (double) s.levelDb; }, [] (ChannelSettings& s, double v) { s.levelDb = clamp ((float) v, -60.0f, 24.0f); }, "ch (dB): level");
    chOp ("CH.DETUNE", [] (const ChannelSettings& s) { return (double) s.detuneCents; }, [] (ChannelSettings& s, double v) { s.detuneCents = clamp ((float) v, -1.0f, 100.0f); }, "ch (cents): unison spread, -1 = auto");
    chOp ("CH.WIDTH", [] (const ChannelSettings& s) { return (double) s.width; }, [] (ChannelSettings& s, double v) { s.width = clamp ((float) v, 0.0f, 200.0f); }, "ch (%): unison stereo width");
    chOp ("CH.BEND", [] (const ChannelSettings& s) { return (double) s.bendRange; }, [] (ChannelSettings& s, double v) { s.bendRange = clamp ((float) v, 0.0f, 48.0f); }, "ch (semis): pitch-bend range");
    chOp ("CH.TRANS", [] (const ChannelSettings& s) { return (double) s.transpose; }, [] (ChannelSettings& s, double v) { s.transpose = clamp ((float) v, -48.0f, 48.0f); }, "ch (semis): transpose");
    chOp ("CH.FINE", [] (const ChannelSettings& s) { return (double) s.fineCents; }, [] (ChannelSettings& s, double v) { s.fineCents = clamp ((float) v, -100.0f, 100.0f); }, "ch (cents): fine tune");
    chOp ("CH.VSENS", [] (const ChannelSettings& s) { return (double) s.velSens; }, [] (ChannelSettings& s, double v) { s.velSens = clamp ((float) v, 0.0f, 100.0f); }, "ch (%): velocity sensitivity");
    chOp ("CH.KEYT", [] (const ChannelSettings& s) { return (double) s.keyTrack; }, [] (ChannelSettings& s, double v) { s.keyTrack = clamp ((float) v, 0.0f, 200.0f); }, "ch (%): filter key tracking");
    chOp ("CH.DRIFT", [] (const ChannelSettings& s) { return (double) s.driftCents; }, [] (ChannelSettings& s, double v) { s.driftCents = clamp ((float) v, 0.0f, 50.0f); }, "ch (cents): analog pitch drift");
    define ("CH.VEL", 1, true, 2, [this] (const double* a) { return chVel[clamp ((int) std::lround (a[0]), 0, 15)] * 127.0; },
            [this] (const double* a) { chVel[clamp ((int) std::lround (a[0]), 0, 15)] = clamp (a[1] / 127.0, 0.0, 1.0); }, "ch (0-127): velocity used by NOTE");
    define ("CH.LEN", 1, true, 2, [this] (const double* a) { return chLenMs[clamp ((int) std::lround (a[0]), 0, 15)]; },
            [this] (const double* a) { chLenMs[clamp ((int) std::lround (a[0]), 0, 15)] = std::max (0.0, a[1]); }, "ch (ms): note length for PI.N / PI.T / NOTE (0 = a 1/16 note)");
    define ("CH.INIT", 1, false, -1, [&E] (const double* a)
    {
        const int ch = clamp ((int) std::lround (a[0]), 0, 15);
        E.editChannel (ch, [] (ChannelSettings& s) { s.resetExtended(); });
        return 0.0;
    }, nullptr, "ch: reset voice settings (VOICES GLIDE PAN ...) to defaults");

    // ---------------- midi routing / io ----------------
    define ("MIDI.MODE", 0, true, 1, [&E] (const double*) { return (double) E.midiMode.load(); },
            [&E] (const double* a) { E.midiMode = clamp ((int) std::lround (a[0]), 0, 3); },
            "0 multi (MIDI ch = Pilot ch), 1 omni -> MIDI.CH, 2 round robin, 3 round robin (free channels first)");
    define ("MIDI.CH", 0, true, 1, [&E] (const double*) { return (double) E.midiTarget.load(); },
            [&E] (const double* a) { E.midiTarget = clamp ((int) std::lround (a[0]), 0, 15); }, "omni target channel");
    define ("MIDI.RR", 2, false, -1, [&E] (const double* a)
    {
        const int lo = clamp ((int) std::lround (std::min (a[0], a[1])), 0, 15), hi = clamp ((int) std::lround (std::max (a[0], a[1])), 0, 15);
        int m = 0; for (int k = lo; k <= hi; ++k) m |= 1 << k;
        E.rrMask = m; return 0.0;
    }, nullptr, "lo hi: channels used by round robin");
    define ("MIDI.MASK", 0, true, 1, [&E] (const double*) { return (double) E.rrMask.load(); },
            [&E] (const double* a) { const int m = (int) std::lround (a[0]) & 0xFFFF; E.rrMask = m == 0 ? 0xFFFF : m; }, "round robin channel bitmask");
    define ("PORT", 0, true, 1, [&E] (const double*) { return (double) E.udpPort.load(); },
            [&E] (const double* a) { E.runLegacy ("port" + std::to_string (clamp ((int) std::lround (a[0]), 1, 65535))); }, "UDP port");

    // ---------------- effects ----------------
    for (int f = 0; f < kNumFx; ++f)
    {
        const std::string code = upper (kFxInfo[f].code);
        for (auto& d : fxParamTable())
        {
            if (d.fx != f) continue;
            const FxParamDef* dp = &d;
            define (code + "." + d.name, 0, true, 1, [&E, dp] (const double*) { return (double) E.getFxParam (*dp); },
                    [&E, dp] (const double* a) { E.setFxParam (*dp, (float) a[0]); },
                    std::string ("(") + d.unit + ") " + d.help + "  [" + fmt (d.lo) + " .. " + fmt (d.hi) + "]");
        }
        define (code + ".W", 0, true, 1, [&E, f] (const double*) { return E.fxW[f].load() * 15.0; },
                [&E, f] (const double* a) { E.setFxRaw (f, clamp ((float) a[0] / 15.0f, 0.0f, 1.0f), -1.0f); }, "Pilot wet digit 0-15");
        define (code + ".V", 0, true, 1, [&E, f] (const double*) { return E.fxV[f].load() * 15.0; },
                [&E, f] (const double* a) { E.setFxRaw (f, -1.0f, clamp ((float) a[0] / 15.0f, 0.0f, 1.0f)); }, "Pilot value digit 0-15");
        define (code + ".CLR", 0, false, -1, [&E, f] (const double*) { E.clearFxOverrides (f); return 0.0; }, nullptr, "clear named overrides (back to W/V behaviour)");
    }
    define ("LIM.CLIPS", 0, true, -1, [&E] (const double*) { return (double) E.limiterClips(); }, nullptr, "samples the safety clamp touched (should stay 0)");
    define ("LIM.GR", 0, true, -1, [&E] (const double*) { return (double) gainToDb (E.lastLimiterGain()); }, nullptr, "current limiter gain reduction dB");
    definePi();
}

//==============================================================================
// PI.* — Pilot as a Disting EX-style instrument (Teletype's EX.* ops, renamed)
void Lang::definePi()
{
    auto& E = eng;
    auto ch16 = [] (double v) { return clamp ((int) std::lround (v), 0, 15); };
    auto vel01 = [] (double v) { if (v > 127.0) v = v * 127.0 / 16383.0; return (float) clamp (v / 127.0, 0.0, 1.0); };   // 0-127 (or Teletype 0-16383)
    auto note = [] (double v) { return clamp ((int) std::lround (v), 0, 127); };

    // ---- parameter table for PI.P / PI.PV / PI.MIN / PI.MAX ----
    const double aMin = envMs (0.0f), aMax = envMs (1.0f);
    struct C { const char* name; const char* op; const char* norm; double lo, hi; };
    const C chParams[] = {
        { "ENG", "CH.ENG", "", 0, 8 }, { "OSC1", "CH.OSC1", "", -1, 15 }, { "OSC2", "CH.OSC2", "", -1, 15 },
        { "ATK", "CH.ATK", "CH.ATKN", aMin, aMax }, { "DEC", "CH.DEC", "CH.DECN", aMin, aMax }, { "SUS", "CH.SUS", "", 0, 100 },
        { "REL", "CH.REL", "CH.RELN", aMin, aMax }, { "CUT", "CH.CUT", "CH.CUTN", 20, 20480 }, { "RES", "CH.RES", "", 0, 100 },
        { "FENV", "CH.FENV", "", 0, 100 }, { "RATIO", "CH.RATIO", "CH.RATION", 0.25, 14 }, { "INDEX", "CH.INDEX", "", 0, 100 },
        { "VOICES", "CH.VOICES", "", 1, 16 }, { "GLIDE", "CH.GLIDE", "", 0, 10000 }, { "LEGATO", "CH.LEGATO", "", 0, 1 },
        { "PAN", "CH.PAN", "", -100, 100 }, { "LEVEL", "CH.LEVEL", "", -60, 24 }, { "DETUNE", "CH.DETUNE", "", -1, 100 },
        { "WIDTH", "CH.WIDTH", "", 0, 200 }, { "BEND", "CH.BEND", "", 0, 48 }, { "TRANS", "CH.TRANS", "", -48, 48 },
        { "FINE", "CH.FINE", "", -100, 100 }, { "VSENS", "CH.VSENS", "", 0, 100 }, { "KEYT", "CH.KEYT", "", 0, 200 },
        { "DRIFT", "CH.DRIFT", "", 0, 50 }, { "VEL", "CH.VEL", "", 0, 127 }, { "LEN", "CH.LEN", "", 0, 10000 },
        { "TIME", "CH.TIME", "", 0, 100 }, { "INTONE", "CH.INTONE", "", 0, 100 }, { "RAMP", "CH.RAMP", "", 0, 100 }, { "CURVE", "CH.CURVE", "", 0, 100 },
        { "FM", "CH.FM", "", 0, 100 }, { "RANGE", "CH.RANGE", "", 0, 1 }, { "MODE", "CH.MODE", "", 0, 2 } };
    piParams.clear();
    for (auto& c : chParams)
    {
        PiParam pp; pp.name = c.name; pp.op = c.op; pp.normOp = c.norm; pp.lo = c.lo; pp.hi = c.hi;
        pp.doc = ops.count (c.op) ? ops.at (c.op).doc : "";
        piParams.push_back (pp);
    }
    // effects: 100 + 10 * fx + k   (k 0 = W digit, 1 = V digit, 2.. = named parameters)
    for (int f = 0; f < kNumFx; ++f)
    {
        const std::string code = upper (kFxInfo[f].code);
        piParams.push_back ({ code + ".W", code + ".W", "", 0, 15, f, 0, "Pilot wet digit 0-15" });
        piParams.push_back ({ code + ".V", code + ".V", "", 0, 15, f, 1, "Pilot value digit 0-15" });
        int k = 2;
        for (auto& d : fxParamTable())
            if (d.fx == f && k < 10)
                piParams.push_back ({ code + "." + d.name, code + "." + d.name, "", d.lo, d.hi, f, k++, std::string ("(") + d.unit + ") " + d.help });
    }

    // ---- notes ----
    define ("PI.CH", 0, true, 1, [this] (const double*) { return (double) piCh; }, [this, ch16] (const double* a) { piCh = ch16 (a[0]); },
            "default channel 0-15 for PI.NOTE / PI.NO / PI.P / PI.ALG (Teletype EX.CH)");
    define ("PI.#", 0, true, 1, [this] (const double*) { return (double) piCh; }, [this, ch16] (const double* a) { piCh = ch16 (a[0]); }, "alias of PI.CH");
    auto noteOn = [&E] (int ch, int n, float v) { E.playNote (ch, n, v, -1.0f); };
    define ("PI.NOTE", 2, false, -1, [this, noteOn, note, vel01] (const double* a) { noteOn (piCh, note (a[0]), vel01 (a[1])); return 0.0; }, nullptr,
            "pitch vel: note on (held) on PI.CH, voice allocated by the channel. vel 0-127 (EX.NOTE)");
    // PI.N ch pitch vel: one call picks the synth too, and the note releases after the channel's CH.LEN (1/16 bar
    // by default), so it can sit in a "DEL x: $ $" loop without stacking held notes. PI.N# holds instead.
    define ("PI.N", 3, false, -1, [this, &E, note, vel01, ch16] (const double* a)
    {
        const int ch = ch16 (a[0]);
        const double l = chLenMs[ch];
        E.playNote (ch, note (a[1]), vel01 (a[2]), (float) (l > 0 ? l : 0.25 * 60000.0 / std::max (20.0, E.tempo())));
        return 0.0;
    }, nullptr, "ch pitch vel: play pitch on channel (synth) ch 0-15 at vel 0-127, released after CH.LEN");
    define ("PI.N#", 3, false, -1, [noteOn, note, vel01, ch16] (const double* a) { noteOn (ch16 (a[0]), note (a[1]), vel01 (a[2])); return 0.0; }, nullptr,
            "ch pitch vel: note on (held) on channel ch (EX.N#)");
    define ("PI.NOTE.O", 1, false, -1, [this, &E, note] (const double* a) { E.releaseNote (piCh, note (a[0])); return 0.0; }, nullptr, "pitch: note off on PI.CH (EX.NOTE.O)");
    define ("PI.NO", 1, false, -1, [this, &E, note] (const double* a) { E.releaseNote (piCh, note (a[0])); return 0.0; }, nullptr, "alias of PI.NOTE.O");
    define ("PI.NO#", 2, false, -1, [&E, note, ch16] (const double* a) { E.releaseNote (ch16 (a[0]), note (a[1])); return 0.0; }, nullptr, "ch pitch: note off on channel ch");
    define ("PI.ALLOFF", 0, false, -1, [this, &E] (const double*) { E.panic(); for (auto& v : voxNote) v = -1; return 0.0; }, nullptr, "all notes off (EX.ALLOFF)");
    define ("PI.AO", 0, false, -1, [this, &E] (const double*) { E.panic(); for (auto& v : voxNote) v = -1; return 0.0; }, nullptr, "alias of PI.ALLOFF");

    // ---- voices: one held note per channel (EX.VOX) ----
    auto vox = [this, &E] (int ch, int n, float v)
    {
        if (voxNote[ch] >= 0) E.releaseNote (ch, voxNote[ch]);
        voxNote[ch] = n; piVel[ch] = v * 127.0;
        E.playNote (ch, n, v, -1.0f);
    };
    define ("PI.VOX", 3, false, -1, [vox, ch16, note, vel01] (const double* a) { vox (ch16 (a[0]), note (a[1]), vel01 (a[2])); return 0.0; }, nullptr,
            "ch pitch vel: play voice ch (one held note per channel, replaces the last) (EX.VOX)");
    define ("PI.V", 3, false, -1, [vox, ch16, note, vel01] (const double* a) { vox (ch16 (a[0]), note (a[1]), vel01 (a[2])); return 0.0; }, nullptr, "alias of PI.VOX");
    auto voxP = [this, vox] (int ch, int n) { vox (ch, n, (float) (piVel[ch] / 127.0)); };
    define ("PI.VOX.P", 2, false, -1, [voxP, ch16, note] (const double* a) { voxP (ch16 (a[0]), note (a[1])); return 0.0; }, nullptr, "ch pitch: change voice ch's pitch (glides with CH.GLIDE / CH.LEGATO)");
    define ("PI.VP", 2, false, -1, [voxP, ch16, note] (const double* a) { voxP (ch16 (a[0]), note (a[1])); return 0.0; }, nullptr, "alias of PI.VOX.P");
    auto voxO = [this, &E] (int ch) { if (voxNote[ch] >= 0) E.releaseNote (ch, voxNote[ch]); voxNote[ch] = -1; };
    define ("PI.VOX.O", 1, false, -1, [voxO, ch16] (const double* a) { voxO (ch16 (a[0])); return 0.0; }, nullptr, "ch: release voice ch (EX.VOX.O)");
    define ("PI.VO", 1, false, -1, [voxO, ch16] (const double* a) { voxO (ch16 (a[0])); return 0.0; }, nullptr, "alias of PI.VOX.O");

    // ---- triggers (drums): one-shot at the channel's CH.LEN ----
    auto trig = [this, &E] (int ch, float v)
    {
        const double l = chLenMs[ch];
        E.playNote (ch, voxNote[ch] >= 0 ? voxNote[ch] : 60, v, (float) (l > 0 ? l : 0.25 * 60000.0 / std::max (20.0, E.tempo())));
    };
    define ("PI.T", 1, false, -1, [trig, ch16] (const double* a) { trig (ch16 (a[0]), 0.5f); return 0.0; }, nullptr, "ch: trigger channel ch at medium velocity (EX.T)");
    define ("PI.TV", 2, false, -1, [trig, ch16, vel01] (const double* a) { trig (ch16 (a[0]), vel01 (a[1])); return 0.0; }, nullptr, "ch vel: trigger channel ch (EX.TV)");

    // ---- algorithm + parameters ----
    define ("PI.ALG", 0, true, 1, [this] (const double*) { double a[1] = { (double) piCh }; return ops.at ("CH.ENG").get (a); },
            [this] (const double* a) { double b[2] = { (double) piCh, a[0] }; ops.at ("CH.ENG").set (b); },
            "PI.CH's engine: 0 analog 1 fm 2 am 3 pluck 4 kick 5 snare 6 hat 7 tom 8 just friends (EX.ALG)");
    define ("PI.A", 0, true, 1, [this] (const double*) { double a[1] = { (double) piCh }; return ops.at ("CH.ENG").get (a); },
            [this] (const double* a) { double b[2] = { (double) piCh, a[0] }; ops.at ("CH.ENG").set (b); }, "alias of PI.ALG");
    auto pGet = [this] (const double* a) { auto* pp = piParam (a[0]); return pp ? piGet (*pp) : 0.0; };
    auto pSet = [this] (const double* a) { if (auto* pp = piParam (a[0])) piSet (*pp, a[1]); };
    define ("PI.PARAM", 1, true, 2, pGet, pSet, "x (y): parameter x of PI.CH / the effects, in real units (EX.PARAM; numbers in COMMANDS.md)");
    define ("PI.P", 1, true, 2, pGet, pSet, "alias of PI.PARAM");
    define ("PI.PV", 2, false, -1, [this] (const double* a)
    {
        if (auto* pp = piParam (a[0]))
        {
            const double t = clamp (a[1] / 16384.0, 0.0, 1.0);
            if (! pp->normOp.empty()) { double b[2] = { (double) piCh, t }; ops.at (pp->normOp).set (b); }   // log-scaled params
            else piSet (*pp, pp->lo + t * (pp->hi - pp->lo));
        }
        return 0.0;
    }, nullptr, "x y: set parameter x from y scaled over 0..16384 (EX.PV)");
    define ("PI.MIN", 1, true, -1, [this] (const double* a) { auto* pp = piParam (a[0]); return pp ? pp->lo : 0.0; }, nullptr, "x: minimum of parameter x");
    define ("PI.MAX", 1, true, -1, [this] (const double* a) { auto* pp = piParam (a[0]); return pp ? pp->hi : 0.0; }, nullptr, "x: maximum of parameter x");

    // ---- presets: whole Pilot sound (channels + effects + routing) ----
    auto loadPreset = [this, &E] (int n)
    {
        if (n < 0 || n >= kScenes || presets[n].empty()) return;
        E.resetChannels(); E.resetEffects();
        const bool q = E.setQuiet (true);
        run (presets[n]);   // Pilot commands only: never re-enters Teletype (lock order: Teletype -> Lang)
        E.setQuiet (q);
    };
    define ("PI.PRESET", 0, true, 1, [this] (const double*) { return (double) piPresetLoaded; }, [this, loadPreset] (const double* a)
    {
        const int n = clamp ((int) std::lround (a[0]), 0, kScenes - 1);
        loadPreset (n); piPresetLoaded = n;
    }, "x: load sound preset x 0-31 (EX.PRESET)");
    define ("PI.PRE", 0, true, 1, [this] (const double*) { return (double) piPresetLoaded; }, [this, loadPreset] (const double* a)
    {
        const int n = clamp ((int) std::lround (a[0]), 0, kScenes - 1);
        loadPreset (n); piPresetLoaded = n;
    }, "alias of PI.PRESET");
    define ("PI.SAVE", 1, false, -1, [this, &E] (const double* a)
    {
        const int n = clamp ((int) std::lround (a[0]), 0, kScenes - 1);
        std::string st = E.serializeState();
        for (int f = 0; f < kNumFx; ++f)
            st += "\n" + upper (kFxInfo[f].code) + ".W " + fmt (E.fxW[f].load() * 15.0) + ";" + upper (kFxInfo[f].code) + ".V " + fmt (E.fxV[f].load() * 15.0);
        presets[n] = st; piPresetLoaded = n;
        saveLibrary();
        return 0.0;
    }, nullptr, "x: save the current sound to preset x 0-31 (EX.SAVE)");
    define ("PI.RESET", 0, false, -1, [&E] (const double*) { E.resetChannels(); E.resetEffects(); return 0.0; }, nullptr, "reset channels + effects to Pilot defaults (EX.RESET)");
    define ("PI.REC", 1, false, -1, [&E] (const double* a) { if (E.onRecord) E.onRecord (a[0] != 0); return 0.0; }, nullptr,
            "1 start / 0 stop recording to ~/Music/Pilot Sealflake (EX.REC)");
}

const Lang::PiParam* Lang::piParam (double num) const
{
    const int n = (int) std::lround (num);
    if (n >= 0 && n < 34) return &piParams[(size_t) n];
    if (n < 100) return nullptr;
    const int f = (n - 100) / 10, k = (n - 100) % 10;
    for (auto& p : piParams) if (p.fx == f && p.slot == k) return &p;
    return nullptr;
}

double Lang::piGet (const PiParam& p) const
{
    double a[1] = { (double) piCh };
    return ops.at (p.op).get (a);
}

void Lang::piSet (const PiParam& p, double v)
{
    v = clamp (v, std::min (p.lo, p.hi), std::max (p.lo, p.hi));
    if (p.fx >= 0) { double a[1] = { v }; ops.at (p.op).set (a); return; }
    double a[2] = { (double) piCh, v };
    ops.at (p.op).set (a);
}

bool Lang::piInfo (int num, double& lo, double& hi, int& scale) const
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    auto* p = piParam (num);
    if (p == nullptr) return false;
    lo = p->lo; hi = p->hi;
    static const char* whole[] = { "ENG", "OSC1", "OSC2", "VOICES", "LEGATO", "VEL", "LEN", "GLIDE", "BITS", "STAGES", "TP", "RANGE", "MODE" };
    bool isWhole = p->name.size() > 2 && (p->name.compare (p->name.size() - 2, 2, ".W") == 0 || p->name.compare (p->name.size() - 2, 2, ".V") == 0);
    const auto dot = p->name.find ('.');
    const std::string base = dot == std::string::npos ? p->name : p->name.substr (dot + 1);
    for (auto* w : whole) if (base == w) isWhole = true;
    scale = (! isWhole && std::max (std::abs (lo), std::abs (hi)) <= 20.0) ? 100 : 1;
    return true;
}

double Lang::piGetCh (int ch, int num) const
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    auto* p = piParam (num);
    if (p == nullptr) return 0.0;
    double a[1] = { (double) clamp (ch, 0, 15) };
    return ops.at (p->op).get (a);
}

void Lang::piSetCh (int ch, int num, double v)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    auto* p = piParam (num);
    if (p == nullptr) return;
    v = clamp (v, std::min (p->lo, p->hi), std::max (p->lo, p->hi));
    if (p->fx >= 0) { double a[1] = { v }; ops.at (p->op).set (a); return; }
    double a[2] = { (double) clamp (ch, 0, 15), v };
    ops.at (p->op).set (a);
}

void Lang::piSetNormCh (int ch, int num, double t)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    auto* p = piParam (num);
    if (p == nullptr) return;
    t = clamp (t, 0.0, 1.0);
    if (! p->normOp.empty()) { double b[2] = { (double) clamp (ch, 0, 15), t }; ops.at (p->normOp).set (b); return; }
    piSetCh (ch, num, p->lo + t * (p->hi - p->lo));
}

double Lang::noteLengthMs (int ch) const
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    const double l = chLenMs[clamp (ch, 0, 15)];
    return l > 0 ? l : 0.25 * 60000.0 / std::max (20.0, eng.tempo());
}

std::string Lang::piParamReference() const
{
    std::string out;
    for (size_t i = 0; i < piParams.size(); ++i)
    {
        auto& p = piParams[i];
        const int num = p.fx < 0 ? (int) i : 100 + 10 * p.fx + p.slot;
        double lo, hi; int scale = 1;
        piInfo (num, lo, hi, scale);
        out += std::to_string (num) + "|" + p.name + "|" + fmt (p.lo) + "|" + fmt (p.hi) + "|" + std::to_string (scale) + "|" + p.doc + "\n";
    }
    return out;
}

//==============================================================================
// validation (Teletype refuses to store a line that doesn't parse)
bool Lang::stackDepth (const std::vector<std::string>& toks, size_t from, int& depth, std::string& err) const
{
    depth = 0;
    for (size_t i = toks.size(); i-- > from;)
    {
        double v;
        if (parseNumber (toks[i], v) || parseNote (toks[i], v)) { ++depth; continue; }
        const Op* op = findOp (toks[i]);
        if (op == nullptr) { err = "UNKNOWN WORD " + upper (toks[i]); return false; }
        if (depth < op->args) { err = upper (toks[i]) + " NEEDS " + std::to_string (op->args); return false; }
        depth -= op->args;
        if (op->ret) ++depth;
    }
    return true;
}

std::string Lang::checkCommand (const std::string& cmd) const
{
    const auto toks = tokenize (cmd);
    if (toks.empty()) return {};
    if (toks.size() == 1)
    {
        if (PilotEngine::isLegacyCommand (toks[0])) return {};
        const std::string lc = upper (toks[0]);
        if (! isWord (toks[0]))
        {
            for (const char* p : { "PORT", "MIDI", "STOP", "PANIC" }) if (lc.rfind (p, 0) == 0) return {};
            return "UNKNOWN WORD " + lc;
        }
    }
    std::string err; int depth = 0;
    const Op* first = findOp (toks[0]);
    if (first != nullptr && first->setArgs >= 0)
    {
        if (! stackDepth (toks, 1, depth, err)) return err;
        if (depth == first->setArgs) return {};
    }
    if (! stackDepth (toks, 0, depth, err)) return err;
    if (depth > 1) return "EXTRA PARAMS";
    return {};
}

std::string Lang::validate (const std::string& line) const
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    const std::string t = trim (line);
    if (t.empty() || t[0] == '#') return {};
    const auto segs = splitTop (t, ';');
    for (size_t i = 0; i < segs.size(); ++i)
    {
        const std::string seg = trim (segs[i]);
        if (seg.empty()) continue;
        const size_t colon = seg.find (':');
        if (colon == std::string::npos) { auto e = checkCommand (seg); if (! e.empty()) return e; continue; }
        const auto toks = tokenize (seg.substr (0, colon));
        if (toks.empty()) return "EMPTY PRE";
        static const std::map<std::string, int> pres { { "IF", 1 }, { "ELIF", 1 }, { "ELSE", 0 }, { "L", 2 }, { "W", 1 }, { "EVERY", 1 },
                                                       { "SKIP", 1 }, { "OTHER", 0 }, { "PROB", 1 }, { "DEL", 1 },
                                                       { "DEL.X", 2 }, { "DEL.R", 2 }, { "DEF", 1 } };
        const auto it = pres.find (upper (toks[0]));
        if (it == pres.end()) return "UNKNOWN PRE " + upper (toks[0]);
        int depth = 0; std::string err;
        if (! stackDepth (toks, 1, depth, err)) return err;
        if (depth < it->second) return it->first + " NEEDS " + std::to_string (it->second);
        if (depth > it->second) return "EXTRA PARAMS";
        std::string body = seg.substr (colon + 1);
        for (size_t k = i + 1; k < segs.size(); ++k) body += ";" + segs[k];
        if (trim (body).empty()) return "EMPTY " + it->first;
        return validate (body);
    }
    return {};
}

//==============================================================================
// Teletype window API
std::vector<std::string> Lang::getScript (int n) const
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    return (n >= 1 && n <= kScripts) ? scripts[n] : std::vector<std::string>();
}
void Lang::setScript (int n, const std::vector<std::string>& lines)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    if (n >= 1 && n <= kScripts) scripts[n] = lines;
}
Lang::PatternView Lang::getPattern (int p) const
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    const auto& s = pat[clamp (p, 0, 3)];
    PatternView v;
    std::copy (s.v, s.v + 64, v.v); v.len = s.len; v.idx = s.idx; v.start = s.start; v.end = s.end; v.wrap = s.wrap;
    return v;
}
void Lang::setPattern (int p, const PatternView& v)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    auto& s = pat[clamp (p, 0, 3)];
    std::copy (v.v, v.v + 64, s.v); s.len = clamp (v.len, 0, 64); s.idx = clamp (v.idx, 0, 63);
    s.start = clamp (v.start, 0, 63); s.end = clamp (v.end, 0, 63); s.wrap = v.wrap;
}
int Lang::currentPattern() const { std::lock_guard<std::recursive_mutex> lk (mtx); return patN; }
std::array<double, 11> Lang::getVars() const { std::lock_guard<std::recursive_mutex> lk (mtx); std::array<double, 11> a; std::copy (vars, vars + 11, a.begin()); return a; }
double Lang::metroPeriod() const { std::lock_guard<std::recursive_mutex> lk (mtx); return metroMs; }
bool Lang::metroActive() const { std::lock_guard<std::recursive_mutex> lk (mtx); return metroOn; }
void Lang::setMetroActive (bool on) { std::lock_guard<std::recursive_mutex> lk (mtx); metroOn = on; nextMetro = 0.0; }
uint32_t Lang::metroTicks() const { std::lock_guard<std::recursive_mutex> lk (mtx); return metroCount; }
int Lang::pendingDelays() const { std::lock_guard<std::recursive_mutex> lk (mtx); return (int) delayed.size(); }
void Lang::clearDelays() { std::lock_guard<std::recursive_mutex> lk (mtx); delayed.clear(); }
bool Lang::isMuted (int n) const { std::lock_guard<std::recursive_mutex> lk (mtx); return n >= 1 && n <= kScripts && muted[n]; }
void Lang::setMuted (int n, bool m) { std::lock_guard<std::recursive_mutex> lk (mtx); if (n >= 1 && n <= kScripts) muted[n] = m; }
void Lang::runScriptNow (int n) { std::lock_guard<std::recursive_mutex> lk (mtx); runScript (n, 1, true); }
std::vector<std::pair<std::string, std::string>> Lang::help() const { return docs; }

std::vector<std::string> Lang::sceneText (int n) const
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    return (n >= 0 && n < kScenes) ? scenes[n].text : std::vector<std::string>();
}
void Lang::setSceneText (int n, const std::vector<std::string>& t)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    if (n >= 0 && n < kScenes) scenes[n].text = t;
}
bool Lang::sceneUsed (int n) const { std::lock_guard<std::recursive_mutex> lk (mtx); return n >= 0 && n < kScenes && scenes[n].used; }
int Lang::currentScene() const { std::lock_guard<std::recursive_mutex> lk (mtx); return scene; }
void Lang::writeScene (int n)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    if (n < 0 || n >= kScenes) return;
    auto& sc = scenes[n];
    sc.used = true;
    for (int i = 0; i <= kScripts; ++i) sc.scripts[i] = scripts[i];
    for (int p = 0; p < 4; ++p) sc.pat[p] = pat[p];
    sc.metroMs = metroMs;
    scene = n;
    saveLibrary();
}
bool Lang::readScene (int n, bool runInitScript)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    if (n < 0 || n >= kScenes || ! scenes[n].used) return false;
    const auto& sc = scenes[n];
    for (int i = 0; i <= kScripts; ++i) scripts[i] = sc.scripts[i];
    for (int p = 0; p < 4; ++p) pat[p] = sc.pat[p];
    metroMs = sc.metroMs; nextMetro = 0.0;
    delayed.clear();
    scene = n;
    if (runInitScript) runScript (10, 1, true);
    return true;
}

void Lang::saveLibrary() const
{
    juce::XmlElement root ("PILOT_TELETYPE_LIBRARY");
    for (int n = 0; n < kScenes; ++n)
    {
        auto& sc = scenes[n];
        if (sc.used || ! sc.text.empty())
        {
            auto* e = root.createNewChildElement ("SCENE");
            e->setAttribute ("n", n); e->setAttribute ("used", sc.used); e->setAttribute ("metro", sc.metroMs);
            for (auto& t : sc.text) e->createNewChildElement ("TEXT")->setAttribute ("text", juce::String (t));
            for (int i = 1; i <= kScripts; ++i)
            {
                if (sc.scripts[i].empty()) continue;
                auto* s = e->createNewChildElement ("SCRIPT");
                s->setAttribute ("n", i);
                for (auto& l : sc.scripts[i]) s->createNewChildElement ("LINE")->setAttribute ("text", juce::String (l));
            }
            for (int p = 0; p < 4; ++p)
            {
                auto* pe = e->createNewChildElement ("PATTERN"); pe->setAttribute ("n", p);
                writePattern (*pe, sc.pat[p].v, sc.pat[p].len, sc.pat[p].idx, sc.pat[p].start, sc.pat[p].end, sc.pat[p].wrap);
            }
        }
        if (! presets[n].empty())
        {
            auto* e = root.createNewChildElement ("PRESET");
            e->setAttribute ("n", n); e->setAttribute ("state", juce::String (presets[n]));
        }
    }
    const auto f = libraryFile();
    f.getParentDirectory().createDirectory();
    root.writeTo (f);
}

void Lang::loadLibrary()
{
    auto xml = juce::XmlDocument::parse (libraryFile());
    if (xml == nullptr) return;
    auto readPat = [] (const juce::XmlElement& e, Pattern& p)
    {
        p.len = e.getIntAttribute ("len"); p.idx = e.getIntAttribute ("idx"); p.start = e.getIntAttribute ("start", 0);
        p.end = e.getIntAttribute ("end", 63); p.wrap = e.getBoolAttribute ("wrap", true);
        juce::StringArray v; v.addTokens (e.getStringAttribute ("values"), " ", "");
        for (int i = 0; i < 64 && i < v.size(); ++i) p.v[i] = v[i].getDoubleValue();
    };
    for (auto* e : xml->getChildWithTagNameIterator ("SCENE"))
    {
        const int n = e->getIntAttribute ("n", -1);
        if (n < 0 || n >= kScenes) continue;
        Scene sc; sc.used = e->getBoolAttribute ("used"); sc.metroMs = e->getDoubleAttribute ("metro", 500.0);
        for (auto* t : e->getChildWithTagNameIterator ("TEXT")) sc.text.push_back (t->getStringAttribute ("text").toStdString());
        for (auto* s : e->getChildWithTagNameIterator ("SCRIPT"))
        {
            const int i = s->getIntAttribute ("n");
            if (i < 1 || i > kScripts) continue;
            for (auto* l : s->getChildWithTagNameIterator ("LINE")) sc.scripts[i].push_back (l->getStringAttribute ("text").toStdString());
        }
        for (auto* pe : e->getChildWithTagNameIterator ("PATTERN"))
        {
            const int p = pe->getIntAttribute ("n", -1);
            if (p >= 0 && p < 4) readPat (*pe, sc.pat[p]);
        }
        scenes[n] = sc;
    }
    for (auto* e : xml->getChildWithTagNameIterator ("PRESET"))
    {
        const int n = e->getIntAttribute ("n", -1);
        if (n >= 0 && n < kScenes) presets[n] = e->getStringAttribute ("state").toStdString();
    }
}

//==============================================================================
std::string Lang::reference() const
{
    std::string out;
    for (auto& d : docs)
    {
        const Op& op = ops.at (opKey (d.first));
        out += d.first;
        out += "|" + std::to_string (op.args) + "|" + (op.ret ? "1" : "0") + "|" + std::to_string (op.setArgs) + "|" + d.second + "\n";
    }
    return out;
}

void Lang::saveState (juce::XmlElement& x) const
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    auto* l = x.createNewChildElement ("LANG");
    l->setAttribute ("metro", metroMs);
    l->setAttribute ("metroOn", metroOn);
    l->setAttribute ("patN", patN);
    juce::String vs;
    for (double v : vars) vs << fmt (v) << " ";
    l->setAttribute ("vars", vs.trim());
    for (int ch = 0; ch < 16; ++ch) { l->setAttribute ("vel" + juce::String (ch), chVel[ch]); l->setAttribute ("len" + juce::String (ch), chLenMs[ch]); }
    for (int n = 1; n <= kScripts; ++n)
    {
        if (scripts[n].empty()) continue;
        auto* s = l->createNewChildElement ("SCRIPT");
        s->setAttribute ("n", n);
        for (auto& line : scripts[n]) s->createNewChildElement ("LINE")->setAttribute ("text", juce::String (line));
    }
    for (int p = 0; p < 4; ++p)
    {
        auto* e = l->createNewChildElement ("PATTERN");
        e->setAttribute ("n", p);
        writePattern (*e, pat[p].v, pat[p].len, pat[p].idx, pat[p].start, pat[p].end, pat[p].wrap);
    }
    juce::String m;
    for (int n = 1; n <= kScripts; ++n) m << (muted[n] ? "1" : "0");
    l->setAttribute ("mute", m);
    l->setAttribute ("piCh", piCh);
    l->setAttribute ("scene", scene);
}

void Lang::loadState (const juce::XmlElement& x)
{
    std::lock_guard<std::recursive_mutex> lk (mtx);
    auto* l = x.getChildByName ("LANG");
    if (l == nullptr) return;
    metroMs = l->getDoubleAttribute ("metro", 500.0);
    metroOn = l->getBoolAttribute ("metroOn", false);
    nextMetro = 0.0;
    patN = l->getIntAttribute ("patN", 0);
    juce::StringArray vs; vs.addTokens (l->getStringAttribute ("vars"), " ", "");
    for (int i = 0; i < 11 && i < vs.size(); ++i) vars[i] = vs[i].getDoubleValue();
    for (int ch = 0; ch < 16; ++ch) { chVel[ch] = l->getDoubleAttribute ("vel" + juce::String (ch), 0.66); chLenMs[ch] = l->getDoubleAttribute ("len" + juce::String (ch), 0.0); }
    for (auto& s : scripts) s.clear();
    for (auto* s : l->getChildWithTagNameIterator ("SCRIPT"))
    {
        const int n = s->getIntAttribute ("n");
        if (n < 1 || n > kScripts) continue;
        for (auto* line : s->getChildWithTagNameIterator ("LINE")) scripts[n].push_back (line->getStringAttribute ("text").toStdString());
    }
    for (auto* e : l->getChildWithTagNameIterator ("PATTERN"))
    {
        const int p = e->getIntAttribute ("n");
        if (p < 0 || p > 3) continue;
        pat[p].len = e->getIntAttribute ("len"); pat[p].idx = e->getIntAttribute ("idx");
        pat[p].start = e->getIntAttribute ("start", 0); pat[p].end = e->getIntAttribute ("end", 63); pat[p].wrap = e->getBoolAttribute ("wrap", true);
        juce::StringArray v; v.addTokens (e->getStringAttribute ("values"), " ", "");
        for (int i = 0; i < 64 && i < v.size(); ++i) pat[p].v[i] = v[i].getDoubleValue();
    }
    const auto m = l->getStringAttribute ("mute");
    for (int n = 1; n <= kScripts; ++n) muted[n] = n - 1 < m.length() && m[n - 1] == '1';
    piCh = clamp (l->getIntAttribute ("piCh", 0), 0, 15);
    scene = clamp (l->getIntAttribute ("scene", 0), 0, kScenes - 1);
}
} // namespace sf
