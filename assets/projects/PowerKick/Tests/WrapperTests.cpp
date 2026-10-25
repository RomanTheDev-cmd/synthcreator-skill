// Tests of the plug-in wrapper (processor, state, presets through the host path, editor).
// The DSP itself is covered by PowerKickTests.cpp.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <atomic>
#include <iostream>
#include <new>

// Counts heap allocations while 'trackAllocs' is set (real-time safety of processBlock).
static std::atomic<bool> trackAllocs { false };
static std::atomic<int> allocCount { 0 };
void* operator new (std::size_t n)
{
    if (trackAllocs.load (std::memory_order_relaxed)) allocCount.fetch_add (1);
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
int failures = 0, checks = 0;
void expect (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok) { ++failures; std::cout << "FAIL: " << what << "\n"; }
}

float rawValue (PowerKickProcessor& p, const char* id) { return p.state.getRawParameterValue (id)->load(); }
void setPlain (PowerKickProcessor& p, const char* id, float v)
{
    auto* param = p.state.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (v));
}

// A mouse event on component c (click or drag), for the mouse handlers of the waveform.
juce::MouseEvent mouseAt (juce::Component* c, bool dragged)
{
    const auto now = juce::Time::getCurrentTime();
    const juce::Point<float> pt (20.0f, 20.0f);
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pt, {}, juce::MouseInputSource::defaultPressure,
                             juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                             juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY, c, c, now, pt, now, 1, dragged);
}


struct Run
{
    double peak = 0;
    bool finite = true;
};

// Plays a kick every `every` samples for `blocks` blocks; `between` runs before each block.
Run play (PowerKickProcessor& p, int blocks, int every = 9600, std::function<void (int)> between = {}, int channels = 2)
{
    Run r;
    juce::AudioBuffer<float> buf (channels, 512);
    for (int b = 0; b < blocks; ++b)
    {
        if (between) between (b);
        juce::MidiBuffer midi;
        const int start = b * 512;
        for (int s = 0; s < 512; ++s)
            if ((start + s) % every == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), s);
        buf.clear();
        p.processBlock (buf, midi);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < 512; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) r.finite = false;
                r.peak = std::max (r.peak, (double) std::abs (v));
            }
    }
    return r;
}

void testDefaultsMatchDefaultPreset()
{
    // A fresh instance and every double-click reset must sound like the default preset.
    PowerKickProcessor p;
    const pk::Preset* def = nullptr;
    for (const auto& pr : pk::factoryPresets())
        if (pr.name == "Hard Techno Core") def = &pr;
    expect (def != nullptr, "default preset Hard Techno Core exists");
    if (def == nullptr) return;
    expect (p.getPresetName() == "Hard Techno Core", "a fresh instance shows Hard Techno Core");
    for (const auto& pid : pk::allParamIds())
    {
        auto* prm = p.state.getParameter (pid);
        const auto it = def->values.find (pid);
        if (it == def->values.end()) continue; // unlisted: the default by definition
        const float want = prm->convertTo0to1 (it->second), have = prm->getDefaultValue(); // double-click reset target
        expect (std::abs (want - have) < 0.002f, "default " + pid + " = Hard Techno Core (" + juce::String (prm->convertFrom0to1 (have), 3)
                                                 + " vs " + juce::String (it->second, 3) + ")");
    }
}

void testStateRoundTrip()
{
    PowerKickProcessor a;
    a.prepareToPlay (48000, 512);
    setPlain (a, pk::id::tune, 3.0f);
    setPlain (a, pk::id::drive, 0.81f);
    setPlain (a, pk::id::width, 66.0f);
    setPlain (a, pk::id::splitFreq, 900.0f);
    setPlain (a, pk::id::bodyTone, 0.25f);
    a.setPresetName ("My Saved Kick");
    juce::MemoryBlock mb;
    a.getStateInformation (mb);

    PowerKickProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    bool same = true;
    for (const auto& id : pk::allParamIds())
        same = same && std::abs (rawValue (a, id.toRawUTF8()) - rawValue (b, id.toRawUTF8())) < 1.0e-4f;
    expect (same, "project state restores every parameter");
    expect (b.getPresetName() == "My Saved Kick", "project state restores the preset name");

    // Garbage must not crash or change anything.
    const float before = rawValue (b, pk::id::tune);
    const char junk[] = "definitely not a PowerKick state";
    b.setStateInformation (junk, (int) sizeof (junk));
    b.setStateInformation (nullptr, 0);
    expect (std::abs (rawValue (b, pk::id::tune) - before) < 1.0e-6f, "garbage state is ignored");
}

