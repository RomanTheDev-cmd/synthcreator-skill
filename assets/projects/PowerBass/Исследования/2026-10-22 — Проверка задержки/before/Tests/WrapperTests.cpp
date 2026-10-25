// Tests of the plug-in wrapper (processor, state, presets through the host path, editor).
// The DSP itself is covered by PowerBassTests.cpp.

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

float rawValue (PowerBassProcessor& p, const char* id) { return p.state.getRawParameterValue (id)->load(); }
void setPlain (PowerBassProcessor& p, const char* id, float v)
{
    auto* param = p.state.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (v));
}

struct Run { double peak = 0; bool finite = true; };

// A rolling 16th bass line at 150 BPM (C2 / D#2), `between` runs before each block.
Run play (PowerBassProcessor& p, int blocks, std::function<void (int)> between = {}, int channels = 2)
{
    Run r;
    juce::AudioBuffer<float> buf (channels, 512);
    const int step = 4800; // 16th at 150 BPM, 48 kHz
    for (int b = 0; b < blocks; ++b)
    {
        if (between) between (b);
        juce::MidiBuffer midi;
        for (int s = 0; s < 512; ++s)
        {
            const int t = b * 512 + s;
            const int note = (t / (step * 8)) % 2 == 0 ? 36 : 39;
            if (t % step == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), s);
            if (t % step == step * 3 / 4) midi.addEvent (juce::MidiMessage::noteOff (1, note), s);
        }
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
    PowerBassProcessor p;
    const pb::Preset* def = nullptr;
    for (const auto& pr : pb::factoryPresets())
        if (pr.name == "Reese Core") def = &pr;
    expect (def != nullptr, "default preset Reese Core exists");
    if (def == nullptr) return;
    expect (p.getPresetName() == "Reese Core", "a fresh instance shows Reese Core");
    for (const auto& pid : pb::allParamIds())
    {
        auto* prm = p.state.getParameter (pid);
        const auto it = def->values.find (pid);
        if (it == def->values.end()) continue; // unlisted: the default by definition
        const float want = prm->convertTo0to1 (it->second), have = prm->getDefaultValue(); // double-click reset target
        expect (std::abs (want - have) < 0.002f, "default " + pid + " = Reese Core (" + juce::String (prm->convertFrom0to1 (have), 3)
                                                 + " vs " + juce::String (it->second, 3) + ")");
    }
}

void testStateRoundTrip()
{
    PowerBassProcessor a;
    a.prepareToPlay (48000, 512);
    setPlain (a, pb::id::cutoff, 1234.0f);
    setPlain (a, pb::id::distType, 2.0f);
    setPlain (a, pb::id::detune, 0.4f);
    setPlain (a, pb::id::stereoDelay, 6.0f);
    a.setPresetName ("My Saved Bass");
    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    PowerBassProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    bool same = true;
    for (const auto& id : pb::allParamIds())
        same = same && std::abs (rawValue (a, id.toRawUTF8()) - rawValue (b, id.toRawUTF8())) < 1.0e-3f;
    expect (same, "project state restores every parameter");
    expect (b.getPresetName() == "My Saved Bass", "project state restores the preset name");
    const float before = rawValue (b, pb::id::cutoff);
    const char junk[] = "not a PowerBass state";
    b.setStateInformation (junk, (int) sizeof (junk));
    expect (std::abs (rawValue (b, pb::id::cutoff) - before) < 1.0e-3f, "garbage state is ignored");
}

void testLegacyProjectMigration()
{
    PowerBassProcessor a;
    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
    for (auto* id : { pb::id::detune, pb::id::stereoDelay })
        if (auto* e = xml->getChildByAttribute ("id", id)) xml->removeChildElement (e, true);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    PowerBassProcessor b;
    b.setStateInformation (old.getData(), (int) old.getSize());
    expect (rawValue (b, pb::id::detune) < 1.0e-4f, "old project: DETUNE off");
    expect (rawValue (b, pb::id::stereoDelay) < 1.0e-4f, "old project: stereo DELAY off");
}

