#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace pb
{
namespace id
{
    // OSC / FM
    inline constexpr const char* shape     = "shape";
    inline constexpr const char* sub       = "sub";
    inline constexpr const char* oscDrive  = "oscDrive";
    inline constexpr const char* fmAmount  = "fmAmount";
    inline constexpr const char* fmRatio   = "fmRatio";
    inline constexpr const char* fmDecay   = "fmDecay";
    inline constexpr const char* oscMix    = "oscMix";
    // FILTER
    inline constexpr const char* cutoff    = "cutoff";
    inline constexpr const char* resonance = "resonance";
    inline constexpr const char* envAmount = "envAmount";
    inline constexpr const char* filterMode = "filterMode";
    // DISTORTION
    inline constexpr const char* distType  = "distType";
    inline constexpr const char* distDrive = "distDrive";
    inline constexpr const char* distTone  = "distTone";
    inline constexpr const char* distMix   = "distMix";
    // CHARACTER
    inline constexpr const char* dark      = "dark";
    inline constexpr const char* bite      = "bite";
    inline constexpr const char* punch     = "punch";
    // STEREO
    inline constexpr const char* width     = "width";
    inline constexpr const char* splitFreq = "splitFreq";
    // ENV / GLIDE / MASTER
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* decay     = "decay";
    inline constexpr const char* sustain   = "sustain";
    inline constexpr const char* release   = "release";
    inline constexpr const char* glide     = "glide";
    inline constexpr const char* glideMode = "glideMode";
    inline constexpr const char* master    = "master";
    // Advanced panel
    inline constexpr const char* filterPos  = "filterPos";
    inline constexpr const char* oversample = "oversample";
    inline constexpr const char* fltDecay   = "fltDecay";
    inline constexpr const char* velSens    = "velSens";
    inline constexpr const char* bendRange  = "bendRange";
}

enum class FilterMode { lp24, lp12, bp, hp };
enum class DistType { saturate, hardClip, fold, destroy, crush };
enum class GlideMode { mono, legato };

constexpr int numFmRatios = 18;
// Harmonic, fractional and inharmonic ratios that work under a bass line.
inline constexpr float fmRatios[numFmRatios] = { 0.5f, 1.0f, 1.41f, 1.5f, 1.73f, 2.0f, 2.5f, 2.76f, 3.0f,
                                                 3.14f, 3.5f, 4.0f, 4.23f, 5.0f, 5.4f, 6.0f, 7.0f, 7.13f };

struct SynthParams
{
    float shape = 2.0f;       // 0 sine, 1 triangle, 2 saw, 3 square
    float sub = 0.5f;
    float oscDrive = 0.2f;
    float fmAmount = 0.0f;
    int fmRatio = 5;          // index into fmRatios
    float fmDecayMs = 200.0f;
    float oscMix = 0.0f;      // 0 analog .. 1 FM
    float cutoffHz = 800.0f;
    float resonance = 0.3f;
    float envAmount = 0.4f;   // -1..1
    FilterMode filterMode = FilterMode::lp24;
    DistType distType = DistType::saturate;
    float distDrive = 0.3f;
    float distTone = 0.5f;
    float distMix = 1.0f;
    float dark = 0.0f, bite = 0.0f, punch = 0.3f;
    float width = 0.0f;       // 0..1
    float splitHz = 300.0f;
    float attackMs = 1.0f, decayMs = 300.0f, sustain = 0.8f, releaseMs = 80.0f;
    float glideMs = 0.0f;
    GlideMode glideMode = GlideMode::legato;
    float masterDb = -6.0f;
    bool filterPostDist = false;
    int oversampleIndex = 1;  // 0 = 2x, 1 = 4x, 2 = 8x
    float fltDecayMs = 250.0f;
    float velSens = 0.5f;
    float bendRange = 2.0f;
};

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
SynthParams readParams (const juce::AudioProcessorValueTreeState& state);
const juce::StringArray& allParamIds();
}