void testLegacyProjectMigration()
{
    // A project saved before the newer parameters existed: they must come back neutral.
    PowerKickProcessor a;
    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
    for (auto* id : { pk::id::bodyTone, pk::id::pitchFall, pk::id::splitFreq })
        if (auto* e = xml->getChildByAttribute ("id", id)) xml->removeChildElement (e, true);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    PowerKickProcessor b;
    b.setStateInformation (old.getData(), (int) old.getSize());
    expect (std::abs (rawValue (b, pk::id::bodyTone) - 1.0f) < 1e-4f, "old project: bodyTone neutral");
    expect (std::abs (rawValue (b, pk::id::pitchFall)) < 1e-4f, "old project: pitchFall neutral");
    expect (std::abs (rawValue (b, pk::id::splitFreq) - 1000.0f) < 1.0f, "old project: SPLIT at the old 1 kHz");
}

void testPresetSwitchingWhilePlaying()
{
    PowerKickProcessor p;
    p.prepareToPlay (48000, 512);
    const int n = (int) p.library.all().size();
    double worst = 0;
    bool finite = true;
    for (int i = 0; i < n; i += std::max (1, n / 60))
    {
        const auto r = play (p, 8, 2400, [&p, i] (int b) { if (b == 4) p.loadPreset (i); });
        worst = std::max (worst, r.peak);
        finite = finite && r.finite;
    }
    expect (finite, "switching presets while playing stays finite");
    expect (worst <= 1.0, "switching presets while playing stays under 0 dBFS (" + juce::String (worst, 3) + ")");
    expect (p.getLatencySamples() >= 0, "latency reported");
}

