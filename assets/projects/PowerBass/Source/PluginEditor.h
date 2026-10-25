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

// A knob with its name above and its value below. The primary knob of a block is larger and
// carries the accent; the others are smaller with a neutral arc. Text scales with the window.
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& title, const juce::String& tooltip);
    void setPrimary (bool);
    bool isPrimary() const { return (bool) slider.getProperties()["primary"]; }
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;

private:
    float scale() const;
    int labelHeight() const;
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

// The current sound as a waveform, like PowerKick's: the note PREVIEW plays (C1, 0.4 s), redrawn when the
// knobs settle. A click plays it, so a preset can be heard without a MIDI keyboard (Roman, 2026-10-23).
class WaveformView : public juce::Component, public juce::SettableTooltipClient
{
public:
    WaveformView();
    std::function<void()> onClick;
    void setAudio (const juce::AudioBuffer<float>& mono, double sampleRate);
    bool hasAudio() const { return ! maxV.empty(); }
    void flash() { flashLevel = 1.0f; repaint(); }
    void decay();
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent&) override;

private:
    std::vector<float> minV, maxV;
    double lengthMs = 0.0;
    float flashLevel = 0.0f;
    bool hover = false;
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

// Preset browser: opens over the controls from the preset name. Categories on the left, a search field
// and sound-type filters on top, every matching preset in one scrolling list. Choosing a preset loads it
// at once (and plays it when AUDITION is on); arrow keys step through the list, Enter or Escape closes.
class PresetBrowser : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PresetBrowser (PowerBassProcessor&);
    ~PresetBrowser() override;
    void open();                                // rescan, show the current preset, focus the search
    void setCategory (const juce::String&);     // "" = all presets, "User" = the user's own
    void toggleGroup (const juce::String&);     // sound type; none selected = every type
    void setSearch (const juce::String&);
    void choose (int row, bool close);
    int numShown() const { return (int) shown.size(); }
    int firstVisibleRow() const { return list.getRowContainingPosition (1, 1); }

    bool keyPressed (const juce::KeyPress&) override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onLoaded, onClose;

private:
    struct SearchField : juce::TextEditor
    {
        std::function<bool (const juce::KeyPress&)> onKey;
        bool keyPressed (const juce::KeyPress& k) override { return (onKey && onKey (k)) || juce::TextEditor::keyPressed (k); }
    };
    // The category column: one row per category with its preset count; "My presets" at the bottom.
    struct Categories : juce::Component
    {
        explicit Categories (PresetBrowser& b) : owner (b) {}
        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override;
        int rowAt (int y) const;
        PresetBrowser& owner;
        int hover = -1;
    };

    int getNumRows() override { return numShown(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int lastRow) override;
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { close(); }
    void returnKeyPressed (int) override { close(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void refilter();
    void rebuildGroups();
    void selectCurrent();
    void close() { if (onClose) onClose(); }
    float scale() const { return getWidth() / 1032.0f; }

    PowerBassProcessor& proc;
    SearchField search;
    juce::TextButton auditionButton { "AUDITION" }, closeButton { "CLOSE" }, allTypesButton { "ALL TYPES" };
    juce::OwnedArray<juce::TextButton> groupButtons;
    Categories categories { *this };
    juce::ListBox list { "Presets", this };
    juce::StringArray categoryNames, groupNames, selectedGroups;
    juce::String category;
    std::vector<int> shown;
    bool updating = false;
    int hoverRow = -1;
    juce::Rectangle<int> titleArea, searchArea, emptyArea;
};

// Hidden settings: filter position, oversampling, detune (reese), filter envelope decay, velocity, bend.
class AdvancedPanel : public juce::Component
{
public:
    explicit AdvancedPanel (juce::AudioProcessorValueTreeState&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Segment filterPos, oversample;
    Knob detune, fltDecay, velSens, bend;
};
}

class PowerBassEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit PowerBassEditor (PowerBassProcessor&);
    ~PowerBassEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void parameterChanged (const juce::String&, float) override { soundDirty = true; }
    void renderWaveform();
    void showMainMenu();
    void stepPreset (int delta);
    void savePreset();
    void askForName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done);
    void showError (const juce::String& message);
    void refreshHeader() { presetButton.setButtonText (proc.getPresetName()); }

    PowerBassProcessor& proc;
    pb::ui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton, saveButton { "SAVE" }, advancedButton { "ADVANCED" },
        menuButton { "..." }, previewButton { "PREVIEW" };

    pb::ui::Knob shape, sub, oscDrive, fmAmount, fmRatio, fmDecay, oscMix;
    pb::ui::Knob cutoff, resonance, envAmount;
    pb::ui::Knob distDrive, distTone, distMix;
    pb::ui::Knob dark, bite, punch;
    pb::ui::Knob width, splitFreq, stereoDelay;
    pb::ui::Knob attack, decay, sustain, release, glide, master;
    pb::ui::Segment filterMode, distType, legato, smooth;
    pb::ui::WaveformView waveform;
    std::atomic<bool> soundDirty { true };
    int dirtyAge = -1;
    pb::ui::Meter meter;
    pb::ui::AdvancedPanel advanced;
    pb::ui::PresetBrowser browser;

    std::unique_ptr<juce::FileChooser> chooser;
    struct Section { juce::String title; juce::Rectangle<int> area; };
    std::vector<Section> sections, subtitles;
    std::vector<juce::Point<float>> arrows; // signal-flow arrows between blocks

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerBassEditor)
};
