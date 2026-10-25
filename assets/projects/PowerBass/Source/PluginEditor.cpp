#include "PluginEditor.h"

using namespace pb::ui;

namespace
{
juce::Font uiFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

void fillPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 10.0f)
{
    g.setGradientFill (juce::ColourGradient (colours::panelTop, r.getX(), r.getY(), colours::panelBot, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.09f), r.getX(), r.getY(),
                                             juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY() + r.getHeight() * 0.5f, false));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, colours::panelBot);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, colours::ink);
    setColour (juce::PopupMenu::headerTextColourId, colours::dim);
    setColour (juce::TooltipWindow::backgroundColourId, colours::panelTop);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::outlineColourId, colours::line);
    setColour (juce::AlertWindow::backgroundColourId, colours::panelBot);
    setColour (juce::AlertWindow::textColourId, colours::text);
    setColour (juce::AlertWindow::outlineColourId, colours::line);
    setColour (juce::TextEditor::backgroundColourId, colours::bgBottom);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::outlineColourId, colours::line);
    setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour (juce::TextButton::buttonColourId, colours::panelTop);
    setColour (juce::TextButton::textColourOffId, colours::text);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float track = std::max (2.5f, radius * 0.09f);
    const float angle = start + pos * (end - start);

    const float face = radius - track * 2.2f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff474a52), c.x, c.y - face, juce::Colour (0xff1f2126), c.x, c.y + face, false));
    g.fillEllipse (c.x - face, c.y - face, face * 2.0f, face * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawEllipse (c.x - face, c.y - face, face * 2.0f, face * 2.0f, 1.0f);

    const float r = radius - track * 0.5f;
    juce::Path bg;
    bg.addCentredArc (c.x, c.y, r, r, 0.0f, start, end, true);
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.strokePath (bg, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = s.getProperties().contains ("bipolar");
    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path val;
        val.addCentredArc (c.x, c.y, r, r, 0.0f, std::min (from, angle), std::max (from, angle), true);
        // The accent marks the primary knob of each block; the others draw a quieter neutral arc.
        const bool primary = (bool) s.getProperties()["primary"];
        g.setColour (! s.isEnabled() ? colours::faint : (primary ? colours::accent : colours::text.withAlpha (0.62f)));
        g.strokePath (val, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (colours::text);
    g.drawLine ({ c.getPointOnCircumference (face * 0.38f, angle), c.getPointOnCircumference (face * 0.78f, angle) },
                std::max (2.0f, radius * 0.06f));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = std::min (8.0f, r.getHeight() * 0.5f);
    if (b.getToggleState())
    {
        g.setColour (colours::accent.withAlpha (down ? 0.8f : 1.0f));
        g.fillRoundedRectangle (r, radius);
        return;
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff383b42).brighter (over ? 0.12f : 0.0f), r.getX(), r.getY(),
                                             juce::Colour (0xff23252a), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colours::white.withAlpha (over ? 0.16f : 0.08f));
    g.drawRoundedRectangle (r, radius, 1.0f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.getToggleState() ? colours::ink : (b.isEnabled() ? colours::text : colours::faint));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.setGradientFill (juce::ColourGradient (colours::panelTop, 0, 0, colours::panelBot, 0, (float) h, false));
    g.fillAll();
    g.setColour (colours::line);
    g.drawRect (0, 0, w, h);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton& b, int h)
{
    // Grows with the window like the knob labels (1080 px wide = 1.0).
    const auto* top = b.getTopLevelComponent();
    const float s = top != nullptr && top->getWidth() > 0 ? (float) top->getWidth() / 1080.0f : 1.0f;
    return uiFont (std::min (12.5f * s, (float) h * 0.46f), true).withExtraKerningFactor (0.06f);
}
juce::Font LookAndFeel::getPopupMenuFont() { return uiFont (14.0f); }

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& t, const juce::String& tooltip)
    : title (t)
{
    setComponentID (paramId);
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityModeParameters (0.4, 1, 0.0, false, juce::ModifierKeys::shiftModifier);
    slider.setTooltip (tooltip);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (s.getParameter (paramId)))
        slider.setDoubleClickReturnValue (true, (double) p->convertFrom0to1 (p->getDefaultValue()));
    setTooltip (tooltip);
}

void Knob::setPrimary (bool p)
{
    slider.getProperties().set ("primary", p);
    resized();
    repaint();
}

float Knob::scale() const
{
    const auto* top = getTopLevelComponent();
    return top != nullptr && top->getWidth() > 0 ? (float) top->getWidth() / 1080.0f : 1.0f;
}

int Knob::labelHeight() const { return juce::roundToInt ((isPrimary() ? 20.0f : 17.0f) * scale()); }

void Knob::resized()
{
    // Name, dial and value as one group, centred in the knob's area.
    const int label = labelHeight();
    const int cap = juce::roundToInt ((isPrimary() ? 112.0f : 62.0f) * scale());
    const int d = std::max (0, std::min ({ getWidth(), getHeight() - 2 * label, cap }));
    auto group = getLocalBounds().withSizeKeepingCentre (getWidth(), d + 2 * label);
    group.removeFromTop (label);
    slider.setBounds (group.removeFromTop (d).withSizeKeepingCentre (d, d));
}

void Knob::paint (juce::Graphics& g)
{
    const float s = scale();
    const bool primary = isPrimary();
    const int label = labelHeight();
    const auto dial = slider.getBounds();
    g.setColour (primary ? colours::text : colours::dim);
    g.setFont (uiFont ((primary ? 12.0f : 10.5f) * s, true).withExtraKerningFactor (0.12f));
    g.drawText (title, juce::Rectangle<int> (0, dial.getY() - label, getWidth(), label), juce::Justification::centred);
    g.setColour (colours::text);
    g.setFont (uiFont ((primary ? 14.0f : 12.0f) * s));
    g.drawText (slider.getTextFromValue (slider.getValue()), juce::Rectangle<int> (0, dial.getBottom(), getWidth(), label),
                juce::Justification::centred);
}

