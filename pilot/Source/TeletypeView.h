// Pilot Sealflake — Teletype screen (docked under Pilot, in the same window)
//
// This is monome Teletype's own firmware (teletype/, via sf::TeletypeHost): its live, edit,
// pattern, scene read / write and help screens. The text the firmware draws on its 128x64 OLED
// is shown in Pilot / Orca style (theme colours, Input Mono 11px, 15px lines, eight rows); the
// few non-text graphics (activity icons, pattern markers, grid) are drawn as scaled pixels. Keys go to the firmware as USB keyboard codes, exactly
// like a keyboard plugged into Teletype: Ctrl = ctrl, Option = alt, Shift = shift, Cmd = win.
// Pilot window shortcuts that stay here: Cmd+T close, Cmd+= / Cmd+- / Cmd+0 zoom,
// Cmd+R record, Ctrl+H help (like Option-H; replaces Teletype's Ctrl-H = backspace), Cmd+V types the clipboard into Teletype (one line per Enter).
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginEditor.h"

namespace ui
{
class TeletypeView : public juce::Component, private juce::Timer
{
public:
    explicit TeletypeView (PilotAudioProcessor&);
    ~TeletypeView() override;

    static constexpr int kBaseW = 445, kBaseH = 180;   // 8 rows of 15px + Pilot's 30px margins
    void setTheme (const Theme& t) { theme = t; repaint(); }
    float zoom() const { return proc.ttZoom; }
    void toggleHelp();   // same as Option-H on the module (Pilot: also Ctrl-H)

    std::function<void()> onZoomChanged, onToggle;

    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }
    void focusLost (FocusChangeType) override { releaseHeld(); }

private:
    void timerCallback() override;
    juce::Colour level (int oledLevel) const;   // OLED brightness 0-15 -> theme colour
    void releaseHeld();
    void typeText (const juce::String&);

    PilotAudioProcessor& proc;
    Theme theme;
    juce::Font font { juce::FontOptions() };
    float charW = 6.6f;
    uint8_t frame[128 * 64] {};

    int heldJuceKey = 0;
    uint8_t heldHid = 0, heldMod = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TeletypeView)
};

} // namespace ui
