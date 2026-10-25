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
        g.setColour (s.isEnabled() ? colours::accent : colours::faint);
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

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return uiFont (std::min (12.5f, (float) h * 0.46f), true).withExtraKerningFactor (0.06f); }
juce::Font LookAndFeel::getPopupMenuFont() { return uiFont (14.0f); }

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& t, const juce::String& tooltip)
    : title (t)
{
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

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (18);
    r.removeFromBottom (18);
    int d = std::min (r.getWidth(), r.getHeight());
    if (auto* top = getTopLevelComponent()) // same knob size in every section
        d = std::min (d, juce::roundToInt ((float) top->getWidth() * 0.076f));
    slider.setBounds (r.withSizeKeepingCentre (d, d));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (colours::dim);
    g.setFont (uiFont (11.0f, true).withExtraKerningFactor (0.12f));
    g.drawText (title, r.removeFromTop (18), juce::Justification::centred);
    g.setColour (colours::text);
    g.setFont (uiFont (12.5f));
    g.drawText (slider.getTextFromValue (slider.getValue()), r.removeFromBottom (18), juce::Justification::centred);
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
        b->onClick = [this, i]
        {
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (paramId)))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 ((float) i));
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
        buttons[i]->setToggleState (i == index, juce::dontSendNotification);
}

//==============================================================================
void Scope::refresh()
{
    // Take the latest 2048 samples and align on a rising zero crossing so the
    // waveform stands still for a held note.
    constexpr int window = 2048;
    const int end = proc.scopeWrite.load (std::memory_order_acquire);
    std::array<float, window> buf {};
    for (int i = 0; i < window; ++i)
        buf[(size_t) i] = proc.scope[(size_t) ((end - window + i) & (PowerBassProcessor::scopeSize - 1))];
    int start = 0;
    for (int i = 1; i < window - (int) shown.size(); ++i)
        if (buf[(size_t) i - 1] < 0.0f && buf[(size_t) i] >= 0.0f) { start = i; break; }
    for (size_t i = 0; i < shown.size(); ++i)
        shown[i] = buf[(size_t) start + i];
    repaint();
}

void Scope::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    fillPanel (g, r, 10.0f);
    auto plot = r.reduced (10.0f, 12.0f);
    const float mid = plot.getCentreY();
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine ((int) mid, plot.getX(), plot.getRight());

    juce::Path p;
    for (size_t i = 0; i < shown.size(); ++i)
    {
        const float x = plot.getX() + plot.getWidth() * (float) i / (float) (shown.size() - 1);
        const float y = mid - juce::jlimit (-1.0f, 1.0f, shown[i]) * plot.getHeight() * 0.48f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    juce::ColourGradient grad (colours::accent, 0, plot.getY(), colours::accent, 0, plot.getBottom(), false);
    grad.addColour (0.5, colours::text);
    g.setGradientFill (grad);
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
      fltDecay (s, pb::id::fltDecay, "FILTER DECAY", "Decay of the filter envelope used by ENV AMOUNT"),
      velSens (s, pb::id::velSens, "VELOCITY", "How much note velocity changes the level"),
      bend (s, pb::id::bendRange, "BEND", "Pitch-bend range in semitones")
{
    for (auto* c : std::initializer_list<juce::Component*> { &filterPos, &oversample, &fltDecay, &velSens, &bend })
        addAndMakeVisible (c);
}

void AdvancedPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0, 4), 12.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff34373e), 0, r.getY(), juce::Colour (0xff1d1f24), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (colours::accent.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
    g.setFont (uiFont (11.0f, true).withExtraKerningFactor (0.18f));
    g.setColour (colours::dim);
    auto t = getLocalBounds().reduced (16, 10);
    g.drawText ("ADVANCED", t.removeFromTop (16), juce::Justification::centredLeft);
    g.drawText ("FILTER POSITION", juce::Rectangle<int> (16, 34, 160, 14), juce::Justification::centredLeft);
    g.drawText ("OVERSAMPLING", juce::Rectangle<int> (16, 84, 160, 14), juce::Justification::centredLeft);
}

void AdvancedPanel::resized()
{
    filterPos.setBounds (14, 50, 160, 26);
    oversample.setBounds (14, 100, 160, 26);
    auto r = getLocalBounds().withTrimmedLeft (190).reduced (6, 28);
    const int w = r.getWidth() / 3;
    fltDecay.setBounds (r.removeFromLeft (w));
    velSens.setBounds (r.removeFromLeft (w));
    bend.setBounds (r);
}