//==============================================================================
Segment::Segment (juce::AudioProcessorValueTreeState& s, const juce::String& pid, const juce::StringArray& labels,
                  const juce::String& tooltip)
    : state (s), paramId (pid)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        b->setTooltip (tooltip);
        b->onClick = [this, i, single = labels.size() == 1]
        {
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (paramId)))
            {
                // One label: an on/off button for a two-state parameter (index 1 = on).
                const int now = juce::roundToInt (state.getRawParameterValue (paramId)->load());
                const float target = single ? (now == 1 ? 0.0f : 1.0f) : (float) i;
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (target));
                p->endChangeGesture();
            }
        };
        addAndMakeVisible (b);
    }
    state.addParameterListener (paramId, this);
    handleAsyncUpdate();
}

Segment::~Segment() { state.removeParameterListener (paramId, this); }

void Segment::resized()
{
    auto r = getLocalBounds();
    const int w = r.getWidth() / std::max (1, buttons.size());
    for (auto* b : buttons)
        b->setBounds (r.removeFromLeft (w).reduced (2, 0));
}

void Segment::handleAsyncUpdate()
{
    const int index = juce::roundToInt (state.getRawParameterValue (paramId)->load());
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (buttons.size() == 1 ? index == 1 : i == index, juce::dontSendNotification);
}

//==============================================================================
WaveformView::WaveformView()
{
    setTooltip ("Click to hear the sound (the C1 note PREVIEW plays)");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void WaveformView::setAudio (const juce::AudioBuffer<float>& mono, double sampleRate)
{
    const int columns = 600;
    minV.assign (columns, 0.0f);
    maxV.assign (columns, 0.0f);
    const int n = mono.getNumSamples();
    // Length of the audible sound (to -60 dB), shown top right.
    const float peak = std::max (mono.getMagnitude (0, 0, n), 1.0e-9f);
    int last = n - 1;
    while (last > 0 && std::abs (mono.getSample (0, last)) < peak * 0.001f) --last;
    lengthMs = 1000.0 * (last + 1) / sampleRate;
    for (int c = 0; c < columns; ++c) // the whole render (fixed length): presets compare at a glance
    {
        const int from = (int) ((double) c * n / columns), to = std::max (from + 1, (int) ((double) (c + 1) * n / columns));
        const auto range = juce::FloatVectorOperations::findMinAndMax (mono.getReadPointer (0, from), std::min (to, n) - from);
        minV[(size_t) c] = range.getStart();
        maxV[(size_t) c] = range.getEnd();
    }
    repaint();
}

void WaveformView::decay()
{
    if (flashLevel > 0.0f)
    {
        flashLevel = std::max (0.0f, flashLevel - 0.08f);
        repaint();
    }
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasDraggedSinceMouseDown() && onClick != nullptr)
        onClick();
}

void WaveformView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    fillPanel (g, r, 10.0f);
    auto plot = r.reduced (12.0f, 10.0f);
    const float mid = plot.getCentreY();
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine ((int) mid, plot.getX(), plot.getRight());
    const double windowMs = 900.0; // the render length (see PowerBassEditor::renderWaveform)
    for (double t = 100.0; t < windowMs; t += 100.0)
        g.drawVerticalLine ((int) (plot.getX() + plot.getWidth() * (float) (t / windowMs)), plot.getY() + 4, plot.getBottom() - 4);

    if (! maxV.empty())
    {
        juce::Path p;
        const auto cols = (int) maxV.size();
        const float half = plot.getHeight() * 0.48f;
        p.startNewSubPath (plot.getX(), mid);
        for (int c = 0; c < cols; ++c)
            p.lineTo (plot.getX() + plot.getWidth() * c / (cols - 1.0f), mid - juce::jlimit (-1.0f, 1.0f, maxV[(size_t) c]) * half);
        for (int c = cols - 1; c >= 0; --c)
            p.lineTo (plot.getX() + plot.getWidth() * c / (cols - 1.0f), mid - juce::jlimit (-1.0f, 1.0f, minV[(size_t) c]) * half);
        p.closeSubPath();
        // Accent at the centre fading into graphite at the peaks (as in PowerKick).
        juce::ColourGradient fill (juce::Colour (0xff6f727a), 0, mid - half, juce::Colour (0xff6f727a), 0, mid + half, false);
        fill.addColour (0.5, colours::accent);
        g.setGradientFill (fill);
        g.fillPath (p);
        if (flashLevel > 0.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.25f * flashLevel));
            g.fillPath (p);
        }
    }

    const auto label = r.reduced (12.0f, 8.0f).removeFromTop (14.0f);
    g.setFont (uiFont (11.0f, true).withExtraKerningFactor (0.1f));
    g.setColour (colours::dim);
    g.drawText (juce::String (juce::roundToInt (lengthMs)) + " MS", label, juce::Justification::topRight);
    g.setColour (hover ? colours::text.withAlpha (0.85f) : colours::dim.withAlpha (0.7f));
    g.drawText (juce::String::fromUTF8 ("CLICK TO PLAY \xe2\x96\xb6"), label, juce::Justification::topLeft);
}

//==============================================================================
void Meter::setLevels (float l, float r)
{
    const float in[2] = { l, r };
    for (int c = 0; c < 2; ++c)
    {
        levels[c] = std::max (in[c], levels[c] * 0.82f);
        if (in[c] >= held[c]) { held[c] = in[c]; holdFrames[c] = 45; }
        else if (--holdFrames[c] <= 0) held[c] = std::max (0.0f, held[c] - 0.02f);
    }
    repaint();
}

