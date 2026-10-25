#include "Params.h"

namespace pk
{
namespace
{
using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using APB = juce::AudioParameterBool;
using Attr = juce::AudioParameterFloatAttributes;

juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

std::unique_ptr<APF> unit (const char* pid, const char* name, float def)
{
    return std::make_unique<APF> (juce::ParameterID { pid, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), def,
                                  Attr().withStringFromValueFunction (pct));
}
}

float baseFrequency (float tuneSemitones) noexcept
{
    // TUNE 0 = A1 (55 Hz): measured hard-techno kicks settle at 50-69 Hz.
    return 55.0f * std::pow (2.0f, tuneSemitones / 12.0f);
}

juce::String noteNameForTune (float semitones)
{
    const auto midi = 33.0f + semitones; // A1 = MIDI 33
    const auto nearest = juce::roundToInt (midi);
    const auto cents = juce::roundToInt ((midi - (float) nearest) * 100.0f);
    auto text = juce::MidiMessage::getMidiNoteName (nearest, true, true, 4);
    if (cents != 0)
        text << (cents > 0 ? " +" : " ") << cents << "c";
    return text;
}

float decayMs (float decayNorm) noexcept
{
    return 60.0f + 1440.0f * std::pow (juce::jlimit (0.0f, 1.0f, decayNorm), 1.7f);
}

const juce::StringArray& allParamIds()
{
    static const juce::StringArray ids { id::tune, id::decay, id::punch, id::body, id::click, id::drive,
                                         id::character, id::rumble, id::output, id::width, id::delay,
                                         id::driveMode, id::charMode, id::pitchDepth, id::pitchTime,
                                         id::clickType, id::clickTone, id::rumbleTime, id::rumbleSync,
                                         id::velSens, id::keyTrack, id::bodyTone, id::pitchFall, id::splitFreq,
                                         id::beatLength, id::subLevel, id::startPhase, id::driveEnv, id::velDrive,
                                         id::oversample, id::pitchCurve, id::keyRoot, id::modDrive, id::choke, id::rumbleMode };
    return ids;
}

// Parameter defaults equal the default preset "Hard Techno Core", so a fresh instance and every
// double-click reset give the same fitted, full kick.
juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<APF> (juce::ParameterID { id::tune, 1 }, "Tune",
                                       juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), -2.5f,
                                       Attr().withStringFromValueFunction ([] (float v, int) { return noteNameForTune (v); })));
    layout.add (std::make_unique<APF> (juce::ParameterID { id::decay, 1 }, "Decay",
                                       juce::NormalisableRange<float> (0.0f, 1.0f), 0.429f,
                                       Attr().withStringFromValueFunction ([] (float v, int)
                                       { return juce::String (juce::roundToInt (decayMs (v))) + " ms"; })));
    layout.add (unit (id::punch, "Punch", 0.029f));
    layout.add (unit (id::body, "Body", 0.8f));
    layout.add (unit (id::click, "Click", 1.0f));
    layout.add (unit (id::drive, "Drive", 0.38f));
    layout.add (unit (id::character, "Character", 0.8f));
    layout.add (unit (id::rumble, "Rumble", 0.0f));
    layout.add (std::make_unique<APF> (juce::ParameterID { id::output, 1 }, "Output",
                                       juce::NormalisableRange<float> (-24.0f, 6.0f, 0.1f), -3.1f,
                                       Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
                                       { return juce::String (v, 1) + " dB"; })));
    layout.add (std::make_unique<APF> (juce::ParameterID { id::width, 1 }, "Stereo Width",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 20.0f,
                                       Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int)
                                       { return juce::String (juce::roundToInt (v)) + " %"; })));
    layout.add (std::make_unique<APF> (juce::ParameterID { id::delay, 1 }, "Stereo Delay",
                                       juce::NormalisableRange<float> (0.0f, 15.0f, 0.01f, 0.6f), 0.0f,
                                       Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int)
                                       { return juce::String (v, 2) + " ms"; })));
    layout.add (std::make_unique<APC> (juce::ParameterID { id::driveMode, 1 }, "Drive Mode",
                                       juce::StringArray { "Saturate", "Clip", "Industrial" }, 1));
    layout.add (std::make_unique<APC> (juce::ParameterID { id::charMode, 1 }, "Character Mode",
                                       juce::StringArray { "Deep", "Punchy", "Dark", "Hard", "Raw" }, 1));

    layout.add (std::make_unique<APF> (juce::ParameterID { id::pitchDepth, 1 }, "Pitch Depth",
                                       juce::NormalisableRange<float> (0.0f, 48.0f, 0.1f), 39.0f,
                                       Attr().withLabel ("st").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " st"; })));
    layout.add (std::make_unique<APF> (juce::ParameterID { id::pitchTime, 1 }, "Pitch Time",
                                       juce::NormalisableRange<float> (4.0f, 150.0f, 0.1f, 0.5f), 50.9f,
                                       Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " ms"; })));
    layout.add (std::make_unique<APC> (juce::ParameterID { id::clickType, 1 }, "Click Type",
                                       juce::StringArray { "Soft", "Sharp", "Metal", "909 Noise", "FM Blip" }, 1));
    layout.add (unit (id::clickTone, "Click Tone", 0.5f));
    layout.add (unit (id::rumbleTime, "Rumble Time", 0.98f));
    layout.add (std::make_unique<APC> (juce::ParameterID { id::rumbleSync, 1 }, "Rumble Sync",
                                       juce::StringArray { "1/16", "1/8", "3/16" }, 0));
    layout.add (unit (id::velSens, "Velocity Sens", 0.6f));
    layout.add (std::make_unique<APB> (juce::ParameterID { id::keyTrack, 1 }, "Key Track", false));
    layout.add (unit (id::bodyTone, "Body Tone", 0.5f));
    {
        juce::NormalisableRange<float> range (150.0f, 2000.0f);
        range.setSkewForCentre (500.0f);
        layout.add (std::make_unique<APF> (juce::ParameterID { id::splitFreq, 1 }, "Stereo Split", range, 600.0f,
                                           Attr().withStringFromValueFunction ([] (float v, int)
                                           { return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz" : juce::String (v / 1000.0f, 1) + " kHz"; })));
    }
    layout.add (std::make_unique<APF> (juce::ParameterID { id::pitchFall, 1 }, "Pitch Fall",
                                       juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f), 13.6f,
                                       Attr().withLabel ("st").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " st"; })));
    // ADVANCED (neutral defaults)
    layout.add (std::make_unique<APC> (juce::ParameterID { id::beatLength, 1 }, "Beat Length", juce::StringArray { "Off", "1/4", "1/8", "1/16" }, 0));
    layout.add (unit (id::subLevel, "Sub Layer", 0.0f));
    layout.add (unit (id::startPhase, "Start Phase", 0.0f));
    layout.add (unit (id::driveEnv, "Drive Attack", 0.0f));
    layout.add (unit (id::velDrive, "Velocity Drive", 0.0f));
    layout.add (std::make_unique<APC> (juce::ParameterID { id::oversample, 1 }, "Oversampling", juce::StringArray { "2x", "4x", "8x" }, 1));
    layout.add (unit (id::pitchCurve, "Pitch Curve", 0.0f));
    layout.add (std::make_unique<APF> (juce::ParameterID { id::keyRoot, 1 }, "Key Root", juce::NormalisableRange<float> (24.0f, 84.0f, 1.0f), 60.0f,
                                       Attr().withStringFromValueFunction ([] (float v, int) { return juce::MidiMessage::getMidiNoteName (juce::roundToInt (v), true, true, 3); })));
    layout.add (unit (id::modDrive, "Mod Wheel Drive", 0.0f));
    layout.add (std::make_unique<APB> (juce::ParameterID { id::choke, 1 }, "Choke", false));
    layout.add (std::make_unique<APC> (juce::ParameterID { id::rumbleMode, 1 }, "Rumble Mode", juce::StringArray { "Classic", "Reverb" }, 0));
    return layout;
}

