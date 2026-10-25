// Headless checks of the PowerBass engine and presets.
// Run: powerbass_tests [--report] | --export <dir>

#include <set>
#include "DSP/BassEngine.h"
#include "Presets.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <thread>
#include <new>

// Counts heap allocations while 'trackAllocs' is set (real-time safety check).
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

using namespace pb;

namespace
{
int failures = 0, checks = 0;
void expect (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok) { ++failures; std::cout << "FAIL: " << what << "\n"; }
}
double db (double g) { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }

struct Note { double on, off; int note; float vel = 1.0f; };

// Renders a note sequence; 'change' may modify params at a given time.
juce::AudioBuffer<float> render (SynthParams p, std::vector<Note> notes, double seconds, double sr = 48000.0,
                                 int block = 256, std::function<void (double, SynthParams&)> change = {})
{
    dsp::BassEngine e;
    e.prepare (sr, block);
    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> out (2, total + block);
    out.clear();
    for (int pos = 0; pos < total; pos += block)
    {
        juce::MidiBuffer m;
        for (auto& n : notes)
        {
            const int on = (int) (n.on * sr), off = (int) (n.off * sr);
            if (on >= pos && on < pos + block) m.addEvent (juce::MidiMessage::noteOn (1, n.note, n.vel), on - pos);
            if (off >= pos && off < pos + block) m.addEvent (juce::MidiMessage::noteOff (1, n.note), off - pos);
        }
        if (change) change (pos / sr, p);
        e.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), block, m, p);
    }
    out.setSize (2, total, true);
    return out;
}

// The note patterns of the render server and of the factory level grid (Tools/normalize_peaks.py):
// 0: rolling 16ths off the kick (gate 75%), 1: legato offbeat 8th line, 2: one held note (0.6 s).
std::vector<Note> patternNotes (int root, int pattern, double bpm, double len)
{
    const double step = 60.0 / bpm / 4.0;
    std::vector<Note> notes;
    if (pattern == 2)
        notes.push_back ({ 0.0, 0.6, root });                 // one held note (as testPresets plays)
    for (int k = 0; pattern != 2 && k * step < len - step; ++k)
    {
        if (pattern == 0 && k % 4 == 0) continue;           // leave the kick its beat
        if (pattern == 1 && k % 2 == 0) continue;           // offbeat 8ths
        const double gate = pattern == 0 ? 0.75 : 2.05;     // legato line overlaps the next note
        const int note = root + ((k / 16) % 2 == 1 && k % 16 >= 12 ? 3 : 0);
        notes.push_back ({ k * step, k * step + gate * step, note });
    }
    return notes;
}

double rms (const juce::AudioBuffer<float>& b, int ch, int from, int to)
{
    double s = 0;
    for (int i = from; i < to; ++i) s += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return std::sqrt (s / std::max (1, to - from));
}

double bandRms (const std::vector<float>& x, double sr, double f1, double f2)
{
    dsp::LR4 lo, hi;
    lo.set (f1, sr); hi.set (f2, sr);
    double sum = 0;
    for (float v : x) { double a, b, c, d; lo.split (v, a, b); hi.split (b, c, d); sum += c * c; }
    return std::sqrt (sum / std::max<size_t> (1, x.size()));
}

// Same with 48 dB/oct edges: for "is there any sub in the side signal" checks, where a
// 24 dB/oct measuring filter would let the (legitimately) wide highs leak in.
double bandRmsSteep (const std::vector<float>& x, double sr, double f1, double f2)
{
    dsp::LR4 lo1, lo2, hi1, hi2;
    lo1.set (f1, sr); lo2.set (f1, sr); hi1.set (f2, sr); hi2.set (f2, sr);
    double sum = 0;
    for (float v : x)
    {
        double a, b, c, d, e, g, h, k;
        lo1.split (v, a, b); lo2.split (b, c, d); hi1.split (d, e, g); hi2.split (e, h, k);
        sum += h * h;
    }
    return std::sqrt (sum / std::max<size_t> (1, x.size()));
}

// Energy in [f1, f2] from a Hann-windowed DFT (Goertzel per bin): practically no leakage from
// strong content outside the band, unlike a filter. Returns RMS-equivalent amplitude.
double bandRmsDft (const std::vector<float>& x, double sr, double f1, double f2)
{
    const int n = (int) x.size();
    std::vector<double> w ((size_t) n);
    double wsum = 0;
    for (int i = 0; i < n; ++i) { w[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * i / (n - 1)); wsum += w[(size_t) i] * w[(size_t) i]; }
    double e = 0;
    for (int k = (int) std::ceil (f1 * n / sr); k <= (int) (f2 * n / sr); ++k)
    {
        const double c = 2.0 * std::cos (2.0 * 3.14159265358979 * k / n);
        double s1 = 0, s2 = 0;
        for (int i = 0; i < n; ++i) { const double s0 = x[(size_t) i] * w[(size_t) i] + c * s1 - s2; s2 = s1; s1 = s0; }
        e += s1 * s1 + s2 * s2 - c * s1 * s2;
    }
    return std::sqrt (2.0 * e / (wsum * n));
}

std::vector<float> mid (const juce::AudioBuffer<float>& a, int from = 0, int to = -1)
{
    if (to < 0) to = a.getNumSamples();
    std::vector<float> v;
    for (int i = from; i < to; ++i) v.push_back (0.5f * (a.getSample (0, i) + a.getSample (1, i)));
    return v;
}
std::vector<float> side (const juce::AudioBuffer<float>& a)
{
    std::vector<float> v;
    for (int i = 0; i < a.getNumSamples(); ++i) v.push_back (0.5f * (a.getSample (0, i) - a.getSample (1, i)));
    return v;
}

