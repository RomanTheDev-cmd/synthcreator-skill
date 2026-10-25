#pragma once

#include "Params.h"
#include <map>

namespace pk
{
// Plain (real-unit) values keyed by parameter ID.
using Snapshot = std::map<juce::String, float>;

struct Preset
{
    juce::String name, category;
    Snapshot values;
    juce::File file; // empty for factory presets
    juce::String group; // sub-group inside the category (e.g. "Long", "Reese"), for the browser
    bool isFactory() const { return file == juce::File(); }
};

const std::vector<Preset>& factoryPresets();

Snapshot captureSnapshot (const juce::AudioProcessorValueTreeState& state);

// Parameters added after the first release, with the value that reproduces the old sound.
// Older preset files and projects that lack them load these values, not today's defaults.
const Snapshot& legacyNeutral();
// Missing IDs fall back to parameter defaults, so a preset always defines the full sound.
void applySnapshot (juce::AudioProcessorValueTreeState& state, const Snapshot& values);
KickParams paramsFromSnapshot (const Snapshot& values);

// Portable XML format (.pkpreset): UTF-8, plain values, no absolute paths.
juce::String presetToXml (const juce::String& name, const juce::String& category, const Snapshot& values);
bool presetFromXml (const juce::String& xml, Preset& out, juce::String& error);

class PresetLibrary
{
public:
    static constexpr const char* extension = ".pkpreset";

    explicit PresetLibrary (juce::File userDirectory = defaultUserDirectory());
    static juce::File defaultUserDirectory();

    void rescan();
    const std::vector<Preset>& all() const { return presets; } // factory first, then user
    int indexOf (const juce::String& name) const;

    bool saveUser (const juce::String& name, const Snapshot& values, juce::String& error);
    bool rename (int index, const juce::String& newName, juce::String& error);
    bool remove (int index, juce::String& error);
    bool importFile (const juce::File& source, juce::String& importedName, juce::String& error);
    bool exportTo (const juce::File& target, const juce::String& name, const Snapshot& values, juce::String& error) const;

private:
    juce::File userDir;
    std::vector<Preset> presets;
    juce::File fileFor (const juce::String& name) const;
};
}
