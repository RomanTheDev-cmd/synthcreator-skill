#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace pk
{
// Parameter IDs. Visible controls first, then hidden "voicing" parameters that
// presets and automation use but the main interface does not show.
namespace id
{
    inline constexpr const char* tune      = "tune";
    inline constexpr const char* decay     = "decay";
    inline constexpr const char* punch     = "punch";
    inline constexpr const char* body      = "body";
    inline constexpr const char* click     = "click";
    inline constexpr const char* drive     = "drive";
    inline constexpr const char* character = "character";
    inline constexpr const char* rumble    = "rumble";
    inline constexpr const char* output    = "output";
    inline constexpr const char* width     = "width";
    inline constexpr const char* delay     = "delay";
    inline constexpr const char* driveMode = "driveMode";
    inline constexpr const char* charMode  = "charMode";

    inline constexpr const char* pitchDepth = "pitchDepth";
    inline constexpr const char* pitchTime  = "pitchTime";
    inline constexpr const char* clickType  = "clickType";
    inline constexpr const char* clickTone  = "clickTone";
    inline constexpr const char* rumbleTime = "rumbleTime";
    inline constexpr const char* rumbleSync = "rumbleSync";
    inline constexpr const char* velSens    = "velSens";
    inline constexpr const char* keyTrack   = "keyTrack";
    inline constexpr const char* bodyTone   = "bodyTone";
    inline constexpr const char* pitchFall  = "pitchFall";
    inline constexpr const char* splitFreq  = "splitFreq";
    // ADVANCED (all neutral by default: existing presets keep their sound)
    inline constexpr const char* beatLength = "beatLength";
    inline constexpr const char* subLevel   = "subLevel";
    inline constexpr const char* startPhase = "startPhase";
    inline constexpr const char* driveEnv   = "driveEnv";
    inline constexpr const char* velDrive   = "velDrive";
    inline constexpr const char* oversample = "oversample";
    inline constexpr const char* pitchCurve = "pitchCurve";
    inline constexpr const char* keyRoot    = "keyRoot";
    inline constexpr const char* modDrive   = "modDrive";
    inline constexpr const char* choke      = "choke";
    inline constexpr const char* rumbleMode = "rumbleMode";
}

enum class DriveMode { saturate, clip, industrial };
enum class CharMode  { deep, punchy, dark, hard, raw };
enum class ClickType { soft, sharp, metal, noise909, fmBlip };

// Plain snapshot of every sound parameter in real units.
struct KickParams
{
    float tune = 0.0f;          // semitones, -12..+12 around A1 (55 Hz)
    float decay = 0.45f;        // 0..1 (mapped to 60..1500 ms)
    float punch = 0.6f;         // 0..1
    float body = 0.5f;          // 0..1
    float click = 0.35f;        // 0..1
    float drive = 0.3f;         // 0..1
    float character = 0.5f;     // 0..1
    float rumble = 0.0f;        // 0..1
    float outputDb = -3.0f;     // -24..+6 dB
    float width = 0.0f;         // 0..100 %
    float delayMs = 0.0f;       // 0..15 ms
    DriveMode driveMode = DriveMode::saturate;
    CharMode charMode = CharMode::punchy;

    float pitchDepth = 30.0f;   // semitones of pitch sweep
    float pitchTime = 30.0f;    // ms, sweep time constant
    ClickType clickType = ClickType::sharp;
    float clickTone = 0.5f;     // 0..1
    float rumbleTime = 0.5f;    // 0..1
    int rumbleSync = 0;         // 0 = 1/16, 1 = 1/8, 2 = 3/16
    float velSens = 0.6f;       // 0..1
    bool keyTrack = false;
    float pitchFall = 0.0f;     // 0..24 st: slow pitch fall through the tail (measured kicks fall 2-4 st at 100-250 ms)
    float bodyTone = 1.0f;
    float splitHz = 500.0f;     // stereo widening above this frequency (150..2000 Hz)
    int beatLength = 0;         // 0 off, 1 = 1/4, 2 = 1/8, 3 = 1/16: the kick ends within that note value
    float subLevel = 0.0f;      // clean sine sub layer under the driven body
    float startPhase = 0.0f;    // 0..1 = 0..90 degrees body start phase ("push")
    float driveEnv = 0.0f;      // extra drive on the attack, up to +24 dB (decays over ~30 ms)
    float velDrive = 0.0f;      // velocity -> drive
    int oversampleIndex = 1;    // 0 = 2x, 1 = 4x, 2 = 8x
    float pitchCurve = 0.0f;    // 0 exponential sweep .. 1 slower, rounder sweep
    int keyRoot = 60;           // key tracking root note
    float modDrive = 0.0f;      // mod wheel -> drive
    bool choke = false;         // a MIDI note-off cuts the kick (note length = kick length)
    int rumbleMode = 0;         // 0 classic comb rumble, 1 diffuse reverb rumble
};

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
KickParams readParams (const juce::AudioProcessorValueTreeState& state);

// Every parameter ID, in a stable order (used by presets, A/B and randomize).
const juce::StringArray& allParamIds();

float decayMs (float decayNorm) noexcept;
juce::String noteNameForTune (float semitones);
float baseFrequency (float tuneSemitones) noexcept;
}
