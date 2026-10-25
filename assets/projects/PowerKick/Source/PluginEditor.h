#pragma once

#include "PluginProcessor.h"

namespace pk::ui
{
namespace colours
{
    // Warm ink to soft white; never pure black.
    inline const juce::Colour bgTop    { 0xff2c2f36 };
    inline const juce::Colour bgBottom { 0xff121318 };
    inline const juce::Colour panelTop { 0xff33363d };
    inline const juce::Colour panelBot { 0xff1d1f24 };
    inline const juce::Colour line     { 0x1affffff };
    inline const juce::Colour text     { 0xffeceae6 };
    inline const juce::Colour dim      { 0xff9a9ca3 };
    inline const juce::Colour faint    { 0xff5b5e66 };
    inline const juce::Colour accent   { 0xffefebe5 }; // warm white, the only highlight
    inline const juce::Colour accentLo { 0xff8e9199 };
    inline const juce::Colour hot      { 0xffff4d4d };
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Font getPopupMenuFont() override;
};

// Flat knob with its name above the dial and the value always visible below.
// An optional sub-selector (e.g. drive mode) sits under the value.
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& title,
          const juce::String& tooltip, bool small = false);
    void setSelector (const juce::String& choiceParamId);
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    juce::Slider slider;

private:
    juce::AudioProcessorValueTreeState& state;
    juce::String title, selectorId;
    bool small;
    juce::Rectangle<int> selectorArea;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A row of buttons bound to a choice (or on/off) parameter.
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

// Deeper voicing, hidden behind the ADVANCED button: pitch envelope, body, drive dynamics,
// length and engine options. Every control is neutral in the factory presets unless used.
class AdvancedPanel : public juce::Component
{
public:
    explicit AdvancedPanel (juce::AudioProcessorValueTreeState&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh(); // enables KEY ROOT only with key tracking on

    std::function<void()> onClose;

private:
    juce::AudioProcessorValueTreeState& state;
    Knob pitchDepth, pitchTime, pitchCurve, pitchFall, keyRoot, clickTone, rumbleTime;
    Knob bodyTone, subLevel, startPhase, driveEnv, velDrive, modDrive, velSens;
    Segment beatLength, rumbleMode, choke, oversample;
    juce::TextButton closeButton { "CLOSE" };
    struct Label { juce::String text; juce::Rectangle<int> area; };
    std::vector<Label> labels;
};

// Preset browser: opens over the controls from the preset name. Categories on the left, a search field
// and sound-type filters on top, every matching preset in one scrolling list. Choosing a preset loads it
// at once (and plays it when AUDITION is on); arrow keys step through the list, Enter or Escape closes.
class PresetBrowser : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PresetBrowser (PowerKickProcessor&);
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
    float scale() const { return getWidth() / 872.0f; }

    PowerKickProcessor& proc;
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

class WaveformView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit WaveformView (PowerKickProcessor&);
    void setAudio (const juce::AudioBuffer<float>& mono, double sampleRate);
    void flash() { flashLevel = 1.0f; repaint(); }
    void decay();
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    PowerKickProcessor& proc;
    std::vector<float> minV, maxV;
    double lengthMs = 0.0;
    float flashLevel = 0.0f;
    bool hover = false, dragStarted = false;
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
}

class PowerKickEditor : public juce::AudioProcessorEditor,
                        private juce::Timer,
                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit PowerKickEditor (PowerKickProcessor&);
    ~PowerKickEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void parameterChanged (const juce::String&, float) override { soundDirty = true; }

    void showMainMenu();
    void showExportMenu();
    void stepPreset (int delta);
    void refreshHeader();
    void askForName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done);
    void showError (const juce::String& message);
    void exportWav (bool stereo);

    PowerKickProcessor& proc;
    pk::ui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton, aButton { "A" }, bButton { "B" },
        copyButton { "A>B" }, menuButton { "..." }, previewButton { "PREVIEW" }, exportButton { "EXPORT WAV" },
        variationButton { "VARIATION" }, undoButton { "UNDO" }, advancedButton { "ADVANCED" };
    std::array<juce::TextButton, 5> charButtons;

    pk::ui::WaveformView waveform;
    pk::ui::Meter meter;
    pk::ui::Knob tune, decay, punch, body, click, drive, character, rumble, output, width, delay, split;
    pk::ui::AdvancedPanel advanced;
    pk::ui::PresetBrowser browser;

    std::unique_ptr<juce::FileChooser> chooser;
    bool soundDirty = true;
    int lastHit = 0, dirtyAge = 0;
    juce::Rectangle<int> displayArea, knobArea, footerArea, monoBadge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerKickEditor)
};