void Meter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float bw = (r.getWidth() - 4.0f) / 2.0f;
    auto toY = [&r] (float lin)
    {
        const float dbv = juce::jlimit (-48.0f, 0.0f, juce::Decibels::gainToDecibels (lin, -48.0f));
        return r.getBottom() - r.getHeight() * (dbv + 48.0f) / 48.0f;
    };
    for (int c = 0; c < 2; ++c)
    {
        auto bar = juce::Rectangle<float> (r.getX() + (float) c * (bw + 4.0f), r.getY(), bw, r.getHeight());
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (bar, 2.0f);
        g.setGradientFill (juce::ColourGradient (colours::accent, 0, r.getY(), colours::text.withAlpha (0.85f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (bar.withTop (toY (levels[c])), 2.0f);
        g.setColour (held[c] >= 0.999f ? colours::hot : colours::text);
        g.fillRect (bar.getX(), toY (held[c]) - 1.0f, bar.getWidth(), 2.0f);
    }
}

//==============================================================================
AdvancedPanel::AdvancedPanel (juce::AudioProcessorValueTreeState& s)
    : filterPos (s, pb::id::filterPos, { "PRE", "POST" }, "Filter before or after the distortion"),
      oversample (s, pb::id::oversample, { "2x", "4x", "8x" }, "Oversampling of the whole voice: higher = cleaner, more CPU"),
      detune (s, pb::id::detune, "DETUNE", "Two extra detuned voices on the analog oscillator: reese movement"),
      fltDecay (s, pb::id::fltDecay, "FILTER DECAY", "Decay of the filter envelope used by ENV AMOUNT"),
      velSens (s, pb::id::velSens, "VELOCITY", "How much note velocity changes the level"),
      bend (s, pb::id::bendRange, "BEND", "Pitch-bend range in semitones")
{
    for (auto* c : std::initializer_list<juce::Component*> { &filterPos, &oversample, &detune, &fltDecay, &velSens, &bend })
        addAndMakeVisible (c);
}

void AdvancedPanel::paint (juce::Graphics& g)
{
    const float s = getWidth() / 640.0f; // scales with the window
    auto S = [s] (float v) { return juce::roundToInt (v * s); };
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0, 4), 12.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff34373e), 0, r.getY(), juce::Colour (0xff1d1f24), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (colours::accent.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
    g.setFont (uiFont (11.0f * s, true).withExtraKerningFactor (0.18f));
    g.setColour (colours::dim);
    auto t = getLocalBounds().reduced (S (16), S (10));
    g.drawText ("ADVANCED", t.removeFromTop (S (16)), juce::Justification::centredLeft);
    g.drawText ("FILTER POSITION", juce::Rectangle<int> (S (16), S (34), S (160), S (14)), juce::Justification::centredLeft);
    g.drawText ("OVERSAMPLING", juce::Rectangle<int> (S (16), S (84), S (160), S (14)), juce::Justification::centredLeft);
}

void AdvancedPanel::resized()
{
    const float s = getWidth() / 640.0f;
    auto S = [s] (float v) { return juce::roundToInt (v * s); };
    filterPos.setBounds (S (14), S (50), S (160), S (26));
    oversample.setBounds (S (14), S (100), S (160), S (26));
    auto r = getLocalBounds().withTrimmedLeft (S (190)).reduced (S (6), S (28));
    const int w = r.getWidth() / 4;
    detune.setBounds (r.removeFromLeft (w));
    fltDecay.setBounds (r.removeFromLeft (w));
    velSens.setBounds (r.removeFromLeft (w));
    bend.setBounds (r);
}

//==============================================================================
namespace
{
const juce::String userCategory { "User" };

// The accent fill used by toggled buttons: the current choice.
void fillSelection (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setColour (colours::accent);
    g.fillRoundedRectangle (r, radius);
}

const juce::Colour selectedText = colours::ink;
}

PresetBrowser::PresetBrowser (PowerBassProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    search.setTextToShowWhenEmpty ("Search presets", colours::dim);
    search.setJustification (juce::Justification::centredLeft);
    search.setTooltip ("Type words of a name, a genre or a sound type");
    search.onTextChange = [this] { refilter(); };
    search.onReturnKey = [this] { close(); };
    search.onEscapeKey = [this] { close(); };
    search.onKey = [this] (const juce::KeyPress& k)
    {
        return (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey) && keyPressed (k);
    };
    auditionButton.setClickingTogglesState (true);
    auditionButton.setToggleState (true, juce::dontSendNotification);
    auditionButton.setTooltip ("Play each preset as you choose it");
    closeButton.setTooltip ("Back to the controls (Enter or Escape)");
    closeButton.onClick = [this] { close(); };
    allTypesButton.setTooltip ("Show every sound type");
    allTypesButton.onClick = [this] { selectedGroups.clear(); refilter(); };
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    list.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, colours::faint);
    list.addMouseListener (this, true);
    for (auto* c : std::initializer_list<juce::Component*> { &search, &auditionButton, &closeButton, &allTypesButton, &categories, &list })
        addAndMakeVisible (c);
}

PresetBrowser::~PresetBrowser()
{
    list.removeMouseListener (this);
}

void PresetBrowser::open()
{
    proc.library.rescan();
    categoryNames.clear();
    for (const auto& p : proc.library.all())
        if (p.category != userCategory)
            categoryNames.addIfNotAlreadyThere (p.category);
    if (category.isNotEmpty() && category != userCategory && ! categoryNames.contains (category))
        category = {};
    rebuildGroups();
    refilter();
    setVisible (true);
    if (isShowing())
        search.grabKeyboardFocus();
}

void PresetBrowser::setCategory (const juce::String& c)
{
    category = c;
    refilter();
}

void PresetBrowser::toggleGroup (const juce::String& g)
{
    if (selectedGroups.contains (g))
        selectedGroups.removeString (g);
    else
        selectedGroups.add (g);
    refilter();
}

void PresetBrowser::setSearch (const juce::String& t)
{
    search.setText (t, false);
    refilter();
}

void PresetBrowser::rebuildGroups()
{
    groupNames.clear();
    for (const auto& p : proc.library.all())
        if (p.group.isNotEmpty())
            groupNames.addIfNotAlreadyThere (p.group);
    if (groupButtons.size() == groupNames.size())
        return;
    groupButtons.clear();
    for (const auto& g : groupNames)
    {
        auto* b = groupButtons.add (new juce::TextButton());
        b->setTooltip ("Show " + g + " sounds; several types can be on together");
        b->onClick = [this, g] { toggleGroup (g); };
        addAndMakeVisible (b);
    }
    resized();
}

void PresetBrowser::refilter()
{
    const auto& all = proc.library.all();
    shown = pb::filterPresets (all, category, selectedGroups, search.getText());
    // Type counts within the chosen category, so an empty type is visibly empty before it is clicked.
    const auto inCategory = pb::filterPresets (all, category, {}, {});
    for (int i = 0; i < groupNames.size(); ++i)
    {
        int n = 0;
        for (int k : inCategory)
            n += all[(size_t) k].group == groupNames[i] ? 1 : 0;
        auto* b = groupButtons[i];
        const bool on = selectedGroups.contains (groupNames[i]);
        b->setButtonText (groupNames[i].toUpperCase() + "  " + juce::String (n));
        b->setToggleState (on, juce::dontSendNotification);
        b->setEnabled (n > 0 || on);
    }
    allTypesButton.setToggleState (selectedGroups.isEmpty(), juce::dontSendNotification);
    resized(); // pill widths follow their labels
    hoverRow = -1;
    list.updateContent();
    selectCurrent();
    categories.repaint();
    repaint();
}

