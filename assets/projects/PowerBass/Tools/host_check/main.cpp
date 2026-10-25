// host_check <path to .vst3>: loads the plug-in through the VST3 interface, as a DAW does, and checks the
// shipped binary itself: one instrument in the bundle, an instance, audio from a 16th roll at several
// sample rates and block sizes (present, finite, below 0 dBFS), a state round trip, the editor in a window
// (opens, plays along, closes). Exit code 0 only if every check passed.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::printf ("%s %s\n", ok ? "OK  " : "FAIL", what.toRawUTF8());
    std::fflush (stdout);
    if (! ok) ++failures;
}

void stage (const char* what) { std::printf ("     ... %s\n", what); std::fflush (stdout); }

// 16ths at 133 BPM (gate 75 %) on one note, rendered in host blocks.
juce::AudioBuffer<float> play (juce::AudioPluginInstance& p, double sr, int block, double seconds, int note)
{
    const int total = (int) (seconds * sr), step = (int) (sr * 60.0 / 133.0 / 4.0);
    const int channels = std::max (2, p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> out (2, total), buf (channels, block);
    out.clear();
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        juce::MidiBuffer midi;
        for (int i = 0; i < n; ++i)
        {
            const int t = pos + i;
            if (t % step == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), i);
            if (t % step == step * 3 / 4) midi.addEvent (juce::MidiMessage::noteOff (1, note), i);
        }
        buf.setSize (channels, n, false, false, true);
        buf.clear();
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            out.copyFrom (c, pos, buf, std::min (c, channels - 1), 0, n);
    }
    return out;
}
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: host_check <path to .vst3>\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI gui;
    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> types;
    vst3.findAllTypesForFile (types, juce::String::fromUTF8 (argv[1]));
    check (types.size() == 1, "the VST3 bundle holds one plug-in (" + juce::String (types.size()) + ")");
    if (types.isEmpty()) return 1;
    const auto& d = *types[0];
    std::printf ("     %s by %s, version %s\n", d.name.toRawUTF8(), d.manufacturerName.toRawUTF8(), d.version.toRawUTF8());
    check (d.isInstrument, "it is an instrument");

    juce::String error;
    auto p = vst3.createInstanceFromDescription (d, 48000.0, 512, error);
    check (p != nullptr, "an instance is created" + (error.isEmpty() ? juce::String() : " (" + error + ")"));
    if (p == nullptr) return 1;
    std::printf ("     %d parameters, %d outputs, latency %d samples\n", p->getParameters().size(),
                 p->getTotalNumOutputChannels(), p->getLatencySamples());
    check (p->getParameters().size() >= 30, "its parameters are visible to the host");

    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 64, 512 })
        {
            p->setPlayConfigDetails (0, 2, sr, block);
            p->prepareToPlay (sr, block);
            const auto a = play (*p, sr, block, 2.0, 36);
            bool finite = true;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < a.getNumSamples(); ++i)
                    finite = finite && std::isfinite (a.getSample (c, i));
            const float peak = a.getMagnitude (0, a.getNumSamples());
            check (finite && peak > 0.01f && peak <= 1.0f, "sound at " + juce::String ((int) sr) + " Hz, block " + juce::String (block)
                   + ": peak " + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS" + (finite ? "" : ", NOT FINITE"));
            p->releaseResources();
        }

    // State round trip through the plug-in's own chunk.
    auto* param = p->getParameters()[0];
    param->setValueNotifyingHost (0.3f);
    juce::MemoryBlock state;
    p->getStateInformation (state);
    param->setValueNotifyingHost (0.8f);
    p->setStateInformation (state.getData(), (int) state.getSize());
    check (state.getSize() > 0 && std::abs (param->getValue() - 0.3f) < 0.02f,
           "state saved and restored (" + juce::String ((int) state.getSize()) + " bytes, " + param->getName (32) + ")");

    // The editor as a DAW shows it: in a window on screen, a few seconds of the message loop (timers, painting,
    // the waveform render), then the window closes. Closing used to hang for ever under Wine: JUCE's VBlankThread
    // never exited while WaitForVBlank() failed (fixed by cmake/patch_juce.cmake, 2026-10-23).
    if (p->hasEditor())
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorIfNeeded());
        check (editor != nullptr && editor->getWidth() > 300 && editor->getHeight() > 200,
               "the editor is created (" + (editor != nullptr ? juce::String (editor->getWidth()) + "x" + juce::String (editor->getHeight()) : juce::String ("none")) + ")");
        if (editor != nullptr)
        {
            auto window = std::make_unique<juce::DocumentWindow> ("host_check", juce::Colours::black, juce::DocumentWindow::closeButton);
            window->setUsingNativeTitleBar (true);
            window->setContentNonOwned (editor.get(), true);
            window->setVisible (true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (3000);
            check (window->isOnDesktop() && editor->isShowing() && editor->getWidth() > 300,
                   "the editor shows in a window (" + juce::String (editor->getWidth()) + "x" + juce::String (editor->getHeight()) + ")");
            p->setPlayConfigDetails (0, 2, 48000.0, 512);
            p->prepareToPlay (48000.0, 512);
            const auto a = play (*p, 48000.0, 512, 0.5, 36);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
            check (a.getMagnitude (0, a.getNumSamples()) > 0.01f, "sound while the editor is open");
            stage ("closing the editor: removing it from the window");
            window->clearContentComponent();
            stage ("closing the editor: closing the window");
            window.reset();
            stage ("closing the editor: deleting the editor");
            editor.reset();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            p->releaseResources();
            check (true, "the editor closes");
        }
    }
    stage ("deleting the instance");
    p.reset();
    stage ("unloading");
    std::printf (failures == 0 ? "PASS\n" : "FAILED %d\n", failures);
    std::fflush (stdout);
    return failures == 0 ? 0 : 1;
}
