#include "KickRenderer.h"
#include "DSP/KickEngine.h"

namespace pk
{
juce::AudioBuffer<float> renderKick (const KickParams& params, double sampleRate, double bpm,
                                     bool stereo, float velocity)
{
    constexpr int block = 512;
    dsp::KickEngine engine;
    engine.prepare (sampleRate, block, params.oversampleIndex); // the latency dropped below is that choice's
    const int latency = engine.getLatencySamples();

    const int maxLen = (int) (sampleRate * 4.0);
    // Never stop before the kick, the rumble gate and the stereo delay are over.
    const double beat = 60.0 / juce::jlimit (40.0, 300.0, bpm);
    const double minSec = decayMs (params.decay) * 0.001 + 0.02 + params.delayMs * 0.001
                          + (params.rumble > 0.0f ? (0.75 + 0.5 + 1.5 * params.rumbleTime) * beat + 0.3 : 0.0);
    const int minLen = std::min (maxLen, (int) (minSec * sampleRate));

    juce::AudioBuffer<float> out (2, maxLen + latency + block);
    out.clear();
    juce::MidiBuffer trigger, none;
    trigger.addEvent (juce::MidiMessage::noteOn (1, 60, velocity), 0);

    int pos = 0, quietRun = 0;
    const int quietNeeded = (int) (0.05 * sampleRate);
    while (pos < maxLen + latency)
    {
        auto* l = out.getWritePointer (0, pos);
        auto* r = out.getWritePointer (1, pos);
        engine.process (l, r, block, pos == 0 ? trigger : none, 0, params, bpm, false);
        for (int i = 0; i < block; ++i)
            quietRun = (std::abs (l[i]) < 3.0e-5f && std::abs (r[i]) < 3.0e-5f) ? quietRun + 1 : 0;
        pos += block;
        if (pos > minLen + latency && quietRun >= quietNeeded)
            break;
    }

    // Drop the oversampling latency at the start and the silence at the end.
    int end = std::min (pos, maxLen + latency);
    while (end > latency + 1 && std::abs (out.getSample (0, end - 1)) < 1.0e-5f
           && std::abs (out.getSample (1, end - 1)) < 1.0e-5f)
        --end;
    const int len = std::max (1, end - latency);

    juce::AudioBuffer<float> result (stereo ? 2 : 1, len);
    if (stereo)
    {
        result.copyFrom (0, 0, out, 0, latency, len);
        result.copyFrom (1, 0, out, 1, latency, len);
    }
    else
    {
        result.copyFrom (0, 0, out, 0, latency, len);
        result.applyGain (0, 0, len, 0.5f);
        result.addFrom (0, 0, out, 1, latency, len, 0.5f);
    }
    const int fade = std::min (len, (int) (0.003 * sampleRate));
    for (int c = 0; c < result.getNumChannels(); ++c)
        result.applyGainRamp (c, len - fade, fade, 1.0f, 0.0f);
    return result;
}

bool writeWav24 (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate,
                 juce::String& error)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
    {
        error = "Cannot write " + file.getFullPathName();
        return false;
    }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sampleRate, (unsigned int) audio.getNumChannels(), 24, {}, 0));
    if (writer == nullptr)
    {
        error = "WAV writer unavailable";
        return false;
    }
    stream.release(); // owned by the writer now
    if (! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
    {
        error = "Write failed";
        return false;
    }
    return true;
}
}
