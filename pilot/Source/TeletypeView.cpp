#include "TeletypeView.h"
#include "TeletypeHost.h"
#include "BinaryData.h"

extern "C"
{
#include "tt_host.h"
}

using namespace ui;
using sf::TeletypeHost;

namespace
{
// USB HID usage codes (teletype/shim/usb_protocol_hid.h)
enum : uint8_t
{
    kHidA = 0x04, kHid1 = 0x1E, kHid0 = 0x27, kHidEnter = 0x28, kHidEsc = 0x29, kHidBackspace = 0x2A, kHidTab = 0x2B,
    kHidSpace = 0x2C, kHidMinus = 0x2D, kHidEqual = 0x2E, kHidBracketL = 0x2F, kHidBracketR = 0x30, kHidBackslash = 0x31,
    kHidSemicolon = 0x33, kHidQuote = 0x34, kHidGrave = 0x35, kHidComma = 0x36, kHidDot = 0x37, kHidSlash = 0x38,
    kHidF1 = 0x3A, kHidPrintScreen = 0x46, kHidInsert = 0x49, kHidHome = 0x4A, kHidPageUp = 0x4B, kHidDelete = 0x4C,
    kHidEnd = 0x4D, kHidPageDown = 0x4E, kHidRight = 0x4F, kHidLeft = 0x50, kHidDown = 0x51, kHidUp = 0x52,
    kHidNumLock = 0x53, kHidKeypad1 = 0x59, kHidKeypad0 = 0x62
};
enum : uint8_t { kModCtrl = 0x01, kModShift = 0x02, kModAlt = 0x04, kModWin = 0x08 };

// a character -> (key, needs shift), US layout
bool hidForChar (juce::juce_wchar c, uint8_t& hid, bool& shift)
{
    shift = false;
    if (c >= 'a' && c <= 'z') { hid = (uint8_t) (kHidA + (c - 'a')); return true; }
    if (c >= 'A' && c <= 'Z') { hid = (uint8_t) (kHidA + (c - 'A')); shift = true; return true; }
    if (c >= '1' && c <= '9') { hid = (uint8_t) (kHid1 + (c - '1')); return true; }
    if (c == '0') { hid = kHid0; return true; }
    static const char* plain = " -=[]\\;'`,./";
    static const uint8_t plainHid[] = { kHidSpace, kHidMinus, kHidEqual, kHidBracketL, kHidBracketR, kHidBackslash,
                                        kHidSemicolon, kHidQuote, kHidGrave, kHidComma, kHidDot, kHidSlash };
    for (int i = 0; plain[i]; ++i) if (c == (juce::juce_wchar) plain[i]) { hid = plainHid[i]; return true; }
    static const char* shifted = "!@#$%^&*()_+{}|:\"~<>?";
    static const char* base = "1234567890-=[]\\;'`,./";
    for (int i = 0; shifted[i]; ++i)
        if (c == (juce::juce_wchar) shifted[i]) { shift = true; return hidForChar ((juce::juce_wchar) base[i], hid, shift) && (shift = true); }
    return false;
}

// a JUCE key press -> HID key code. macOS reports Shift-1 as '!', letters as capitals:
// map back to the physical key, the firmware applies the shift itself.
uint8_t hidForKey (const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    using KP = juce::KeyPress;
    if (code == KP::returnKey) return kHidEnter;
    if (code == KP::escapeKey) return kHidEsc;
    if (code == KP::backspaceKey) return kHidBackspace;
    if (code == KP::tabKey) return kHidTab;
    if (code == KP::spaceKey) return kHidSpace;
    if (code == KP::deleteKey) return kHidDelete;
    if (code == KP::insertKey) return kHidInsert;
    if (code == KP::homeKey) return kHidHome;
    if (code == KP::endKey) return kHidEnd;
    if (code == KP::pageUpKey) return kHidPageUp;
    if (code == KP::pageDownKey) return kHidPageDown;
    if (code == KP::leftKey) return kHidLeft;
    if (code == KP::rightKey) return kHidRight;
    if (code == KP::upKey) return kHidUp;
    if (code == KP::downKey) return kHidDown;
    for (int i = 0; i < 12; ++i) if (code == KP::F1Key + i) return (uint8_t) (kHidF1 + i);
    if (code == KP::F13Key) return kHidPrintScreen;
    for (int i = 1; i <= 9; ++i) if (code == KP::numberPad0 + i) return (uint8_t) (kHidKeypad1 + i - 1);
    if (code == KP::numberPad0) return kHidKeypad0;
    uint8_t hid; bool shift;
    if (code > 0 && code < 128 && hidForChar ((juce::juce_wchar) std::tolower (code), hid, shift)) return hid;
    if (code > 0 && code < 128 && hidForChar ((juce::juce_wchar) code, hid, shift)) return hid;
    return 0;
}

// macOS: Cmd is Teletype's "win" key and Pilot's shortcut key. Windows / Linux: JUCE reports Ctrl as the
// command key, so Ctrl stays Teletype's ctrl and Pilot's shortcuts inside the Teletype screen are Ctrl+Shift.
bool winDown (const juce::ModifierKeys& m)
{
   #if JUCE_MAC
    return m.isCommandDown();
   #else
    (void) m; return false;
   #endif
}
bool pilotShortcut (const juce::ModifierKeys& m)
{
   #if JUCE_MAC
    return m.isCommandDown();
   #else
    return m.isCtrlDown() && m.isShiftDown();
   #endif
}

uint8_t hidMods (const juce::ModifierKeys& m)
{
    return (uint8_t) ((m.isCtrlDown() ? kModCtrl : 0) | (m.isShiftDown() ? kModShift : 0)
                      | (m.isAltDown() ? kModAlt : 0) | (winDown (m) ? kModWin : 0));
}
} // namespace

