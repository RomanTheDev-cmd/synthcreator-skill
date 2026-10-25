// Tests of the plug-in wrapper (processor, state, presets through the host path, editor).
// The DSP itself is covered by PowerKickTests.cpp.

#include "PluginProcessor.h"
#include <iostream>

namespace
{
int failures = 0, checks = 0;
void expect (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok) { ++failures; std::cout << "FAIL: " << what << "\n"; }
}

float rawValue (PowerKickProcessor& p, const char* id) { return p.state.getRawParameterValue (id)->load(); }
void setPlain (PowerKickProcessor& p, const char* id, float v)
{
    auto* param = p.state.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (v));
}

struct Run
{
    double peak = 0;
    bool finite = true;
};

// Plays a kick every `every` samples for `blocks` blocks; `between` runs before each block.
Run play (PowerKickProcessor& p, int blocks, int every = 9600, std::function<void (int)> between = {}, int channels = 2)
{
    Run r;
    juce::AudioBuffer<float> buf (channels, 512);
    for (int b = 0; b < blocks; ++b)
    {
        if (between) between (b);
        juce::MidiBuffer midi;
        const int start = b * 512;
        for (int s = 0; s < 512; ++s)
            if ((start + s) % every == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), s);
        buf.clear();
        p.processBlock (buf, midi);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < 512; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) r.finite = false;
                r.peak = std::max (r.peak, (double) std::abs (v));
            }
    }
    return r;
}

void testDefaultsMatchDefaultPreset()
{
    // A fresh instance and every double-click reset must sound like the default preset.
    PowerKickProcessor p;
    const pk::Preset* def = nullptr;
    for (const auto& pr : pk::factoryPresets())
        if (pr.name == "Hard Techno Core") def = &pr;
    expect (def != nullptr, "default preset Hard Techno Core exists");
    if (def == nullptr) return;
    expect (p.getPresetName() == "Hard Techno Core", "a fresh instance shows Hard Techno Core");
    for (const auto& pid : pk::allParamIds())
    {
        auto* prm = p.state.getParameter (pid);
        const auto it = def->values.find (pid);
        if (it == def->values.end()) continue; // unlisted: the default by definition
        const float want = prm->convertTo0to1 (it->second), have = prm->getDefaultValue(); // double-click reset target
        expect (std::abs (want - have) < 0.002f, "default " + pid + " = Hard Techno Core (" + juce::String (prm->convertFrom0to1 (have), 3)
                                                 + " vs " + juce::String (it->second, 3) + ")");
    }
}

void testStateRoundTrip()
{
    PowerKickProcessor a;
    a.prepareToPlay (48000, 512);
    setPlain (a, pk::id::tune, 3.0f);
    setPlain (a, pk::id::drive, 0.81f);
    setPlain (a, pk::id::width, 66.0f);
    setPlain (a, pk::id::splitFreq, 900.0f);
    setPlain (a, pk::id::bodyTone, 0.25f);
    a.setPresetName ("My Saved Kick");
    juce::MemoryBlock mb;
    a.getStateInformation (mb);

    PowerKickProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    bool same = true;
    for (const auto& id : pk::allParamIds())
        same = same && std::abs (rawValue (a, id.toRawUTF8()) - rawValue (b, id.toRawUTF8())) < 1.0e-4f;
    expect (same, "project state restores every parameter");
    expect (b.getPresetName() == "My Saved Kick", "project state restores the preset name");

    // Garbage must not crash or change anything.
    const float before = rawValue (b, pk::id::tune);
    const char junk[] = "definitely not a PowerKick state";
    b.setStateInformation (junk, (int) sizeof (junk));
    b.setStateInformation (nullptr, 0);
    expect (std::abs (rawValue (b, pk::id::tune) - before) < 1.0e-6f, "garbage state is ignored");
}