void PresetBrowser::selectCurrent()
{
    const auto& all = proc.library.all();
    const auto name = proc.getPresetName();
    int row = -1;
    for (int r = 0; r < numShown() && row < 0; ++r)
        if (all[(size_t) shown[(size_t) r]].name == name)
            row = r;
    const juce::ScopedValueSetter<bool> guard (updating, true);
    if (row >= 0)
        list.selectRow (row); // scrolls to the current preset
    else
    {
        list.deselectAllRows();
        list.scrollToEnsureRowIsOnscreen (0); // a new list without it starts at the top
    }
}

void PresetBrowser::choose (int row, bool closeAfter)
{
    if (row < 0 || row >= numShown())
        return;
    proc.loadPreset (shown[(size_t) row]);
    if (auditionButton.getToggleState())
        proc.previewRequested = true;
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);
        list.selectRow (row);
    }
    if (onLoaded)
        onLoaded();
    if (closeAfter)
        close();
}

void PresetBrowser::selectedRowsChanged (int lastRow)
{
    if (! updating && lastRow >= 0)
        choose (lastRow, false);
}

bool PresetBrowser::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey || k == juce::KeyPress::returnKey)
    {
        close();
        return true;
    }
    if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey)
    {
        if (shown.empty())
            return true;
        const int current = list.getSelectedRow();
        const int next = current < 0 ? 0 : juce::jlimit (0, numShown() - 1, current + (k == juce::KeyPress::downKey ? 1 : -1));
        if (next != current)
            choose (next, false);
        list.scrollToEnsureRowIsOnscreen (next);
        return true;
    }
    return false;
}

void PresetBrowser::mouseMove (const juce::MouseEvent& e)
{
    int row = -1;
    if (e.eventComponent == &list || list.isParentOf (e.eventComponent))
        row = list.getRowContainingPosition (e.getEventRelativeTo (&list).x, e.getEventRelativeTo (&list).y);
    if (row != hoverRow)
    {
        list.repaintRow (hoverRow);
        hoverRow = row;
        list.repaintRow (hoverRow);
    }
}

void PresetBrowser::mouseExit (const juce::MouseEvent&)
{
    if (hoverRow >= 0)
    {
        list.repaintRow (hoverRow);
        hoverRow = -1;
    }
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= numShown())
        return;
    const auto& p = proc.library.all()[(size_t) shown[(size_t) row]];
    const float s = scale();
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (2.0f * s, 1.5f * s);
    if (selected)
        fillSelection (g, r, 6.0f * s);
    else if (row == hoverRow)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (r, 6.0f * s);
    }
    // Type (and genre when all genres are listed) on the right, quieter than the name.
    juce::String meta = p.category == userCategory ? juce::String ("MY PRESET") : p.group.toUpperCase();
    if (category.isEmpty() && p.category != userCategory)
        meta = p.category.toUpperCase() + (meta.isNotEmpty() ? "   " + meta : juce::String());
    auto text = r.reduced (10.0f * s, 0.0f);
    const auto metaFont = uiFont (10.5f * s, true).withExtraKerningFactor (0.08f);
    const float metaW = (float) juce::GlyphArrangement::getStringWidthInt (metaFont, meta);
    g.setFont (metaFont);
    g.setColour (selected ? selectedText.withAlpha (0.65f) : colours::dim);
    g.drawText (meta, text.removeFromRight (metaW), juce::Justification::centredRight, false);
    text.removeFromRight (12.0f * s);
    g.setFont (uiFont (14.0f * s));
    g.setColour (selected ? selectedText : colours::text);
    g.drawText (p.name, text, juce::Justification::centredLeft, true);
}

void PresetBrowser::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float s = scale();
    // Same overlay as the ADVANCED panel.
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0, 4), 12.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff34373e), 0, r.getY(), juce::Colour (0xff1d1f24), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (colours::accent.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

    g.setFont (uiFont (11.0f * s, true).withExtraKerningFactor (0.18f));
    g.setColour (colours::text);
    g.drawText ("PRESETS", titleArea, juce::Justification::centredLeft);
    // Hairline between the category column and the list.
    g.setColour (colours::line);
    g.fillRect (juce::Rectangle<int> (categories.getRight() + juce::roundToInt (8 * s), categories.getY(), 1, categories.getHeight()));

    if (shown.empty())
    {
        const bool noOwn = category == userCategory && search.getText().trim().isEmpty() && selectedGroups.isEmpty();
        g.setColour (colours::dim);
        g.setFont (uiFont (14.0f * s));
        g.drawFittedText (noOwn ? "No presets of your own yet.\nSave one with SAVE at the top."
                                : "No presets match.\nClear the search or choose another type.",
                          emptyArea, juce::Justification::centred, 2);
    }
}

void PresetBrowser::paintOverChildren (juce::Graphics& g)
{
    // Magnifier drawn inside the search field.
    const float s = scale();
    const auto b = search.getBounds().toFloat();
    const float d = 10.0f * s;
    const juce::Point<float> c (b.getX() + 15.0f * s, b.getCentreY() - 1.0f * s);
    g.setColour (colours::dim);
    g.drawEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d, 1.4f * s);
    const auto from = c.getPointOnCircumference (d * 0.5f, juce::MathConstants<float>::pi * 0.75f);
    g.drawLine ({ from, from + juce::Point<float> (3.5f * s, 3.5f * s) }, 1.6f * s);
}

