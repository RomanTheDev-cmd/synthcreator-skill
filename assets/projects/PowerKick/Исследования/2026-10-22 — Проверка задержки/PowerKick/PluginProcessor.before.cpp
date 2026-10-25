#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "KickRenderer.h"

PowerKickProcessor::PowerKickProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "PowerKick", pk::createLayout())
{
    if (const int i = library.indexOf (getPresetName()); i >= 0)
        pk::applySnapshot (state, library.all()[(size_t) i].values);
    slotA = slotB = pk::captureSnapshot (state);
}

bool PowerKickProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void PowerKickProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate, std::max (32, std::min (samplesPerBlock, 1024)));
    setLatencySamples (engine.getLatencySamples());
}

void PowerKickProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    double bpm = fallbackBpm;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm(); b.hasValue() && *b > 20.0)
                bpm = *b;
    hostBpm.store (bpm);

    if (triggerRequested.exchange (false))
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);

    for (const auto meta : midi)
        if (meta.getMessage().isNoteOn())
        {
            hitCounter.fetch_add (1);
            break;
        }

    const auto params = pk::readParams (state);
    // The engine renders stereo; a mono bus receives the mono sum.
    juce::AudioBuffer<float> stereo;
    float* l = buffer.getWritePointer (0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    if (r == nullptr)
    {
        stereo.setSize (1, n, false, false, true);
        r = stereo.getWritePointer (0);
    }
    engine.process (l, r, n, midi, 0, params, bpm, monoCheck.load() || buffer.getNumChannels() == 1);
    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);

    auto peak = [n] (const float* x)
    {
        const auto range = juce::FloatVectorOperations::findMinAndMax (x, n);
        return std::max (std::abs (range.getStart()), std::abs (range.getEnd()));
    };
    meterL.store (std::max (peak (l), meterL.load() * 0.85f));
    meterR.store (std::max (peak (r), meterR.load() * 0.85f));
}

//==============================================================================
void PowerKickProcessor::loadPreset (int index)
{
    const auto& all = library.all();
    if (index < 0 || index >= (int) all.size())
        return;
    pk::applySnapshot (state, all[(size_t) index].values);
    setPresetName (all[(size_t) index].name);
    variationUndo.clear();
}

void PowerKickProcessor::switchSlot (bool toB)
{
    if (toB == slotBActive)
        return;
    (slotBActive ? slotB : slotA) = pk::captureSnapshot (state);
    (slotBActive ? slotNameB : slotNameA) = getPresetName();
    if (toB && ! slotBUsed)
    {
        slotB = slotA;
        slotNameB = slotNameA;
        slotBUsed = true;
    }
    slotBActive = toB;
    pk::applySnapshot (state, slotBActive ? slotB : slotA);
    setPresetName (slotBActive ? slotNameB : slotNameA);
    variationUndo.clear();
}

void PowerKickProcessor::copyAToB()
{
    if (! slotBActive)
    {
        slotA = pk::captureSnapshot (state);
        slotNameA = getPresetName();
    }
    slotB = slotA;
    slotNameB = slotNameA;
    slotBUsed = true;
    if (slotBActive)
        pk::applySnapshot (state, slotB);
}

namespace
{
double hitRms (const pk::KickParams& p)
{
    const auto a = pk::renderKick (p, 48000.0, 140.0, false);
    const int n = std::min (a.getNumSamples(), 7200); // first 150 ms: the hit
    double sum = 0;
    for (int i = 0; i < n; ++i) sum += a.getSample (0, i) * a.getSample (0, i);
    return std::sqrt (sum / std::max (1, n));
}
}

void PowerKickProcessor::makeVariation()
{
    const auto before = pk::captureSnapshot (state);
    variationUndo.push_back (before);
    if (variationUndo.size() > 32)
        variationUndo.erase (variationUndo.begin());

    auto& rnd = juce::Random::getSystemRandom();
    auto s = before;
    auto nudge = [&] (const char* pid, float amount, float lo, float hi)
    { s[pid] = juce::jlimit (lo, hi, s[pid] + (rnd.nextFloat() * 2.0f - 1.0f) * amount); };

    // Timbre moves; TUNE, OUTPUT, CHARACTER mode and stereo stay as they are.
    nudge (pk::id::decay, 0.10f, 0.08f, 1.0f);
    nudge (pk::id::punch, 0.20f, 0.2f, 1.0f);
    nudge (pk::id::body, 0.20f, 0.1f, 1.0f);
    nudge (pk::id::click, 0.20f, 0.0f, 0.9f);
    nudge (pk::id::drive, 0.20f, 0.0f, 1.0f);
    nudge (pk::id::character, 0.20f, 0.0f, 1.0f);
    nudge (pk::id::pitchDepth, 8.0f, 14.0f, 48.0f);
    nudge (pk::id::pitchTime, 10.0f, 10.0f, 60.0f);
    nudge (pk::id::clickTone, 0.3f, 0.0f, 1.0f);
    if (s[pk::id::rumble] > 0.05f)
        nudge (pk::id::rumble, 0.15f, 0.05f, 0.8f);
    if (rnd.nextFloat() < 0.25f) s[pk::id::driveMode] = (float) rnd.nextInt (3);
    if (rnd.nextFloat() < 0.25f) s[pk::id::clickType] = (float) rnd.nextInt (3);
    // Very hot drive with a loud click turns harsh: keep them balanced.
    if (s[pk::id::drive] > 0.75f)
        s[pk::id::click] = std::min (s[pk::id::click], 0.6f);

    // Match the hit loudness of the original so the variation is judged on timbre.
    const double ref = hitRms (pk::paramsFromSnapshot (before));
    const double now = hitRms (pk::paramsFromSnapshot (s));
    if (ref > 1e-6 && now > 1e-6)
        s[pk::id::output] = juce::jlimit (-24.0f, 6.0f, s[pk::id::output] + (float) (20.0 * std::log10 (ref / now)));

    pk::applySnapshot (state, s);
}

bool PowerKickProcessor::undoVariation()
{
    if (variationUndo.empty())
        return false;
    pk::applySnapshot (state, variationUndo.back());
    variationUndo.pop_back();
    return true;
}

//==============================================================================
juce::AudioProcessorEditor* PowerKickProcessor::createEditor() { return new PowerKickEditor (*this); }

void PowerKickProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = state.copyState();
    tree.setProperty ("presetName", getPresetName(), nullptr);
    tree.setProperty ("fallbackBpm", fallbackBpm, nullptr);
    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void PowerKickProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            setPresetName (tree.getProperty ("presetName", getPresetName()).toString());
            fallbackBpm = (double) tree.getProperty ("fallbackBpm", 140.0);
            // Parameters added after this project was saved come back neutral (old sound).
            juce::StringArray missing;
            for (const auto& [pid, value] : pk::legacyNeutral())
                if (xml->getChildByAttribute ("id", pid) == nullptr)
                    missing.add (pid);
            state.replaceState (tree);
            for (const auto& pid : missing)
                if (auto* param = state.getParameter (pid))
                    param->setValueNotifyingHost (param->convertTo0to1 (pk::legacyNeutral().at (pid)));
            slotA = slotB = pk::captureSnapshot (state);
            slotBActive = slotBUsed = false;
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PowerKickProcessor(); }