KickParams readParams (const juce::AudioProcessorValueTreeState& s)
{
    auto v = [&s] (const char* pid) { return s.getRawParameterValue (pid)->load(); };
    KickParams p;
    p.tune = v (id::tune);
    p.decay = v (id::decay);
    p.punch = v (id::punch);
    p.body = v (id::body);
    p.click = v (id::click);
    p.drive = v (id::drive);
    p.character = v (id::character);
    p.rumble = v (id::rumble);
    p.outputDb = v (id::output);
    p.width = v (id::width);
    p.delayMs = v (id::delay);
    p.driveMode = (DriveMode) juce::roundToInt (v (id::driveMode));
    p.charMode = (CharMode) juce::roundToInt (v (id::charMode));
    p.pitchDepth = v (id::pitchDepth);
    p.pitchTime = v (id::pitchTime);
    p.clickType = (ClickType) juce::roundToInt (v (id::clickType));
    p.clickTone = v (id::clickTone);
    p.rumbleTime = v (id::rumbleTime);
    p.rumbleSync = juce::roundToInt (v (id::rumbleSync));
    p.velSens = v (id::velSens);
    p.keyTrack = v (id::keyTrack) > 0.5f;
    p.bodyTone = v (id::bodyTone);
    p.pitchFall = v (id::pitchFall);
    p.splitHz = v (id::splitFreq);
    p.beatLength = juce::roundToInt (v (id::beatLength));
    p.subLevel = v (id::subLevel);
    p.startPhase = v (id::startPhase);
    p.driveEnv = v (id::driveEnv);
    p.velDrive = v (id::velDrive);
    p.oversampleIndex = juce::roundToInt (v (id::oversample));
    p.pitchCurve = v (id::pitchCurve);
    p.keyRoot = juce::roundToInt (v (id::keyRoot));
    p.modDrive = v (id::modDrive);
    p.choke = v (id::choke) > 0.5f;
    p.rumbleMode = juce::roundToInt (v (id::rumbleMode));
    return p;
}
}