void PresetBrowser::resized()
{
    const float s = scale();
    auto S = [s] (float v) { return juce::roundToInt (v * s); };
    auto r = getLocalBounds().reduced (S (16), S (12));

    auto top = r.removeFromTop (S (30));
    closeButton.setBounds (top.removeFromRight (S (72)).withSizeKeepingCentre (S (72), S (24)));
    top.removeFromRight (S (8));
    auditionButton.setBounds (top.removeFromRight (S (100)).withSizeKeepingCentre (S (100), S (24)));
    titleArea = top.removeFromLeft (S (200));
    const int searchW = std::min (top.getWidth() - S (16), S (340));
    searchArea = top.removeFromLeft (searchW).withSizeKeepingCentre (searchW, S (28));
    search.setBounds (searchArea);
    const auto font = uiFont (14.0f * s);
    search.setFont (font);
    search.applyFontToAllText (font);
    search.setIndents (S (30), juce::jmax (0, (searchArea.getHeight() - juce::roundToInt (font.getHeight())) / 2));
    r.removeFromTop (S (12));

    auto left = r.removeFromLeft (S (230)); // bass genres have long names
    r.removeFromLeft (S (17));

    // Sound types as pills, wrapping onto a second line when the window is small.
    const int pillH = S (26), gap = S (6);
    auto pillWidth = [&] (juce::TextButton& b)
    {
        auto& lf = getLookAndFeel();
        return juce::GlyphArrangement::getStringWidthInt (lf.getTextButtonFont (b, pillH), b.getButtonText()) + S (20);
    };
    std::vector<juce::TextButton*> pills { &allTypesButton };
    for (auto* b : groupButtons)
        pills.push_back (b);
    int x = r.getX(), y = r.getY();
    for (auto* b : pills)
    {
        const int w = pillWidth (*b);
        if (x > r.getX() && x + w > r.getRight())
        {
            x = r.getX();
            y += pillH + gap;
        }
        b->setBounds (x, y, w, pillH);
        x += w + gap;
    }
    r.setTop (y + pillH + S (12));
    list.setRowHeight (S (28));
    list.setBounds (r);
    emptyArea = r.withHeight (S (90));
    categories.setBounds (left); // starts level with the type filters, so a second filter line leaves no gap
}

int PresetBrowser::Categories::rowAt (int y) const
{
    const float s = owner.scale();
    const int rowH = juce::roundToInt (30 * s), n = owner.categoryNames.size() + 1;
    if (y < rowH * n)
        return y / rowH;
    const int userTop = rowH * n + juce::roundToInt (14 * s);
    return y >= userTop && y < userTop + rowH ? n : -1;
}

void PresetBrowser::Categories::paint (juce::Graphics& g)
{
    const float s = owner.scale();
    const int rowH = juce::roundToInt (30 * s), n = owner.categoryNames.size() + 1;
    const auto& all = owner.proc.library.all();
    for (int i = 0; i <= n; ++i)
    {
        const bool user = i == n;
        const juce::String key = i == 0 ? juce::String() : (user ? userCategory : owner.categoryNames[i - 1]);
        const juce::String label = i == 0 ? juce::String ("All presets") : (user ? juce::String ("My presets") : key);
        const int top = user ? rowH * n + juce::roundToInt (14 * s) : rowH * i;
        if (user)
        {
            g.setColour (colours::line);
            g.fillRect (juce::Rectangle<int> (0, rowH * n + juce::roundToInt (7 * s), getWidth(), 1));
        }
        auto r = juce::Rectangle<float> (0.0f, (float) top, (float) getWidth(), (float) rowH).reduced (0.0f, 1.5f * s);
        const bool selected = owner.category == key;
        if (selected)
            fillSelection (g, r, 6.0f * s);
        else if (i == hover)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (r, 6.0f * s);
        }
        auto text = r.reduced (10.0f * s, 0.0f);
        const auto count = juce::String ((int) pb::filterPresets (all, key, {}, {}).size());
        g.setFont (uiFont (12.0f * s, true));
        g.setColour (selected ? selectedText.withAlpha (0.65f) : colours::dim);
        g.drawText (count, text.removeFromRight (34.0f * s), juce::Justification::centredRight);
        g.setFont (uiFont (13.5f * s));
        g.setColour (selected ? selectedText : colours::text);
        g.drawText (label, text, juce::Justification::centredLeft, true);
    }
}

void PresetBrowser::Categories::mouseMove (const juce::MouseEvent& e)
{
    const int row = rowAt (e.y);
    if (row != hover)
    {
        hover = row;
        repaint();
    }
}

void PresetBrowser::Categories::mouseDown (const juce::MouseEvent& e)
{
    const int row = rowAt (e.y), n = owner.categoryNames.size() + 1;
    if (row < 0)
        return;
    owner.setCategory (row == 0 ? juce::String() : (row == n ? userCategory : owner.categoryNames[row - 1]));
}