double peak (const juce::AudioBuffer<float>& a)
{
    return std::max (a.getMagnitude (0, 0, a.getNumSamples()), a.getMagnitude (1, 0, a.getNumSamples()));
}
bool finite (const juce::AudioBuffer<float>& a)
{
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (! std::isfinite (a.getSample (c, i))) return false;
    return true;
}
double maxStep (const juce::AudioBuffer<float>& a, int from, int to)
{
    double m = 0;
    for (int i = std::max (1, from); i < to; ++i) m = std::max (m, (double) std::abs (a.getSample (0, i) - a.getSample (0, i - 1)));
    return m;
}
double centroid (const std::vector<float>& x, double sr)
{
    const double bands[] = { 30, 60, 120, 250, 500, 1000, 2000, 4000, 8000, 16000 };
    double num = 0, den = 0;
    for (int i = 0; i + 1 < 10; ++i)
    {
        const double e = std::pow (bandRms (x, sr, bands[i], bands[i + 1]), 2.0);
        num += e * std::log2 (std::sqrt (bands[i] * bands[i + 1]));
        den += e;
    }
    return num / std::max (den, 1e-20);
}
double zcFreq (const juce::AudioBuffer<float>& a, double sr, int from, int to)
{
    int first = -1, last = -1, count = 0;
    for (int i = std::max (1, from); i < to; ++i)
        if (a.getSample (0, i - 1) < 0.0f && a.getSample (0, i) >= 0.0f) { if (first < 0) first = i; last = i; ++count; }
    return count > 1 ? (count - 1) * sr / (last - first) : 0.0;
}

// Fixed test scenarios (copies of early factory presets), independent of the current preset library.
SynthParams scenario (const juce::String& name)
{
    static const std::map<juce::String, std::vector<std::pair<const char*, float>>> table {
        { "Acid Burn", { {"shape",2.0f},{"sub",.2f},{"cutoff",455},{"resonance",.8f},{"envAmount",.75f},{"fltDecay",180},{"filterMode",0},{"distType",0},{"distDrive",.55f},{"distTone",0.65f},{"glide",70},{"glideMode",1},{"decay",300},{"sustain",.7f},{"width",0.35f},{"splitFreq",250},{"master",-5.2f} } },
        { "Chrome Snarl", { {"fmAmount",.8f},{"fmRatio",17},{"oscMix",.7f},{"fmDecay",90},{"cutoff",6500},{"resonance",.2f},{"envAmount",.2f},{"distType",2},{"distDrive",.5f},{"distTone",0.75f},{"bite",.6f},{"width",0.60f},{"splitFreq",300},{"master",-6.8f} } },
        { "Raw Saw", { {"shape",2.0f},{"sub",.5f},{"cutoff",2160},{"resonance",.25f},{"envAmount",.3f},{"distType",0},{"distDrive",.3f},{"distTone",0.70f},{"punch",.3f},{"width",0.00f},{"splitFreq",300},{"master",-4.5f} } },
        { "Total Collapse", { {"shape",2.5f},{"cutoff",2100},{"resonance",.4f},{"distType",3},{"distDrive",.95f},{"distTone",0.55f},{"bite",.8f},{"fmAmount",.3f},{"fmRatio",6},{"oscMix",.3f},{"width",0.50f},{"splitFreq",200},{"master",-9.7f} } },
    };
    Snapshot s;
    for (const auto& [k, v] : table.at (name)) s[k] = v;
    return paramsFromSnapshot (s);
}

SynthParams preset (const char* name)
{
    for (const auto& p : factoryPresets())
        if (p.name == name) return paramsFromSnapshot (p.values);
    expect (false, juce::String ("missing preset ") + name);
    return {};
}

const std::vector<Note> oneNote { { 0.0, 0.6, 36 } };

//==============================================================================
void testPresets (bool report)
{
    const auto& f = factoryPresets();
    expect (f.size() == 304, "304 factory presets, got " + juce::String ((int) f.size()));
    std::map<juce::String, int> cats;
    std::set<juce::String> names;
    for (const auto& p : f)
    {
        ++cats[p.category];
        expect (names.insert (p.name).second, "unique preset name: " + p.name);
        expect (juce::StringArray { "Reese", "FM", "Plucks", "Sustained" }.contains (p.group), p.name + ": browser group (" + p.group + ")");
    }
    expect (cats.size() == 10, "10 categories");
    // Rolling basses are the core of the library (100 of 304; Dark Underground has 4 hand-designed variants of
    // Reese Core and Dark Pulse since 2026-10-23).
    const std::map<juce::String, int> expected { { "Rolling Techno Bass", 100 }, { "Dark Underground Bass", 64 },
        { "Hard Techno Rumble Bass", 55 }, { "Raw Analog Bass", 20 }, { "Acid-Inspired Bass", 13 }, { "Destroyed Bass", 13 },
        { "Schranz Distorted Bass", 12 }, { "Metallic Bass", 12 }, { "Industrial FM Bass", 10 }, { "Deep Sub Bass", 5 } };
    for (auto& [c, n] : expected)
        expect (cats[c] == n, c + " has " + juce::String (n) + " presets (got " + juce::String (cats[c]) + ")");

    for (const auto& pr : f)
    {
        const auto a = render (paramsFromSnapshot (pr.values), oneNote, 1.4);
        const double pk = peak (a);
        expect (finite (a), pr.name + ": finite");
        expect (pk <= 1.0, pr.name + ": peak <= 0 dBFS");
        expect (pk > 0.2, pr.name + ": audible (" + juce::String (db (pk), 1) + " dB)");
        const auto m = mid (a), s = side (a);
        const double subSide = db (bandRmsDft (s, 48000, 20, 90)) - db (bandRms (m, 48000, 20, 16000));
        expect (subSide < -50.0, pr.name + ": sub stays mono (" + juce::String (subSide, 1) + " dB)");
        // ...over the whole bass range, not only at C2 (low notes put harmonics near SPLIT).
        for (int note : { 28, 48 })
        {
            const auto b = render (paramsFromSnapshot (pr.values), std::vector<Note> { { 0.0, 0.6, note } }, 0.8);
            const double ss = db (bandRmsDft (side (b), 48000, 20, 90)) - db (bandRms (mid (b), 48000, 20, 16000));
            expect (ss < -50.0, pr.name + ": sub stays mono at note " + juce::String (note) + " (" + juce::String (ss, 1) + " dB)");
        }
        double mean = 0;
        for (int i = 0; i < 28800; ++i) mean += a.getSample (0, i);
        mean /= 28800.0;
        expect (std::abs (mean) < 5.0e-3, pr.name + ": no DC offset (" + juce::String (mean, 5) + ")");
        // Silence after release: the voice must stop.
        expect (rms (a, 0, (int) (1.3 * 48000), (int) (1.4 * 48000)) < 1.0e-4 || paramsFromSnapshot (pr.values).releaseMs > 600,
                pr.name + ": decays to silence after release");
        if (report)
            std::cout << juce::String (pr.name).paddedRight (' ', 20) << " peak " << juce::String (db (pk), 1) << "  noteRMS "
                      << juce::String (db (rms (a, 0, 0, 12000)), 1) << "  side/mid " << juce::String (db (bandRms (s, 48000, 200, 16000)) - db (bandRms (m, 48000, 200, 16000)), 1)
                      << "  centroid " << juce::String (centroid (m, 48000), 2) << "\n";
    }
}