void testPresetSwitchingWhilePlaying()
{
    PowerBassProcessor p;
    p.prepareToPlay (48000, 512);
    const int n = (int) p.library.all().size();
    double worst = 0;
    bool finite = true;
    for (int i = 0; i < n; i += std::max (1, n / 60))
    {
        const auto r = play (p, 10, [&p, i] (int b) { if (b == 5) p.loadPreset (i); });
        worst = std::max (worst, r.peak);
        finite = finite && r.finite;
    }
    expect (finite, "switching presets while playing stays finite");
    expect (worst <= 1.0, "switching presets while playing stays under 0 dBFS (" + juce::String (worst, 3) + ")");
}

void testOversamplingLatency()
{
    PowerBassProcessor p;
    p.prepareToPlay (48000, 512);
    int lat[3];
    for (int os = 0; os < 3; ++os)
    {
        setPlain (p, pb::id::oversample, (float) os);
        play (p, 6); // the switch happens inside a short fade, then latency is re-reported
        lat[os] = p.getLatencySamples();
    }
    expect (lat[0] >= 0 && lat[1] >= 0 && lat[2] >= 0, "latency reported for 2x/4x/8x");
    expect (! (lat[0] == lat[1] && lat[1] == lat[2]) || lat[0] == 0, "latency follows the oversampling setting");
}

void testAutomation()
{
    PowerBassProcessor p;
    p.prepareToPlay (48000, 512);
    const auto r = play (p, 160, [&p] (int b)
    {
        const float t = (float) (b % 40) / 40.0f;
        setPlain (p, pb::id::cutoff, 60.0f + 8000.0f * t);
        setPlain (p, pb::id::resonance, t);
        setPlain (p, pb::id::distDrive, t);
        setPlain (p, pb::id::fmAmount, 1.0f - t);
        setPlain (p, pb::id::width, t);
        setPlain (p, pb::id::stereoDelay, 15.0f * t);
        setPlain (p, pb::id::splitFreq, 100.0f + 900.0f * t);
        setPlain (p, pb::id::master, -30.0f + 30.0f * t);
        setPlain (p, pb::id::distType, (float) (b % 5));
        setPlain (p, pb::id::filterMode, (float) (b % 4));
    });
    expect (r.finite && r.peak <= 1.0, "heavy automation stays finite and under 0 dBFS (" + juce::String (r.peak, 3) + ")");
}

void testMonoBus()
{
    PowerBassProcessor p;
    juce::AudioProcessor::BusesLayout mono;
    mono.outputBuses.add (juce::AudioChannelSet::mono());
    expect (p.setBusesLayout (mono), "mono output bus accepted");
    p.prepareToPlay (48000, 512);
    const auto r = play (p, 30, {}, 1);
    expect (r.finite && r.peak > 0.05 && r.peak <= 1.0, "mono bus plays the bass (" + juce::String (r.peak, 3) + ")");
}

void testPreview()
{
    PowerBassProcessor p;
    p.prepareToPlay (48000, 512);
    p.previewRequested = true;
    juce::AudioBuffer<float> buf (2, 512);
    double peak = 0, tail = 0;
    for (int b = 0; b < 200; ++b)
    {
        juce::MidiBuffer none;
        buf.clear();
        p.processBlock (buf, none);
        const double m = std::max (buf.getMagnitude (0, 0, 512), buf.getMagnitude (1, 0, 512));
        if (b < 40) peak = std::max (peak, m);
        if (b > 180) tail = std::max (tail, m);
    }
    expect (peak > 0.05, "PREVIEW plays a note");
    expect (tail < 1.0e-3, "PREVIEW note is released (no hanging note)");
}

void testEditor()
{
#if JUCE_MAC
    juce::ScopedJuceInitialiser_GUI gui;
    PowerBassProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    expect (ed != nullptr && ed->getWidth() == 1080 && ed->getHeight() == 720, "editor opens at 1080x720");
    ed->setBoundsConstrained ({ 0, 0, 100, 100 });
    expect (ed->getWidth() >= 864 && ed->getHeight() >= 576, "editor keeps its minimum size when the host shrinks it");
    ed.reset();
#endif
}
}

int main()
{
    testDefaultsMatchDefaultPreset();
    testStateRoundTrip();
    testLegacyProjectMigration();
    testPresetSwitchingWhilePlaying();
    testOversamplingLatency();
    testAutomation();
    testMonoBus();
    testPreview();
    testEditor();
    std::cout << (failures == 0 ? "PASS " : "FAILED ") << (checks - failures) << "/" << checks << " wrapper checks\n";
    return failures == 0 ? 0 : 1;
}