//==============================================================================
PowerBassEditor::PowerBassEditor (PowerBassProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      shape (p.state, pb::id::shape, "SHAPE", "Waveform: sine, triangle, saw, square, with smooth morphing"),
      sub (p.state, pb::id::sub, "SUB", "Sine one octave below"),
      oscDrive (p.state, pb::id::oscDrive, "OSC DRIVE", "Saturation inside the oscillator"),
      fmAmount (p.state, pb::id::fmAmount, "FM AMOUNT", "FM depth: from light colour to hard metallic harmonics"),
      fmRatio (p.state, pb::id::fmRatio, "FM RATIO", "Modulator ratio: harmonic, fractional and inharmonic steps"),
      fmDecay (p.state, pb::id::fmDecay, "FM DECAY", "How fast the FM brightness falls after each note"),
      oscMix (p.state, pb::id::oscMix, "MIX", "Analog engine <-> FM engine"),
      cutoff (p.state, pb::id::cutoff, "CUTOFF", "Filter frequency"),
      resonance (p.state, pb::id::resonance, "RESONANCE", "Filter resonance"),
      envAmount (p.state, pb::id::envAmount, "ENV AMOUNT", "How far the envelope opens (+) or closes (-) the filter"),
      distDrive (p.state, pb::id::distDrive, "DRIVE", "Distortion amount; the sub below 120 Hz is protected"),
      distTone (p.state, pb::id::distTone, "TONE", "Dark and heavy <-> bright and cutting"),
      distMix (p.state, pb::id::distMix, "MIX", "Clean <-> distorted"),
      dark (p.state, pb::id::dark, "DARK", "Darker, heavier and denser, without pumping the sub"),
      bite (p.state, pb::id::bite, "BITE", "Sharper, more aggressive mids and highs"),
      punch (p.state, pb::id::punch, "PUNCH", "Short pitch and level hit at the start of each note"),
      width (p.state, pb::id::width, "WIDTH", "Stereo width of the harmonics above SPLIT; below stays mono"),
      splitFreq (p.state, pb::id::splitFreq, "SPLIT", "Everything below this frequency stays mono"),
      stereoDelay (p.state, pb::id::stereoDelay, "DELAY", "Time offset between left and right above SPLIT; the mono sum is unchanged"),
      attack (p.state, pb::id::attack, "ATTACK", "Amp envelope attack"),
      decay (p.state, pb::id::decay, "DECAY", "Amp envelope decay"),
      sustain (p.state, pb::id::sustain, "SUSTAIN", "Amp envelope sustain level"),
      release (p.state, pb::id::release, "RELEASE", "Amp envelope release"),
      glide (p.state, pb::id::glide, "GLIDE", "Portamento time"),
      master (p.state, pb::id::master, "MASTER", "Output level"),
      filterMode (p.state, pb::id::filterMode, { "LP24", "LP12", "BP", "HP" }, "Filter mode"),
      distType (p.state, pb::id::distType, { "SATURATE", "CLIP", "FOLD", "DESTROY", "CRUSH" }, "Distortion algorithm"),
      legato (p.state, pb::id::glideMode, { "LEGATO" },
              "On: a note played before the previous one is released continues it without a new attack (and slides with GLIDE). "
              "Off: every note starts anew"),
      smooth (p.state, pb::id::smoothStart, { "SMOOTH" },
              "On: notes that follow each other continue the sound without a new attack (the behaviour before October 2026). "
              "Off: every note starts with the full ATTACK"),
      advanced (p.state), browser (p)
{
    setLookAndFeel (&lnf);
    envAmount.slider.getProperties().set ("bipolar", true);
    for (auto* k : { &shape, &fmAmount, &cutoff, &distDrive, &width, &master }) // one per block, and the output level
        k->setPrimary (true);

    for (auto* c : std::initializer_list<juce::Component*> {
             &shape, &sub, &oscDrive, &fmAmount, &fmRatio, &fmDecay, &oscMix, &cutoff, &resonance, &envAmount, &distDrive, &distTone,
             &distMix, &dark, &bite, &punch, &width, &splitFreq, &stereoDelay, &attack, &decay, &sustain, &release, &glide, &master, &filterMode,
             &distType, &legato, &smooth, &waveform, &meter, &prevButton, &nextButton, &presetButton, &saveButton, &advancedButton, &menuButton,
             &previewButton })
        addAndMakeVisible (c);
    addChildComponent (advanced);
    waveform.setComponentID ("waveform");
    legato.setComponentID ("legato");
    smooth.setComponentID ("smooth");
    addChildComponent (browser); // over everything below the header when open
    presetButton.setComponentID ("presetButton");
    browser.setComponentID ("presetBrowser");
    advancedButton.setComponentID ("advancedButton");
    nextButton.setComponentID ("nextButton");

    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    presetButton.onClick = [this]
    {
        if (browser.isVisible())
        {
            browser.onClose();
            return;
        }
        if (advancedButton.getToggleState())
            advancedButton.setToggleState (false, juce::sendNotification);
        presetButton.setToggleState (true, juce::dontSendNotification);
        browser.open();
    };
    browser.onLoaded = [this] { refreshHeader(); };
    browser.onClose = [this]
    {
        browser.setVisible (false);
        presetButton.setToggleState (false, juce::dontSendNotification);
    };
    saveButton.onClick = [this] { savePreset(); };
    menuButton.onClick = [this] { showMainMenu(); };
    previewButton.onClick = [this] { proc.previewRequested = true; waveform.flash(); };
    advancedButton.setClickingTogglesState (true);
    advancedButton.onClick = [this]
    {
        if (advancedButton.getToggleState() && browser.isVisible())
            browser.onClose();
        advanced.setVisible (advancedButton.getToggleState());
    };
    presetButton.setTooltip ("Browse presets");
    saveButton.setTooltip ("Save the current sound as a user preset");
    advancedButton.setTooltip ("Filter position, oversampling, filter envelope, velocity, bend");
    menuButton.setTooltip ("Rename, delete, import and export presets");
    previewButton.setTooltip ("Play a short C1 note");

    setResizable (true, true);
    setResizeLimits (864, 576, 1620, 1080);
    getConstrainer()->setFixedAspectRatio (1080.0 / 720.0);
    setSize (1080, 720);
    refreshHeader();
    waveform.onClick = [this] { proc.previewRequested = true; waveform.flash(); };
    for (const auto& pid : pb::allParamIds())
        proc.state.addParameterListener (pid, this);
    renderWaveform();
    startTimerHz (30);
}

PowerBassEditor::~PowerBassEditor()
{
    for (const auto& pid : pb::allParamIds())
        proc.state.removeParameterListener (pid, this);
    setLookAndFeel (nullptr);
}

// The note PREVIEW plays (C1, velocity 0.9, 0.4 s), rendered offline for the waveform: 0.9 s at 24 kHz.
void PowerBassEditor::renderWaveform()
{
    constexpr double sr = 24000.0;
    constexpr int block = 512, total = (int) (0.9 * sr), off = (int) (0.4 * sr);
    const auto params = pb::readParams (proc.state);
    pb::dsp::BassEngine engine;
    engine.prepare (sr, block, params);
    juce::AudioBuffer<float> st (2, total), mono (1, total);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        juce::MidiBuffer midi;
        if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, 0.9f), 0);
        if (off >= pos && off < pos + n) midi.addEvent (juce::MidiMessage::noteOff (1, 36), off - pos);
        engine.process (st.getWritePointer (0, pos), st.getWritePointer (1, pos), n, midi, params);
    }
    for (int i = 0; i < total; ++i)
        mono.setSample (0, i, 0.5f * (st.getSample (0, i) + st.getSample (1, i)));
    waveform.setAudio (mono, sr);
}