// Factory level (Tools/normalize_peaks.py): MASTER puts the loudest moment of the level grid at -1 dBFS,
// so no note or roll reaches the emergency ceiling (0.9 linear, -0.915 dBFS) and no preset is left quieter than the
// others. The grid: both riffs at 150 BPM on 5 roots, a held note on every semitone E1-C3, and rolling
// 16ths at 130/170 and 32nds at 130/150/170 BPM on the same roots -- a resonant filter peaks differently
// on every note and, through its envelope, at every tempo. Measured 40 dB down (MASTER is linear).
void testFactoryLevels()
{
    const auto& f = factoryPresets();
    std::vector<double> loudest (f.size(), 0.0), masters (f.size(), 0.0);
    std::atomic<size_t> next { 0 };
    auto work = [&]
    {
        for (size_t i; (i = next++) < f.size();)
        {
            auto p = paramsFromSnapshot (f[i].values);
            const double master = p.masterDb;
            masters[i] = master;
            p.masterDb = -40.0f;
            double pk = 0.0;
            auto take = [&] (int root, int pattern, double bpm)
            {
                const auto a = render (p, patternNotes (root, pattern, bpm, 3.0), 3.0, 48000.0, 512);
                pk = std::max ({ pk, (double) a.getMagnitude (0, 0, a.getNumSamples()), (double) a.getMagnitude (1, 0, a.getNumSamples()) });
            };
            for (int root : { 28, 33, 38, 43, 48 })
            {
                for (int pattern : { 0, 1 })
                    take (root, pattern, 150.0);
                for (double bpm : { 130.0, 170.0, 260.0, 300.0, 340.0 }) // 32nds at T BPM = 16ths at 2T
                    take (root, 0, bpm);
            }
            for (int root = 28; root <= 48; ++root)
                take (root, 2, 150.0);
            loudest[i] = db (pk) + 40.0 + master;
        }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < std::max (1u, std::thread::hardware_concurrency()); ++t)
        pool.emplace_back (work);
    for (auto& t : pool)
        t.join();
    // MASTER moves in 0.1 dB steps in the plug-in: a finer value would be rounded on load.
    int offStep = 0;
    for (const auto& pr : f)
        if (const auto it = pr.values.find (id::master); it != pr.values.end())
            offStep += std::abs (it->second * 10.0f - std::round (it->second * 10.0f)) > 1.0e-3f ? 1 : 0;
    expect (offStep == 0, "factory MASTER values sit on the 0.1 dB parameter step (" + juce::String (offStep) + " off)");
    int over = 0, under = 0, capped = 0;
    double hi = -100.0, lo = 100.0;
    juce::String hiName, loName;
    for (size_t i = 0; i < f.size(); ++i)
    {
        if (loudest[i] > hi) { hi = loudest[i]; hiName = f[i].name; }
        if (loudest[i] < lo) { lo = loudest[i]; loName = f[i].name; }
        over += loudest[i] > -0.95 ? 1 : 0;
        // MASTER cannot go above its +6 dB maximum: a preset already there may stay up to 0.5 dB short
        // (Warm Rumble 02 since every note restarts from silence, 2026-10-23).
        const bool atMax = masters[i] >= 5.95 && loudest[i] > -1.6;
        capped += loudest[i] <= -1.1 && atMax ? 1 : 0;
        under += loudest[i] <= -1.1 && ! atMax ? 1 : 0;
    }
    expect (over == 0, "no factory preset gets near the emergency ceiling on any note E1-C3 (" + juce::String (over)
                       + " over -0.95 dBFS, loudest " + hiName + " " + juce::String (hi, 2) + " dBFS)");
    expect (under == 0, "every factory preset peaks at -1 dBFS over the level grid (" + juce::String (under)
                        + " under -1.1 dBFS, quietest " + loName + " " + juce::String (lo, 2) + " dBFS; "
                        + juce::String (capped) + " at the MASTER maximum)");
    expect (capped <= 2, "at most two presets need MASTER beyond its maximum (" + juce::String (capped) + ")");
}

