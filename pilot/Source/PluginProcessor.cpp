#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Lang.h"
#include "TeletypeHost.h"

using namespace sf;

juce::AudioProcessorValueTreeState::ParameterLayout PilotAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < kNumFx; ++i)
    {
        const juce::String name (kFxInfo[i].name);
        auto hex = [] (float v, int) { return juce::String::charToString (to16 (v)) + "  (" + juce::String (v, 2) + ")"; };
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { fxParamId (i, false), 1 }, name + " " + kFxInfo[i].wLabel,
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f), kFxInfo[i].defW,
                                                                 juce::AudioParameterFloatAttributes().withStringFromValueFunction (hex)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { fxParamId (i, true), 1 }, name + " " + kFxInfo[i].vLabel,
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f), kFxInfo[i].defV,
                                                                 juce::AudioParameterFloatAttributes().withStringFromValueFunction (hex)));
    }
    return layout;
}

PilotAudioProcessor::PilotAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PilotSealflake", createLayout()),
      udp ([this] (const juce::String& s) { submitCommand (s); })
{
    for (int i = 0; i < kNumFx; ++i)
    {
        fxWParam[i] = apvts.getRawParameterValue (fxParamId (i, false));
        fxVParam[i] = apvts.getRawParameterValue (fxParamId (i, true));
    }
    engine.onPortCommand = [this] (int port) { pendingPort = port; triggerAsyncUpdate(); };
    engine.onRecord = [this] (bool on) { pendingRec = on ? 1 : 0; triggerAsyncUpdate(); };

    for (int i = 0; i < kNumFx; ++i) { pendW[i] = -1.0f; pendV[i] = -1.0f; }
    engine.onFxCommand = [this] (int fx, float w, float v) { applyFxCommand (fx, w, v); };

    writerThread.startThread();
    setUdp (true, 49161);
}

PilotAudioProcessor::~PilotAudioProcessor()
{
    udp.stop();
    engine.onFxCommand = nullptr;
    engine.onRecord = nullptr;
    if (isRecording()) toggleRecording();
    writerThread.stopThread (2000);
    cancelPendingUpdate();
}

// Effect commands from UDP / the command line are applied on the message thread,
// so the host sees them as normal parameter changes (automation-safe).
void PilotAudioProcessor::applyFxCommand (int fx, float w, float v)
{
    if (w >= 0) pendW[fx] = w;
    if (v >= 0) pendV[fx] = v;
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm != nullptr && mm->isThisTheMessageThread()) handleAsyncUpdate();
    else triggerAsyncUpdate();
}

void PilotAudioProcessor::handleAsyncUpdate()
{
    const int port = pendingPort.exchange (-1);
    if (port > 0) setUdp (true, port);
    const int rec = pendingRec.exchange (-1);
    if (rec >= 0 && (rec == 1) != isRecording()) toggleRecording();

    for (int i = 0; i < kNumFx; ++i)
    {
        const float w = pendW[i].exchange (-1.0f), v = pendV[i].exchange (-1.0f);
        if (w >= 0) if (auto* p = apvts.getParameter (fxParamId (i, false))) p->setValueNotifyingHost (w);
        if (v >= 0) if (auto* p = apvts.getParameter (fxParamId (i, true))) p->setValueNotifyingHost (v);
    }
}

juce::String PilotAudioProcessor::submitCommand (const juce::String& text)
{
    return juce::String (engine.submit (text.toStdString()));
}

bool PilotAudioProcessor::setUdp (bool enabled, int port)
{
    udpWanted = enabled;
    udpPortNumber = juce::jlimit (1, 65535, port);
    engine.udpPort = udpPortNumber;
    if (! enabled) { udp.stop(); return true; }
    return udp.start (udpPortNumber);
}

bool PilotAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void PilotAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    for (int i = 0; i < kNumFx; ++i)
    {
        lastW[i] = fxWParam[i]->load(); lastV[i] = fxVParam[i]->load();
        engine.fxW[i] = lastW[i]; engine.fxV[i] = lastV[i];
    }
    engine.prepare (sampleRate, juce::jmax (samplesPerBlock, 32));
    monoScratch.assign ((size_t) juce::jmax (samplesPerBlock, 32), 0.0f);
    setLatencySamples (engine.latency());
}

void PilotAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0 || n == 0) return;

    double hostBpm = -1.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) hostBpm = *b;

    // host parameters -> engine, only when the host value actually changed
    // (command-line edits update the engine immediately and reach the host asynchronously)
    for (int i = 0; i < kNumFx; ++i)
    {
        const float w = fxWParam[i]->load (std::memory_order_relaxed), v = fxVParam[i]->load (std::memory_order_relaxed);
        if (w != lastW[i]) { lastW[i] = w; engine.fxW[i].store (w, std::memory_order_relaxed); }
        if (v != lastV[i]) { lastV[i] = v; engine.fxV[i].store (v, std::memory_order_relaxed); }
    }

    const bool ttOwner = TeletypeHost::get().isOwner (&engine);
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    if (R == nullptr)
    {
        if ((int) monoScratch.size() < n) monoScratch.resize ((size_t) n);   // only for mono buses
        R = monoScratch.data();
    }

    int pos = 0;
    auto renderTo = [&] (int end)
    {
        if (end > pos) { engine.process (L + pos, R + pos, end - pos, hostBpm); pos = end; }
    };

    for (const auto meta : midi)
    {
        renderTo (juce::jlimit (0, n, meta.samplePosition));
        const auto m = meta.getMessage();
        engine.handleMidi (m);
        if (ttOwner) TeletypeHost::get().midiIn (m);   // Teletype's MI.* ops
    }
    renderTo (n);

    // Teletype's I2M.* ops -> MIDI out
    midi.clear();
    if (ttOwner) TeletypeHost::get().drainMidiOut (midi, 0);

    if (buffer.getNumChannels() == 1)
        for (int i = 0; i < n; ++i) L[i] = 0.5f * (L[i] + R[i]);
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);

    // recording
    {
        const juce::ScopedTryLock sl (writerLock);
        if (sl.isLocked())
            if (auto* w = activeWriter.load())
            {
                const float* chans[2] = { L, R };
                w->write (chans, n);
            }
    }
}

void PilotAudioProcessor::toggleRecording()
{
    if (isRecording())
    {
        {
            const juce::ScopedLock sl (writerLock);
            activeWriter = nullptr;
        }
        threadedWriter.reset();
        return;
    }
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Pilot Sealflake");
    dir.createDirectory();
    auto file = dir.getChildFile ("pilot-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".wav");
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
    if (os == nullptr) return;
    juce::WavAudioFormat wav;
    auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (currentSampleRate).withNumChannels (2).withBitsPerSample (24);
    auto writer = wav.createWriterFor (os, opts);
    if (writer == nullptr) return;
    threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writerThread, 32768);
    lastRecording = file;
    const juce::ScopedLock sl (writerLock);
    activeWriter = threadedWriter.get();
}

void PilotAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    xml->setAttribute ("channels", juce::String (engine.serializeState()));
    engine.lang().saveState (*xml);
    xml->setAttribute ("udpEnabled", udpWanted);
    xml->setAttribute ("udpPort", udpPortNumber);
    xml->setAttribute ("bpm", engine.bpm.load());
    xml->setAttribute ("hostTempo", engine.useHostTempo.load());
    xml->setAttribute ("theme", themeData);
    xml->setAttribute ("zoom", (double) zoom);
    xml->setAttribute ("animate", animate);
    xml->setAttribute ("ttOpen", ttOpen);
    xml->setAttribute ("ttZoom", (double) ttZoom);
    if (TeletypeHost::get().isOwner (&engine))
    {
        // the Teletype scene being worked on (scripts, patterns, text), in Teletype's USB stick format
        auto* tt = xml->createNewChildElement ("TELETYPE");
        tt->setAttribute ("scene", TeletypeHost::get().sceneNumber());
        tt->addTextElement (juce::String (TeletypeHost::get().sceneText()));
    }
    copyXmlToBinary (*xml, destData);
}

void PilotAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    const auto channels = xml->getStringAttribute ("channels");
    const bool en = xml->getBoolAttribute ("udpEnabled", true);
    const int port = xml->getIntAttribute ("udpPort", 49161);
    engine.bpm = xml->getDoubleAttribute ("bpm", 120.0);
    engine.useHostTempo = xml->getBoolAttribute ("hostTempo", true);
    xml->removeAttribute ("channels"); xml->removeAttribute ("udpEnabled"); xml->removeAttribute ("udpPort");
    themeData = xml->getStringAttribute ("theme");
    zoom = (float) xml->getDoubleAttribute ("zoom", 1.0);
    animate = xml->getBoolAttribute ("animate", true);
    ttOpen = xml->getBoolAttribute ("ttOpen", false);
    ttZoom = (float) xml->getDoubleAttribute ("ttZoom", 1.0);
    xml->removeAttribute ("ttOpen"); xml->removeAttribute ("ttZoom");
    xml->removeAttribute ("bpm"); xml->removeAttribute ("hostTempo");
    xml->removeAttribute ("theme"); xml->removeAttribute ("zoom"); xml->removeAttribute ("animate");
    engine.lang().loadState (*xml);
    if (auto* l = xml->getChildByName ("LANG")) xml->removeChildElement (l, true);
    juce::String ttScene;
    bool hasTt = false;
    if (auto* t = xml->getChildByName ("TELETYPE"))
    {
        hasTt = true;
        ttScene = t->getAllSubText();
        xml->removeChildElement (t, true);
    }
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    for (int i = 0; i < kNumFx; ++i) { engine.fxW[i] = fxWParam[i]->load(); engine.fxV[i] = fxVParam[i]->load(); lastW[i] = engine.fxW[i]; lastV[i] = engine.fxV[i]; }
    if (channels.isNotEmpty()) engine.submit (channels.toStdString(), false);
    if (hasTt && TeletypeHost::get().isOwner (&engine)) TeletypeHost::get().loadSceneText (ttScene.toStdString(), true);
    if (en != udpWanted || port != udpPortNumber || (en && ! udp.isListening())) setUdp (en, port);
}

juce::AudioProcessorEditor* PilotAudioProcessor::createEditor() { return new PilotAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PilotAudioProcessor(); }