void testOversamplingLatencyTracksProcessing()
{
    for (int choice = 0; choice < 3; ++choice)
    {
        PowerKickProcessor saved;
        setPlain (saved, pk::id::oversample, (float) choice);
        juce::MemoryBlock state;
        saved.getStateInformation (state);
        PowerKickProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        restored.prepareToPlay (48000, 127);
        juce::dsp::Oversampling<float> reference (1, (size_t) (choice + 1),
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        reference.initProcessing (127);
        const int expected = (int) std::lround (reference.getLatencyInSamples());
        expect (restored.getLatencySamples() == expected, "restored oversampling " + juce::String (2 << choice)
            + "x: latency is correct before the first audio block");
    }
    PowerKickProcessor p;
    p.prepareToPlay (48000, 127);
    juce::AudioBuffer<float> out (2, 127);
    for (int choice : { 0, 1, 2, 0, 1 })
    {
        setPlain (p, pk::id::oversample, (float) choice);
        // Independent reference: the latency of JUCE's selected oversampling filter.
        juce::dsp::Oversampling<float> reference (1, (size_t) (choice + 1),
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        reference.initProcessing (127);
        const int expected = (int) std::lround (reference.getLatencyInSamples());
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 37);
        out.clear();
        p.processBlock (out, midi);
        expect (p.getLatencySamples() == expected, "oversampling " + juce::String (2 << choice)
            + "x: host latency " + juce::String (p.getLatencySamples()) + " equals filter latency " + juce::String (expected));
    }
}

void testLatencyOfStateRestoredAfterPrepare()
{
    // Some hosts restore the project after prepareToPlay: the restored choice's latency must be
    // reported before the first audio block.
    PowerKickProcessor source;
    setPlain (source, pk::id::oversample, 0.0f); // 2x: 4 samples at 48 kHz, the default 4x has 6
    juce::MemoryBlock saved;
    source.getStateInformation (saved);
    PowerKickProcessor late, early;
    late.prepareToPlay (48000, 512);
    const int before = late.getLatencySamples();
    late.setStateInformation (saved.getData(), (int) saved.getSize());
    early.setStateInformation (saved.getData(), (int) saved.getSize());
    early.prepareToPlay (48000, 512);
    expect (before != early.getLatencySamples(), "the restored oversampling has another latency (else the next check is empty)");
    expect (late.getLatencySamples() == early.getLatencySamples(), "state restored after prepare reports its latency before the first block ("
            + juce::String (late.getLatencySamples()) + " vs " + juce::String (early.getLatencySamples()) + ")");
}

// Re-preparing at another rate must sound like a fresh instance (no state carried over).
void testReprepareMatchesFresh()
{
    for (double startRate : { 44100.0, 96000.0 })
    {
        const double finalRate = startRate == 96000.0 ? 44100.0 : 96000.0;
        PowerKickProcessor reused, fresh;
        reused.prepareToPlay (startRate, 512);
        play (reused, 30, 4800);
        reused.prepareToPlay (finalRate, 512);
        fresh.prepareToPlay (finalRate, 512);
        double error = 0.0;
        for (int b = 0; b < 40; ++b)
        {
            juce::AudioBuffer<float> a (2, 512), c (2, 512);
            a.clear(); c.clear();
            juce::MidiBuffer ma, mc;
            if (b == 0) { ma.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 37); mc = ma; }
            reused.processBlock (a, ma);
            fresh.processBlock (c, mc);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    error = std::max (error, (double) std::abs (a.getSample (ch, i) - c.getSample (ch, i)));
        }
        expect (error < 1.0e-6, "reprepare " + juce::String (startRate) + " -> " + juce::String (finalRate)
                + " matches a fresh instance (error " + juce::String (error, 9) + ")");
    }
}

// Every factory preset loads in the plug-in exactly as the tests and tools render it
// (paramsFromSnapshot): a missing key must not fall back to different values in the two paths.
void testFactoryPresetsLoadAsMeasured()
{
    PowerKickProcessor p;
    const auto& factory = pk::factoryPresets();
    int bad = 0;
    juce::String first;
    for (int i = 0; i < (int) factory.size(); ++i)
    {
        p.loadPreset (i);
        const auto got = pk::paramsFromSnapshot (pk::captureSnapshot (p.state));
        const auto want = pk::paramsFromSnapshot (factory[(size_t) i].values);
        juce::String diff;
        auto near = [&diff] (const char* what, double a, double b)
        {
            if (std::abs (a - b) > 0.051 + 0.01 * std::abs (b))
                diff << what << " " << juce::String (a, 3) << " vs " << juce::String (b, 3) << "; ";
        };
        near ("tune", got.tune, want.tune); near ("decay", got.decay, want.decay); near ("punch", got.punch, want.punch);
        near ("body", got.body, want.body); near ("click", got.click, want.click); near ("drive", got.drive, want.drive);
        near ("character", got.character, want.character); near ("rumble", got.rumble, want.rumble);
        near ("output", got.outputDb, want.outputDb); near ("width", got.width, want.width); near ("delay", got.delayMs, want.delayMs);
        near ("driveMode", (int) got.driveMode, (int) want.driveMode); near ("charMode", (int) got.charMode, (int) want.charMode);
        near ("pitchDepth", got.pitchDepth, want.pitchDepth); near ("pitchTime", got.pitchTime, want.pitchTime);
        near ("clickType", (int) got.clickType, (int) want.clickType); near ("clickTone", got.clickTone, want.clickTone);
        near ("rumbleTime", got.rumbleTime, want.rumbleTime); near ("rumbleSync", got.rumbleSync, want.rumbleSync);
        near ("velSens", got.velSens, want.velSens); near ("keyTrack", got.keyTrack, want.keyTrack);
        near ("pitchFall", got.pitchFall, want.pitchFall); near ("bodyTone", got.bodyTone, want.bodyTone);
        near ("split", got.splitHz, want.splitHz); near ("beatLength", got.beatLength, want.beatLength);
        near ("subLevel", got.subLevel, want.subLevel); near ("startPhase", got.startPhase, want.startPhase);
        near ("driveEnv", got.driveEnv, want.driveEnv); near ("velDrive", got.velDrive, want.velDrive);
        near ("oversample", got.oversampleIndex, want.oversampleIndex); near ("pitchCurve", got.pitchCurve, want.pitchCurve);
        near ("keyRoot", got.keyRoot, want.keyRoot); near ("modDrive", got.modDrive, want.modDrive);
        near ("choke", got.choke, want.choke); near ("rumbleMode", got.rumbleMode, want.rumbleMode);
        if (diff.isNotEmpty() && bad++ < 3)
            first << factory[(size_t) i].name << ": " << diff;
    }
    expect (bad == 0, "factory presets load in the plug-in as measured (" + juce::String (bad) + " differ; " + first + ")");
}