void testWidth()
{
    auto p = scenario ("Chrome Snarl");
    p.width = 0.0f;
    const auto a = render (p, oneNote, 0.8);
    double diff = 0;
    for (int i = 0; i < a.getNumSamples(); ++i) diff = std::max (diff, (double) std::abs (a.getSample (0, i) - a.getSample (1, i)));
    expect (diff == 0.0, "WIDTH 0 gives identical L/R");

    for (float split : { 100.0f, 300.0f, 1000.0f })
    {
        p.splitHz = split;
        p.width = 0.0f;
        const auto ref = render (p, oneNote, 0.8);
        p.width = 1.0f;
        const auto wide = render (p, oneNote, 0.8);
        const auto mr = mid (ref), mw = mid (wide);
        const double below = db (bandRms (mw, 48000, 20, split * 0.5)) - db (bandRms (mr, 48000, 20, split * 0.5));
        const double above = db (bandRms (mw, 48000, split * 2.0, 16000)) - db (bandRms (mr, 48000, split * 2.0, 16000));
        expect (std::abs (below) < 0.3, "mono sum below split unchanged at " + juce::String (split) + " Hz (" + juce::String (below, 2) + " dB)");
        expect (std::abs (above) < 2.5, "mono sum above split within 2.5 dB (" + juce::String (above, 2) + " dB)");
        const double ch = db (rms (wide, 0, 0, wide.getNumSamples())) - db (rms (ref, 0, 0, ref.getNumSamples()));
        expect (ch < 0.5, "width is not a loudness boost (" + juce::String (ch, 2) + " dB)");
        const auto s = side (wide), m = mid (wide);
        // Side content two octaves below the split, relative to the whole mono signal.
        const double lowSide = db (bandRms (s, 48000, 20, split * 0.25)) - db (bandRms (m, 48000, 20, 16000));
        expect (lowSide < -50.0, "two octaves below split stays mono (" + juce::String (lowSide, 1) + " dB)");
        expect (db (bandRms (s, 48000, split * 2.0, 16000)) - db (bandRms (m, 48000, split * 2.0, 16000)) > -12.0, "above split is audibly wide");
    }
}

// Test written by Codex (2026-10-22): the stereo stage must not delay the lows. With WIDTH 0 the
// centre is the input itself, so SPLIT cannot change the mono waveform.
void testSplitDoesNotMoveTheMonoSignal()
{
    auto p = scenario ("Chrome Snarl");
    p.width = 0.0f;
    p.masterDb = -40.0f; // keep the ceiling out of a comparison of stereo-stage phase
    p.splitHz = 150.0f;
    const auto reference = render (p, oneNote, 0.8);
    for (float split : { 300.0f, 1000.0f })
    {
        p.splitHz = split;
        const auto actual = render (p, oneNote, 0.8);
        double error = 0.0;
        for (int i = 0; i < actual.getNumSamples(); ++i)
            error = std::max (error, (double) std::abs (actual.getSample (0, i) - reference.getSample (0, i)));
        expect (error < 1.0e-7, "SPLIT must not disperse the centre when WIDTH is zero (error "
                                  + juce::String (error, 9) + ")");
    }
}

void testRatesBlocksOversampling()
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int block : { 16, 173, 512, 2048 })
            for (int osIdx : { 0, 1, 2 })
            {
                auto p = scenario ("Total Collapse");
                p.oversampleIndex = osIdx;
                const auto a = render (p, oneNote, 0.5, sr, block);
                expect (finite (a) && peak (a) <= 1.0 && peak (a) > 0.2,
                        "sr " + juce::String (sr) + " block " + juce::String (block) + " os " + juce::String (2 << osIdx));
            }
    const auto a = render (scenario ("Acid Burn"), oneNote, 0.8, 48000, 64);
    const auto b = render (scenario ("Acid Burn"), oneNote, 0.8, 48000, 512);
    double diff = 0;
    for (int i = 0; i < a.getNumSamples(); ++i) diff = std::max (diff, (double) std::abs (a.getSample (0, i) - b.getSample (0, i)));
    expect (diff < 1.0e-3, "buffer-size independent (max diff " + juce::String (diff, 6) + ")");
}

void testNoClicks()
{
    // Steady sustained note, then many parameters jump at once.
    auto p = scenario ("Raw Saw");
    p.sustain = 1.0f;
    const std::vector<Note> held { { 0.0, 2.0, 36 } };
    const auto a = render (p, held, 1.0, 48000, 256, [] (double t, SynthParams& q)
    {
        if (t >= 0.5)
        {
            q.cutoffHz = 4000; q.resonance = 0.8f; q.distDrive = 0.9f; q.distMix = 0.4f; q.width = 1.0f;
            q.masterDb = 0.0f; q.fmAmount = 0.6f; q.shape = 3.0f; q.dark = 1.0f; q.bite = 1.0f; q.splitHz = 1000;
        }
    });
    const int at = 24000;
    const double before = maxStep (a, at - 4800, at), after = maxStep (a, at + 4800, at + 9600), jump = maxStep (a, at, at + 960);
    expect (jump < std::max (before, after) * 1.5, "no click on parameter jump (" + juce::String (jump, 4) + " vs " + juce::String (std::max (before, after), 4) + ")");

    // Discrete changes (preset switch): distortion type, filter mode, position, oversampling.
    const auto b = render (p, held, 1.0, 48000, 256, [] (double t, SynthParams& q)
    {
        if (t >= 0.5) { q.distType = DistType::fold; q.filterMode = FilterMode::bp; q.filterPostDist = true; q.oversampleIndex = 2; }
    });
    const double sw = maxStep (b, at, at + 960), st = std::max (maxStep (b, at - 4800, at), maxStep (b, at + 4800, at + 9600));
    expect (sw < st * 1.5, "no click on preset-style switch (" + juce::String (sw, 4) + " vs " + juce::String (st, 4) + ")");

    // Mono retrigger while sounding: no discontinuity.
    auto q = scenario ("Raw Saw");
    q.glideMode = GlideMode::mono;
    const auto c = render (q, { { 0.0, 0.3, 36 }, { 0.3, 0.6, 38 } }, 0.6);
    const int rt = (int) (0.3 * 48000);
    // Compare with the first note's own attack: the filter envelope legitimately re-opens.
    const double rStep = maxStep (c, rt, rt + 480), rRef = maxStep (c, 0, 4800);
    expect (rStep < rRef * 1.6, "mono retrigger is click-free (" + juce::String (rStep, 4) + " vs " + juce::String (rRef, 4) + ")");
}

