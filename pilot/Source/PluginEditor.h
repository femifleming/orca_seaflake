#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

// The interface reproduces Pilot's look exactly: black, Input Mono 11px / 15px lines,
// uppercase hex readouts, tiny per-channel waveforms, '•' recorder and a bare command input.
namespace ui
{
struct Theme
{
    juce::Colour background { 0xff000000 }, f_high { 0xffffffff }, f_med { 0xff777777 }, f_low { 0xff444444 }, f_inv { 0xff000000 },
                 b_high { 0xffeeeeee }, b_med { 0xff333333 }, b_low { 0xff444444 }, b_inv { 0xffffffff };
    static bool parse (const juce::String& text, Theme& out);   // Ecosystem theme (.svg or .json)
};

class TeletypeView;

class CommandLine : public juce::TextEditor
{
public:
    std::function<void (const juce::String&)> onCommand;
    std::function<bool (const juce::KeyPress&)> onShortcut;
    bool keyPressed (const juce::KeyPress& k) override;
private:
    juce::StringArray history;
    int idx = 0;
};
} // namespace ui

class PilotAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  public juce::FileDragAndDropTarget,
                                  private juce::Timer
{
public:
    explicit PilotAudioProcessorEditor (PilotAudioProcessor&);
    ~PilotAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    // Pilot menu actions
    void toggleRecording();
    void stopRecording();
    void resetMixer();
    void modZoom (float mod, bool set = false);
    void toggleAnimations();
    void openTheme();
    void resetTheme();
    bool handleShortcut (const juce::KeyPress&);
    void toggleTeletype();

    // hooks provided by the standalone app
    static inline std::function<void()> onShowAudioSettings;
    static inline std::function<void()> onToggleFullscreen;
    static inline std::function<void()> onContextMenu;   // Windows / Linux standalone: Pilot's menus

private:
    void timerCallback() override;
    int pilotHeight() const;
    int teletypeHeight() const;
    void applyTheme (const juce::String& data);
    void drawSpan (juce::Graphics&, float& x, float y, const juce::String& text, const juce::Font& f, juce::Colour c);
    void drawCanvas (juce::Graphics&, float x, float y, const float* values, float level, float activity);
    juce::Point<float> fxOrigin (int fx) const;
    juce::Rectangle<float> fxValueChar (int fx, int which) const;
    juce::Rectangle<float> channelIdRect (int ch) const;
    juce::RangedAudioParameter* paramAt (juce::Point<float> p, int& fxOut, bool& isValue) const;

    PilotAudioProcessor& proc;
    ui::Theme theme;
    juce::Font regular, medium;
    float charW = 6.6f;

    ui::CommandLine commandLine;
    std::unique_ptr<ui::TeletypeView> teletype;   // docked under the Pilot screen, same window
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ComponentDragger dragger;
    bool draggingWindow = false;
    juce::RangedAudioParameter* dragParam = nullptr;
    float dragStart = 0.0f;

    // display state (frozen when animations are off, like Pilot)
    float chWave[16][256] {}, fxWave[sf::kNumFx][256] {};
    float chLevel[16] {}, fxLevel[sf::kNumFx] {};
    uint32_t lastNoteCount[16] {};
    double lastNoteMs[16] {};
    sf::ChannelSettings settingsCache[16];
    float fxW[sf::kNumFx] {}, fxV[sf::kNumFx] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PilotAudioProcessorEditor)
};