//==============================================================================
PowerBassEditor::PowerBassEditor (PowerBassProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      shape (p.state, pb::id::shape, "SHAPE", "Waveform: sine, triangle, saw, square, with smooth morphing"),
      sub (p.state, pb::id::sub, "SUB", "Sine one octave below"),
      oscDrive (p.state, pb::id::oscDrive, "DRIVE", "Saturation inside the oscillator"),
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
      attack (p.state, pb::id::attack, "ATTACK", "Amp envelope attack"),
      decay (p.state, pb::id::decay, "DECAY", "Amp envelope decay"),
      sustain (p.state, pb::id::sustain, "SUSTAIN", "Amp envelope sustain level"),
      release (p.state, pb::id::release, "RELEASE", "Amp envelope release"),
      glide (p.state, pb::id::glide, "GLIDE", "Portamento time"),
      master (p.state, pb::id::master, "MASTER", "Output level"),
      filterMode (p.state, pb::id::filterMode, { "LP24", "LP12", "BP", "HP" }, "Filter mode"),
      distType (p.state, pb::id::distType, { "SATURATE", "CLIP", "FOLD", "DESTROY", "CRUSH" }, "Distortion algorithm"),
      glideMode (p.state, pb::id::glideMode, { "MONO", "LEGATO" }, "Mono: every note retriggers. Legato: overlapping notes slide"),
      scope (p), advanced (p.state)
{
    setLookAndFeel (&lnf);
    envAmount.slider.getProperties().set ("bipolar", true);

    for (auto* c : std::initializer_list<juce::Component*> {
             &shape, &sub, &oscDrive, &fmAmount, &fmRatio, &fmDecay, &oscMix, &cutoff, &resonance, &envAmount, &distDrive, &distTone,
             &distMix, &dark, &bite, &punch, &width, &splitFreq, &attack, &decay, &sustain, &release, &glide, &master, &filterMode,
             &distType, &glideMode, &scope, &meter, &prevButton, &nextButton, &presetButton, &saveButton, &advancedButton, &menuButton,
             &previewButton })
        addAndMakeVisible (c);
    addChildComponent (advanced);

    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    presetButton.onClick = [this] { showPresetMenu(); };
    saveButton.onClick = [this] { savePreset(); };
    menuButton.onClick = [this] { showMainMenu(); };
    previewButton.onClick = [this] { proc.previewRequested = true; };
    advancedButton.setClickingTogglesState (true);
    advancedButton.onClick = [this] { advanced.setVisible (advancedButton.getToggleState()); };
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
    startTimerHz (30);
}

PowerBassEditor::~PowerBassEditor() { setLookAndFeel (nullptr); }

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

    for (const auto& sec : sections)
    {
        fillPanel (g, sec.area.toFloat(), 12.0f * s);
        g.setFont (uiFont (10.5f * s, true).withExtraKerningFactor (0.2f));
        g.setColour (colours::faint);
        g.drawText (sec.title, sec.area.reduced (juce::roundToInt (14 * s), juce::roundToInt (8 * s)).removeFromTop (juce::roundToInt (14 * s)),
                    juce::Justification::centredLeft);
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
    auto knobRow = [&] (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs)
    {
        const int w = area.getWidth() / (int) knobs.size();
        for (auto* k : knobs) k->setBounds (area.removeFromLeft (w).reduced (S (4), 0));
    };
    auto section = [&] (const juce::String& title, juce::Rectangle<int> area)
    {
        sections.push_back ({ title, area });
        return area.reduced (S (12), S (10)).withTrimmedTop (S (18));
    };
    const int gap = S (14);

    // Row 1: OSC / FM | FILTER
    auto row1 = body.removeFromTop (S (196));
    body.removeFromTop (gap);
    auto osc = section ("OSC / FM", row1.removeFromLeft (S (690)));
    row1.removeFromLeft (gap);
    auto flt = section ("FILTER", row1);
    knobRow (osc.withTrimmedTop (S (14)), { &shape, &sub, &oscDrive, &fmAmount, &fmRatio, &fmDecay, &oscMix });
    filterMode.setBounds (flt.removeFromTop (S (26)));
    flt.removeFromTop (S (6));
    knobRow (flt, { &cutoff, &resonance, &envAmount });

    // Row 2: DISTORTION | CHARACTER | STEREO
    auto row2 = body.removeFromTop (S (196));
    body.removeFromTop (gap);
    auto dist = section ("DISTORTION", row2.removeFromLeft (S (430)));
    row2.removeFromLeft (gap);
    auto chr = section ("CHARACTER", row2.removeFromLeft (S (316)));
    row2.removeFromLeft (gap);
    auto st = section ("STEREO", row2);
    distType.setBounds (dist.removeFromTop (S (26)));
    dist.removeFromTop (S (6));
    knobRow (dist, { &distDrive, &distTone, &distMix });
    knobRow (chr.withTrimmedTop (S (14)), { &dark, &bite, &punch });
    knobRow (st.withTrimmedTop (S (14)), { &width, &splitFreq });

    // Row 3: ENVELOPE | GLIDE | OUTPUT (scope + master + meter)
    auto row3 = body;
    auto env = section ("ENVELOPE", row3.removeFromLeft (S (430)));
    row3.removeFromLeft (gap);
    auto gl = section ("GLIDE", row3.removeFromLeft (S (180)));
    row3.removeFromLeft (gap);
    auto out = section ("OUTPUT", row3);
    knobRow (env.withTrimmedTop (S (6)), { &attack, &decay, &sustain, &release });
    glideMode.setBounds (gl.removeFromBottom (S (26)));
    gl.removeFromBottom (S (6));
    glide.setBounds (gl);
    meter.setBounds (out.removeFromRight (S (20)).reduced (0, S (6)));
    out.removeFromRight (S (8));
    master.setBounds (out.removeFromRight (S (92)));
    out.removeFromRight (S (8));
    scope.setBounds (out);

    advanced.setBounds (getWidth() - S (24) - 560, S (64), 560, 170);
}

void PowerBassEditor::timerCallback()
{
    meter.setLevels (proc.meterL.load(), proc.meterR.load());
    scope.refresh();
    width.repaint();
    splitFreq.setEnabled (proc.state.getRawParameterValue (pb::id::width)->load() > 0.0f);
}

//==============================================================================
void PowerBassEditor::stepPreset (int delta)
{
    const auto& all = proc.library.all();
    if (all.empty()) return;
    int i = proc.library.indexOf (proc.currentPresetName);
    i = (i < 0 ? 0 : i + delta + (int) all.size()) % (int) all.size();
    proc.loadPreset (i);
    refreshHeader();
}

void PowerBassEditor::showPresetMenu()
{
    proc.library.rescan();
    const auto& all = proc.library.all();
    juce::PopupMenu menu;
    juce::StringArray cats;
    for (const auto& p : all) cats.addIfNotAlreadyThere (p.category);
    for (const auto& c : cats)
    {
        juce::PopupMenu sub;
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].category == c)
                sub.addItem ((int) i + 1, all[i].name, true, all[i].name == proc.currentPresetName);
        menu.addSubMenu (c, sub);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                        [this] (int r) { if (r > 0) { proc.loadPreset (r - 1); refreshHeader(); } });
}

void PowerBassEditor::savePreset()
{
    askForName ("Save preset", proc.currentPresetName, [this] (juce::String name)
    {
        juce::String e;
        if (! proc.library.saveUser (name, pb::captureSnapshot (proc.state), e)) { showError (e); return; }
        proc.currentPresetName = name.trim();
        refreshHeader();
    });
}

void PowerBassEditor::showMainMenu()
{
    const int current = proc.library.indexOf (proc.currentPresetName);
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
            askForName ("Rename preset", proc.currentPresetName, [this, current] (juce::String name)
            {
                juce::String e;
                if (! proc.library.rename (current, name, e)) { showError (e); return; }
                proc.currentPresetName = name.trim();
                refreshHeader();
            });
        else if (r == 3)
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete preset",
                                                "Move \"" + proc.currentPresetName + "\" to the Trash?", "Delete", "Cancel", this,
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
                                                               .getChildFile (juce::File::createLegalFileName (proc.currentPresetName) + ".pbpreset"),
                                                           "*.pbpreset");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      if (fc.getResult() == juce::File()) return;
                                      juce::String e;
                                      if (! proc.library.exportTo (fc.getResult().withFileExtension ("pbpreset"), proc.currentPresetName,
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