void testLegacyProjectMigration()
{
    // A project saved before the newer parameters existed: they must come back neutral.
    PowerKickProcessor a;
    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
    for (auto* id : { pk::id::bodyTone, pk::id::pitchFall, pk::id::splitFreq })
        if (auto* e = xml->getChildByAttribute ("id", id)) xml->removeChildElement (e, true);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    PowerKickProcessor b;
    b.setStateInformation (old.getData(), (int) old.getSize());
    expect (std::abs (rawValue (b, pk::id::bodyTone) - 1.0f) < 1e-4f, "old project: bodyTone neutral");
    expect (std::abs (rawValue (b, pk::id::pitchFall)) < 1e-4f, "old project: pitchFall neutral");
    expect (std::abs (rawValue (b, pk::id::splitFreq) - 1000.0f) < 1.0f, "old project: SPLIT at the old 1 kHz");
}

void testPresetSwitchingWhilePlaying()
{
    PowerKickProcessor p;
    p.prepareToPlay (48000, 512);
    const int n = (int) p.library.all().size();
    double worst = 0;
    bool finite = true;
    for (int i = 0; i < n; i += std::max (1, n / 60))
    {
        const auto r = play (p, 8, 2400, [&p, i] (int b) { if (b == 4) p.loadPreset (i); });
        worst = std::max (worst, r.peak);
        finite = finite && r.finite;
    }
    expect (finite, "switching presets while playing stays finite");
    expect (worst <= 1.0, "switching presets while playing stays under 0 dBFS (" + juce::String (worst, 3) + ")");
    expect (p.getLatencySamples() >= 0, "latency reported");
}

void testOversamplingLatencyTracksProcessing()
{
    PowerKickProcessor p;
    p.prepareToPlay (48000, 127);
    juce::AudioBuffer<float> out (2, 127);
    for (int choice : { 0, 1, 2, 0, 1 })
    {
        setPlain (p, pk::id::oversample, (float) choice);
        // Independent reference: the latency of JUCE's selected oversampling filter.
        juce::dsp::Oversampling<float> reference (1, (size_t) (choice + 1),
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        reference.initProcessing (127);
        const int expected = (int) std::lround (reference.getLatencyInSamples());
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 37);
        out.clear();
        p.processBlock (out, midi);
        expect (p.getLatencySamples() == expected, "oversampling " + juce::String (2 << choice)
            + "x: host latency " + juce::String (p.getLatencySamples()) + " equals filter latency " + juce::String (expected));
    }
}

void testAutomation()
{
    PowerKickProcessor p;
    p.prepareToPlay (48000, 512);
    p.loadPreset (p.library.indexOf ("Hard Techno Core"));
    // Sweep many parameters every block while kicks play.
    const auto r = play (p, 120, 4800, [&p] (int b)
    {
        const float t = (float) (b % 40) / 40.0f;
        setPlain (p, pk::id::drive, t);
        setPlain (p, pk::id::character, 1.0f - t);
        setPlain (p, pk::id::width, 100.0f * t);
        setPlain (p, pk::id::delay, 15.0f * t);
        setPlain (p, pk::id::splitFreq, 150.0f + 1800.0f * t);
        setPlain (p, pk::id::output, -24.0f + 30.0f * t);
        setPlain (p, pk::id::charMode, (float) (b % 5));
        setPlain (p, pk::id::driveMode, (float) (b % 3));
    });
    expect (r.finite && r.peak <= 1.0, "heavy automation stays finite and under 0 dBFS (" + juce::String (r.peak, 3) + ")");
}

void testMonoBus()
{
    PowerKickProcessor p;
    juce::AudioProcessor::BusesLayout mono;
    mono.outputBuses.add (juce::AudioChannelSet::mono());
    expect (p.setBusesLayout (mono), "mono output bus accepted");
    p.prepareToPlay (48000, 512);
    const auto r = play (p, 20, 4800, {}, 1);
    expect (r.finite && r.peak > 0.1 && r.peak <= 1.0, "mono bus produces the kick (" + juce::String (r.peak, 3) + ")");
}

