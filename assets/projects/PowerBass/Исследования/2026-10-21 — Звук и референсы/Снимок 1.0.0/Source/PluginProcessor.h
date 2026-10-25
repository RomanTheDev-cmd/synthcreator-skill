#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/BassEngine.h"
#include "Presets.h"

class PowerBassProcessor : public juce::AudioProcessor
{
public:
    PowerBassProcessor();
    ~PowerBassProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PowerBass"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return currentPresetName; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void loadPreset (int index);

    juce::AudioProcessorValueTreeState state;
    pb::PresetLibrary library;
    juce::String currentPresetName { "Schranz Saw" };

    // Oscilloscope: the audio thread writes, the editor reads (lock-free ring).
    static constexpr int scopeSize = 4096;
    std::array<float, scopeSize> scope {};
    std::atomic<int> scopeWrite { 0 };
    std::atomic<float> meterL { 0.0f }, meterR { 0.0f };
    std::atomic<bool> previewRequested { false };

private:
    pb::dsp::BassEngine engine;
    int reportedLatency = -1;
    int previewSamplesLeft = 0;
    juce::AudioBuffer<float> monoScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerBassProcessor)
};