// OVERSAMPLE is a quality setting of the instance, not part of a sound: presets, A/B and
// variations never change it (factory presets do not store it, so it used to fall back to 4x,
// and the plug-in latency changed while the host was playing).
void testSnapshotsKeepOversampling()
{
    auto os = [] (PowerKickProcessor& p) { return juce::roundToInt (rawValue (p, pk::id::oversample)); };
    PowerKickProcessor p;
    p.prepareToPlay (48000.0, 512);
    // Latency differs between 2x and 4x (4x and 8x round to the same samples), so the latency
    // check runs at 2x; the setting itself is checked at 2x and 8x.
    setPlain (p, pk::id::oversample, 1.0f);
    play (p, 4);
    const int latency4x = p.getLatencySamples();
    const auto& all = p.library.all();
    for (const int choice : { 0, 2 })
    {
        setPlain (p, pk::id::oversample, (float) choice);
        play (p, 4);
        const int latencyChosen = p.getLatencySamples();
        if (choice == 0)
            expect (latencyChosen != latency4x, "2x and 4x report different latencies, else the latency check is empty (2x "
                    + juce::String (latencyChosen) + ", 4x " + juce::String (latency4x) + ")");
        const juce::String name = juce::String (2 << choice) + "x";
        int kept = 0, loaded = 0, withControl = 0;
        for (int i = 0; i < (int) all.size(); ++i)
        {
            p.loadPreset (i);
            if (juce::roundToInt (rawValue (p, pk::id::oversample)) == choice) ++kept;
            // Control: the preset itself was applied.
            if (const auto it = all[(size_t) i].values.find (pk::id::decay); it != all[(size_t) i].values.end())
            {
                ++withControl;
                if (std::abs (rawValue (p, pk::id::decay) - it->second) < 1.0e-3f) ++loaded;
            }
        }
        expect (kept == (int) all.size(), "every preset keeps OVERSAMPLE " + name + " (" + juce::String (kept) + "/" + juce::String ((int) all.size()) + ")");
        expect (withControl > 0 && loaded == withControl, "the presets themselves were applied (DECAY " + juce::String (loaded) + "/" + juce::String (withControl) + ")");
        play (p, 4);
        expect (p.getLatencySamples() == latencyChosen, "latency at " + name + " unchanged by preset switches ("
                + juce::String (p.getLatencySamples()) + " vs " + juce::String (latencyChosen) + ")");
    }

    // A saved preset that does contain OVERSAMPLE does not change it either.
    pk::Snapshot saved { { pk::id::oversample, 0.0f }, { pk::id::decay, 0.3f } };
    pk::applySnapshot (p.state, saved);
    expect (os (p) == 2 && std::abs (rawValue (p, pk::id::decay) - 0.3f) < 1.0e-3f, "a snapshot with OVERSAMPLE 2x keeps 8x but applies its DECAY");

    // A/B: slot A is captured at 8x; the setting changed while on B stays when going back to A.
    setPlain (p, pk::id::oversample, 2.0f);
    p.switchSlot (true);
    expect (os (p) == 2, "switching to B keeps OVERSAMPLE");
    setPlain (p, pk::id::oversample, 0.0f);
    p.switchSlot (false);
    expect (os (p) == 0, "switching back to A keeps the OVERSAMPLE chosen on B (got index " + juce::String (os (p)) + ")");

    // Variation undo restores the sound, not an older OVERSAMPLE.
    p.makeVariation();
    setPlain (p, pk::id::oversample, 1.0f);
    expect (p.undoVariation() && os (p) == 1, "undoing a variation keeps OVERSAMPLE (got index " + juce::String (os (p)) + ")");
}