void testGlideAndLegato()
{
    SynthParams p;
    p.shape = 0; p.sub = 0; p.distDrive = 0; p.distMix = 0; p.cutoffHz = 18000; p.envAmount = 0; p.punch = 0;
    p.oscDrive = 0; p.attackMs = 1; p.sustain = 1; p.releaseMs = 20;
    const std::vector<Note> legato { { 0.0, 0.55, 45 }, { 0.5, 1.2, 57 } }; // A2 110 Hz -> A3 220 Hz, overlapping
    p.glideMs = 0; p.glideMode = GlideMode::legato;
    auto a = render (p, legato, 1.2);
    const double jumpF = zcFreq (a, 48000, (int) (0.52 * 48000), (int) (0.6 * 48000));
    expect (std::abs (jumpF - 220.0) < 4.0, "no glide: jumps to 220 Hz (" + juce::String (jumpF, 1) + ")");
    p.glideMs = 400;
    a = render (p, legato, 1.2);
    const double midF = zcFreq (a, 48000, (int) (0.55 * 48000), (int) (0.6 * 48000));
    const double endF = zcFreq (a, 48000, (int) (1.05 * 48000), (int) (1.15 * 48000));
    expect (midF > 115.0 && midF < 200.0, "glide in progress (" + juce::String (midF, 1) + " Hz)");
    expect (std::abs (endF - 220.0) < 4.0, "glide arrives (" + juce::String (endF, 1) + " Hz)");

    // Legato: overlapping note must not restart the envelope (slow attack keeps rising).
    p.glideMs = 0; p.attackMs = 800; p.glideMode = GlideMode::legato;
    auto l = render (p, legato, 1.0);
    p.glideMode = GlideMode::mono;
    auto m = render (p, legato, 1.0);
    const int t = (int) (0.7 * 48000);
    expect (rms (l, 0, t, t + 2400) > rms (m, 0, t, t + 2400) * 0.98, "legato does not retrigger, mono does");
}

// Amplitude of a (near) sine of known frequency at sample i: sqrt(x^2 + (x' / 2 pi f)^2). Unlike short
// RMS windows it does not dip at zero crossings, so it shows where the sound itself fades.
double sineAmp (const juce::AudioBuffer<float>& a, int i, double f, double sr)
{
    const double x = a.getSample (0, i);
    const double d = (a.getSample (0, i + 1) - a.getSample (0, i - 1)) * 0.5 * sr / (2.0 * 3.14159265358979 * f);
    return std::sqrt (x * x + d * d);
}
double meanSineAmp (const juce::AudioBuffer<float>& a, int from, int to, double f, double sr)
{
    double s = 0;
    for (int i = from; i < to; ++i) s += sineAmp (a, i, f, sr);
    return s / std::max (1, to - from);
}
double minSineAmp (const juce::AudioBuffer<float>& a, int from, int to, double f, double sr)
{
    double m = 1.0e9;
    for (int i = from; i < to; ++i) m = std::min (m, sineAmp (a, i, f, sr));
    return m;
}

// A note that starts while the previous one still sounds restarts from silence: the old sound
// fades out in about 1 ms and the new note runs the whole ATTACK, like a note after a rest.
// (It used to start from the current envelope level, so with a long SUSTAIN/RELEASE the notes of a
// rolling line ran together and ATTACK did nothing - Roman's "Bass 2" in Logic, 2026-10-23.)
void testEveryNoteHasFullAttack()
{
    SynthParams p;                      // sustained like "Biting Reese 02"; a sine so the level is measurable
    p.shape = 0.0f; p.sub = 0.0f; p.sustain = 0.9f; p.decayMs = 43.0f; p.releaseMs = 400.0f;
    p.glideMode = GlideMode::legato; p.glideMs = 0.0f; p.cutoffHz = 18000.0f; p.resonance = 0.0f; p.distTone = 1.0f;
    p.envAmount = 0.0f; p.punch = 0.0f; p.distDrive = 0.0f; p.distMix = 0.0f; p.fmAmount = 0.0f; p.oscDrive = 0.0f;
    const double sr = 48000.0, step = 60.0 / 133.0 / 4.0, c1 = 32.703; // 16ths at 133 BPM, back to back, C1
    std::vector<Note> run;
    for (int k = 0; k < 8; ++k) run.push_back ({ 0.2 + k * step, 0.2 + (k + 1) * step, 36 });
    const auto onAt = [&] (int k) { return (int) ((0.2 + k * step) * sr); };

    // ATTACK 40 ms (linear): 4..12 ms into a note it is 10..30 % of full, so below half the level at 60..100 ms.
    p.attackMs = 40.0f;
    const auto a = render (p, run, 0.2 + 9 * step);
    int slow = 0;
    for (int k = 1; k < 8; ++k)
        if (meanSineAmp (a, onAt (k) + 192, onAt (k) + 576, c1, sr) < 0.5 * meanSineAmp (a, onAt (k) + 2880, onAt (k) + 4800, c1, sr)) ++slow;
    expect (slow == 7, "every back-to-back note runs the 40 ms ATTACK (" + juce::String (slow) + "/7)");

    // Fastest ATTACK: each back-to-back note starts exactly like the same note after a rest - its first
    // 10 ms match a fresh note's waveform (allowing up to 2 ms for the previous note to fade out).
    auto q = p;
    q.attackMs = 0.5f;
    const auto b = render (q, run, 0.2 + 9 * step);
    const auto fresh = render (q, { { 0.2, 0.2 + step, 36 } }, 0.2 + 2 * step);
    const int f0 = onAt (0), len = 480;
    const double ref = rms (fresh, 0, f0, f0 + len);
    int alike = 0;
    for (int k = 1; k < 8; ++k)
    {
        double best = 1.0e9;
        for (int lag = 0; lag <= 96; ++lag)
        {
            double e = 0;
            for (int i = 0; i < len; ++i)
            {
                const double d = b.getSample (0, onAt (k) + lag + i) - fresh.getSample (0, f0 + i);
                e += d * d;
            }
            best = std::min (best, std::sqrt (e / len) / ref);
        }
        if (best < 0.2) ++alike;
    }
    expect (alike == 7, "every back-to-back note starts like a note after a rest (" + juce::String (alike) + "/7)");

    // The same key struck again with its old note-off arriving a sample later (on before off):
    // the new note must keep sounding until its own note-off, not fall into the release.
    const auto c = render (q, { { 0.1, 0.5 + 1.0 / sr, 36 }, { 0.5, 0.9, 36 } }, 1.0);
    expect (meanSineAmp (c, (int) (0.75 * sr), (int) (0.85 * sr), c1, sr) > 0.8 * meanSineAmp (c, (int) (0.35 * sr), (int) (0.45 * sr), c1, sr),
            "a re-struck key keeps sounding when its old note-off comes after the new note-on");

    // SMOOTH (the old behaviour, a switch): back-to-back notes continue the sound - no dip at a note start,
    // and a slow ATTACK does not start again from silence.
    auto sm = q;
    sm.smoothStart = true;
    const auto e = render (sm, run, 0.2 + 9 * step);
    int flowing = 0;
    for (int k = 1; k < 8; ++k)
        if (minSineAmp (e, onAt (k), onAt (k) + 144, c1, sr) > 0.5 * meanSineAmp (e, onAt (k) + 480, onAt (k) + 960, c1, sr)) ++flowing;
    expect (flowing == 7, "SMOOTH: back-to-back notes continue without a new attack (" + juce::String (flowing) + "/7)");
    auto smSlow = p;
    smSlow.smoothStart = true;
    const auto g = render (smSlow, run, 0.2 + 9 * step);
    expect (meanSineAmp (g, onAt (4) + 192, onAt (4) + 576, c1, sr) > 0.8 * meanSineAmp (g, onAt (4) + 2880, onAt (4) + 4800, c1, sr),
            "SMOOTH: a slow ATTACK continues from the level reached, not from silence");

    // Control: legato between different, overlapping keys does not restart (no dip; G1 = 49 Hz after the change).
    const auto d = render (q, { { 0.1, 0.55, 36 }, { 0.5, 0.9, 43 } }, 1.0);
    const double g1 = 48.999;
    expect (minSineAmp (d, (int) (0.5 * sr) + 48, (int) (0.5 * sr) + 144, g1, sr) > 0.5 * meanSineAmp (d, (int) (0.6 * sr), (int) (0.62 * sr), g1, sr),
            "legato between overlapping keys does not restart");
}

