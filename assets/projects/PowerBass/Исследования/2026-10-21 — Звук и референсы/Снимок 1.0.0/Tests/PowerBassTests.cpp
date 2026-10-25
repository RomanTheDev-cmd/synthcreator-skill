// Headless checks of the PowerBass engine and presets.
// Run: powerbass_tests [--report] | --export <dir>

#include "DSP/BassEngine.h"
#include "Presets.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <iostream>
#include <cstdio>
#include <sstream>
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
    expect (f.size() == 50, "50 factory presets, got " + juce::String ((int) f.size()));
    std::map<juce::String, int> cats;
    for (const auto& p : f) ++cats[p.category];
    expect (cats.size() == 10, "10 categories");
    for (auto& [c, n] : cats) expect (n == 5, c + " has 5 presets");

    for (const auto& pr : f)
    {
        const auto a = render (paramsFromSnapshot (pr.values), oneNote, 1.4);
        const double pk = peak (a);
        expect (finite (a), pr.name + ": finite");
        expect (pk <= 1.0, pr.name + ": peak <= 0 dBFS");
        expect (pk > 0.2, pr.name + ": audible (" + juce::String (db (pk), 1) + " dB)");
        const auto m = mid (a), s = side (a);
        const double subSide = db (bandRmsSteep (s, 48000, 20, 90)) - db (bandRms (m, 48000, 20, 16000));
        expect (subSide < -50.0, pr.name + ": sub stays mono (" + juce::String (subSide, 1) + " dB)");
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

void testWidth()
{
    auto p = preset ("Chrome Snarl");
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

void testRatesBlocksOversampling()
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int block : { 16, 173, 512, 2048 })
            for (int osIdx : { 0, 1, 2 })
            {
                auto p = preset ("Total Collapse");
                p.oversampleIndex = osIdx;
                const auto a = render (p, oneNote, 0.5, sr, block);
                expect (finite (a) && peak (a) <= 1.0 && peak (a) > 0.2,
                        "sr " + juce::String (sr) + " block " + juce::String (block) + " os " + juce::String (2 << osIdx));
            }
    const auto a = render (preset ("Acid Burn"), oneNote, 0.8, 48000, 64);
    const auto b = render (preset ("Acid Burn"), oneNote, 0.8, 48000, 512);
    double diff = 0;
    for (int i = 0; i < a.getNumSamples(); ++i) diff = std::max (diff, (double) std::abs (a.getSample (0, i) - b.getSample (0, i)));
    expect (diff < 1.0e-3, "buffer-size independent (max diff " + juce::String (diff, 6) + ")");
}

void testNoClicks()
{
    // Steady sustained note, then many parameters jump at once.
    auto p = preset ("Raw Saw");
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
    auto q = preset ("Raw Saw");
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

// High-frequency share: energy above 1 kHz relative to the whole signal, in dB.
double brightness (const std::vector<float>& x) { return db (bandRms (x, 48000, 1000, 16000)) - db (bandRms (x, 48000, 25, 16000)); }

void testCharacterAndModes()
{
    auto base = preset ("Raw Saw");
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

void testNoAllocation()
{
    dsp::BassEngine e;
    e.prepare (48000, 512);
    auto p = preset ("Total Collapse");
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
        // _pattern 0: rolling 16ths off the kick (gate 75%), 1: legato 8th line. Writes float32 stereo interleaved.
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
            const double step = 60.0 / bpm / 4.0;
            std::vector<Note> notes;
            for (int k = 0; k * step < len - step; ++k)
            {
                if (pattern == 0 && k % 4 == 0) continue;           // leave the kick its beat
                if (pattern == 1 && k % 2 == 0) continue;           // offbeat 8ths
                const double gate = pattern == 0 ? 0.75 : 2.05;     // legato line overlaps the next note
                const int note = root + ((k / 16) % 2 == 1 && k % 16 >= 12 ? 3 : 0);
                notes.push_back ({ k * step, k * step + gate * step, note });
            }
            const auto audio = render (paramsFromSnapshot (s), notes, len, 48000.0, 512);
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
    testWidth();
    testRatesBlocksOversampling();
    testNoClicks();
    testGlideAndLegato();
    testCharacterAndModes();
    testNoAllocation();
    testPresetFiles();
    std::cout << (failures == 0 ? "PASS " : "FAILED ") << (checks - failures) << "/" << checks << " checks\n";
    return failures == 0 ? 0 : 1;
}
