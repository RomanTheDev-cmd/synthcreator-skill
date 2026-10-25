#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "Params.h"

namespace pk
{
// Renders one hit offline with the same engine the plug-in plays, including
// the rumble and stereo-delay tails. Trailing silence is trimmed.
juce::AudioBuffer<float> renderKick (const KickParams& params, double sampleRate, double bpm,
                                     bool stereo, float velocity = 1.0f);

// 24-bit PCM WAV.
bool writeWav24 (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate,
                 juce::String& error);
}