// High-frequency share: energy above 1 kHz relative to the whole signal, in dB.
double brightness (const std::vector<float>& x) { return db (bandRms (x, 48000, 1000, 16000)) - db (bandRms (x, 48000, 25, 16000)); }

void testCharacterAndModes()
{
    auto base = scenario ("Raw Saw");
    const std::vector<Note> f1 { { 0.0, 0.6, 41 } };
    const auto ref = mid (render (base, f1, 0.6));
    const double b0 = brightness (ref), sub0 = bandRms (ref, 48000, 20, 60);

    auto d = base; d.dark = 1.0f;
    const auto dm = mid (render (d, f1, 0.6));
    expect (brightness (dm) < b0 - 3.0, "DARK darkens (" + juce::String (brightness (dm) - b0, 1) + " dB)");
    expect (db (bandRms (dm, 48000, 20, 60)) - db (sub0) < 2.0, "DARK does not pump the sub");
    auto b = base; b.bite = 1.0f;
    const auto bm = mid (render (b, f1, 0.6));
    expect (brightness (bm) > b0 + 3.0, "BITE brightens (" + juce::String (brightness (bm) - b0, 1) + " dB)");
    expect (db (bandRms (bm, 48000, 20, 60)) - db (sub0) < 2.0, "BITE does not pump the sub");

    auto f = base; f.fmAmount = 0.8f; f.cutoffHz = 8000;
    auto f0 = base; f0.cutoffHz = 8000;
    const double bf = brightness (mid (render (f, f1, 0.6))), bf0 = brightness (mid (render (f0, f1, 0.6)));
    expect (bf > bf0 + 3.0, "FM AMOUNT adds harmonics (" + juce::String (bf - bf0, 1) + " dB)");

    // Each distortion type and filter mode produces a distinct spectrum.
    std::vector<std::vector<double>> prints;
    auto spectrum = [] (const std::vector<float>& x)
    {
        std::vector<double> v;
        const double e[] = { 30, 80, 200, 500, 1200, 3000, 7000, 16000 };
        for (int i = 0; i + 1 < 8; ++i) v.push_back (db (bandRms (x, 48000, e[i], e[i + 1])));
        return v;
    };
    auto distinct = [&] (const juce::String& what, const std::vector<std::vector<double>>& s)
    {
        for (size_t i = 0; i < s.size(); ++i)
            for (size_t j = i + 1; j < s.size(); ++j)
            {
                double m = 0;
                for (size_t k = 0; k < s[i].size(); ++k) m = std::max (m, std::abs (s[i][k] - s[j][k]));
                expect (m > 1.5, what + " " + juce::String ((int) i) + " vs " + juce::String ((int) j) + " differ (" + juce::String (m, 1) + " dB)");
            }
    };
    for (int t = 0; t < 5; ++t)
    {
        auto p = base; p.distType = (DistType) t; p.distDrive = 0.8f; p.cutoffHz = 3000;
        prints.push_back (spectrum (mid (render (p, oneNote, 0.6))));
    }
    distinct ("distortion type", prints);
    prints.clear();
    for (int m = 0; m < 4; ++m)
    {
        auto p = base; p.filterMode = (FilterMode) m; p.cutoffHz = 600; p.resonance = 0.5f; p.distDrive = 0.1f;
        prints.push_back (spectrum (mid (render (p, oneNote, 0.6))));
    }
    distinct ("filter mode", prints);
}

