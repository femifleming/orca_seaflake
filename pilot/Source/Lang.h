// Pilot Sealflake — Teletype-style stack language
//
//   line      :=  [PRE args :] command { ; command }
//   command   :=  word { word }            (prefix notation, evaluated right-to-left on a stack)
//   word      :=  number | note (C4, F#2, Bb3) | OP
//
// A leading op with a setter form ("CH.VOICES 0 4", "A 5", "M 250") sets; anywhere else it gets.
// Words may be separated by spaces or '_' and dots in op names are optional (Orca-friendly).
// A single word that is a classic Pilot command (04C, REV8c, ...) always runs as one.
// Script lines starting with '#' are commented out (Teletype's alt-/).
#pragma once
#include <array>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace juce { class XmlElement; }

namespace sf
{
class PilotEngine;

class Lang
{
public:
    explicit Lang (PilotEngine&);

    std::string run (const std::string& text);   // thread-safe; returns result / error text
    void tick (double nowMs);                    // scheduler thread: metro + DEL
    void saveState (juce::XmlElement&) const;
    void loadState (const juce::XmlElement&);
    void runInit();                              // script 10
    std::string reference() const;               // op list (name|args|returns|setArgs|doc lines)
    std::string piParamReference() const;        // PI.P parameter numbers (num|name|lo|hi|scale|doc lines)

    // PI.* parameters for the Teletype ops (TeletypeHost). Values in real units;
    // Teletype sees them as whole numbers times piScale (100 for small fractional ranges).
    bool piInfo (int num, double& lo, double& hi, int& scale) const;
    double piGetCh (int ch, int num) const;
    void piSetCh (int ch, int num, double v);
    void piSetNormCh (int ch, int num, double t);   // t 0..1 over the range (log for times / cutoff)
    double noteLengthMs (int ch) const;             // CH.LEN, or 1/16 note at the tempo when 0

    static constexpr int kScripts = 10;          // 1-8, 9 = metro (M), 10 = init (I)
    static constexpr int kScenes = 32;
    static inline std::string libraryPathOverride;   // tests: keep the user's scene library untouched

    // ---- Teletype window API (any thread; all calls lock) ----
    struct PatternView { double v[64] {}; int len = 0, idx = 0, start = 0, end = 63; bool wrap = true; };
    std::vector<std::string> getScript (int n) const;
    void setScript (int n, const std::vector<std::string>& lines);
    std::string validate (const std::string& line) const;   // "" when the line is valid
    PatternView getPattern (int p) const;
    void setPattern (int p, const PatternView&);
    int currentPattern() const;
    std::array<double, 11> getVars() const;
    double metroPeriod() const;
    bool metroActive() const;
    void setMetroActive (bool);
    uint32_t metroTicks() const;
    int pendingDelays() const;
    void clearDelays();
    bool isMuted (int n) const;
    void setMuted (int n, bool);
    void runScriptNow (int n);                   // F1-F10: runs even when muted
    std::vector<std::pair<std::string, std::string>> help() const;

    // scenes (scripts + patterns + metro + text), kept in a library file shared by app + plugin
    std::vector<std::string> sceneText (int n) const;
    void setSceneText (int n, const std::vector<std::string>&);
    bool sceneUsed (int n) const;
    void writeScene (int n);
    bool readScene (int n, bool runInitScript = true);
    int currentScene() const;

private:
    struct Op
    {
        int args = 0;            // getter arity
        bool ret = true;         // getter leaves a value
        int setArgs = -1;        // setter arity (-1 = none)
        std::function<double (const double*)> get;
        std::function<void (const double*)> set;
        std::string doc;
    };
    struct Ctx { int script = 0; bool inIf = false, ifFired = false, lastFired = true; int depth = 0; };
    struct Delayed { double due; int script; std::string body; };
    struct Pattern { double v[64] {}; int len = 0, idx = 0, start = 0, end = 63; bool wrap = true; };
    struct Scene { bool used = false; std::vector<std::string> text; std::vector<std::string> scripts[kScripts + 1]; Pattern pat[4]; double metroMs = 500.0; };
    struct PiParam { std::string name, op, normOp; double lo, hi; int fx = -1; int slot = -1; std::string doc; };

    void define (const std::string& name, int args, bool ret, int setArgs,
                 std::function<double (const double*)> get, std::function<void (const double*)> set, const std::string& doc);
    void defineAll();
    void definePi();
    void runLine (const std::string& line, Ctx& ctx, std::string& out);
    void runCommand (const std::string& cmd, Ctx& ctx, std::string& out);
    void runPre (const std::string& pre, const std::string& body, Ctx& ctx, std::string& out);
    bool evalRange (const std::vector<std::string>& toks, size_t from, std::vector<double>& st, std::string& err);
    bool applyOp (const Op& op, const std::string& name, std::vector<double>& st, std::string& err);
    const Op* findOp (const std::string& token) const;
    bool isWord (const std::string& token) const;
    bool stackDepth (const std::vector<std::string>& toks, size_t from, int& depth, std::string& err) const;
    std::string checkCommand (const std::string& cmd) const;
    void runScript (int n, int depth, bool force = false);
    double now() const;
    const PiParam* piParam (double num) const;
    double piGet (const PiParam&) const;
    void piSet (const PiParam&, double v);
    void loadLibrary();
    void saveLibrary() const;

    PilotEngine& eng;
    Ctx liveCtx;
    int curScript = 0;     // script running the current command (SCRIPT / $ with no argument)
    int scriptNest = 0;    // nested SCRIPT calls in progress
    mutable std::recursive_mutex mtx;
    std::map<std::string, Op> ops;
    std::map<std::string, std::string> aliases;            // dotless name -> op (Orca)
    std::vector<std::pair<std::string, std::string>> docs;   // ordered for reference()

    double vars[11] { 1, 2, 3, 4, 0, 0, 0, 0, 0, 0, 0 };     // A B C D X Y Z T I J K
    std::vector<std::string> scripts[kScripts + 1];
    bool muted[kScripts + 1] {};
    double lastRun[kScripts + 1] {};
    Pattern pat[4];
    int patN = 0;
    double metroMs = 500.0, nextMetro = 0.0, t0 = 0.0, lastNow = 0.0;
    bool metroOn = false;
    uint32_t metroCount = 0;
    std::vector<Delayed> delayed;
    std::map<std::string, long> everyCount;
    std::mt19937 rng { 0x5EA1u };
    double chVel[16], chLenMs[16];
    double drunk = 0.0, counterO = 0.0;

    // PI.* (Pilot as a Disting-EX-style instrument)
    int piCh = 0, piPresetLoaded = 0;
    double piVel[16];
    int voxNote[16];
    std::vector<PiParam> piParams;

    Scene scenes[kScenes];
    std::string presets[kScenes];   // PI.SAVE / PI.PRESET sound snapshots
    int scene = 0;
};
} // namespace sf
