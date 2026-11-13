#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "PilotEngine.h"

//==============================================================================
// Listens for Orca ';' UDP messages on a background thread
class UdpListener : private juce::Thread
{
public:
    explicit UdpListener (std::function<void (const juce::String&)> cb) : juce::Thread ("Pilot UDP"), callback (std::move (cb)) {}
    ~UdpListener() override { stop(); }

    bool start (int p)
    {
        stop();
        port = p;
        socket = std::make_unique<juce::DatagramSocket> (false);
        if (! socket->bindToPort (port)) { socket.reset(); listening = false; return false; }
        listening = true;
        startThread (juce::Thread::Priority::high);
        return true;
    }
    void stop()
    {
        signalThreadShouldExit();
        if (socket) socket->shutdown();
        stopThread (1000);
        socket.reset();
        listening = false;
    }
    bool isListening() const { return listening; }
    int getPort() const { return port; }
    std::atomic<int> packets { 0 };

private:
    void run() override
    {
        char buf[8192];
        while (! threadShouldExit() && socket != nullptr)
        {
            const int ready = socket->waitUntilReady (true, 50);
            if (ready <= 0) continue;
            const int n = socket->read (buf, (int) sizeof (buf) - 1, false);
            if (n > 0) { ++packets; callback (juce::String::fromUTF8 (buf, n)); }
        }
    }
    std::function<void (const juce::String&)> callback;
    std::unique_ptr<juce::DatagramSocket> socket;
    std::atomic<bool> listening { false };
    int port = 49161;
};

//==============================================================================
class PilotAudioProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    PilotAudioProcessor();
    ~PilotAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Pilot Sealflake"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }   // Teletype I2M.* ops
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // command entry from any non-audio thread (UDP / command line)
    juce::String submitCommand (const juce::String& text);   // returns result / error text

    // UI state (Pilot-style): Ecosystem theme, zoom, animations
    juce::String themeData;
    float zoom = 1.0f;
    bool animate = true;
    bool ttOpen = false;      // Teletype window
    float ttZoom = 1.0f;

    // UDP
    bool setUdp (bool enabled, int port);
    bool udpListening() const { return udp.isListening(); }
    int udpPort() const { return udpPortNumber; }
    bool udpEnabled() const { return udpWanted; }
    int udpPackets() const { return udp.packets.load(); }

    // recording
    bool isRecording() const { return activeWriter.load() != nullptr; }
    void toggleRecording();
    juce::File lastRecording;

    juce::AudioProcessorValueTreeState apvts;
    sf::PilotEngine engine;

    static juce::String fxParamId (int fx, bool value) { return juce::String (sf::kFxInfo[fx].code) + (value ? "_v" : "_w"); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void applyFxCommand (int fx, float w, float v);
    void handleAsyncUpdate() override;
    std::atomic<float> pendW[sf::kNumFx], pendV[sf::kNumFx];
    std::atomic<int> pendingPort { -1 }, pendingRec { -1 };

    UdpListener udp;
    int udpPortNumber = 49161;
    bool udpWanted = true;

    std::atomic<float>* fxWParam[sf::kNumFx] {};
    std::atomic<float>* fxVParam[sf::kNumFx] {};
    float lastW[sf::kNumFx] {}, lastV[sf::kNumFx] {};
    std::vector<float> monoScratch;

    // recorder
    juce::TimeSliceThread writerThread { "Pilot Recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter { nullptr };
    juce::CriticalSection writerLock;
    double currentSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PilotAudioProcessor)
};
