#include "PluginEditor.h"
#include "TeletypeView.h"
#include "BinaryData.h"

using namespace sf;
using namespace ui;

namespace
{
// Pilot geometry (desktop/sources/links/main.css)
constexpr float kPad = 30.0f, kLine = 15.0f, kSpanPad = 2.55f, kFontPx = 11.0f;
constexpr int kWinW = 445, kWinH = 210;
// effect columns, exactly as Pilot positions them (#chbit..#chrev at 220px, #chpha..#chlim at 300px)
const char* const kFxColA[8] = { "bit", "dis", "wah", "che", "fee", "del", "tre", "rev" };
const char* const kFxColB[8] = { "pha", "vib", "cho", "ste", "equ", "com", "vol", "lim" };

// Pilot's to16(): Math.floor(float * 15) in base 36
juce::String hex1 (float v) { return juce::String::charToString (str36 ((int) std::floor (clamp (v, 0.0f, 1.0f) * 15.0f + 1.0e-3f))).toUpperCase(); }

juce::Colour parseColour (juce::String s)
{
    s = s.trim().trimCharactersAtStart ("#");
    if (s.length() == 3) s = juce::String::charToString (s[0]) + s[0] + s[1] + s[1] + s[2] + s[2];
    if (s.length() != 6 || ! s.containsOnly ("0123456789abcdefABCDEF")) return {};
    return juce::Colour::fromString ("ff" + s);
}

double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
}

//==============================================================================
bool Theme::parse (const juce::String& text, Theme& out)
{
    static const char* keys[] = { "background", "f_high", "f_med", "f_low", "f_inv", "b_high", "b_med", "b_low", "b_inv" };
    juce::StringPairArray found;
    const auto t = text.trim();
    if (t.startsWith ("{"))
    {
        auto v = juce::JSON::parse (t);
        auto data = v.getProperty ("data", juce::var());
        for (auto* src : { data.getDynamicObject(), v.getDynamicObject() })
            if (src != nullptr && found.size() == 0)
                for (auto* k : keys) if (src->hasProperty (k)) found.set (k, src->getProperty (k).toString());
    }
    else if (auto xml = juce::XmlDocument::parse (t))
    {
        std::function<void (const juce::XmlElement&)> walk = [&] (const juce::XmlElement& e)
        {
            const auto id = e.getStringAttribute ("id");
            for (auto* k : keys) if (id == k && e.hasAttribute ("fill")) found.set (k, e.getStringAttribute ("fill"));
            for (auto* c : e.getChildIterator()) walk (*c);
        };
        walk (*xml);
    }
    for (auto* k : keys) if (! found.containsKey (k)) return false;
    Theme th;
    juce::Colour* dst[] = { &th.background, &th.f_high, &th.f_med, &th.f_low, &th.f_inv, &th.b_high, &th.b_med, &th.b_low, &th.b_inv };
    for (int i = 0; i < 9; ++i)
    {
        auto c = parseColour (found[keys[i]]);
        if (c.isTransparent()) return false;
        *dst[i] = c;
    }
    out = th;
    return true;
}

bool CommandLine::keyPressed (const juce::KeyPress& k)
{
    if (onShortcut && onShortcut (k)) return true;
    if (k == juce::KeyPress::returnKey)
    {
        const auto t = getText();
        history.add (t);
        idx = history.size();
        if (onCommand && t.isNotEmpty()) onCommand (t);
        clear();
        return true;
    }
    if (k == juce::KeyPress::upKey)
    {
        if (history.size() > 0 && idx > 0) setText (history[--idx], false);
        return true;
    }
    if (k == juce::KeyPress::downKey)
    {
        if (idx < history.size() - 1) setText (history[++idx], false);
        else { idx = history.size(); clear(); }
        return true;
    }
    return TextEditor::keyPressed (k);
}

