#include "Params.h"

namespace pb
{
namespace
{
using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using APB = juce::AudioParameterBool;
using Attr = juce::AudioParameterFloatAttributes;
using Range = juce::NormalisableRange<float>;

juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
juce::String ms (float v, int) { return v < 1000.0f ? juce::String (v, v < 10.0f ? 1 : 0) + " ms" : juce::String (v / 1000.0f, 2) + " s"; }
juce::String hz (float v, int) { return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz" : juce::String (v / 1000.0f, 1) + " kHz"; }

Range skewed (float lo, float hi, float centre)
{
    Range r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}

void addUnit (juce::AudioProcessorValueTreeState::ParameterLayout& l, const char* pid, const char* name, float def)
{
    l.add (std::make_unique<APF> (juce::ParameterID { pid, 1 }, name, Range (0.0f, 1.0f), def, Attr().withStringFromValueFunction (pct)));
}

void addFloat (juce::AudioProcessorValueTreeState::ParameterLayout& l, const char* pid, const char* name, Range r, float def,
               std::function<juce::String (float, int)> text)
{
    l.add (std::make_unique<APF> (juce::ParameterID { pid, 1 }, name, r, def, Attr().withStringFromValueFunction (std::move (text))));
}
}

const juce::StringArray& allParamIds()
{
    static const juce::StringArray ids { id::shape, id::sub, id::oscDrive, id::fmAmount, id::fmRatio, id::fmDecay, id::oscMix,
                                         id::cutoff, id::resonance, id::envAmount, id::filterMode, id::distType, id::distDrive,
                                         id::distTone, id::distMix, id::dark, id::bite, id::punch, id::width, id::splitFreq,
                                         id::attack, id::decay, id::sustain, id::release, id::glide, id::glideMode, id::master,
                                         id::filterPos, id::oversample, id::fltDecay, id::velSens, id::bendRange, id::detune, id::stereoDelay };
    return ids;
}

// Parameter defaults equal the default preset "Reese Core" (fitted to the largest reference cluster),
// so a fresh instance and every double-click reset give the same full bass.
juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    addFloat (l, id::shape, "Shape", Range (0.0f, 3.0f), 1.079f, [] (float v, int)
    {
        static const char* names[] = { "SINE", "TRI", "SAW", "SQUARE" };
        const int i = juce::jlimit (0, 3, juce::roundToInt (v));
        return std::abs (v - (float) i) < 0.05f ? juce::String (names[i])
                                                 : juce::String (names[(int) v]) + juce::String::fromUTF8 ("\xe2\x86\x92") + names[std::min (3, (int) v + 1)];
    });
    addUnit (l, id::sub, "Sub", 0.2945f);
    addUnit (l, id::oscDrive, "Osc Drive", 1.0f);
    addUnit (l, id::fmAmount, "FM Amount", 0.08069f);
    addFloat (l, id::fmRatio, "FM Ratio", Range (0.0f, (float) (numFmRatios - 1), 1.0f), 15.0f, [] (float v, int)
    { return juce::String::fromUTF8 ("\xc3\x97") + juce::String (fmRatios[juce::jlimit (0, numFmRatios - 1, juce::roundToInt (v))], 2).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."); });
    addFloat (l, id::fmDecay, "FM Decay", skewed (5.0f, 3000.0f, 200.0f), 55.05f, ms);
    addUnit (l, id::oscMix, "Analog/FM Mix", 0.1877f);

    addFloat (l, id::cutoff, "Cutoff", skewed (30.0f, 18000.0f, 800.0f), 1352.0f, hz);
    addUnit (l, id::resonance, "Resonance", 1.0f);
    addFloat (l, id::envAmount, "Env Amount", Range (-1.0f, 1.0f), -0.2122f, [] (float v, int)
    { return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
    l.add (std::make_unique<APC> (juce::ParameterID { id::filterMode, 1 }, "Filter Mode", juce::StringArray { "LP 24", "LP 12", "BP", "HP" }, 0));

    l.add (std::make_unique<APC> (juce::ParameterID { id::distType, 1 }, "Distortion Type",
                                  juce::StringArray { "Saturate", "Hard Clip", "Fold", "Destroy", "Crush" }, 3));
    addUnit (l, id::distDrive, "Distortion Drive", 0.875f);
    addUnit (l, id::distTone, "Distortion Tone", 0.9884f);
    addUnit (l, id::distMix, "Distortion Mix", 0.75f);

    addUnit (l, id::dark, "Dark", 0.3208f);
    addUnit (l, id::bite, "Bite", 0.375f);
    addUnit (l, id::punch, "Punch", 0.689f);

    addUnit (l, id::width, "Width", 0.6111f);
    addFloat (l, id::splitFreq, "Split Freq", skewed (100.0f, 1000.0f, 300.0f), 150.0f, hz);

    addFloat (l, id::attack, "Attack", skewed (0.5f, 2000.0f, 30.0f), 3.926f, ms);
    addFloat (l, id::decay, "Decay", skewed (5.0f, 5000.0f, 300.0f), 2372.0f, ms);
    addUnit (l, id::sustain, "Sustain", 0.8287f);
    addFloat (l, id::release, "Release", skewed (5.0f, 5000.0f, 200.0f), 400.0f, ms);
    addFloat (l, id::glide, "Glide", skewed (0.0f, 1000.0f, 100.0f), 0.0f, ms);
    l.add (std::make_unique<APC> (juce::ParameterID { id::glideMode, 1 }, "Glide Mode", juce::StringArray { "Mono", "Legato" }, 1));
    addFloat (l, id::master, "Master", Range (-30.0f, 6.0f, 0.1f), -10.1f, [] (float v, int) { return juce::String (v, 1) + " dB"; });

    l.add (std::make_unique<APB> (juce::ParameterID { id::filterPos, 1 }, "Filter After Distortion", false));
    l.add (std::make_unique<APC> (juce::ParameterID { id::oversample, 1 }, "Oversampling", juce::StringArray { "2x", "4x", "8x" }, 1));
    addFloat (l, id::fltDecay, "Filter Env Decay", skewed (10.0f, 3000.0f, 250.0f), 15.97f, ms);
    addUnit (l, id::velSens, "Velocity Sens", 0.5f);
    addUnit (l, id::detune, "Detune", 0.8572f);
    {
        Range r (0.0f, 15.0f, 0.01f, 0.6f);
        addFloat (l, id::stereoDelay, "Stereo Delay", r, 0.0f, [] (float v, int) { return juce::String (v, 2) + " ms"; });
    }
    addFloat (l, id::bendRange, "Bend Range", Range (0.0f, 12.0f, 1.0f), 2.0f, [] (float v, int) { return juce::String (juce::roundToInt (v)) + " st"; });
    return l;
}

SynthParams readParams (const juce::AudioProcessorValueTreeState& s)
{
    auto v = [&s] (const char* pid) { return s.getRawParameterValue (pid)->load(); };
    SynthParams p;
    p.shape = v (id::shape);
    p.sub = v (id::sub);
    p.oscDrive = v (id::oscDrive);
    p.fmAmount = v (id::fmAmount);
    p.fmRatio = juce::jlimit (0, numFmRatios - 1, juce::roundToInt (v (id::fmRatio)));
    p.fmDecayMs = v (id::fmDecay);
    p.oscMix = v (id::oscMix);
    p.cutoffHz = v (id::cutoff);
    p.resonance = v (id::resonance);
    p.envAmount = v (id::envAmount);
    p.filterMode = (FilterMode) juce::roundToInt (v (id::filterMode));
    p.distType = (DistType) juce::roundToInt (v (id::distType));
    p.distDrive = v (id::distDrive);
    p.distTone = v (id::distTone);
    p.distMix = v (id::distMix);
    p.dark = v (id::dark);
    p.bite = v (id::bite);
    p.punch = v (id::punch);
    p.width = v (id::width);
    p.splitHz = v (id::splitFreq);
    p.attackMs = v (id::attack);
    p.decayMs = v (id::decay);
    p.sustain = v (id::sustain);
    p.releaseMs = v (id::release);
    p.glideMs = v (id::glide);
    p.glideMode = (GlideMode) juce::roundToInt (v (id::glideMode));
    p.masterDb = v (id::master);
    p.filterPostDist = v (id::filterPos) > 0.5f;
    p.oversampleIndex = juce::jlimit (0, 2, juce::roundToInt (v (id::oversample)));
    p.fltDecayMs = v (id::fltDecay);
    p.velSens = v (id::velSens);
    p.bendRange = v (id::bendRange);
    p.detune = v (id::detune);
    p.stereoDelayMs = v (id::stereoDelay);
    return p;
}
}