//==============================================================================
namespace
{
constexpr float kPad = 30.0f, kLine = 15.0f, kFontPx = 11.0f;
double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
}

TeletypeView::TeletypeView (PilotAudioProcessor& p) : proc (p)
{
    auto reg = juce::Typeface::createSystemTypefaceFor (BinaryData::input_mono_regular_ttf, BinaryData::input_mono_regular_ttfSize);
    font = juce::Font (juce::FontOptions (reg).withPointHeight (kFontPx));
    charW = juce::GlyphArrangement::getStringWidth (font, "0");
    setWantsKeyboardFocus (true);
    setOpaque (true);
    TeletypeHost::get().refresh (frame);
    startTimerHz (30);
}

TeletypeView::~TeletypeView() { releaseHeld(); }

juce::Colour TeletypeView::level (int l) const
{
    if (l >= 12) return theme.f_high;
    if (l >= 5) return theme.f_med;
    return theme.f_low;
}

void TeletypeView::timerCallback()
{
    TeletypeHost::get().refresh (frame);
    repaint();   // the cursor blinks
}

void TeletypeView::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    g.addTransform (juce::AffineTransform::scale (proc.ttZoom));
    g.setFont (font);

    // OLED x (0-127) -> the full width between Pilot's margins; each 8px OLED line -> one 15px row
    const float width = (float) getWidth() / proc.ttZoom - 2.0f * kPad;
    const float sx = width / 128.0f;
    auto rowY = [] (int l) { return kPad + (float) l * kLine; };
    const bool blinkOn = std::fmod (nowMs(), 1000.0) < 600.0 || ! hasKeyboardFocus (true);

    tt_seg_t segs[48];
    uint8_t mask[128 * 64];
    std::memcpy (mask, TeletypeHost::get().textMask(), sizeof (mask));

    // Live mode's activity icons (live_mode.c refresh_activities: script trigger / mute marks at
    // x 87-95, slew 98, delay 106, stack 114, metro 122 on the top line) as text in Pilot's font:
    // "12345678 / D S M", each dim until active, like the icons.
    const bool icons = TeletypeHost::get().mode() == 0 && TeletypeHost::get().liveSubmode() != 3;   // not full grid
    if (icons)
    {
        juce::String str;
        std::vector<int> lv;
        for (int i = 0; i < 8; ++i)
        {
            const int y = (i % 2) ? 3 : 1;
            const int v = std::max (frame[y * 128 + 87 + i], frame[y * 128 + 88 + i]);
            if (v > 0) { str << juce::String (i + 1); lv.push_back (v); }
        }
        const int iconX[4] = { 98 + 4, 106, 114, 122 };   // a pixel each icon always lights
        const char* iconCh = "/DSM";
        for (int i = 0; i < 4; ++i)
        {
            str << " " << juce::String::charToString ((juce::juce_wchar) iconCh[i]);
            lv.push_back (0);
            lv.push_back (frame[iconX[i]]);
        }
        const float right = kPad + width;
        float x = right - (float) str.length() * charW;
        for (int i = 0; i < str.length(); ++i, x += charW)
        {
            if (str[i] == ' ') continue;
            g.setColour (level (lv[(size_t) i]));
            g.drawText (str.substring (i, i + 1), juce::Rectangle<float> (x, rowY (0), charW + 4.0f, kLine), juce::Justification::centredLeft, false);
        }
        for (int y = 0; y < 8; ++y)
            for (int px = 85; px < 128; ++px) mask[y * 128 + px] = 1;   // no pixel icons
    }
    for (int l = 0; l < 8; ++l)
    {
        int n = 0;
        uint8_t bg = 0;
        TeletypeHost::get().screenLine (l, segs, 48, n, bg);
        if (bg > 0)
        {
            g.setColour (theme.b_med);
            g.fillRect (juce::Rectangle<float> (kPad - 4.0f, rowY (l) + 1.0f, width + 8.0f, kLine - 2.0f));
        }
        // graphics: pixels the firmware drew that are not text (icons, pattern markers, grid)
        for (int y = l * 8; y < l * 8 + 8; ++y)
            for (int x = 0; x < 128; ++x)
            {
                const int v = frame[y * 128 + x];
                if (v == bg || mask[y * 128 + x]) continue;
                g.setColour (v == 0 ? theme.background : level (v));
                g.fillRect (juce::Rectangle<float> (kPad + (float) x * sx, rowY (l) + (float) (y - l * 8) * kLine / 8.0f, sx, kLine / 8.0f + 0.5f));
            }
        // text, in Pilot's font
        for (int i = 0; i < n; ++i)
        {
            const auto& s = segs[i];
            const juce::String text (s.text);
            const float tw = (float) text.length() * charW;
            float x0 = kPad + (float) s.x * sx;
            if (s.right) x0 = kPad + (float) (s.x + s.w) * sx - tw;
            if (s.bg > bg)
            {
                g.setColour (theme.b_med);
                g.fillRect (juce::Rectangle<float> (x0 - 1.0f, rowY (l) + 1.0f, tw + 2.0f, kLine - 2.0f));
            }
            // runs of characters with the same brightness
            for (int a = 0; a < text.length();)
            {
                int b = a + 1;
                while (b < text.length() && s.cfg[b] == s.cfg[a]) ++b;
                g.setColour (level (s.cfg[a]));
                g.drawText (text.substring (a, b), juce::Rectangle<float> (x0 + (float) a * charW, rowY (l), (float) (b - a) * charW + 4.0f, kLine),
                            juce::Justification::centredLeft, false);
                a = b;
            }
            if (s.hi >= 0 && s.hi < text.length() && blinkOn)
            {
                // the line editor's cursor: an inverted block, like Pilot's command line
                const float cx = x0 + (float) s.hi * charW;
                g.setColour (theme.b_inv);
                g.fillRect (juce::Rectangle<float> (cx, rowY (l) + 1.0f, charW, kLine - 2.0f));
                g.setColour (theme.f_inv);
                g.drawText (text.substring (s.hi, s.hi + 1), juce::Rectangle<float> (cx, rowY (l), charW + 4.0f, kLine), juce::Justification::centredLeft, false);
            }
        }
    }
}

