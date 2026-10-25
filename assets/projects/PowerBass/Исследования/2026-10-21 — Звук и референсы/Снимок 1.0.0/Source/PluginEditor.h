#pragma once

#include "PluginProcessor.h"

namespace pb::ui
{
namespace colours
{
    // Graphite to warm white with a single orange accent (same family as PowerKick).
    inline const juce::Colour bgTop    { 0xff2a2d33 };
    inline const juce::Colour bgBottom { 0xff111216 };
    inline const juce::Colour panelTop { 0xff2f3238 };
    inline const juce::Colour panelBot { 0xff1b1d21 };
    inline const juce::Colour line     { 0x14ffffff };
    inline const juce::Colour text     { 0xffeceae6 };
    inline const juce::Colour dim      { 0xff9a9ca3 };
    inline const juce::Colour faint    { 0xff5b5e66 };
    inline const juce::Colour accent   { 0xffff7a1f };
    inline const juce::Colour ink      { 0xff16171b };
    inline const juce::Colour hot      { 0xffff4d4d };
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Font getPopupMenuFont() override;
};

class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& title, const juce::String& tooltip);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;

private:
    juce::String title;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Row of buttons bound to a choice (or bool) parameter.
class Segment : public juce::Component, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    Segment (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::StringArray& labels,
             const juce::String& tooltip);
    ~Segment() override;
    void resized() override;

private:
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    juce::AudioProcessorValueTreeState& state;
    juce::String paramId;
    juce::OwnedArray<juce::TextButton> buttons;
};

class Scope : public juce::Component
{
public:
    explicit Scope (PowerBassProcessor& p) : proc (p) {}
    void refresh();
    void paint (juce::Graphics&) override;

private:
    PowerBassProcessor& proc;
    std::array<float, 1024> shown {};
};

class Meter : public juce::Component
{
public:
    void setLevels (float l, float r);
    void paint (juce::Graphics&) override;

private:
    float levels[2] {}, held[2] {};
    int holdFrames[2] {};
};

// Hidden settings: filter position, oversampling, filter envelope decay, velocity, bend.
class AdvancedPanel : public juce::Component
{
public:
    explicit AdvancedPanel (juce::AudioProcessorValueTreeState&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Segment filterPos, oversample;
    Knob fltDecay, velSens, bend;
};
}

class PowerBassEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PowerBassEditor (PowerBassProcessor&);
    ~PowerBassEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPresetMenu();
    void showMainMenu();
    void stepPreset (int delta);
    void savePreset();
    void askForName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done);
    void showError (const juce::String& message);
    void refreshHeader() { presetButton.setButtonText (proc.currentPresetName); }

    PowerBassProcessor& proc;
    pb::ui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton, saveButton { "SAVE" }, advancedButton { "ADVANCED" },
        menuButton { "..." }, previewButton { "PREVIEW" };

    pb::ui::Knob shape, sub, oscDrive, fmAmount, fmRatio, fmDecay, oscMix;
    pb::ui::Knob cutoff, resonance, envAmount;
    pb::ui::Knob distDrive, distTone, distMix;
    pb::ui::Knob dark, bite, punch;
    pb::ui::Knob width, splitFreq;
    pb::ui::Knob attack, decay, sustain, release, glide, master;
    pb::ui::Segment filterMode, distType, glideMode;
    pb::ui::Scope scope;
    pb::ui::Meter meter;
    pb::ui::AdvancedPanel advanced;

    std::unique_ptr<juce::FileChooser> chooser;
    struct Section { juce::String title; juce::Rectangle<int> area; };
    std::vector<Section> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerBassEditor)
};