// Clicking the waveform plays the kick (Roman has no MIDI keyboard, 2026-10-23); dragging it still exports a WAV.
void testWaveformClickPlays()
{
#if JUCE_MAC
    juce::ScopedJuceInitialiser_GUI gui;
    PowerKickProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* w = ed->findChildWithID ("waveform");
    expect (w != nullptr, "the waveform is on the window");
    if (w == nullptr)
        return;
    p.triggerRequested = false;
    w->mouseDown (mouseAt (w, false));
    w->mouseUp (mouseAt (w, false));
    expect (p.triggerRequested.exchange (false), "a click on the waveform plays the kick");
    w->mouseDown (mouseAt (w, false));
    w->mouseUp (mouseAt (w, true));
    expect (! p.triggerRequested.load(), "dragging the waveform (WAV export) does not play it");
#endif
}

// The preset browser's filter: category, any of the selected groups, and words of a search text.
void testPresetFilter()
{
    const auto& all = pk::factoryPresets();
    auto names = [&all] (const std::vector<int>& idx)
    {
        juce::StringArray n;
        for (int i : idx) n.add (all[(size_t) i].name);
        return n;
    };
    expect (pk::filterPresets (all, {}, {}, {}).size() == all.size(), "no filter shows every preset");
    expect (pk::filterPresets (all, "Hard Techno", {}, {}).size() == 110, "category Hard Techno: 110 presets");
    expect (pk::filterPresets (all, "Hard Techno", { "Clean" }, {}).size() == 45, "Hard Techno + Clean: 45 presets");
    const auto two = pk::filterPresets (all, "Techno", { "Short", "Long" }, {});
    expect (two.size() == 17, "Techno + Short or Long: 9 + 8 presets (" + juce::String ((int) two.size()) + ")");
    const auto found = names (pk::filterPresets (all, {}, {}, "  classic PUNCH "));
    expect (found.contains ("Classic Punch"), "search finds a preset by words in any case");
    bool allMatch = true;
    for (const auto& n : found)
        allMatch = allMatch && n.containsIgnoreCase ("classic") && n.containsIgnoreCase ("punch");
    expect (allMatch && found.size() >= 1, "every search result contains every word");
    // Words also match the category and the group: "schranz wide" covers all 14 Schranz presets of the
    // Wide group, plus Schranz presets with "Wide" in their name.
    const auto sw = pk::filterPresets (all, {}, {}, "schranz wide");
    const auto group = pk::filterPresets (all, "Schranz", { "Wide" }, {});
    bool covers = group.size() == 14;
    for (int i : group) covers = covers && std::find (sw.begin(), sw.end(), i) != sw.end();
    bool onlySchranz = true;
    for (int i : sw)
        onlySchranz = onlySchranz && all[(size_t) i].category == "Schranz"
                      && (all[(size_t) i].group == "Wide" || all[(size_t) i].name.containsIgnoreCase ("wide"));
    expect (covers && onlySchranz, "search matches the category and the group too (" + juce::String ((int) sw.size()) + " results)");
    expect (pk::filterPresets (all, {}, {}, "zzzz").empty(), "a search without matches is empty");
    expect (pk::filterPresets (all, "User", {}, {}).empty(), "User shows only the user's own presets (none among factory presets)");
    // Sorted for reading: genres in library order, names alphabetically (natural order) within a genre.
    const auto order = pk::filterPresets (all, {}, {}, {});
    bool sorted = true;
    for (size_t k = 1; k < order.size(); ++k)
    {
        const auto& a = all[(size_t) order[k - 1]];
        const auto& b = all[(size_t) order[k]];
        if (a.category == b.category)
            sorted = sorted && a.name.compareNatural (b.name) <= 0;
    }
    juce::StringArray genres;
    for (int i : order) if (! genres.contains (all[(size_t) i].category)) genres.add (all[(size_t) i].category);
    expect (sorted && genres.joinIntoString ("|") == "Techno|Hard Techno|Schranz|Industrial|Rave / Hard Dance",
            "results are sorted by genre, then by name (" + genres.joinIntoString (", ") + ")");
}