//==============================================================================
PilotAudioProcessorEditor::PilotAudioProcessorEditor (PilotAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    auto reg = juce::Typeface::createSystemTypefaceFor (BinaryData::input_mono_regular_ttf, BinaryData::input_mono_regular_ttfSize);
    auto med = juce::Typeface::createSystemTypefaceFor (BinaryData::input_mono_medium_ttf, BinaryData::input_mono_medium_ttfSize);
    regular = juce::Font (juce::FontOptions (reg).withPointHeight (kFontPx));
    medium  = juce::Font (juce::FontOptions (med).withPointHeight (kFontPx));
    charW = juce::GlyphArrangement::getStringWidth (regular, "0");

    commandLine.setFont (regular);
    commandLine.setIndents (3, 1);
    commandLine.setBorder ({});
    commandLine.setScrollbarsShown (false);
    commandLine.setPopupMenuEnabled (false);
    commandLine.setEscapeAndReturnKeysConsumed (false);
    commandLine.onCommand = [this] (const juce::String& t)
    {
        if (t.trim().equalsIgnoreCase ("teletype") || t.trim().equalsIgnoreCase ("tt")) { toggleTeletype(); return; }
        // like Teletype's live screen: the result (or error) of the last line, dimmed in the input
        commandLine.setTextToShowWhenEmpty (proc.submitCommand (t), theme.f_low);
    };
    commandLine.onShortcut = [this] (const juce::KeyPress& k) { return handleShortcut (k); };
    addAndMakeVisible (commandLine);

    applyTheme (proc.themeData);
    for (int ch = 0; ch < 16; ++ch) { settingsCache[ch] = proc.engine.getSettings (ch); lastNoteCount[ch] = proc.engine.channelNotes (ch); }
    for (int i = 0; i < kNumFx; ++i) { fxW[i] = proc.engine.fxW[i].load(); fxV[i] = proc.engine.fxV[i].load(); }

    setWantsKeyboardFocus (true);
    setResizable (true, false);
    setResizeLimits (200, 190, 4000, 3000);
    setSize (kWinW, kWinH);
    startTimer (30);   // Pilot redraws at most every 30 ms
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<juce::Component> (&commandLine)] { if (sp) sp->grabKeyboardFocus(); });
    if (proc.ttOpen)
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<PilotAudioProcessorEditor> (this)] { if (sp && ! sp->teletype) sp->toggleTeletype(); });
}

PilotAudioProcessorEditor::~PilotAudioProcessorEditor() { teletype.reset(); }

int PilotAudioProcessorEditor::pilotHeight() const { return juce::roundToInt ((float) kWinH * proc.zoom); }
int PilotAudioProcessorEditor::teletypeHeight() const { return juce::roundToInt ((float) TeletypeView::kBaseH * proc.ttZoom); }

void PilotAudioProcessorEditor::toggleTeletype()
{
    if (teletype)
    {
        const int h = juce::jmax (pilotHeight(), getHeight() - teletype->getHeight() - 1);
        removeChildComponent (teletype.get());
        teletype.reset();
        proc.ttOpen = false;
        setResizeLimits (200, 190, 4000, 3000);
        setSize (getWidth(), h);
        commandLine.grabKeyboardFocus();
        return;
    }
    teletype = std::make_unique<TeletypeView> (proc);
    teletype->setTheme (theme);
    teletype->onToggle = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<PilotAudioProcessorEditor> (this)] { if (sp && sp->teletype) sp->toggleTeletype(); }); };
    teletype->onZoomChanged = [this] { setSize (getWidth(), pilotHeight() + 1 + teletypeHeight()); };
    addAndMakeVisible (*teletype);
    proc.ttOpen = true;
    setResizeLimits (200, 190 + 140, 4000, 3000);
    setSize (juce::jmax (getWidth(), juce::roundToInt ((float) TeletypeView::kBaseW * proc.ttZoom)), getHeight() + 1 + teletypeHeight());
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<juce::Component> (teletype.get())] { if (sp) sp->grabKeyboardFocus(); });
}

