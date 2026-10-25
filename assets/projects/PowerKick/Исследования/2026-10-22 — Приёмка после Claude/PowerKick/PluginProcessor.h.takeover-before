#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/KickEngine.h"
#include "Presets.h"

class PowerKickProcessor : public juce::AudioProcessor
{
public:
    PowerKickProcessor();
    ~PowerKickProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PowerKick"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return getPresetName(); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState state;
    pk::PresetLibrary library;

    // UI <-> audio thread communication.
    std::atomic<bool> triggerRequested { false };
    std::atomic<bool> monoCheck { false };
    std::atomic<float> meterL { 0.0f }, meterR { 0.0f };
    std::atomic<int> hitCounter { 0 }; // bumps on every note-on, the UI redraws the waveform

    double getHostBpm() const { return hostBpm.load(); }
    double getCurrentSampleRate() const { return currentSampleRate > 0 ? currentSampleRate : 48000.0; }

    // Preset / comparison state shown by the editor.
    // The preset name is read by the host from its own thread: guarded.
    juce::String getPresetName() const { const juce::SpinLock::ScopedLockType l (nameLock); return presetName; }
    void setPresetName (const juce::String& n) { const juce::SpinLock::ScopedLockType l (nameLock); presetName = n; }
    pk::Snapshot slotA, slotB;
    juce::String slotNameA, slotNameB;
    bool slotBActive = false, slotBUsed = false; // B starts as a copy of A on first use
    std::vector<pk::Snapshot> variationUndo;
    double fallbackBpm = 140.0;

    void loadPreset (int index);
    void switchSlot (bool toB);
    void copyAToB();
    void makeVariation();
    bool undoVariation();

private:
    juce::String presetName { "Hard Techno Core" };
    mutable juce::SpinLock nameLock;
    pk::dsp::KickEngine engine;
    std::atomic<double> hostBpm { 140.0 };
    double currentSampleRate = 0.0;
    // Latency of each oversampling choice at the prepared rate, for a project restored after prepare.
    std::array<int, 3> latencyByOversampling { -1, -1, -1 };
    juce::AudioBuffer<float> monoScratch; // right channel for a mono bus, allocated in prepareToPlay

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerKickProcessor)
};
