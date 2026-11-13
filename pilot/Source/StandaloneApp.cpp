// Custom standalone app: frameless black window like Pilot's Electron app (frame: false on macOS),
// with Pilot's menus in the macOS menu bar (right-click menu on Windows / Linux).
#include "PluginEditor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/juce_audio_plugin_client.h>
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

using namespace juce;

class PilotWindow final : public ResizableWindow
{
public:
    explicit PilotWindow (StandalonePluginHolder& h)
        : ResizableWindow ("Pilot Sealflake", Colours::black, false), holder (h)
    {
        setUsingNativeTitleBar (false);
        auto* ed = holder.processor->createEditorIfNeeded();
        setContentOwned (ed, true);
        addToDesktop (getDesktopWindowStyleFlags());
        setResizable (true, false);
        setResizeLimits (200, 190, 4000, 3000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        toFront (true);
    }

    ~PilotWindow() override { clearContentComponent(); }

    int getDesktopWindowStyleFlags() const override
    {
        return ComponentPeer::windowAppearsOnTaskbar | ComponentPeer::windowHasDropShadow | ComponentPeer::windowIsResizable;
    }
    BorderSize<int> getBorderThickness() const override { return {}; }
    BorderSize<int> getContentComponentBorder() const override { return {}; }
    void userTriedToCloseWindow() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

    PilotAudioProcessorEditor* editor() { return dynamic_cast<PilotAudioProcessorEditor*> (getContentComponent()); }

private:
    StandalonePluginHolder& holder;
};

//==============================================================================
class PilotMenu final : public MenuBarModel
{
public:
    std::function<PilotAudioProcessorEditor*()> getEditor;
    std::function<void()> showSettings, fullscreen;

    StringArray getMenuBarNames() override { return { "File", "Edit", "View", "Theme" }; }

    PopupMenu getMenuForIndex (int, const String& name) override
    {
        PopupMenu m;
        if (name == "File")
        {
            m.addItem (1, "Record  (Cmd+R)");
            m.addItem (2, "Stop Recording  (Esc)");
            m.addSeparator();
            m.addItem (3, "Reset  (Cmd+Backspace)");
        }
        else if (name == "View")
        {
            m.addItem (10, "Zoom In  (Cmd+=)");
            m.addItem (11, "Zoom Out  (Cmd+-)");
            m.addItem (12, "Zoom Reset  (Cmd+0)");
            m.addItem (13, "Toggle Animations  (`)");
            m.addSeparator();
            m.addItem (14, "Fullscreen  (Cmd+Enter)");
            m.addSeparator();
            m.addItem (15, "Teletype  (Cmd+T)");
        }
        else if (name == "Theme")
        {
            m.addItem (20, "Open Theme  (Cmd+Shift+O)");
            m.addItem (21, "Reset Theme  (Cmd+Shift+Backspace)");
            m.addItem (22, "Download Themes..");
        }
        return m;
    }

    void menuItemSelected (int id, int) override
    {
        if (id == 99) { if (showSettings) showSettings(); return; }
        if (id == 98) { URL ("https://github.com/hundredrabbits/Pilot").launchInDefaultBrowser(); return; }
        if (id == 22) { URL ("https://github.com/hundredrabbits/Themes").launchInDefaultBrowser(); return; }
        if (id == 14) { if (fullscreen) fullscreen(); return; }
        auto* e = getEditor ? getEditor() : nullptr;
        if (e == nullptr) return;
        switch (id)
        {
            case 1: e->toggleRecording(); break;
            case 2: e->stopRecording(); break;
            case 3: e->resetMixer(); break;
            case 10: e->modZoom (0.25f); break;
            case 11: e->modZoom (-0.25f); break;
            case 12: e->modZoom (1.0f, true); break;
            case 13: e->toggleAnimations(); break;
            case 15: e->toggleTeletype(); break;
            case 20: e->openTheme(); break;
            case 21: e->resetTheme(); break;
            default: break;
        }
    }
};

//==============================================================================
class PilotApp final : public JUCEApplication
{
public:
    PilotApp()
    {
        PropertiesFile::Options o;
        o.applicationName = "Pilot Sealflake";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        props.setStorageParameters (o);
    }

    const String getApplicationName() override    { return "Pilot Sealflake"; }
    const String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override    { return false; }

    void initialise (const String&) override
    {
        holder = std::make_unique<StandalonePluginHolder> (props.getUserSettings(), false, String{}, nullptr,
                                                           Array<StandalonePluginHolder::PluginInOuts>(), false);
        window = std::make_unique<PilotWindow> (*holder);

        PilotAudioProcessorEditor::onShowAudioSettings = [this] { holder->showAudioSettingsDialog(); };
        PilotAudioProcessorEditor::onToggleFullscreen = [this]
        {
            auto& d = Desktop::getInstance();
            d.setKioskModeComponent (d.getKioskModeComponent() == nullptr ? window.get() : nullptr, false);
        };

        menu.getEditor = [this] { return window ? window->editor() : nullptr; };
        menu.showSettings = PilotAudioProcessorEditor::onShowAudioSettings;
        menu.fullscreen = PilotAudioProcessorEditor::onToggleFullscreen;
        PopupMenu extra;
        extra.addItem (98, "About Pilot");
        extra.addSeparator();
        extra.addItem (99, "Audio/MIDI Settings...  (Cmd+,)");
       #if JUCE_MAC
        MenuBarModel::setMacMainMenu (&menu, &extra);
       #else
        // Windows / Linux: the frameless window has no menu bar, so Pilot's menus open on right-click
        PilotAudioProcessorEditor::onContextMenu = [this]
        {
            PopupMenu all;
            for (auto& name : menu.getMenuBarNames()) all.addSubMenu (name, menu.getMenuForIndex (0, name));
            all.addSeparator();
            all.addItem (99, "Audio/MIDI Settings...  (Ctrl+,)");
            all.addItem (98, "About Pilot");
            all.addSeparator();
            all.addItem (97, "Quit");
            all.showMenuAsync (PopupMenu::Options(), [this] (int id)
            {
                if (id == 97) { systemRequestedQuit(); return; }
                if (id > 0) menu.menuItemSelected (id, 0);
            });
        };
       #endif
    }

    void shutdown() override
    {
       #if JUCE_MAC
        MenuBarModel::setMacMainMenu (nullptr);
       #endif
        PilotAudioProcessorEditor::onContextMenu = nullptr;
        PilotAudioProcessorEditor::onShowAudioSettings = nullptr;
        PilotAudioProcessorEditor::onToggleFullscreen = nullptr;
        if (holder) holder->savePluginState();
        window = nullptr;
        holder = nullptr;
        props.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (holder) holder->savePluginState();
        quit();
    }

    void anotherInstanceStarted (const String&) override {}

private:
    ApplicationProperties props;
    std::unique_ptr<StandalonePluginHolder> holder;
    std::unique_ptr<PilotWindow> window;
    PilotMenu menu;
};

JUCEApplicationBase* juce_CreateApplication();
JUCEApplicationBase* juce_CreateApplication() { return new PilotApp(); }