void PowerBassEditor::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (colours::bgTop, 0, 0, colours::bgBottom, 0, r.getHeight(), false));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x12fff1e6), r.getWidth() * 0.15f, 0.0f,
                                             juce::Colour (0x00fff1e6), r.getWidth() * 0.6f, r.getHeight() * 0.6f, true));
    g.fillAll();

    const float s = (float) getWidth() / 1080.0f;
    auto header = getLocalBounds().removeFromTop (juce::roundToInt (60 * s)).reduced (juce::roundToInt (24 * s), 0);
    const auto wordFont = uiFont (19.0f * s, true).withExtraKerningFactor (0.08f);
    g.setFont (wordFont);
    g.setColour (colours::text);
    g.drawText ("POWER", header, juce::Justification::centredLeft);
    g.setColour (colours::accent);
    g.drawText ("BASS", header.withTrimmedLeft (juce::GlyphArrangement::getStringWidthInt (wordFont, "POWER") + 2), juce::Justification::centredLeft);
    g.setColour (colours::line);
    g.drawHorizontalLine (juce::roundToInt (60 * s), 0.0f, (float) getWidth());

    // Blocks of the signal chain with a readable title each.
    for (const auto& sec : sections)
    {
        fillPanel (g, sec.area.toFloat(), 12.0f * s);
        g.setFont (uiFont (12.0f * s, true).withExtraKerningFactor (0.2f));
        g.setColour (colours::text);
        g.drawText (sec.title, sec.area.reduced (juce::roundToInt (16 * s), juce::roundToInt (10 * s)).removeFromTop (juce::roundToInt (18 * s)),
                    juce::Justification::centredLeft);
    }
    g.setFont (uiFont (10.0f * s, true).withExtraKerningFactor (0.18f));
    g.setColour (colours::faint);
    for (const auto& sub : subtitles)
        g.drawText (sub.title, sub.area, juce::Justification::centred);
    // Signal flow between the blocks of a row.
    g.setColour (colours::dim);
    for (const auto& a : arrows)
    {
        const float len = 7.0f * s, head = 4.0f * s;
        g.drawLine (a.x - len, a.y, a.x + len, a.y, 1.6f * s);
        juce::Path tip;
        tip.addTriangle (a.x + len + 1.5f * s, a.y, a.x + len - head, a.y - head, a.x + len - head, a.y + head);
        g.fillPath (tip);
    }
}

void PowerBassEditor::resized()
{
    const float s = (float) getWidth() / 1080.0f;
    auto S = [s] (float v) { return juce::roundToInt (v * s); };
    sections.clear();

    // Header
    auto header = getLocalBounds().removeFromTop (S (60)).reduced (S (24), S (14));
    menuButton.setBounds (header.removeFromRight (S (40)));
    header.removeFromRight (S (8));
    advancedButton.setBounds (header.removeFromRight (S (96)));
    header.removeFromRight (S (8));
    previewButton.setBounds (header.removeFromRight (S (86)));
    auto centre = getLocalBounds().removeFromTop (S (60)).withSizeKeepingCentre (S (420), S (32));
    prevButton.setBounds (centre.removeFromLeft (S (32)));
    saveButton.setBounds (centre.removeFromRight (S (64)));
    centre.removeFromRight (S (6));
    nextButton.setBounds (centre.removeFromRight (S (32)));
    presetButton.setBounds (centre.reduced (S (6), 0));

    auto body = getLocalBounds().withTrimmedTop (S (72)).reduced (S (20), 0).withTrimmedBottom (S (16));
    subtitles.clear();
    arrows.clear();

    // Display: what the chain below produces, with OUTPUT (MASTER and its meter) at its right, as in PowerKick.
    auto display = body.removeFromTop (S (140));
    auto output = display.removeFromRight (S (150));
    display.removeFromRight (S (14));
    waveform.setBounds (display);
    meter.setBounds (output.removeFromRight (S (14)).reduced (0, S (18)));
    output.removeFromRight (S (6));
    master.setBounds (output);
    body.removeFromTop (S (14));

    // Two rows of three blocks along the signal path: SOURCE -> FM -> FILTER, DRIVE -> AMP -> STEREO.
    const int gutter = S (28), rowGap = S (14);
    const int rowH = (body.getHeight() - rowGap) / 2;
    auto rowA = body.removeFromTop (rowH);
    body.removeFromTop (rowGap);
    auto rowB = body;
    const int blockW = (rowA.getWidth() - 2 * gutter) / 3;
    auto blocks = [&] (juce::Rectangle<int> row, std::array<const char*, 3> titles)
    {
        std::array<juce::Rectangle<int>, 3> inner;
        for (int i = 0; i < 3; ++i)
        {
            const auto area = i < 2 ? row.removeFromLeft (blockW) : row;
            sections.push_back ({ titles[(size_t) i], area });
            inner[(size_t) i] = area.reduced (S (14), S (10)).withTrimmedTop (S (24));
            if (i < 2)
                arrows.push_back (row.removeFromLeft (gutter).getCentre().toFloat().withY ((float) area.getCentreY()));
        }
        return inner;
    };
    // The primary knob on the left, the others in a grid on the right.
    auto primaryAndGrid = [&] (juce::Rectangle<int> a, Knob& main, std::initializer_list<Knob*> others, int cols)
    {
        main.setBounds (a.removeFromLeft (a.getWidth() * 46 / 100));
        a.removeFromLeft (S (6));
        const int n = (int) others.size(), rows = std::max (1, (n + cols - 1) / cols), rowGap = S (8);
        const int cw = a.getWidth() / cols, rh = (a.getHeight() - rowGap * (rows - 1)) / rows;
        int i = 0;
        for (auto* k : others)
        {
            k->setBounds (a.getX() + (i % cols) * cw, a.getY() + (i / cols) * (rh + rowGap), cw, rh);
            ++i;
        }
    };

    const auto a = blocks (rowA, { "SOURCE", "FM", "FILTER" });
    primaryAndGrid (a[0], shape, { &sub, &oscDrive }, 1);
    primaryAndGrid (a[1], fmAmount, { &fmRatio, &fmDecay, &oscMix }, 2); // MIX: analog <-> FM
    auto flt = a[2];
    filterMode.setBounds (flt.removeFromTop (S (26)));
    flt.removeFromTop (S (8));
    primaryAndGrid (flt, cutoff, { &resonance, &envAmount }, 2);

    const auto b = blocks (rowB, { "DRIVE", "AMP", "STEREO" });
    auto drv = b[0];
    distType.setBounds (drv.removeFromTop (S (26)));
    drv.removeFromTop (S (8));
    primaryAndGrid (drv, distDrive, { &distTone, &distMix, &dark, &bite }, 2);

    // AMP: the envelope on top; the attack hit, glide and the LEGATO / SMOOTH buttons below.
    auto amp = b[1];
    auto envRow = amp.removeFromTop ((amp.getHeight() - S (8)) / 2);
    amp.removeFromTop (S (8)); // breathing room between the envelope and the row below
    const int ew = envRow.getWidth() / 4;
    for (auto* k : { &attack, &decay, &sustain, &release })
        k->setBounds (envRow.removeFromLeft (ew));
    const int pw = amp.getWidth() / 4;
    punch.setBounds (amp.removeFromLeft (pw));
    glide.setBounds (amp.removeFromLeft (pw));
    auto modes = amp.withSizeKeepingCentre (amp.getWidth() - S (8), S (26) * 2 + S (8));
    legato.setBounds (modes.removeFromTop (S (26)));
    modes.removeFromTop (S (8));
    smooth.setBounds (modes.removeFromTop (S (26)));

    // STEREO: width of the highs above SPLIT (mono below), and the left/right DELAY.
    primaryAndGrid (b[2], width, { &splitFreq, &stereoDelay }, 1);

    advanced.setBounds (getWidth() - S (24) - S (640), S (64), S (640), S (170));
    browser.setBounds (getLocalBounds().withTrimmedTop (S (60)).reduced (S (24), S (14)));
}

