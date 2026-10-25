#include "Presets.h"

namespace pb
{
namespace
{
juce::String sanitiseName (juce::String name)
{
    name = name.trim().removeCharacters ("\\/:*?\"<>|");
    return name.isEmpty() ? juce::String ("Untitled") : name.substring (0, 64);
}

struct Def { const char* name; const char* category; std::initializer_list<std::pair<const char*, float>> values; const char* group = ""; };

// Distortion types: 0 Saturate, 1 Hard Clip, 2 Fold, 3 Destroy, 4 Crush.
// Filter modes: 0 LP24, 1 LP12, 2 BP, 3 HP. FM ratio is an index into fmRatios:
// 0 0.5 | 1 1 | 2 1.41 | 3 1.5 | 4 1.73 | 5 2 | 6 2.5 | 7 2.76 | 8 3 | 9 3.14 | 10 3.5 | 11 4 | 12 4.23 | 13 5 | 14 5.4 | 15 6 | 16 7 | 17 7.13
// "master" is filled in by level calibration (equal sustained loudness).
const Def defs[] = {
#include "FactoryPresets.inc"
};
}

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> list = []
    {
        std::vector<Preset> v;
        for (const auto& d : defs)
        {
            Preset p { d.name, d.category, {}, {} };
            p.group = d.group;
            for (const auto& [key, value] : d.values)
                p.values[key] = value;
            v.push_back (std::move (p));
        }
        return v;
    }();
    return list;
}

const Snapshot& legacyNeutral()
{
    static const Snapshot neutral { { id::detune, 0.0f }, { id::stereoDelay, 0.0f } };
    return neutral;
}

Snapshot captureSnapshot (const juce::AudioProcessorValueTreeState& state)
{
    Snapshot s;
    for (const auto& pid : allParamIds())
        s[pid] = state.getRawParameterValue (pid)->load();
    return s;
}

void applySnapshot (juce::AudioProcessorValueTreeState& state, const Snapshot& values)
{
    for (const auto& pid : allParamIds())
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (pid));
        if (param == nullptr)
            continue;
        const auto found = values.find (pid);
        const float norm = found != values.end() ? param->convertTo0to1 (found->second) : param->getDefaultValue();
        param->beginChangeGesture();
        param->setValueNotifyingHost (norm);
        param->endChangeGesture();
    }
}

SynthParams paramsFromSnapshot (const Snapshot& s)
{
    SynthParams p;
    auto get = [&s] (const char* pid, float def) { auto it = s.find (pid); return it != s.end() ? it->second : def; };
    p.shape = get (id::shape, p.shape);
    p.sub = get (id::sub, p.sub);
    p.oscDrive = get (id::oscDrive, p.oscDrive);
    p.fmAmount = get (id::fmAmount, p.fmAmount);
    p.fmRatio = juce::jlimit (0, numFmRatios - 1, juce::roundToInt (get (id::fmRatio, (float) p.fmRatio)));
    p.fmDecayMs = get (id::fmDecay, p.fmDecayMs);
    p.oscMix = get (id::oscMix, p.oscMix);
    p.cutoffHz = get (id::cutoff, p.cutoffHz);
    p.resonance = get (id::resonance, p.resonance);
    p.envAmount = get (id::envAmount, p.envAmount);
    p.filterMode = (FilterMode) juce::roundToInt (get (id::filterMode, (float) p.filterMode));
    p.distType = (DistType) juce::roundToInt (get (id::distType, (float) p.distType));
    p.distDrive = get (id::distDrive, p.distDrive);
    p.distTone = get (id::distTone, p.distTone);
    p.distMix = get (id::distMix, p.distMix);
    p.dark = get (id::dark, p.dark);
    p.bite = get (id::bite, p.bite);
    p.punch = get (id::punch, p.punch);
    p.width = get (id::width, p.width);
    p.splitHz = get (id::splitFreq, p.splitHz);
    p.attackMs = get (id::attack, p.attackMs);
    p.decayMs = get (id::decay, p.decayMs);
    p.sustain = get (id::sustain, p.sustain);
    p.releaseMs = get (id::release, p.releaseMs);
    p.glideMs = get (id::glide, p.glideMs);
    p.glideMode = (GlideMode) juce::roundToInt (get (id::glideMode, (float) p.glideMode));
    p.masterDb = get (id::master, p.masterDb);
    p.filterPostDist = get (id::filterPos, 0.0f) > 0.5f;
    p.oversampleIndex = juce::jlimit (0, 2, juce::roundToInt (get (id::oversample, (float) p.oversampleIndex)));
    p.fltDecayMs = get (id::fltDecay, p.fltDecayMs);
    p.velSens = get (id::velSens, p.velSens);
    p.bendRange = get (id::bendRange, p.bendRange);
    p.detune = get (id::detune, p.detune);
    p.stereoDelayMs = get (id::stereoDelay, p.stereoDelayMs);
    return p;
}