void testAutomation()
{
    PowerKickProcessor p;
    p.prepareToPlay (48000, 512);
    p.loadPreset (p.library.indexOf ("Hard Techno Core"));
    // Sweep many parameters every block while kicks play.
    const auto r = play (p, 120, 4800, [&p] (int b)
    {
        const float t = (float) (b % 40) / 40.0f;
        setPlain (p, pk::id::drive, t);
        setPlain (p, pk::id::character, 1.0f - t);
        setPlain (p, pk::id::width, 100.0f * t);
        setPlain (p, pk::id::delay, 15.0f * t);
        setPlain (p, pk::id::splitFreq, 150.0f + 1800.0f * t);
        setPlain (p, pk::id::output, -24.0f + 30.0f * t);
        setPlain (p, pk::id::charMode, (float) (b % 5));
        setPlain (p, pk::id::driveMode, (float) (b % 3));
    });
    expect (r.finite && r.peak <= 1.0, "heavy automation stays finite and under 0 dBFS (" + juce::String (r.peak, 3) + ")");
}

void testMonoBus()
{
    PowerKickProcessor p;
    juce::AudioProcessor::BusesLayout mono;
    mono.outputBuses.add (juce::AudioChannelSet::mono());
    expect (p.setBusesLayout (mono), "mono output bus accepted");
    p.prepareToPlay (48000, 512);
    const auto r = play (p, 20, 4800, {}, 1);
    expect (r.finite && r.peak > 0.1 && r.peak <= 1.0, "mono bus produces the kick (" + juce::String (r.peak, 3) + ")");
    juce::AudioBuffer<float> buf (1, 512);
    juce::MidiBuffer midi;
    midi.ensureSize (256);
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 10);
    allocCount = 0;
    trackAllocs = true;
    p.processBlock (buf, midi);
    trackAllocs = false;
    expect (allocCount.load() == 0, "mono bus processBlock does not allocate (" + juce::String (allocCount.load()) + " allocations)");
}

void testABAndVariation()
{
    PowerKickProcessor p;
    p.prepareToPlay (48000, 512);
    const float tuneA = rawValue (p, pk::id::tune);
    p.switchSlot (true); // B starts as a copy of A
    expect (std::abs (rawValue (p, pk::id::tune) - tuneA) < 1.0e-5f, "B starts as a copy of A");
    setPlain (p, pk::id::tune, tuneA + 2.0f);
    p.switchSlot (false);
    expect (std::abs (rawValue (p, pk::id::tune) - tuneA) < 1.0e-4f, "A keeps its own settings");
    p.switchSlot (true);
    expect (std::abs (rawValue (p, pk::id::tune) - (tuneA + 2.0f)) < 1.0e-4f, "B keeps its own settings");
    p.switchSlot (false);

    const auto before = pk::captureSnapshot (p.state);
    p.makeVariation();
    const auto after = pk::captureSnapshot (p.state);
    expect (std::abs (after.at (pk::id::tune) - before.at (pk::id::tune)) < 1.0e-4f, "VARIATION keeps TUNE");
    expect (after.at (pk::id::charMode) == before.at (pk::id::charMode), "VARIATION keeps the CHARACTER mode");
    bool changed = false;
    for (const auto& [k, v] : before) changed = changed || std::abs (after.at (k) - v) > 1.0e-4f;
    expect (changed, "VARIATION changes the sound");
    expect (p.undoVariation(), "UNDO available");
    bool restored = true;
    for (const auto& [k, v] : before) restored = restored && std::abs (pk::captureSnapshot (p.state).at (k) - v) < 1.0e-3f;
    expect (restored, "UNDO restores the sound exactly");
}