void testDetune()
{
    // Reese: detuned voices make the sustained note beat; pitch centre and mono sub stay.
    SynthParams p;
    p.shape = 2.0f; p.sub = 0.0f; p.cutoffHz = 6000; p.envAmount = 0; p.distDrive = 0; p.distMix = 0; p.punch = 0;
    p.sustain = 1.0f; p.attackMs = 1; p.width = 0;
    const std::vector<Note> held { { 0.0, 1.5, 45 } };
    auto beating = [] (const juce::AudioBuffer<float>& a)
    {
        std::vector<double> fr;
        for (int i = (int) (0.3 * 48000); i + 480 < (int) (1.4 * 48000); i += 480) fr.push_back (db (rms (a, 0, i, i + 480)));
        double m = 0; for (double v : fr) m += v; m /= (double) fr.size();
        double var = 0; for (double v : fr) var += (v - m) * (v - m);
        return std::sqrt (var / (double) fr.size());
    };
    p.detune = 0.0f;
    const auto dry = render (p, held, 1.5);
    p.detune = 0.6f;
    const auto wet = render (p, held, 1.5);
    expect (beating (wet) > beating (dry) + 1.0, "DETUNE makes the note beat (" + juce::String (beating (wet), 2) + " vs " + juce::String (beating (dry), 2) + " dB)");
    // Spectral peak (Goertzel, 0.25 Hz steps over 1.1 s): zero crossings are unreliable on a beating sound.
    const auto sine = [&] { auto q = p; q.shape = 0.0f; return render (q, held, 1.5); }();
    double f = 0, best = -1;
    for (double hz = 95; hz <= 125; hz += 0.25)
    {
        double re = 0, im = 0;
        for (int i = (int) (0.3 * 48000); i < (int) (1.4 * 48000); ++i)
        {
            const double ph = 2.0 * 3.14159265358979 * hz * i / 48000.0;
            re += sine.getSample (0, i) * std::cos (ph); im += sine.getSample (0, i) * std::sin (ph);
        }
        if (re * re + im * im > best) { best = re * re + im * im; f = hz; }
    }
    expect (std::abs (12.0 * std::log2 (f / 110.0)) < 0.3, "DETUNE keeps the pitch centre (" + juce::String (f, 1) + " Hz)");
}

void testDriveSweepIsSmooth()
{
    // Found by Codex: Fold's level compensation sampled the curve at 4 points, which can all
    // land on zeros (DRIVE ~72%), giving an ~11 dB jump. Level must follow DRIVE smoothly.
    auto base = scenario ("Raw Saw");
    base.masterDb = -30.0f; // stay far from the output protection
    const std::vector<Note> held { { 0.0, 0.5, 38 } };
    for (int t = 0; t < 5; ++t)
    {
        double prev = 0, worst = 0, at = 0;
        for (int i = 0; i <= 40; ++i)
        {
            auto p = base; p.distType = (DistType) t; p.distMix = 1.0f; p.distDrive = 0.5f + 0.01f * (float) i;
            const double level = db (rms (render (p, held, 0.5), 0, 4800, 24000));
            if (i > 0 && std::abs (level - prev) > worst) { worst = std::abs (level - prev); at = p.distDrive; }
            prev = level;
        }
        expect (worst <= 3.0, "distortion " + juce::String (t) + ": level follows DRIVE smoothly (max step "
                                  + juce::String (worst, 2) + " dB at " + juce::String (at, 2) + ")");
    }
}

void testStereoDelay()
{
    // DELAY: a real L/R time offset above SPLIT; the mono sum must not change at all.
    auto p = scenario ("Chrome Snarl");
    p.width = 1.0f; p.splitHz = 300.0f; p.masterDb = -18.0f; // well below the emergency ceiling
    p.stereoDelayMs = 0.0f;
    const auto a = render (p, oneNote, 0.8);
    p.stereoDelayMs = 5.0f;
    const auto b = render (p, oneNote, 0.8);
    const auto sa = side (a), sb = side (b);
    const double gain = db (bandRms (sb, 48000, 600, 16000)) - db (bandRms (sa, 48000, 600, 16000));
    expect (gain >= 3.0, "DELAY 5 ms adds width above SPLIT (+" + juce::String (gain, 1) + " dB)");
    double diff = 0;
    for (int i = 0; i < a.getNumSamples(); ++i)
        diff = std::max (diff, (double) std::abs ((a.getSample (0, i) + a.getSample (1, i)) - (b.getSample (0, i) + b.getSample (1, i))));
    expect (diff < 1.0e-5, "DELAY leaves the mono sum unchanged (" + juce::String (diff, 8) + ")");
    expect (db (bandRmsDft (sb, 48000, 20, 90)) - db (bandRms (mid (b), 48000, 20, 16000)) < -50.0, "DELAY keeps the sub mono");
}

void testNoAllocation()
{
    dsp::BassEngine e;
    e.prepare (48000, 512);
    auto p = scenario ("Total Collapse");
    juce::AudioBuffer<float> out (2, 512);
    juce::MidiBuffer m;
    m.ensureSize (256);
    e.process (out.getWritePointer (0), out.getWritePointer (1), 512, m, p); // first call may set up
    m.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 10);
    trackAllocs = true;
    allocCount = 0;
    for (int i = 0; i < 50; ++i)
    {
        if (i == 25) { p.distType = DistType::crush; p.oversampleIndex = 2; p.filterMode = FilterMode::hp; }
        e.process (out.getWritePointer (0), out.getWritePointer (1), 512, m, p);
    }
    trackAllocs = false;
    expect (allocCount.load() == 0, "no heap allocation in the audio thread (" + juce::String (allocCount.load()) + ")");
}