void PowerBassEditor::timerCallback()
{
    meter.setLevels (proc.meterL.load(), proc.meterR.load());
    waveform.decay();
    // Redraw the sound once the knobs settle (a few timer ticks after the last change).
    if (soundDirty.exchange (false))
        dirtyAge = 0;
    if (dirtyAge >= 0 && ++dirtyAge > 3)
    {
        dirtyAge = -1;
        renderWaveform();
    }
    width.repaint();
    const bool stereoOn = proc.state.getRawParameterValue (pb::id::width)->load() > 0.0f;
    splitFreq.setEnabled (stereoOn);
    stereoDelay.setEnabled (stereoOn);
}

//==============================================================================
void PowerBassEditor::stepPreset (int delta)
{
    if (browser.isVisible()) // with the browser open, < > step through its filtered list
    {
        browser.keyPressed (juce::KeyPress (delta > 0 ? juce::KeyPress::downKey : juce::KeyPress::upKey));
        return;
    }
    // Closed: every preset, in the browser's order (genre, then name).
    proc.library.rescan();
    const auto order = pb::filterPresets (proc.library.all(), {}, {}, {});
    if (order.empty()) return;
    const int current = proc.library.indexOf (proc.getPresetName());
    const auto at = std::find (order.begin(), order.end(), current);
    const int n = (int) order.size();
    const int pos = at == order.end() ? 0 : ((int) (at - order.begin()) + delta + n) % n;
    proc.loadPreset (order[(size_t) pos]);
    refreshHeader();
}

void PowerBassEditor::savePreset()
{
    askForName ("Save preset", proc.getPresetName(), [this] (juce::String name)
    {
        juce::String e;
        if (! proc.library.saveUser (name, pb::captureSnapshot (proc.state), e)) { showError (e); return; }
        proc.setPresetName (name.trim());
        refreshHeader();
    });
}

void PowerBassEditor::showMainMenu()
{
    const int current = proc.library.indexOf (proc.getPresetName());
    const bool isUser = current >= 0 && ! proc.library.all()[(size_t) current].isFactory();
    juce::PopupMenu menu;
    menu.addItem (1, "Save as...");
    menu.addItem (2, "Rename...", isUser);
    menu.addItem (3, "Delete...", isUser);
    menu.addSeparator();
    menu.addItem (4, "Import preset...");
    menu.addItem (5, "Export preset...");
    menu.addItem (6, "Show preset folder");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton), [this, current] (int r)
    {
        if (r == 1)
            savePreset();
        else if (r == 2)
            askForName ("Rename preset", proc.getPresetName(), [this, current] (juce::String name)
            {
                juce::String e;
                if (! proc.library.rename (current, name, e)) { showError (e); return; }
                proc.setPresetName (name.trim());
                refreshHeader();
            });
        else if (r == 3)
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete preset",
                                                "Move \"" + proc.getPresetName() + "\" to the Trash?", "Delete", "Cancel", this,
                                                juce::ModalCallbackFunction::create ([this, current] (int ok)
                                                {
                                                    juce::String e;
                                                    if (ok == 1 && ! proc.library.remove (current, e)) showError (e);
                                                    refreshHeader();
                                                }));
        else if (r == 4)
        {
            chooser = std::make_unique<juce::FileChooser> ("Import PowerBass preset", juce::File(), "*.pbpreset");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      if (fc.getResult() == juce::File()) return;
                                      juce::String name, e;
                                      if (! proc.library.importFile (fc.getResult(), name, e)) { showError (e); return; }
                                      proc.loadPreset (proc.library.indexOf (name));
                                      refreshHeader();
                                  });
        }
        else if (r == 5)
        {
            chooser = std::make_unique<juce::FileChooser> ("Export PowerBass preset",
                                                           juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                                                               .getChildFile (juce::File::createLegalFileName (proc.getPresetName()) + ".pbpreset"),
                                                           "*.pbpreset");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      if (fc.getResult() == juce::File()) return;
                                      juce::String e;
                                      if (! proc.library.exportTo (fc.getResult().withFileExtension ("pbpreset"), proc.getPresetName(),
                                                                   pb::captureSnapshot (proc.state), e))
                                          showError (e);
                                  });
        }
        else if (r == 6)
        {
            auto dir = pb::PresetLibrary::defaultUserDirectory();
            dir.createDirectory();
            dir.revealToUser();
        }
    });
}

void PowerBassEditor::askForName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, "Preset name", juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    w->addTextEditor ("name", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int r)
    {
        const auto name = w->getTextEditorContents ("name").trim();
        if (r == 1 && name.isNotEmpty()) done (name);
    }), true);
}

void PowerBassEditor::showError (const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "PowerBass", message, "OK", this);
}