void testPreview()
{
    PowerKickProcessor p;
    p.prepareToPlay (48000, 512);
    p.triggerRequested = true;
    const auto r = play (p, 10, 1 << 30);
    expect (r.peak > 0.1, "PREVIEW plays a kick");
}

// The preset browser replaces the nested preset menus: one panel, one list, filters on top.
void testPresetBrowser()
{
#if JUCE_MAC
    juce::ScopedJuceInitialiser_GUI gui;
    PowerKickProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* button = dynamic_cast<juce::Button*> (ed->findChildWithID ("presetButton"));
    auto* advanced = dynamic_cast<juce::Button*> (ed->findChildWithID ("advancedButton"));
    auto* browser = dynamic_cast<pk::ui::PresetBrowser*> (ed->findChildWithID ("presetBrowser"));
    expect (button != nullptr && advanced != nullptr && browser != nullptr && ! browser->isVisible(),
            "the preset browser exists and starts closed");
    if (button == nullptr || advanced == nullptr || browser == nullptr)
        return;
    button->onClick();
    expect (browser->isVisible(), "clicking the preset name opens the browser");
    expect (browser->numShown() == (int) pk::factoryPresets().size(), "the browser lists every preset at first ("
            + juce::String (browser->numShown()) + ")");
    // The list opens scrolled to the current preset; a new filter without it starts at the top.
    browser->setCategory ("Schranz");
    expect (browser->firstVisibleRow() == 0, "a filter without the current preset starts at the top of the list (row "
            + juce::String (browser->firstVisibleRow()) + ")");
    browser->setCategory ({});
    browser->setCategory ("Hard Techno");
    browser->toggleGroup ("Clean");
    expect (browser->numShown() == 45, "category and sound type filter the list (" + juce::String (browser->numShown()) + ")");
    const auto rows = pk::filterPresets (pk::factoryPresets(), "Hard Techno", { "Clean" }, {});
    auto nameAt = [&rows] (int r) { return pk::factoryPresets()[(size_t) rows[(size_t) r]].name; };
    p.triggerRequested = false;
    browser->choose (2, false);
    expect (p.getPresetName() == nameAt (2) && browser->isVisible(), "choosing a row loads that preset and keeps the browser open");
    expect (p.triggerRequested.exchange (false), "with AUDITION on, a chosen preset plays at once");
    browser->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
    expect (p.getPresetName() == nameAt (3), "the down arrow loads the next preset of the list");
    browser->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    browser->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    expect (p.getPresetName() == nameAt (1), "the up arrow loads the previous preset of the list");
    browser->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    expect (! browser->isVisible(), "Escape closes the browser");
    button->onClick();
    expect (browser->numShown() == 45, "the browser remembers its filters while the window is open");
    browser->setSearch ("zzzz");
    expect (browser->numShown() == 0, "a search without matches shows an empty list");
    browser->setSearch ({});
    browser->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    expect (! browser->isVisible(), "Enter closes the browser");
    button->onClick();
    advanced->setToggleState (true, juce::sendNotification);
    expect (! browser->isVisible(), "opening ADVANCED closes the browser");
    button->onClick();
    expect (browser->isVisible() && ! advanced->getToggleState(), "opening the browser closes ADVANCED");
    browser->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    // With the browser closed, < > step through every preset in the browser's order (genre, then name).
    if (auto* next = dynamic_cast<juce::Button*> (ed->findChildWithID ("nextButton")))
    {
        const auto order = pk::filterPresets (pk::factoryPresets(), {}, {}, {});
        size_t k = 0; // a place where the browser's order differs from the library order
        while (k + 1 < order.size() && order[k + 1] == order[k] + 1)
            ++k;
        p.loadPreset (order[k]);
        next->onClick();
        expect (p.getPresetName() == pk::factoryPresets()[(size_t) order[k + 1]].name, "> loads the next preset in the browser's order");
    }
    else
        expect (false, "next button found");
    ed.reset();
#endif
}