juce::String presetToXml (const juce::String& name, const juce::String& category, const Snapshot& values)
{
    juce::XmlElement root ("PowerBassPreset");
    root.setAttribute ("format", 1);
    root.setAttribute ("name", name);
    root.setAttribute ("category", category);
    for (const auto& [pid, value] : values)
    {
        auto* e = root.createNewChildElement ("Param");
        e->setAttribute ("id", pid);
        e->setAttribute ("value", (double) value);
    }
    return root.toString();
}

bool presetFromXml (const juce::String& xml, Preset& out, juce::String& error)
{
    auto root = juce::parseXML (xml);
    if (root == nullptr || ! root->hasTagName ("PowerBassPreset"))
    {
        error = "Not a PowerBass preset";
        return false;
    }
    if (root->getIntAttribute ("format", 0) > 1)
    {
        error = "Preset was made by a newer PowerBass version";
        return false;
    }
    out.name = sanitiseName (root->getStringAttribute ("name"));
    out.category = root->getStringAttribute ("category", "User");
    out.values.clear();
    const auto& known = allParamIds();
    for (auto* e : root->getChildWithTagNameIterator ("Param"))
    {
        const auto pid = e->getStringAttribute ("id");
        const auto value = (float) e->getDoubleAttribute ("value", std::numeric_limits<double>::quiet_NaN());
        if (known.contains (pid) && std::isfinite (value))
            out.values[pid] = value;
    }
    if (! out.values.empty())
        for (const auto& [pid, value] : legacyNeutral())
            if (out.values.find (pid) == out.values.end())
                out.values[pid] = value;
    if (out.values.empty())
    {
        error = "Preset contains no PowerBass parameters";
        return false;
    }
    return true;
}

//==============================================================================
PresetLibrary::PresetLibrary (juce::File dir) : userDir (std::move (dir)) { rescan(); }

juce::File PresetLibrary::defaultUserDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("PowerBass/Presets");
}

juce::File PresetLibrary::fileFor (const juce::String& name) const
{
    return userDir.getChildFile (sanitiseName (name) + extension);
}

void PresetLibrary::rescan()
{
    presets = factoryPresets();
    juce::Array<juce::File> files = userDir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension);
    files.sort();
    for (const auto& f : files)
    {
        Preset p;
        juce::String err;
        if (presetFromXml (f.loadFileAsString(), p, err))
        {
            p.name = f.getFileNameWithoutExtension();
            p.category = "User";
            p.file = f;
            presets.push_back (std::move (p));
        }
    }
}

int PresetLibrary::indexOf (const juce::String& name) const
{
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].name == name)
            return (int) i;
    return -1;
}

bool PresetLibrary::saveUser (const juce::String& name, const Snapshot& values, juce::String& error)
{
    if (! userDir.createDirectory())
    {
        error = "Cannot create " + userDir.getFullPathName();
        return false;
    }
    const auto file = fileFor (name);
    if (! file.replaceWithText (presetToXml (sanitiseName (name), "User", values)))
    {
        error = "Cannot write " + file.getFullPathName();
        return false;
    }
    rescan();
    return true;
}

bool PresetLibrary::rename (int index, const juce::String& newName, juce::String& error)
{
    if (index < 0 || index >= (int) presets.size() || presets[(size_t) index].isFactory())
    {
        error = "Factory presets cannot be renamed";
        return false;
    }
    const auto target = fileFor (newName);
    if (target.existsAsFile())
    {
        error = "A preset with this name already exists";
        return false;
    }
    const auto values = presets[(size_t) index].values;
    const auto old = presets[(size_t) index].file;
    if (! target.replaceWithText (presetToXml (sanitiseName (newName), "User", values)) || ! old.deleteFile())
    {
        error = "Rename failed";
        return false;
    }
    rescan();
    return true;
}

bool PresetLibrary::remove (int index, juce::String& error)
{
    if (index < 0 || index >= (int) presets.size() || presets[(size_t) index].isFactory())
    {
        error = "Factory presets cannot be deleted";
        return false;
    }
    // Moved to the trash rather than destroyed, so a mistaken delete can be undone.
    const auto file = presets[(size_t) index].file;
    // Only ever to the Trash: if that is impossible the file stays (never a silent hard delete).
    if (! file.moveToTrash())
    {
        error = "Could not move the preset to the Trash; nothing was deleted";
        return false;
    }
    rescan();
    return true;
}

bool PresetLibrary::importFile (const juce::File& source, juce::String& importedName, juce::String& error)
{
    Preset p;
    if (! presetFromXml (source.loadFileAsString(), p, error))
        return false;
    auto name = p.name.isNotEmpty() ? p.name : source.getFileNameWithoutExtension();
    for (int n = 2; fileFor (name).existsAsFile() || indexOf (name) >= 0; ++n)
        name = p.name + " " + juce::String (n);
    importedName = name;
    return saveUser (name, p.values, error);
}

bool PresetLibrary::exportTo (const juce::File& target, const juce::String& name, const Snapshot& values,
                              juce::String& error) const
{
    if (! target.replaceWithText (presetToXml (sanitiseName (name), "User", values)))
    {
        error = "Cannot write " + target.getFullPathName();
        return false;
    }
    return true;
}
}