void testLegacyPresetMigration()
{
    // Presets saved before DETUNE / stereo DELAY existed must not pick up today's defaults.
    const juce::String xml = "<PowerBassPreset format=\"1\" name=\"Old\" category=\"User\">"
                             "<Param id=\"shape\" value=\"2\"/><Param id=\"cutoff\" value=\"900\"/></PowerBassPreset>";
    Preset p;
    juce::String err;
    expect (presetFromXml (xml, p, err), "old preset parses");
    expect (p.values.count (id::detune) && std::abs (p.values[id::detune]) < 1e-6f, "old preset: DETUNE off");
    expect (p.values.count (id::stereoDelay) && std::abs (p.values[id::stereoDelay]) < 1e-6f, "old preset: stereo DELAY off");
}

void testPresetFiles()
{
    Snapshot s = factoryPresets()[3].values;
    s[id::width] = 0.42f;
    s[id::splitFreq] = 512.0f;
    Preset back;
    juce::String err;
    expect (presetFromXml (presetToXml (juce::String::fromUTF8 ("Тёмный бас"), "User", s), back, err), "preset XML parses");
    expect (back.name == juce::String::fromUTF8 ("Тёмный бас"), "unicode name round-trips");
    expect (std::abs (back.values[id::width] - 0.42f) < 1e-5f && std::abs (back.values[id::splitFreq] - 512.0f) < 1e-3f, "width/split stored");
    expect (! presetFromXml ("<x/>", back, err), "rejects foreign XML");

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("powerbass-test-" + juce::String (juce::Random::getSystemRandom().nextInt()));
    {
        PresetLibrary lib (dir);
        expect (lib.saveUser ("Mine", s, err), "save: " + err);
        expect (lib.rename (lib.indexOf ("Mine"), "Renamed", err), "rename: " + err);
        auto ex = dir.getChildFile ("x.pbpreset");
        expect (lib.exportTo (ex, "Renamed", s, err), "export: " + err);
        juce::String name;
        expect (lib.importFile (ex, name, err) && name == "Renamed 2", "import with unique name (" + name + ")");
        expect (! lib.remove (0, err), "factory protected");
        expect (lib.remove (lib.indexOf ("Renamed"), err), "delete: " + err);
    }
    dir.deleteRecursively();
}
}

int main (int argc, char** argv)
{
    // No GUI initialisation: the tests are pure DSP and file I/O (and run under Wine).
    if (argc > 1 && juce::String (argv[1]) == "--dump-presets")
    {
        for (const auto& pr : factoryPresets())
        {
            std::cout << pr.name << "|" << pr.category << "|";
            for (const auto& [k, v] : pr.values) std::cout << k << "=" << v << " ";
            std::cout << "\n";
        }
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "--serve")
    {
        // Render server for Tools/fit_bass.py. Each stdin line: "paramId=value ... _root=38 _bpm=150 _len=3 _pattern=0"
        // _pattern 0: rolling 16ths off the kick (gate 75%), 1: legato 8th line, 2: one held note (0.6 s).
        // Writes float32 stereo interleaved.
        std::string line;
        std::vector<float> out;
        while (std::getline (std::cin, line))
        {
            Snapshot s;
            double len = 3.0, bpm = 150.0;
            int root = 38, pattern = 0;
            std::istringstream ss (line);
            std::string tok;
            while (ss >> tok)
            {
                const auto eq = tok.find ('=');
                if (eq == std::string::npos) continue;
                const auto key = juce::String (tok.substr (0, eq));
                const float value = std::stof (tok.substr (eq + 1));
                if (key == "_len") len = value;
                else if (key == "_bpm") bpm = value;
                else if (key == "_root") root = (int) value;
                else if (key == "_pattern") pattern = (int) value;
                else s[key] = value;
            }
            const auto audio = render (paramsFromSnapshot (s), patternNotes (root, pattern, bpm, len), len, 48000.0, 512);
            out.resize ((size_t) audio.getNumSamples() * 2);
            for (int i = 0; i < audio.getNumSamples(); ++i)
            {
                out[(size_t) i * 2] = audio.getSample (0, i);
                out[(size_t) i * 2 + 1] = audio.getSample (1, i);
            }
            std::fwrite (out.data(), sizeof (float), out.size(), stdout);
            std::fflush (stdout);
        }
        return 0;
    }
    if (argc > 2 && juce::String (argv[1]) == "--export")
    {
        // A short riff per preset for listening: C1 C1 D#1 C1 F1 with gaps.
        juce::File dir (juce::String::fromUTF8 (argv[2]));
        dir.createDirectory();
        const std::vector<Note> riff { { 0.0, 0.2, 36 }, { 0.25, 0.45, 36 }, { 0.5, 0.9, 39 }, { 0.95, 1.15, 36 }, { 1.25, 1.85, 41 } };
        int n = 1;
        for (const auto& pr : factoryPresets())
        {
            const auto a = render (paramsFromSnapshot (pr.values), riff, 2.4);
            auto file = dir.getChildFile (juce::String (n++).paddedLeft ('0', 2) + " " + pr.category + " - " + pr.name + ".wav");
            file.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> st (file.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (st.get(), 48000, 2, 24, {}, 0));
            if (w) { st.release(); w->writeFromAudioSampleBuffer (a, 0, a.getNumSamples()); }
        }
        return 0;
    }
    const bool report = argc > 1 && juce::String (argv[1]) == "--report";
    testPresets (report);
    testFactoryLevels();
    testWidth();
    testSplitDoesNotMoveTheMonoSignal();
    testRatesBlocksOversampling();
    testNoClicks();
    testGlideAndLegato();
    testEveryNoteHasFullAttack();
    testCharacterAndModes();
    testDetune();
    testDriveSweepIsSmooth();
    testStereoDelay();
    testNoAllocation();
    testLegacyPresetMigration();
    testPresetFiles();
    std::cout << (failures == 0 ? "PASS " : "FAILED ") << (checks - failures) << "/" << checks << " checks\n";
    return failures == 0 ? 0 : 1;
}