void testEditor()
{
#if JUCE_MAC
    juce::ScopedJuceInitialiser_GUI gui;
    PowerKickProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    expect (ed != nullptr && ed->getWidth() == 920 && ed->getHeight() == 660, "editor opens at 920x660");
    // Hosts resize through the constrainer (setBoundsConstrained), not a raw setSize.
    ed->setBoundsConstrained ({ 0, 0, 100, 100 });
    expect (ed->getWidth() >= 736 && ed->getHeight() >= 528, "editor keeps its minimum size when the host shrinks it");
    ed->setBoundsConstrained ({ 0, 0, 1380, 990 });
    expect (std::abs ((double) ed->getWidth() / ed->getHeight() - 920.0 / 660.0) < 0.02, "editor keeps its aspect ratio");
    ed.reset();
#endif
}
}

// --snapshot DIR: renders the editor (main view and ADVANCED open) to PNG files for a visual check.
int snapshot (const juce::File& dir)
{
    juce::ScopedJuceInitialiser_GUI gui;
    PowerKickProcessor p;
    p.prepareToPlay (48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    dir.createDirectory();
    auto save = [&] (const juce::String& name, float scale)
    {
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
        auto f = dir.getChildFile (name);
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
    };
    save ("main.png", 1.0f);
    for (auto* c : ed->getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "ADVANCED")
            b->setToggleState (true, juce::sendNotification);
    save ("advanced.png", 1.0f);
    ed->setBoundsConstrained ({ 0, 0, 736, 528 });
    save ("advanced_small.png", 1.0f);
    // Preset browser: all presets, a filtered list, an empty search, the small window.
    ed->setBoundsConstrained ({ 0, 0, 920, 660 });
    auto* button = dynamic_cast<juce::Button*> (ed->findChildWithID ("presetButton"));
    auto* browser = dynamic_cast<pk::ui::PresetBrowser*> (ed->findChildWithID ("presetBrowser"));
    if (button != nullptr && browser != nullptr)
    {
        button->onClick();
        save ("browser.png", 1.0f);
        browser->setCategory ("Hard Techno");
        browser->toggleGroup ("Clean");
        save ("browser_filtered.png", 1.0f);
        browser->setSearch ("nothing like this");
        save ("browser_empty.png", 1.0f);
        browser->setSearch ({});
        ed->setBoundsConstrained ({ 0, 0, 736, 528 });
        save ("browser_small.png", 1.0f);
    }
    ed.reset();
    return 0;
}

int main (int argc, char** argv)
{
    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
        return snapshot (juce::File (juce::String::fromUTF8 (argv[2])));
    testDefaultsMatchDefaultPreset();
    testStateRoundTrip();
    testLegacyProjectMigration();
    testPresetSwitchingWhilePlaying();
    testOversamplingLatencyTracksProcessing();
    testSnapshotsKeepOversampling();
    testLatencyOfStateRestoredAfterPrepare();
    testReprepareMatchesFresh();
    testFactoryPresetsLoadAsMeasured();
    testPresetFilter();
    testAutomation();
    testMonoBus();
    testABAndVariation();
    testPreview();
    testWaveformClickPlays();
    testEditor();
    testPresetBrowser();
    std::cout << (failures == 0 ? "PASS " : "FAILED ") << (checks - failures) << "/" << checks << " wrapper checks\n";
    return failures == 0 ? 0 : 1;
}