void PilotAudioProcessorEditor::applyTheme (const juce::String& data)
{
    Theme t;
    theme = (data.isNotEmpty() && Theme::parse (data, t)) ? t : Theme();
    commandLine.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    commandLine.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    commandLine.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    commandLine.setColour (juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
    commandLine.setColour (juce::TextEditor::textColourId, theme.f_high);
    commandLine.setColour (juce::TextEditor::highlightColourId, theme.b_med);
    commandLine.setColour (juce::TextEditor::highlightedTextColourId, theme.f_high);
    commandLine.setColour (juce::CaretComponent::caretColourId, theme.f_high);
    commandLine.applyColourToAllText (theme.f_high);
    if (juce::JUCEApplicationBase::isStandaloneApp())
        if (auto* w = dynamic_cast<juce::ResizableWindow*> (getTopLevelComponent())) w->setBackgroundColour (theme.background);
    if (teletype) teletype->setTheme (theme);
    repaint();
}

void PilotAudioProcessorEditor::resized()
{
    const float z = proc.zoom;
    // #commander { top: 11*15px; left: 55px }  input width: calc(100vw - 70px)
    const float w = juce::jmax (20.0f, (float) getWidth() / z - 70.0f);
    commandLine.setTransform ({});
    commandLine.setBounds (55, 165, (int) w, 15);
    commandLine.setTransform (juce::AffineTransform::scale (z));
    if (teletype)
    {
        const int top = juce::jmin (pilotHeight(), getHeight() - 1) + 1;   // 1px divider above the Teletype screen
        teletype->setBounds (0, top, getWidth(), juce::jmax (0, getHeight() - top));
    }
}

//==============================================================================
void PilotAudioProcessorEditor::timerCallback()
{
    if (proc.animate)
    {
        const double now = nowMs();
        for (int ch = 0; ch < 16; ++ch)
        {
            proc.engine.channelTap (ch).read (chWave[ch], 256);
            double sq = 0; for (float v : chWave[ch]) sq += (double) v * v;
            chLevel[ch] = juce::jmax ((float) std::sqrt (sq / 256.0), chLevel[ch] * 0.95f);   // Tone.Meter(0.95)
            const uint32_t n = proc.engine.channelNotes (ch);
            if (n != lastNoteCount[ch]) { lastNoteCount[ch] = n; lastNoteMs[ch] = now; }
            settingsCache[ch] = proc.engine.getSettings (ch);
        }
        for (int i = 0; i < kNumFx; ++i)
        {
            proc.engine.fxTap (i).read (fxWave[i], 256);
            double sq = 0; for (float v : fxWave[i]) sq += (double) v * v;
            fxLevel[i] = juce::jmax ((float) std::sqrt (sq / 256.0), fxLevel[i] * 0.95f);
            fxW[i] = proc.engine.fxW[i].load(); fxV[i] = proc.engine.fxV[i].load();
        }
    }
    repaint();
}

void PilotAudioProcessorEditor::drawSpan (juce::Graphics& g, float& x, float y, const juce::String& text, const juce::Font& f, juce::Colour c)
{
    x += kSpanPad;
    g.setFont (f);
    g.setColour (c);
    const float w = juce::GlyphArrangement::getStringWidth (f, text);
    g.drawText (text, juce::Rectangle<float> (x, y, w + 4.0f, kLine), juce::Justification::centredLeft, false);
    x += w + kSpanPad;
}

// Pilot's 52x26 canvas shown at 26x13 (interface.js): waveform every 17th of 256 samples + activity dot
void PilotAudioProcessorEditor::drawCanvas (juce::Graphics& g, float x, float y, const float* values, float level, float activity)
{
    constexpr float W = 52.0f, H = 26.0f;
    if (activity > 0.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (juce::jmin (1.0f, activity)));
        g.fillEllipse (x + 0.0f, y + 1.0f, 2.0f, 2.0f);
    }
    juce::Path p;
    const float y0 = std::floor (((values[0] + 1.0f) / 2.0f) * H);
    p.startNewSubPath (x, y + y0 * 0.5f);
    if (level > 0.01f)
    {
        for (int i = 1; i < 256; ++i)
        {
            if (i % 17 != 0) continue;
            const float px = clamp (std::floor (W * ((float) i / 256.0f)), 2.0f, W - 2.0f);
            const float py = clamp (std::floor (((values[i] + 1.0f) / 2.0f) * H), 2.0f, H - 2.0f);
            p.lineTo (x + px * 0.5f, y + py * 0.5f);
        }
    }
    p.lineTo (x + W * 0.5f, y + y0 * 0.5f);
    g.setColour (theme.f_high);
    g.strokePath (p, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
}

juce::Point<float> PilotAudioProcessorEditor::fxOrigin (int fx) const
{
    for (int r = 0; r < 8; ++r)
    {
        if (fxIndexOf (kFxColA[r]) == fx) return { kPad + 220.0f, kPad + (float) r * kLine };
        if (fxIndexOf (kFxColB[r]) == fx) return { kPad + 300.0f, kPad + (float) r * kLine };
    }
    return {};
}

juce::Rectangle<float> PilotAudioProcessorEditor::fxValueChar (int fx, int which) const
{
    const auto o = fxOrigin (fx);
    const float x = o.x + kSpanPad * 3.0f + 3.0f * charW + (float) which * charW;
    return { x, o.y, charW, kLine };
}

juce::Rectangle<float> PilotAudioProcessorEditor::channelIdRect (int ch) const
{
    return { kPad + (float) (ch / 8) * 110.0f, kPad + (float) (ch % 8) * kLine, kSpanPad * 2.0f + charW, kLine };
}

void PilotAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    if (teletype)
    {
        g.setColour (theme.b_med);
        g.fillRect (0, teletype->getY() - 1, getWidth(), 1);
    }
    g.addTransform (juce::AffineTransform::scale (proc.zoom));
    const double now = nowMs();

    // channels
    for (int ch = 0; ch < 16; ++ch)
    {
        const float x0 = kPad + (float) (ch / 8) * 110.0f, y0 = kPad + (float) (ch % 8) * kLine;
        const auto& s = settingsCache[ch];
        juce::String env, osc;
        if (s.engine == EngJF)   // Just Friends: TIME INTONE RAMP CURVE, then range + mode (SNSU = sound / sustain)
        {
            for (float v : { s.jf.time, s.jf.intone, s.jf.ramp, s.jf.curve }) env += hex1 (v);
            osc = juce::String (jfRangeCode (s.jf.range)) + jfModeCode (s.jf.mode);
        }
        else
        {
            for (float v : s.env) env += hex1 (v);
            osc = juce::String (s.wave1 >= 0 ? kWaveCodes[s.wave1] : "--") + (s.wave2 >= 0 ? kWaveCodes[s.wave2] : "--");
        }
        float x = x0;
        drawSpan (g, x, y0, juce::String::charToString (str36 (ch)).toUpperCase(), medium, theme.f_low);
        drawSpan (g, x, y0, env.toUpperCase(), regular, theme.f_high);
        drawSpan (g, x, y0, osc.toUpperCase(), regular, theme.f_high);
        const float act = lastNoteMs[ch] > 0 ? (float) (1.0 - (now - lastNoteMs[ch]) / 500.0) : 0.0f;
        drawCanvas (g, x0 + 95.0f + 10.0f - 26.0f, y0 + 0.5f, chWave[ch], chLevel[ch], act);
    }

    // effects
    for (int i = 0; i < kNumFx; ++i)
    {
        const auto o = fxOrigin (i);
        float x = o.x;
        drawSpan (g, x, o.y, juce::String (kFxInfo[i].code).toUpperCase(), medium, theme.f_low);
        drawSpan (g, x, o.y, hex1 (fxW[i]) + hex1 (fxV[i]), regular, theme.f_high);
        drawCanvas (g, o.x + 65.0f + 10.0f - 26.0f, o.y + 0.5f, fxWave[i], fxLevel[i], 0.0f);
    }

    // recorder (#recorder: top 11*15px, left 30px, #333; blinks b_inv -> black while recording).
    // Pilot's setSpeed() writes the BPM into this element, so it shows the tempo (120 at start).
    juce::Colour rc (0xff333333);
    if (proc.isRecording())
    {
        const float t = (float) std::fmod (now, 500.0) / 500.0f;
        rc = theme.b_inv.interpolatedWith (juce::Colours::black, t);
    }
    g.setFont (medium);
    g.setColour (rc);
    g.drawText (juce::String (juce::roundToInt (proc.engine.effectiveBpm())), juce::Rectangle<float> (30.0f, 165.0f, 40.0f, kLine), juce::Justification::centredLeft, false);
}

