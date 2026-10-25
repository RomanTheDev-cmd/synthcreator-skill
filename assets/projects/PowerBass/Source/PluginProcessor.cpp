#include "PluginProcessor.h"
#include "PluginEditor.h"

PowerBassProcessor::PowerBassProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "PowerBass", pb::createLayout())
{
    if (const int i = library.indexOf (getPresetName()); i >= 0)
        pb::applySnapshot (state, library.all()[(size_t) i].values);
}

bool PowerBassProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void PowerBassProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedRate = sampleRate; // not getSampleRate(): it is only valid if the host set it first
    engine.prepare (sampleRate, std::max (32, std::min (samplesPerBlock, 1024)), pb::readParams (state));
    monoScratch.setSize (1, std::max (512, samplesPerBlock));
    for (int i = 0; i < 3; ++i)
        latencyByOversampling[(size_t) i] = engine.getLatencySamplesFor (i);
    reportedLatency = engine.getLatencySamples();
    setLatencySamples (reportedLatency);
}

void PowerBassProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    // Preview button: a 400 ms C1 note.
    if (previewRequested.exchange (false))
    {
        midi.addEvent (juce::MidiMessage::noteOn (1, 36, 0.9f), 0);
        previewSamplesLeft = (int) (preparedRate * 0.4);
    }
    if (previewSamplesLeft > 0)
    {
        if (previewSamplesLeft <= n)
            midi.addEvent (juce::MidiMessage::noteOff (1, 36), previewSamplesLeft - 1);
        previewSamplesLeft = std::max (0, previewSamplesLeft - n);
    }

    const auto params = pb::readParams (state);
    float* l = buffer.getWritePointer (0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    if (r != nullptr)
        engine.process (l, r, n, midi, params);
    else
        for (int done = 0; done < n;) // mono bus: render in parts through the preallocated scratch
        {
            const int len = std::min (n - done, monoScratch.getNumSamples());
            auto* sc = monoScratch.getWritePointer (0);
            engine.process (l + done, sc, len, midi, params, done);
            for (int i = 0; i < len; ++i) l[done + i] = 0.5f * (l[done + i] + sc[i]);
            done += len;
        }
    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);

    if (const int lat = engine.getLatencySamples(); lat != reportedLatency)
    {
        reportedLatency = lat;
        setLatencySamples (lat);
    }

    int w = scopeWrite.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        scope[(size_t) w] = r != nullptr ? 0.5f * (l[i] + r[i]) : l[i];
        w = (w + 1) & (scopeSize - 1);
    }
    scopeWrite.store (w, std::memory_order_release);

    auto peak = [n] (const float* x)
    {
        const auto range = juce::FloatVectorOperations::findMinAndMax (x, n);
        return std::max (std::abs (range.getStart()), std::abs (range.getEnd()));
    };
    meterL.store (std::max (peak (l), meterL.load() * 0.85f));
    meterR.store (std::max (r != nullptr ? peak (r) : peak (l), meterR.load() * 0.85f));
}

void PowerBassProcessor::loadPreset (int index)
{
    const auto& all = library.all();
    if (index < 0 || index >= (int) all.size())
        return;
    pb::applySnapshot (state, all[(size_t) index].values);
    setPresetName (all[(size_t) index].name);
}

juce::AudioProcessorEditor* PowerBassProcessor::createEditor() { return new PowerBassEditor (*this); }

void PowerBassProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = state.copyState();
    tree.setProperty ("presetName", getPresetName(), nullptr);
    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void PowerBassProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            setPresetName (tree.getProperty ("presetName", getPresetName()).toString());
            // Parameters added after this project was saved come back neutral (old sound).
            juce::StringArray missing;
            for (const auto& [pid, value] : pb::legacyNeutral())
                if (xml->getChildByAttribute ("id", pid) == nullptr)
                    missing.add (pid);
            state.replaceState (tree);
            for (const auto& pid : missing)
                if (auto* param = state.getParameter (pid))
                    param->setValueNotifyingHost (param->convertTo0to1 (pb::legacyNeutral().at (pid)));
            // A host may restore the project after prepareToPlay: report the restored oversampling's
            // latency now, not only after the first block (processBlock keeps it in sync afterwards).
            const auto restored = (size_t) juce::jlimit (0, 2, pb::readParams (state).oversampleIndex);
            if (latencyByOversampling[restored] >= 0)
                setLatencySamples (latencyByOversampling[restored]);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PowerBassProcessor(); }