//==============================================================================
void TeletypeView::typeText (const juce::String& text)
{
    auto& tt = TeletypeHost::get();
    for (auto c : text)
    {
        if (c == '\r') continue;
        if (c == '\n') { tt.key (kHidEnter, 0, false); continue; }
        uint8_t hid; bool shift;
        if (c == '\t') c = ' ';
        if (hidForChar (c, hid, shift)) tt.key (hid, shift ? kModShift : 0, false);
    }
}

void TeletypeView::releaseHeld()
{
    if (heldHid != 0) TeletypeHost::get().keyRelease (heldHid, heldMod);
    heldHid = 0; heldJuceKey = 0; heldMod = 0;
}

bool TeletypeView::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    const int code = k.getKeyCode();
    auto zoomTo = [this] (float z) { proc.ttZoom = juce::jlimit (0.5f, 4.0f, z); if (onZoomChanged) onZoomChanged(); };

    // Pilot window shortcuts
    if (pilotShortcut (m))
    {
        const int lc = std::tolower (code);
        if (lc == 't') { if (onToggle) onToggle(); return true; }
        if (code == '=' || code == '+') { zoomTo (proc.ttZoom + 0.25f); return true; }
        if (code == '-') { zoomTo (proc.ttZoom - 0.25f); return true; }
        if (code == '0') { zoomTo (1.0f); return true; }
        if (lc == 'r') { proc.toggleRecording(); return true; }
        if (lc == 'v') { typeText (juce::SystemClipboard::getTextFromClipboard()); return true; }
       #if ! JUCE_MAC
        if (code == juce::KeyPress::backspaceKey)   // Windows / Linux: Teletype's win-Esc (clear delays, stack, slews)
        {
            releaseHeld();
            TeletypeHost::get().key (kHidEsc, kModWin, false);
            TeletypeHost::get().keyRelease (kHidEsc, kModWin);
            TeletypeHost::get().refresh (frame);
            repaint();
            return true;
        }
       #endif
        if (code != juce::KeyPress::escapeKey) return false;   // other Cmd shortcuts belong to Pilot
    }

    // Pilot: Ctrl-H opens / closes help, like Option-H
    if (m.isCtrlDown() && ! m.isAltDown() && ! m.isShiftDown() && ! winDown (m) && std::tolower (code) == 'h')
    { toggleHelp(); return true; }

    const uint8_t hid = hidForKey (k);
    if (hid == 0) return ! pilotShortcut (m);
    const uint8_t mod = hidMods (m);
    const bool held = heldJuceKey == code && heldHid == hid && juce::KeyPress::isKeyCurrentlyDown (code);
    if (! held && heldHid != 0 && heldHid != hid) releaseHeld();
    heldJuceKey = code; heldHid = hid; heldMod = mod;
    TeletypeHost::get().key (hid, mod, held);
    TeletypeHost::get().refresh (frame);
    repaint();
    return true;
}

void TeletypeView::toggleHelp()
{
    releaseHeld();
    uint8_t hid; bool shift;
    if (! hidForChar ('h', hid, shift)) return;
    TeletypeHost::get().key (hid, kModAlt, false);
    TeletypeHost::get().keyRelease (hid, kModAlt);
    TeletypeHost::get().refresh (frame);
    repaint();
}

bool TeletypeView::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown && heldJuceKey != 0 && ! juce::KeyPress::isKeyCurrentlyDown (heldJuceKey)) releaseHeld();
    return false;
}