void testABAndVariation()
{
    PowerKickProcessor p;
    p.prepareToPlay (48000, 512);
    const float tuneA = rawValue (p, pk::id::tune);
    p.switchSlot (true); // B starts as a copy of A
    expect (std::abs (rawValue (p, pk::id::tune) - tuneA) < 1.0e-5f, "B starts as a copy of A");
    setPlain (p, pk::id::tune, tuneA + 2.0f);
    p.switchSlot (false);
    expect (std::abs (rawValue (p, pk::id::tune) - tuneA) < 1.0e-4f, "A keeps its own settings");
    p.switchSlot (true);
    expect (std::abs (rawValue (p, pk::id::tune) - (tuneA + 2.0f)) < 1.0e-4f, "B keeps its own settings");
    p.switchSlot (false);

    const auto before = pk::captureSnapshot (p.state);
    p.makeVariation();
    const auto after = pk::captureSnapshot (p.state);
    expect (std::abs (after.at (pk::id::tune) - before.at (pk::id::tune)) < 1.0e-4f, "VARIATION keeps TUNE");
    expect (after.at (pk::id::charMode) == before.at (pk::id::charMode), "VARIATION keeps the CHARACTER mode");
    bool changed = false;
    for (const auto& [k, v] : before) changed = changed || std::abs (after.at (k) - v) > 1.0e-4f;
    expect (changed, "VARIATION changes the sound");
    expect (p.undoVariation(), "UNDO available");
    bool restored = true;
    for (const auto& [k, v] : before) restored = restored && std::abs (pk::captureSnapshot (p.state).at (k) - v) < 1.0e-3f;
    expect (restored, "UNDO restores the sound exactly");
}

void testPreview()
{
    PowerKickProcessor p;
    p.prepareToPlay (48000, 512);
    p.triggerRequested = true;
    const auto r = play (p, 10, 1 << 30);
    expect (r.peak > 0.1, "PREVIEW plays a kick");
}

void testEditor()
{
#if JUCE_MAC
    juce::ScopedJuceInitialiser_GUI gui;
    PowerKickProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    expect (ed != nullptr && ed->getWidth() == 920 && ed->getHeight() == 660, "editor opens at 920x660");
    // Hosts resize through the constrainer (setBoundsConstrained), not a raw setSize.
    ed->setBoundsConstrained ({ 0, 0, 100, 100 });
    expect (ed->getWidth() >= 736 && ed->getHeight() >= 528, "editor keeps its minimum size when the host shrinks it");
    ed->setBoundsConstrained ({ 0, 0, 1380, 990 });
    expect (std::abs ((double) ed->getWidth() / ed->getHeight() - 920.0 / 660.0) < 0.02, "editor keeps its aspect ratio");
    ed.reset();
#endif
}
}

// --snapshot DIR: renders the editor (main view and ADVANCED open) to PNG files for a visual check.
int snapshot (const juce::File& dir)
{
    juce::ScopedJuceInitialiser_GUI gui;
    PowerKickProcessor p;
    p.prepareToPlay (48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    dir.createDirectory();
    auto save = [&] (const juce::String& name, float scale)
    {
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
        auto f = dir.getChildFile (name);
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
    };
    save ("main.png", 1.0f);
    for (auto* c : ed->getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "ADVANCED")
            b->setToggleState (true, juce::sendNotification);
    save ("advanced.png", 1.0f);
    ed->setBoundsConstrained ({ 0, 0, 736, 528 });
    save ("advanced_small.png", 1.0f);
    ed.reset();
    return 0;
}

int main (int argc, char** argv)
{
    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
        return snapshot (juce::File (juce::String::fromUTF8 (argv[2])));
    testDefaultsMatchDefaultPreset();
    testStateRoundTrip();
    testLegacyProjectMigration();
    testPresetSwitchingWhilePlaying();
    testOversamplingLatencyTracksProcessing();
    testAutomation();
    testMonoBus();
    testABAndVariation();
    testPreview();
    testEditor();
    std::cout << (failures == 0 ? "PASS " : "FAILED ") << (checks - failures) << "/" << checks << " wrapper checks\n";
    return failures == 0 ? 0 : 1;
}
