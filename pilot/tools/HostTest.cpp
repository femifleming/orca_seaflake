// Loads the built VST3 like a DAW would: scan, instantiate, play MIDI, save/restore state, open editor.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String path = argc > 1 ? argv[1] : "";
    juce::AudioPluginFormatManager fm;
    fm.addFormat (new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> found;
    juce::KnownPluginList list;
    list.scanAndAddFile (path, true, found, *fm.getFormat (0));
    if (found.isEmpty()) { std::printf ("FAIL: no plugin found\n"); return 1; }
    auto& d = *found[0];
    std::printf ("found: %s by %s, instrument=%d, ins=%d outs=%d\n", d.name.toRawUTF8(), d.manufacturerName.toRawUTF8(), d.isInstrument, d.numInputChannels, d.numOutputChannels);
    juce::String err;
    auto inst = fm.createPluginInstance (d, 48000.0, 512, err);
    if (! inst) { std::printf ("FAIL: %s\n", err.toRawUTF8()); return 1; }
    std::printf ("params: %d  latency: %d\n", inst->getParameters().size(), inst->getLatencySamples());

    bool ok = true;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int bs : { 32, 512, 1024 })
        {
            inst->setPlayConfigDetails (0, 2, sr, bs);
            inst->prepareToPlay (sr, bs);
            juce::AudioBuffer<float> buf (2, bs);
            float peak = 0; bool nan = false;
            for (int b = 0; b < (int) (sr / bs); ++b)
            {
                juce::MidiBuffer midi;
                if (b % 8 == 0) for (int ch = 1; ch <= 16; ++ch) midi.addEvent (juce::MidiMessage::noteOn (ch, 48 + ch, (juce::uint8) 100), b % bs);
                if (b % 8 == 4) for (int ch = 1; ch <= 16; ++ch) midi.addEvent (juce::MidiMessage::noteOff (ch, 48 + ch), 3);
                buf.clear();
                inst->processBlock (buf, midi);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) { const float v = buf.getSample (c, i); if (! std::isfinite (v)) nan = true; peak = std::max (peak, std::abs (v)); }
            }
            inst->releaseResources();
            std::printf ("sr %6.0f bs %4d  peak %.3f %s\n", sr, bs, peak, nan ? "NaN!" : "");
            if (nan || peak <= 0.0f || peak > 1.0f) ok = false;
        }

    juce::MemoryBlock state;
    inst->getStateInformation (state);
    inst->getParameters()[0]->setValueNotifyingHost (0.9f);
    inst->setStateInformation (state.getData(), (int) state.getSize());
    std::printf ("state: %d bytes, restored param0 = %.3f\n", (int) state.getSize(), inst->getParameters()[0]->getValue());

    std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
    std::printf ("editor: %s %dx%d\n", ed ? "ok" : "none", ed ? ed->getWidth() : 0, ed ? ed->getHeight() : 0);
    ed.reset();
    inst.reset();
    std::printf (ok ? "HOST TEST PASSED\n" : "HOST TEST FAILED\n");
    return ok ? 0 : 1;
}