//==============================================================================
juce::RangedAudioParameter* PilotAudioProcessorEditor::paramAt (juce::Point<float> p, int& fxOut, bool& isValue) const
{
    for (int i = 0; i < kNumFx; ++i)
        for (int which = 0; which < 2; ++which)
            if (fxValueChar (i, which).expanded (0.5f, 0.0f).contains (p))
            {
                fxOut = i; isValue = which == 1;
                return proc.apvts.getParameter (PilotAudioProcessor::fxParamId (i, isValue));
            }
    return nullptr;
}

void PilotAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onContextMenu) { onContextMenu(); return; }
    const auto p = e.position / proc.zoom;
    int fx; bool isV;
    if (auto* prm = paramAt (p, fx, isV))
    {
        dragParam = prm;
        dragStart = prm->getValue();
        prm->beginChangeGesture();
        return;
    }
    for (int ch = 0; ch < 16; ++ch)
        if (channelIdRect (ch).contains (p))
        {
            const auto s = proc.engine.getSettings (ch);
            const char oct = s.engine == EngKick || s.engine == EngTom ? '2' : s.engine == EngHat ? '5' : '4';
            proc.submitCommand (juce::String::charToString (str36 (ch)) + oct + "C");
            return;
        }
    if (juce::JUCEApplicationBase::isStandaloneApp())
        if (auto* top = getTopLevelComponent())
        {
            draggingWindow = true;
            dragger.startDraggingComponent (top, e.getEventRelativeTo (top));
        }
}

void PilotAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragParam != nullptr)
    {
        const int steps = (int) std::round (-(float) e.getDistanceFromDragStartY() / 6.0f);
        dragParam->setValueNotifyingHost (clamp (std::round (dragStart * 15.0f + (float) steps) / 15.0f, 0.0f, 1.0f));
        return;
    }
    if (draggingWindow)
        if (auto* top = getTopLevelComponent()) dragger.dragComponent (top, e.getEventRelativeTo (top), nullptr);
}

void PilotAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragParam != nullptr) { dragParam->endChangeGesture(); dragParam = nullptr; }
    draggingWindow = false;
    commandLine.grabKeyboardFocus();
}

void PilotAudioProcessorEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    int fx; bool isV;
    if (auto* prm = paramAt (e.position / proc.zoom, fx, isV))
        prm->setValueNotifyingHost (isV ? kFxInfo[fx].defV : kFxInfo[fx].defW);
}

void PilotAudioProcessorEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int fx; bool isV;
    if (auto* prm = paramAt (e.position / proc.zoom, fx, isV))
    {
        const float dir = w.deltaY > 0 ? 1.0f : (w.deltaY < 0 ? -1.0f : 0.0f);
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (clamp (std::round (prm->getValue() * 15.0f + dir) / 15.0f, 0.0f, 1.0f));
        prm->endChangeGesture();
    }
}

//==============================================================================
bool PilotAudioProcessorEditor::handleShortcut (const juce::KeyPress& k)
{
    const auto cmd = juce::ModifierKeys::commandModifier, shift = juce::ModifierKeys::shiftModifier;
    const int code = k.getKeyCode();
    const auto mods = k.getModifiers();
    if (k == juce::KeyPress ('r', cmd, 0))                         { toggleRecording(); return true; }
    if (k == juce::KeyPress::escapeKey)                            { stopRecording(); return true; }
    if (mods.isCommandDown() && ! mods.isShiftDown() && code == juce::KeyPress::backspaceKey) { resetMixer(); return true; }
    if (mods.isCommandDown() && mods.isShiftDown() && code == juce::KeyPress::backspaceKey)   { resetTheme(); return true; }
    if (k == juce::KeyPress ('o', cmd | shift, 0))                 { openTheme(); return true; }
    if (mods.isCommandDown() && (code == '=' || code == '+'))      { modZoom (0.25f); return true; }
    if (mods.isCommandDown() && code == '-')                       { modZoom (-0.25f); return true; }
    if (mods.isCommandDown() && code == '0')                       { modZoom (1.0f, true); return true; }
    if (k.getTextCharacter() == '`' && ! mods.isCommandDown())     { toggleAnimations(); return true; }
    if (mods.isCommandDown() && code == juce::KeyPress::returnKey) { if (onToggleFullscreen) onToggleFullscreen(); return true; }
    if (k == juce::KeyPress (',', cmd, 0))                         { if (onShowAudioSettings) onShowAudioSettings(); return true; }
    if (k == juce::KeyPress ('t', cmd, 0))                         { toggleTeletype(); return true; }
   #if JUCE_MAC
    const bool plainCtrl = mods.isCtrlDown() && ! mods.isCommandDown();
   #else
    const bool plainCtrl = mods.isCtrlDown();
   #endif
    if (plainCtrl && ! mods.isAltDown() && ! mods.isShiftDown() && std::tolower (code) == 'h')
    {   // help: open the Teletype screen on its help pages (page 18 = Pilot)
        if (! teletype) toggleTeletype();
        if (teletype) { teletype->toggleHelp(); teletype->grabKeyboardFocus(); }
        return true;
    }
    return false;
}

bool PilotAudioProcessorEditor::keyPressed (const juce::KeyPress& k) { return handleShortcut (k); }

void PilotAudioProcessorEditor::toggleRecording()
{
    const bool was = proc.isRecording();
    proc.toggleRecording();
    if (was && proc.lastRecording.existsAsFile()) proc.lastRecording.revealToUser();
}

void PilotAudioProcessorEditor::stopRecording() { if (proc.isRecording()) toggleRecording(); }
void PilotAudioProcessorEditor::resetMixer() { proc.submitCommand ("reset"); }

void PilotAudioProcessorEditor::modZoom (float mod, bool set)
{
    const int ttH = teletype ? teletype->getHeight() : 0;
    proc.zoom = clamp (set ? mod : proc.zoom + mod, 0.25f, 5.0f);
    if (teletype) setSize (getWidth(), pilotHeight() + 1 + ttH);   // keep the Teletype screen its size
    resized();
    repaint();
}

void PilotAudioProcessorEditor::toggleAnimations() { proc.animate = ! proc.animate; }

void PilotAudioProcessorEditor::openTheme()
{
    chooser = std::make_unique<juce::FileChooser> ("Open Theme", juce::File(), "*.svg;*.json");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) filesDropped ({ f.getFullPathName() }, 0, 0);
                          });
}

void PilotAudioProcessorEditor::resetTheme() { proc.themeData = {}; applyTheme ({}); }

bool PilotAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (f.endsWithIgnoreCase (".svg") || f.endsWithIgnoreCase (".json")) return true;
    return false;
}

void PilotAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
    {
        const auto text = juce::File (f).loadFileAsString();
        Theme t;
        if (Theme::parse (text, t)) { proc.themeData = text; applyTheme (text); return; }
    }
}
