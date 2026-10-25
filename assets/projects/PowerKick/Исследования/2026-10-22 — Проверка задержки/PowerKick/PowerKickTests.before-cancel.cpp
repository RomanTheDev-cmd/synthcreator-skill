// Headless checks of the PowerKick engine, presets and WAV export.
// Run: powerkick_tests [--report]   (--report prints per-preset levels)

#include <algorithm>
#include <array>
#include "DSP/KickEngine.h"
#include "KickRenderer.h"
#include "Presets.h"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <set>

using namespace pk;

namespace
{
int failures = 0, checks = 0;

void expect (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::cout << "FAIL: " << what << "\n";
    }
}

struct Stats { double peak = 0, rms = 0; bool finite = true; };

Stats stats (const juce::AudioBuffer<float>& b, int start = 0, int len = -1)
{
    Stats s;
    if (len < 0) len = b.getNumSamples() - start;
    double sum = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = start; i < start + len; ++i)
        {
            const double x = b.getSample (c, i);
            if (! std::isfinite (x)) s.finite = false;
            s.peak = std::max (s.peak, std::abs (x));
            sum += x * x;
        }
    s.rms = std::sqrt (sum / std::max (1, len * b.getNumChannels()));
    return s;
}

double rmsOf (const std::vector<float>& v)
{
    double sum = 0;
    for (float x : v) sum += (double) x * x;
    return std::sqrt (sum / (double) std::max<size_t> (1, v.size()));
}

double rms (const juce::AudioBuffer<float>& b, int from, int to)
{
    double s = 0;
    for (int i = from; i < std::min (to, b.getNumSamples()); ++i) s += (double) b.getSample (0, i) * b.getSample (0, i);
    return std::sqrt (s / std::max (1, to - from));
}

double db (double g) { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }

// Energy of x in [f1, f2] via a 4th-order band filter (two LR4 splits).
// 48 dB/oct edges: for "is there sub in the side signal", where wide highs would leak
// through a 24 dB/oct measuring filter.
double bandRmsSteep (const float* x, int n, double sr, double f1, double f2)
{
    dsp::LR4 lo1, lo2, hi1, hi2;
    lo1.set (f1, sr); lo2.set (f1, sr); hi1.set (f2, sr); hi2.set (f2, sr);
    double sum = 0;
    for (int i = 0; i < n; ++i)
    {
        double a, b, c, d, e, g, h, k;
        lo1.split (x[i], a, b); lo2.split (b, c, d); hi1.split (d, e, g); hi2.split (e, h, k);
        sum += h * h;
    }
    return std::sqrt (sum / std::max (1, n));
}

// Energy in [f1, f2] from a Hann-windowed DFT (Goertzel per bin): no leakage from strong
// content outside the band, unlike a filter. RMS-equivalent amplitude.
double bandRmsDft (const float* x, int n, double sr, double f1, double f2)
{
    std::vector<double> w ((size_t) n);
    double wsum = 0;
    for (int i = 0; i < n; ++i) { w[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * i / (n - 1)); wsum += w[(size_t) i] * w[(size_t) i]; }
    double e = 0;
    for (int k = (int) std::ceil (f1 * n / sr); k <= (int) (f2 * n / sr); ++k)
    {
        const double c = 2.0 * std::cos (2.0 * 3.14159265358979 * k / n);
        double s1 = 0, s2 = 0;
        for (int i = 0; i < n; ++i) { const double s0 = x[i] * w[(size_t) i] + c * s1 - s2; s2 = s1; s1 = s0; }
        e += s1 * s1 + s2 * s2 - c * s1 * s2;
    }
    return std::sqrt (2.0 * e / (wsum * n));
}

double bandRms (const float* x, int n, double sr, double f1, double f2)
{
    dsp::LR4 lo, hi;
    lo.set (f1, sr);
    hi.set (f2, sr);
    double sum = 0;
    for (int i = 0; i < n; ++i)
    {
        double a, b, c, d;
        lo.split (x[i], a, b);
        hi.split (b, c, d);
        sum += c * c;
    }
    return std::sqrt (sum / std::max (1, n));
}

KickParams preset (const char* name)
{
    for (const auto& p : factoryPresets())
        if (p.name == name)
            return paramsFromSnapshot (p.values);
    expect (false, juce::String ("missing preset ") + name);
    return {};
}

// Streams one hit (or, with step > 0, a roll: a full-velocity hit every `step` samples) through the
// engine with a chosen host buffer size.
juce::AudioBuffer<float> stream (const KickParams& p, double sr, int hostBlock, int seconds = 1, int step = 0, double bpm = 140.0,
                                 float velocity = 1.0f, bool alternate = false)
{
    dsp::KickEngine e;
    e.prepare (sr, hostBlock);
    const int total = (int) (sr * seconds);
    juce::AudioBuffer<float> out (2, total + hostBlock);
    int hit = 0;
    for (int pos = 0; pos < total; pos += hostBlock)
    {
        juce::MidiBuffer m;
        for (int t = pos; t < pos + hostBlock; ++t)
            if (t == 0 || (step > 0 && t % step == 0))
                // alternate: hits 1.0 / velocity / 1.0 / ... (accented rolls); otherwise every hit at velocity
                m.addEvent (juce::MidiMessage::noteOn (1, 60, alternate && (hit++ % 2) == 0 ? 1.0f : velocity), t - pos);
        e.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), hostBlock, m, 0, p, bpm, false);
    }
    out.setSize (2, total, true);
    return out;
}

void testPresetLibrary()
{
    const auto& f = factoryPresets();
    expect (f.size() == 300, "300 factory presets, got " + juce::String ((int) f.size()));
    std::map<juce::String, int> count;
    for (const auto& p : f) ++count[p.category];
    expect (count["Techno"] == 30 && count["Hard Techno"] == 110 && count["Schranz"] == 100 && count["Industrial"] == 56
                && count["Rave / Hard Dance"] == 4,
            "preset category split 30/110/100/56/4");
    std::set<juce::String> names;
    for (const auto& p : f) names.insert (p.name);
    expect (names.size() == f.size(), "factory preset names are unique");
    int grouped = 0;
    for (const auto& p : f) grouped += p.group.isNotEmpty() ? 1 : 0;
    expect (grouped == (int) f.size(), "every factory preset has a browser group");
    int wide = 0;
    for (const auto& p : f) wide += p.values.at (id::width) >= 50.0f ? 1 : 0;
    expect (wide >= 5, "several presets demonstrate the stereo engine");
}

void testPresetsRender (bool report)
{
    for (const auto& pr : factoryPresets())
    {
        const auto p = paramsFromSnapshot (pr.values);
        const auto audio = renderKick (p, 48000.0, 140.0, true);
        const auto s = stats (audio);
        expect (s.finite, pr.name + ": finite output");
        expect (s.peak <= 1.0, pr.name + ": peak <= 0 dBFS (" + juce::String (db (s.peak), 2) + ")");
        expect (s.peak > 0.25, pr.name + ": audible (" + juce::String (db (s.peak), 1) + " dB)");
        // Sub must be mono: side signal below 100 Hz at least 50 dB under the mid.
        const int n = audio.getNumSamples();
        std::vector<float> mid ((size_t) n), side ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            mid[(size_t) i] = 0.5f * (audio.getSample (0, i) + audio.getSample (1, i));
            side[(size_t) i] = 0.5f * (audio.getSample (0, i) - audio.getSample (1, i));
        }
        const double subMid = bandRms (mid.data(), n, 48000.0, 20.0, 100.0);
        const double subSide = bandRmsDft (side.data(), std::min (n, 48000), 48000.0, 20.0, 100.0)
                               * std::sqrt ((double) std::min (n, 48000) / n);
        expect (db (subSide) - db (subMid) < -50.0, pr.name + ": sub stays mono (" + juce::String (db (subSide) - db (subMid), 1) + " dB)");
        if (report)
        {
            // Level of the first 150 ms: the hit itself, which is what A/B listening compares.
            const auto hit = stats (audio, 0, std::min (n, 7200));
            std::cout << juce::String (pr.name).paddedRight (' ', 18) << " peak " << juce::String (db (s.peak), 1)
                      << "  hitRMS " << juce::String (db (hit.rms), 1) << "  len " << juce::String (n / 48.0, 0)
                      << " ms  side/mid " << juce::String (db (rmsOf (side)) - db (rmsOf (mid)), 1)
                      << " dB\n";
        }
    }
}

// Peak a preset would reach with hits every `step` samples (velocities alternating 1.0 / 0.8), measured
// 24 dB under its OUTPUT so the emergency ceiling cannot hide it (OUTPUT is linear after the drive).
double retriggerPeak (KickParams p, int step, double bpm, bool fullVelocity = false)
{
    p.outputDb -= 24.0f;
    dsp::KickEngine e;
    e.prepare (48000.0, 512);
    const int total = 96256; // a whole number of 512-sample blocks
    juce::AudioBuffer<float> out (2, total);
    int hit = 0;
    for (int pos = 0; pos < total; pos += 512)
    {
        juce::MidiBuffer m;
        for (int t = pos; t < pos + 512; ++t)
            if (t % step == 0)
                m.addEvent (juce::MidiMessage::noteOn (1, 60, (fullVelocity || (hit++ % 2) == 0) ? 1.0f : 0.8f), t - pos);
        e.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), 512, m, 0, p, bpm, false);
    }
    return std::max (out.getMagnitude (0, 0, total), out.getMagnitude (1, 0, total)) * std::pow (10.0, 24.0 / 20.0);
}

void testRetriggerPeaks()
{
    // Rolls on 16ths and 32nds at 150 BPM: overlapping hits must not reach the emergency ceiling
    // (0.9), which would otherwise shape the sound.
    int bad = 0;
    double worst = 0.0;
    juce::String worstName;
    for (const auto& pr : factoryPresets())
    {
        const auto p = paramsFromSnapshot (pr.values);
        for (int step : { 4800, 2400 })
        {
            const double pk = std::max (retriggerPeak (p, step, 150.0), retriggerPeak (p, step, 150.0, true));
            if (pk > worst) { worst = pk; worstName = pr.name; }
            if (pk > 0.9)
            {
                ++bad;
                std::cout << "  retrigger " << (step == 4800 ? "1/16" : "1/32") << " " << pr.name << ": " << juce::String (db (pk), 2) << " dBFS\n";
            }
        }
    }
    expect (bad == 0, "rolls on 16ths/32nds stay under the emergency ceiling (" + juce::String (bad) + " cases over, worst "
                      + worstName + " " + juce::String (db (worst), 2) + " dBFS)");
}

void testWholeChainHandoff()
{
    // Probe before the emergency ceiling: clipping must not hide retained filter energy.
    for (const char* name : { "Warm Click F#1 III", "Classic Punch" })
    {
        auto p = preset (name); p.outputDb -= 24.0f;
        const int step = 1714, onset = step + 47; // 1 ms hand-off at 48 kHz
        const auto single = stream (p, 48000, 127, 1, 0, 210.0);
        const auto baseline = stream (p, 48000, 127, 1, step, 210.0);
        expect (stats (baseline).peak * std::pow (10.0, 24.0 / 20.0) <= 0.9,
                juce::String (name) + ": 210 BPM roll does not reach the emergency ceiling");
        double diff = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 480; ++i)
                diff = std::max (diff, std::abs ((double) baseline.getSample (c, onset + i) - single.getSample (c, i)));
        expect (diff < 1.0e-5, juce::String (name) + ": a repeated hit starts with a fresh complete signal path (difference "
                + juce::String (diff, 8) + ")");
        for (int block : { 32, 512, 1024 })
        {
            const auto other = stream (p, 48000, block, 1, step, 210.0);
            double blockDiff = 0.0;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < baseline.getNumSamples(); ++i)
                    blockDiff = std::max (blockDiff, std::abs ((double) baseline.getSample (c, i) - other.getSample (c, i)));
            expect (blockDiff < 1.0e-5, juce::String (name) + ": hand-off is independent of host block " + juce::String (block));
        }
    }
    // A smooth, click-free source isolates a discontinuity from intentional noisy attacks.
    auto p = preset ("Hard Techno Core");
    p.drive = 0; p.character = 0; p.bodyTone = 1; p.rumble = 0; p.click = 0; p.width = 0;
    p.punch = 0; p.pitchDepth = 0; p.pitchFall = 0; p.outputDb = -12;
    const auto single = stream (p, 48000, 127, 1);
    const auto roll = stream (p, 48000, 127, 1, 1714);
    double jump = 0.0;
    for (int i = 1714; i < 1714 + 96; ++i)
        jump = std::max (jump, std::abs ((double) roll.getSample (0, i) - roll.getSample (0, i - 1)));
    // Two 1 ms fades plus the sine slope bound the expected sample-to-sample change.
    const double limit = stats (single).peak * (3.0 / 48.0 + 2.0 * dsp::pi * baseFrequency (p.tune) / 48000.0);
    expect (jump <= limit, "complete hand-off has no hard cut in the clean body (jump " + juce::String (jump, 6) + ")");
}

void testWidthZeroIsTrueMono()
{
    auto p = preset ("Steel Press");
    p.width = 0.0f;
    p.delayMs = 15.0f; // Delay alone must not create stereo
    const auto a = stream (p, 48000.0, 256);
    double maxDiff = 0;
    for (int i = 0; i < a.getNumSamples(); ++i)
        maxDiff = std::max (maxDiff, (double) std::abs (a.getSample (0, i) - a.getSample (1, i)));
    expect (maxDiff == 0.0, "WIDTH 0 gives bit-identical L/R (diff " + juce::String (maxDiff) + ")");
}

void testMonoSumKeepsKick()
{
    // Mono sum of the widest setting vs. width 0: sub and body unchanged, highs within 2.5 dB.
    for (float delay : { 0.0f, 1.0f, 5.0f, 15.0f })
    {
        auto p = preset ("Wide Iron Sub");
        p.width = 0.0f;
        const auto ref = stream (p, 48000.0, 512);
        p.width = 100.0f;
        p.delayMs = delay;
        const auto wide = stream (p, 48000.0, 512);
        const int n = ref.getNumSamples();
        std::vector<float> mRef ((size_t) n), mWide ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            mRef[(size_t) i] = 0.5f * (ref.getSample (0, i) + ref.getSample (1, i));
            mWide[(size_t) i] = 0.5f * (wide.getSample (0, i) + wide.getSample (1, i));
        }
        const double bands[][2] = { { 30, 120 }, { 120, 1000 }, { 1000, 3000 }, { 3000, 8000 }, { 8000, 16000 } };
        for (auto& b : bands)
        {
            const double d = db (bandRms (mWide.data(), n, 48000, b[0], b[1])) - db (bandRms (mRef.data(), n, 48000, b[0], b[1]));
            const double limit = b[0] < 1000 ? 0.3 : 2.5;
            expect (std::abs (d) <= limit, "mono sum " + juce::String (b[0]) + "-" + juce::String (b[1]) + " Hz at delay "
                                               + juce::String (delay) + " ms changes " + juce::String (d, 2) + " dB");
        }
        // Per-channel loudness must not grow when widening (no loudness trick).
        const double chRef = stats (ref).rms, chWide = stats (wide).rms;
        expect (db (chWide) - db (chRef) < 0.5, "widening is not a loudness boost (" + juce::String (db (chWide) - db (chRef), 2) + " dB)");
        // And the stereo engine must actually do something.
        double side = 0;
        for (int i = 0; i < n; ++i) side += std::pow (wide.getSample (0, i) - wide.getSample (1, i), 2.0f);
        expect (side > 0.0, "WIDTH 100 produces a side signal");
    }
}

void testSampleRatesAndBlocks()
{
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (int block : { 16, 64, 173, 512, 2048 })
        {
            const auto a = stream (preset ("Schranz Metal"), sr, block);
            const auto s = stats (a);
            expect (s.finite && s.peak <= 1.0 && s.peak > 0.25,
                    "sr " + juce::String (sr) + " block " + juce::String (block) + " peak " + juce::String (db (s.peak), 2));
        }
    // Same sound regardless of host buffer size.
    const auto a = stream (preset ("Hard Techno Core"), 48000.0, 64), b = stream (preset ("Hard Techno Core"), 48000.0, 512);
    double diff = 0;
    for (int i = 0; i < a.getNumSamples(); ++i) diff = std::max (diff, (double) std::abs (a.getSample (0, i) - b.getSample (0, i)));
    expect (diff < 0.02, "buffer-size independent output (max diff " + juce::String (diff, 5) + ")");
}

// Largest sample-to-sample jump relative to the signal's own largest jump.
double maxStep (const juce::AudioBuffer<float>& a, int ch, int from, int to)
{
    double m = 0;
    for (int i = std::max (1, from); i < to; ++i) m = std::max (m, (double) std::abs (a.getSample (ch, i) - a.getSample (ch, i - 1)));
    return m;
}

void testNoClicksOnParameterJumps()
{
    // Long low kick (smooth waveform); jump stereo, output, drive and character mid-tail.
    auto p = preset ("Deep Drop");
    p.click = 0.0f;
    const double sr = 48000.0;
    dsp::KickEngine e;
    e.prepare (sr, 256);
    juce::AudioBuffer<float> out (2, (int) sr + 256); // whole blocks must fit
    juce::MidiBuffer on, none;
    on.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    for (int pos = 0; pos < (int) sr; pos += 256)
    {
        if (pos == 256 * 60) // ~320 ms, inside the tail
        {
            p.width = 100.0f; p.delayMs = 12.0f; p.outputDb = 6.0f; p.drive = 0.9f; p.character = 1.0f;
            p.charMode = CharMode::raw;
        }
        e.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), 256, pos == 0 ? on : none, 0, p, 140.0, false);
    }
    const double before = maxStep (out, 0, 256 * 40, 256 * 60);
    const double at = maxStep (out, 0, 256 * 60, 256 * 62);
    // Drive legitimately sharpens the waveform; compare with the settled new sound too.
    const double after = maxStep (out, 0, 256 * 64, 256 * 80);
    expect (at < std::max (before, after) * 1.5 + 0.002, "no click on parameter jump (step " + juce::String (at, 4) + " vs " + juce::String (std::max (before, after), 4) + ")");

    // Retrigger in the middle of a loud hit: no discontinuity beyond normal waveform steps.
    auto q = preset ("Deep Drop");
    q.click = 0.0f;
    dsp::KickEngine e2;
    e2.prepare (sr, 256);
    juce::AudioBuffer<float> o2 (2, (int) sr / 2 + 256);
    for (int pos = 0; pos < (int) sr / 2; pos += 256)
    {
        juce::MidiBuffer m;
        if (pos == 0 || pos == 256 * 20) m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 37);
        e2.process (o2.getWritePointer (0, pos), o2.getWritePointer (1, pos), 256, m, 0, q, 140.0, false);
    }
    const double normal = maxStep (o2, 0, 0, 256 * 20); // includes the first attack
    const double retrig = maxStep (o2, 0, 256 * 20, 256 * 21);
    expect (retrig < normal * 1.6, "retrigger is click-free (" + juce::String (retrig, 4) + " vs " + juce::String (normal, 4) + ")");
}

void testCharacterAudible()
{
    // Each mode at full CHARACTER must move the spectrum clearly (> 2 dB in some band) vs. CHARACTER 0.
    auto base = preset ("Classic Punch");
    base.character = 0.0f;
    const auto ref = renderKick (base, 48000.0, 140.0, false);
    const double bands[][2] = { { 30, 90 }, { 90, 250 }, { 250, 800 }, { 800, 2500 }, { 2500, 8000 } };
    for (int m = 0; m < 5; ++m)
    {
        auto p = base;
        p.charMode = (CharMode) m;
        p.character = 1.0f;
        const auto a = renderKick (p, 48000.0, 140.0, false);
        const int n = std::min (a.getNumSamples(), ref.getNumSamples());
        double maxDelta = 0;
        for (auto& b : bands)
            maxDelta = std::max (maxDelta, std::abs (db (bandRms (a.getReadPointer (0), n, 48000, b[0], b[1]))
                                                     - db (bandRms (ref.getReadPointer (0), n, 48000, b[0], b[1]))));
        expect (maxDelta > 2.0, "character mode " + juce::String (m) + " audible (" + juce::String (maxDelta, 1) + " dB)");
    }
}

void testSettledPitch()
{
    // After the sweep the body must sit on TUNE at any sample rate (pitchFall off: pure settle check).
    for (double sr : { 22050.0, 24000.0, 44100.0, 96000.0 })
    {
        auto p = preset ("Classic Punch");
        p.pitchFall = 0.0f;
        p.rumble = 0.0f; // the rumble tail has its own frequencies: measure the body alone
        const double expected = baseFrequency (p.tune);
        const auto a = renderKick (p, sr, 140.0, false);
        const float* x = a.getReadPointer (0);
        int first = -1, last = -1, count = 0;
        for (int i = (int) (0.15 * sr); i < (int) (0.25 * sr) && i < a.getNumSamples(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                if (first < 0) first = i;
                last = i;
                ++count;
            }
        const double f = count > 1 ? (count - 1) * sr / (last - first) : 0.0;
        expect (std::abs (f - expected) < 1.0, "settled pitch (" + juce::String (expected, 2) + " Hz) at " + juce::String (sr) + " Hz is " + juce::String (f, 2) + " Hz");
    }
}

// Level (dB) of a 40 ms RMS window of channel 0, centred on t (>= 2 periods of a ~50 Hz body).
double frameDb (const juce::AudioBuffer<float>& a, double t, double sr)
{
    const int from = std::max (0, (int) ((t - 0.02) * sr)), to = std::min (a.getNumSamples(), (int) ((t + 0.02) * sr));
    double s = 0;
    for (int i = from; i < to; ++i) s += (double) a.getSample (0, i) * a.getSample (0, i);
    return db (std::sqrt (s / std::max (1, to - from)));
}

void testGatedBody()
{
    // Measured hard-techno kicks hold their body (tail only 1-2 dB under the hit) and then stop.
    auto p = preset ("Hard Techno Core");
    p.body = 1.0f; p.rumble = 0.0f; p.drive = 0.0f; p.click = 0.0f; p.character = 0.0f; // no clipping anywhere
    p.outputDb = -18.0f; // stay clear of the output safety limiter: measure the envelope itself
    const auto a = stream (p, 48000.0, 256);
    const double D = decayMs (p.decay) * 0.001;
    const double early = frameDb (a, 0.2 * D, 48000), late = frameDb (a, 0.75 * D, 48000);
    expect (early - late <= 2.5, "BODY 100% holds the body (drop " + juce::String (early - late, 1) + " dB at 75% of decay)");
    // The end must not click: the last 10 ms may not step harder than the body does.
    const int endI = (int) (D * 48000);
    expect (maxStep (a, 0, endI - 480, endI + 480) <= maxStep (a, 0, (int) (0.3 * D * 48000), (int) (0.5 * D * 48000)) * 1.2,
            "gated end is click-free");
}

void testBodyTone()
{
    auto p = preset ("Classic Schranz");
    // The original driven-schranz scenario: hard drive with clip and HARD character.
    p.rumble = 0.0f; p.drive = 0.8f; p.driveMode = DriveMode::clip; p.charMode = CharMode::hard; p.character = 0.7f; p.click = 0.55f;
    p.bodyTone = 1.0f;
    const auto open = renderKick (p, 48000.0, 140.0, false);
    p.bodyTone = 0.0f;
    const auto dark = renderKick (p, 48000.0, 140.0, false);
    auto seg = [] (const juce::AudioBuffer<float>& a, double t0, double t1, double f1, double f2)
    { return db (bandRms (a.getReadPointer (0, (int) (t0 * 48000)), (int) ((t1 - t0) * 48000), 48000, f1, f2)); };
    const double midDrop = seg (open, 0.045, 0.14, 700, 3000) - seg (dark, 0.045, 0.14, 700, 3000);
    const double lowDiff = std::abs (seg (open, 0.045, 0.14, 30, 120) - seg (dark, 0.045, 0.14, 30, 120));
    const double attackDiff = std::abs (seg (open, 0.0, 0.006, 700, 8000) - seg (dark, 0.0, 0.006, 700, 8000));
    // Measured kicks scoop the body mids but keep the distortion "fizz" on top.
    const double highDiff = std::abs (seg (open, 0.045, 0.14, 4000, 12000) - seg (dark, 0.045, 0.14, 4000, 12000));
    expect (highDiff <= 3.0, "bodyTone keeps the body's top end (" + juce::String (highDiff, 1) + " dB)");
    expect (midDrop >= 8.0, "bodyTone 0 scoops the body mids (" + juce::String (midDrop, 1) + " dB)");
    expect (lowDiff <= 1.5, "bodyTone keeps the low body (" + juce::String (lowDiff, 2) + " dB)");
    expect (attackDiff <= 1.5, "bodyTone keeps the attack (" + juce::String (attackDiff, 2) + " dB)");
}

double pitchAt (const juce::AudioBuffer<float>& a, double t, double sr)
{
    // Zero-crossing frequency over a 60 ms window centred on t.
    int first = -1, last = -1, count = 0;
    for (int i = std::max (1, (int) ((t - 0.03) * sr)); i < std::min (a.getNumSamples(), (int) ((t + 0.03) * sr)); ++i)
        if (a.getSample (0, i - 1) < 0.0f && a.getSample (0, i) >= 0.0f) { if (first < 0) first = i; last = i; ++count; }
    return count > 1 ? (count - 1) * sr / (last - first) : 0.0;
}

void testPitchFall()
{
    // Measured kicks keep falling in pitch through the tail (2-4 semitones from 100 to 250 ms).
    auto p = preset ("Deep Drop");
    p.click = 0.0f; p.rumble = 0.0f; p.drive = 0.0f; p.character = 0.0f; p.decay = 0.45f; p.body = 0.9f; p.outputDb = -12.0f;
    double st[2];
    for (int k = 0; k < 2; ++k)
    {
        p.pitchFall = k == 0 ? 0.0f : 12.0f;
        const auto a = renderKick (p, 48000.0, 140.0, false);
        st[k] = 12.0 * std::log2 (pitchAt (a, 0.10, 48000) / pitchAt (a, 0.25, 48000));
    }
    expect (st[1] - st[0] >= 2.0, "pitchFall 12 adds a 2+ st fall from 100 to 250 ms (" + juce::String (st[1] - st[0], 2) + " st)");
}

// Median fundamental (Hz) in 30-60 / 60-120 / 120-200 ms after the onset: rising zero crossings of
// the 15-500 Hz part (brick-wall FFT filter), as Tools/kick_features.py measures the references.
std::array<double, 3> kickPitchWindows (const juce::AudioBuffer<float>& a, double sr)
{
    const int n = a.getNumSamples();
    std::vector<float> mono ((size_t) n);
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        mono[(size_t) i] = 0.5f * (a.getSample (0, i) + a.getSample (a.getNumChannels() - 1, i));
        peak = std::max (peak, std::abs (mono[(size_t) i]));
    }
    int on = 0;
    while (on < n && std::abs (mono[(size_t) on]) <= 0.1f * peak) ++on;
    on = std::max (0, on - (int) (0.001 * sr));
    const int len = std::min (n - on, (int) (0.35 * sr));
    constexpr int order = 15, size = 1 << order; // 0.35 s at 48 kHz fits
    std::vector<float> buf (2 * size, 0.0f);
    for (int i = 0; i < len; ++i) buf[(size_t) i] = mono[(size_t) (on + i)];
    juce::dsp::FFT fft (order);
    fft.performRealOnlyForwardTransform (buf.data());
    for (int k = 0; k <= size / 2; ++k)
    {
        const double f = k * sr / size;
        if (f > 500.0 || f < 15.0) { buf[(size_t) (2 * k)] = 0.0f; buf[(size_t) (2 * k + 1)] = 0.0f; }
    }
    fft.performRealOnlyInverseTransform (buf.data());
    // Hysteresis: a rising crossing counts only after the signal really went negative (a quarter of
    // its current amplitude), so a clipped wave lingering around zero cannot add half-periods.
    const float follow = (float) std::exp (-1.0 / (0.02 * sr));
    float env = 0.0f, lowPeak = 0.0f;
    for (int i = 0; i < len; ++i) lowPeak = std::max (lowPeak, std::abs (buf[(size_t) i]));
    const float audible = 0.03f * lowPeak; // a period below -30 dB of the hit carries no audible pitch
    float periodMax = 0.0f;
    bool armed = false;
    std::vector<double> mids, freqs; // instantaneous frequency at each period's midpoint
    double prev = -1.0;
    for (int i = 0; i + 1 < len; ++i)
    {
        env = std::max (std::abs (buf[(size_t) i]), env * follow);
        periodMax = std::max (periodMax, std::abs (buf[(size_t) i]));
        if (buf[(size_t) i] < -0.25f * env) armed = true;
        if (armed && buf[(size_t) i] < 0.0f && buf[(size_t) i + 1] >= 0.0f)
        {
            armed = false;
            const double t = (i + buf[(size_t) i] / (buf[(size_t) i] - buf[(size_t) i + 1] + 1.0e-20f)) / sr;
            if (prev >= 0.0 && periodMax > audible && 1.0 / (t - prev) > 20.0 && 1.0 / (t - prev) < 1500.0)
            {
                mids.push_back (0.5 * (t + prev));
                freqs.push_back (1.0 / (t - prev));
            }
            prev = t;
            periodMax = 0.0f;
        }
    }
    // Sampled on a 4 ms grid (interpolated, undefined outside the measured span), median per window.
    const double edges[4] = { 0.03, 0.06, 0.12, 0.20 };
    std::array<double, 3> out {};
    for (int w = 0; w < 3; ++w)
    {
        std::vector<double> f;
        for (int k = 1; k < 50; ++k)
        {
            const double g = 0.004 * k;
            if (g < edges[w] || g >= edges[w + 1] || mids.size() < 2 || g < mids.front() || g > mids.back()) continue;
            const auto hi = (size_t) (std::upper_bound (mids.begin(), mids.end(), g) - mids.begin());
            const size_t i1 = std::min (hi, mids.size() - 1), i0 = i1 - 1;
            const double u = (g - mids[i0]) / std::max (1.0e-12, mids[i1] - mids[i0]);
            f.push_back (freqs[i0] + u * (freqs[i1] - freqs[i0]));
        }
        if (f.empty()) continue;
        std::sort (f.begin(), f.end());
        out[(size_t) w] = f.size() % 2 ? f[f.size() / 2] : 0.5 * (f[f.size() / 2 - 1] + f[f.size() / 2]);
    }
    return out;
}

void testPresetsStayInKickRegister()
{
    // A kick that stays high or glides down slowly from far above reads as a pitched lead.
    // Limits: the reference kicks' p90-p97 (Tools/pitch_guard.py fits to 170/100/80 Hz) + margin.
    const double cap[3] = { 180.0, 105.0, 85.0 };
    const char* window[3] = { "30-60 ms", "60-120 ms", "120-200 ms" };
    int bad = 0;
    for (const auto& pr : factoryPresets())
    {
        auto a = stream (paramsFromSnapshot (pr.values), 48000.0, 512, 1); // as Tools/fit_kicks.py renders
        a.setSize (2, (int) (0.6 * 48000.0), true);
        const auto f = kickPitchWindows (a, 48000.0);
        for (int w = 0; w < 3; ++w)
            if (f[(size_t) w] > cap[w])
            {
                ++bad;
                expect (false, pr.name + ": " + window[w] + " pitch " + juce::String (f[(size_t) w], 0) + " Hz is above the kick register ("
                               + juce::String (cap[w], 0) + " Hz)");
            }
    }
    expect (bad == 0, "every factory preset stays in the kick register");
}

void testDriveSweepIsSmooth()
{
    // Same class of bug Codex found in PowerBass: a few-point peak estimate of a non-monotonic
    // shaper can land on its zeros and make the level jump. Level must follow DRIVE smoothly.
    auto base = preset ("Classic Punch");
    base.rumble = 0.0f; base.outputDb = -24.0f; base.character = 0.0f;
    for (int m = 0; m < 3; ++m)
    {
        double prev = 0, worst = 0, at = 0;
        for (int i = 0; i <= 100; ++i)
        {
            auto p = base; p.driveMode = (DriveMode) m; p.drive = 0.01f * (float) i;
            const auto a = renderKick (p, 48000.0, 140.0, false);
            const double level = db (stats (a, 0, std::min (a.getNumSamples(), 7200)).rms);
            if (i > 0 && std::abs (level - prev) > worst) { worst = std::abs (level - prev); at = p.drive; }
            prev = level;
        }
        expect (worst <= 2.0, "drive mode " + juce::String (m) + ": level follows DRIVE smoothly (max step "
                                  + juce::String (worst, 2) + " dB at " + juce::String (at, 2) + ")");
    }
}

double sideToMidDb (const juce::AudioBuffer<float>& a, double f1, double f2)
{
    std::vector<float> m ((size_t) a.getNumSamples()), sd ((size_t) a.getNumSamples());
    for (int i = 0; i < a.getNumSamples(); ++i)
    {
        m[(size_t) i] = 0.5f * (a.getSample (0, i) + a.getSample (1, i));
        sd[(size_t) i] = 0.5f * (a.getSample (0, i) - a.getSample (1, i));
    }
    return db (bandRms (sd.data(), (int) sd.size(), 48000, f1, f2)) - db (bandRms (m.data(), (int) m.size(), 48000, f1, f2));
}

void testStereoControlsAreAudible()
{
    // Roman: "WIDTH and DELAY seem to do nothing". WIDTH must give a clearly audible image on a
    // normal kick, DELAY must add a real time offset; the mono sum stays untouched (other tests).
    auto p = preset ("Hard Techno Core");
    p.rumble = 0.0f;
    p.width = 100.0f; p.delayMs = 0.0f; p.splitHz = 500.0f; // the WIDTH knob itself, at the default SPLIT
    const auto wide = renderKick (p, 48000.0, 140.0, true);
    // WIDTH acts above SPLIT (the sub always stays mono), so that is where it must be clearly audible.
    const double above = sideToMidDb (wide, 500, 16000);
    p.width = 0.0f;
    const double narrow = sideToMidDb (renderKick (p, 48000.0, 140.0, true), 500, 16000);
    p.width = 100.0f;
    std::printf ("  WIDTH 100%%: side/mid above SPLIT %.1f dB (WIDTH 0: %.1f dB)\n", above, narrow);
    expect (above >= -10.0, "WIDTH 100% is clearly audible above SPLIT (side " + juce::String (above, 1) + " dB re mid)");
    p.delayMs = 5.0f;
    const auto delayed = renderKick (p, 48000.0, 140.0, true);
    const double gain = sideToMidDb (delayed, 500, 16000) - sideToMidDb (wide, 500, 16000);
    expect (gain >= 3.0, "DELAY 5 ms audibly adds width to the highs (+" + juce::String (gain, 1) + " dB side)");
}

void testStereoSplit()
{
    // SPLIT sets where widening starts (Roman's request): lower SPLIT, wider low-mids.
    auto p = preset ("Hard Techno Core");
    p.rumble = 0.0f; p.width = 100.0f; p.delayMs = 3.0f;
    p.splitHz = 150.0f; // the lowest SPLIT
    const auto lowest = renderKick (p, 48000.0, 140.0, true);
    p.splitHz = 300.0f;
    const auto low = renderKick (p, 48000.0, 140.0, true);
    p.splitHz = 1500.0f;
    const auto high = renderKick (p, 48000.0, 140.0, true);
    const double diff = sideToMidDb (low, 400, 800) - sideToMidDb (high, 400, 800);
    expect (diff >= 10.0, "SPLIT 300 Hz widens 400-800 Hz far more than SPLIT 1.5 kHz (" + juce::String (diff, 1) + " dB)");
    // Side below 90 Hz (48 dB/oct measurement) relative to the whole mono kick.
    std::vector<float> m ((size_t) lowest.getNumSamples()), sd ((size_t) lowest.getNumSamples());
    for (int i = 0; i < lowest.getNumSamples(); ++i)
    {
        m[(size_t) i] = 0.5f * (lowest.getSample (0, i) + lowest.getSample (1, i));
        sd[(size_t) i] = 0.5f * (lowest.getSample (0, i) - lowest.getSample (1, i));
    }
    const double sub = db (bandRmsSteep (sd.data(), (int) sd.size(), 48000, 20, 90)) - db (bandRms (m.data(), (int) m.size(), 48000, 20, 16000));
    expect (sub < -50.0, "sub stays mono with a low SPLIT (" + juce::String (sub, 1) + " dB)");
}

// ---- ADVANCED features: each is neutral by default, so the 300 presets keep their sound.
// Fingerprints taken 2026-10-22 after the chain without low-end delay and the full refit.
struct Fingerprint { const char* name; int length; double energy, s0, s1; };
const Fingerprint fingerprints[] = {
{ "Hard Techno Core", 23369, 4651.3739736222133, 0.593998611, -0.399458647 },
{ "Classic Punch", 35246, 4333.6229538089819, 0.835408568, 0.399561167 },
{ "Classic Schranz", 21448, 4079.3667284940884, -0.14877364, 0.227083951 },
{ "Industrial Rumble", 34386, 5430.3677036759982, -0.505761802, 0.462805092 },
{ "Gabba Lite", 25159, 3272.8972305468687, -0.303709507, 0.353954315 },
};

void testExistingSoundUnchanged()
{
    for (const auto& f : fingerprints)
    {
        const auto a = renderKick (preset (f.name), 48000.0, 140.0, true);
        double e = 0;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < a.getNumSamples(); ++i) e += (double) a.getSample (c, i) * a.getSample (c, i);
        // Tolerances cover math-library differences between platforms (Windows CRT vs Apple libm:
        // energy ~1e-7 relative, samples ~1e-5, i.e. around -100 dB); any real change is far larger.
        const bool same = a.getNumSamples() == f.length && std::abs (e - f.energy) <= 1.0e-6 * f.energy
                          && std::abs (a.getSample (0, 2000) - f.s0) < 1.0e-4 && std::abs (a.getSample (1, 9000) - f.s1) < 1.0e-4;
        expect (same, juce::String (f.name) + ": sound unchanged by the new (neutral) features");
    }
}

// Band level inside [t0, t1]: the filters run from the start of the signal (warmed up), only
// the window is measured -- starting cold mid-signal would leak a step transient into the band.
double segDb (const juce::AudioBuffer<float>& a, double t0, double t1, double f1, double f2)
{
    const int i0 = (int) (t0 * 48000), i1 = std::min (a.getNumSamples(), (int) (t1 * 48000));
    dsp::LR4 lo, hi;
    lo.set (f1, 48000); hi.set (f2, 48000);
    double sum = 0;
    for (int i = 0; i < i1; ++i)
    {
        double x1, x2, y1, y2;
        lo.split (a.getSample (0, i), x1, x2);
        hi.split (x2, y1, y2);
        if (i >= i0) sum += y1 * y1;
    }
    return db (std::sqrt (sum / std::max (1, i1 - i0)));
}

void testAdvancedFeatures()
{
    auto base = preset ("Classic Schranz");
    base.rumble = 0.0f; base.outputDb = -12.0f; base.width = 0.0f;
    // Attack features are judged on a clean kick: heavy clipping would mask any attack detail.
    auto clean = base; clean.drive = 0.1f; clean.driveMode = DriveMode::saturate; clean.character = 0.0f;

    // Beat length: the kick ends within a quarter note at 150 BPM.
    { auto p = base; p.decay = 0.9f;
      const auto off = renderKick (p, 48000.0, 150.0, false);
      p.beatLength = 1;
      const auto on = renderKick (p, 48000.0, 150.0, false);
      expect (on.getNumSamples() <= (int) (0.42 * 48000) && off.getNumSamples() > (int) (0.8 * 48000),
              "BEAT LENGTH 1/4 ends the kick within a beat (" + juce::String (on.getNumSamples() / 48.0, 0) + " ms)"); }

    // Sub layer: more clean low end in the body, top unchanged.
    { auto p = base; const auto a = renderKick (p, 48000.0, 140.0, false);
      p.subLevel = 1.0f; const auto b = renderKick (p, 48000.0, 140.0, false);
      expect (segDb (b, 0.1, 0.3, 35, 80) - segDb (a, 0.1, 0.3, 35, 80) >= 3.0, "SUB LAYER adds clean low end (" + juce::String (segDb (b, 0.1, 0.3, 35, 80) - segDb (a, 0.1, 0.3, 35, 80), 2) + " dB)");
      expect (std::abs (segDb (b, 0.1, 0.3, 1000, 8000) - segDb (a, 0.1, 0.3, 1000, 8000)) < 1.0, "SUB LAYER leaves the top alone (" + juce::String (segDb (b, 0.1, 0.3, 1000, 8000) - segDb (a, 0.1, 0.3, 1000, 8000), 2) + " dB)"); }

    // New click types are audible and different from each other.
    { auto p = clean; p.click = 0.0f; const auto none = renderKick (p, 48000.0, 140.0, false);
      p.click = 0.9f; p.clickType = ClickType::noise909; const auto n9 = renderKick (p, 48000.0, 140.0, false);
      p.clickType = ClickType::fmBlip; const auto fm = renderKick (p, 48000.0, 140.0, false);
      expect (segDb (n9, 0, 0.01, 2000, 10000) - segDb (none, 0, 0.01, 2000, 10000) > 6.0, "909 NOISE click is audible (" + juce::String (segDb (n9, 0, 0.01, 2000, 10000) - segDb (none, 0, 0.01, 2000, 10000), 2) + ")");
      expect (segDb (fm, 0, 0.01, 1500, 6000) - segDb (none, 0, 0.01, 1500, 6000) > 6.0, "FM BLIP click is audible (" + juce::String (segDb (fm, 0, 0.01, 1500, 6000) - segDb (none, 0, 0.01, 1500, 6000), 2) + ")");
      expect (std::abs ((segDb (n9, 0, 0.01, 5000, 12000) - segDb (n9, 0, 0.01, 700, 2000))
                        - (segDb (fm, 0, 0.01, 5000, 12000) - segDb (fm, 0, 0.01, 700, 2000))) > 3.0, "the two new clicks differ"); }

    // Start phase changes the first cycle without changing the level.
    { auto p = clean; p.punch = 0.0f; const auto a = renderKick (p, 48000.0, 140.0, false);
      p.startPhase = 1.0f; const auto b = renderKick (p, 48000.0, 140.0, false);
      double dsum = 0, asum = 0;
      for (int i = 0; i < 96; ++i) { dsum += std::pow (a.getSample (0, i) - b.getSample (0, i), 2.0); asum += std::pow (a.getSample (0, i), 2.0); }
      expect (std::sqrt (dsum / std::max (asum, 1e-20)) > 0.3, "START PHASE changes the first 2 ms (" + juce::String (std::sqrt (dsum / std::max (asum, 1e-20)), 2) + ")");
      expect (std::abs (segDb (a, 0.02, 0.2, 20, 500) - segDb (b, 0.02, 0.2, 20, 500)) < 1.0, "START PHASE keeps the body level"); }

    // Drive attack: more harmonics in the first 20 ms only.
    { auto p = clean; p.drive = 0.3f; const auto a = renderKick (p, 48000.0, 140.0, false);
      p.driveEnv = 1.0f; const auto b = renderKick (p, 48000.0, 140.0, false);
      expect (segDb (b, 0, 0.02, 1000, 4000) - segDb (a, 0, 0.02, 1000, 4000) >= 3.0, "DRIVE ATTACK adds bite to the attack (" + juce::String (segDb (b, 0, 0.02, 1000, 4000) - segDb (a, 0, 0.02, 1000, 4000), 2) + ")");
      expect (std::abs (segDb (b, 0.15, 0.3, 1000, 4000) - segDb (a, 0.15, 0.3, 1000, 4000)) < 1.5, "DRIVE ATTACK leaves the tail"); }

    // Velocity -> drive: soft notes get cleaner than loud ones.
    { auto tilt = [] (const juce::AudioBuffer<float>& x) { return segDb (x, 0.03, 0.2, 1000, 4000) - segDb (x, 0.03, 0.2, 30, 120); };
      auto p = base; p.drive = 0.5f; p.velSens = 0.0f;
      const double plain = tilt (renderKick (p, 48000.0, 140.0, false, 1.0f)) - tilt (renderKick (p, 48000.0, 140.0, false, 0.3f));
      p.velDrive = 1.0f;
      const double linked = tilt (renderKick (p, 48000.0, 140.0, false, 1.0f)) - tilt (renderKick (p, 48000.0, 140.0, false, 0.3f));
      expect (linked - plain >= 2.0, "VELOCITY DRIVE: soft notes are cleaner (" + juce::String (linked - plain, 1) + " dB)"); }

    // Oversampling 2x/4x/8x: same sound in the musical range, all finite.
    { auto p = base; double lvl[3];
      for (int i = 0; i < 3; ++i) { p.oversampleIndex = i; const auto a = renderKick (p, 48000.0, 140.0, false);
                                    lvl[i] = segDb (a, 0, 0.2, 30, 2000); expect (stats (a).finite && stats (a).peak <= 1.0, "oversampling " + juce::String (2 << i) + "x renders"); }
      expect (std::abs (lvl[0] - lvl[1]) < 1.0 && std::abs (lvl[2] - lvl[1]) < 1.0, "oversampling keeps the sound"); }

    // Pitch curve: a rounder sweep is clearly higher at 30 ms.
    { auto p = base; p.punch = 0.0f; p.click = 0.0f; p.drive = 0.0f; p.character = 0.0f;
      const auto a = renderKick (p, 48000.0, 140.0, false);
      p.pitchCurve = 1.0f; const auto b = renderKick (p, 48000.0, 140.0, false);
      expect (12.0 * std::log2 (pitchAt (b, 0.03, 48000) / pitchAt (a, 0.03, 48000)) >= 2.0, "PITCH CURVE changes the sweep"); }

    // Key root: with key tracking, the root note plays at TUNE.
    { auto p = base; p.keyTrack = true; p.keyRoot = 48; p.punch = 0.0f; p.click = 0.0f; p.drive = 0.0f; p.character = 0.0f; p.pitchFall = 0.0f;
      dsp::KickEngine e; e.prepare (48000, 512);
      juce::AudioBuffer<float> out (2, 48000); juce::MidiBuffer on, none; on.addEvent (juce::MidiMessage::noteOn (1, 48, 1.0f), 0);
      for (int pos = 0; pos < 48000; pos += 512) e.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), std::min (512, 48000 - pos), pos == 0 ? on : none, 0, p, 140, false);
      expect (std::abs (pitchAt (out, 0.25, 48000) - baseFrequency (p.tune)) < 1.0, "KEY ROOT plays TUNE on the root note"); }

    // Mod wheel -> drive.
    { auto p = base; p.drive = 0.2f; p.modDrive = 1.0f;
      auto run = [&p] (int cc)
      { dsp::KickEngine e; e.prepare (48000, 512); juce::AudioBuffer<float> out (2, 24576); juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::controllerEvent (1, 1, cc), 0); m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 1);
        juce::MidiBuffer none;
        for (int pos = 0; pos < 24576; pos += 512) e.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), 512, pos == 0 ? m : none, 0, p, 140, false);
        return segDb (out, 0.03, 0.2, 1000, 4000) - segDb (out, 0.03, 0.2, 30, 120); };
      expect (run (127) - run (0) >= 3.0, "MOD WHEEL DRIVE adds drive"); }

    // CHOKE: a note-off cuts the kick, so the MIDI note length sets the kick length.
    { auto run = [] (bool choke)
      { auto p = preset ("Deep Drop"); p.choke = choke; p.outputDb = -12.0f; p.rumble = 0.0f;
        dsp::KickEngine e; e.prepare (48000, 64); juce::AudioBuffer<float> out (2, 19200);
        const int lat = e.getLatencySamples();
        for (int pos = 0; pos < 19200; pos += 64)
        { juce::MidiBuffer m;
          if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
          if (pos == 4800) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
          e.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), 64, m, 0, p, 140, false); }
        return rms (out, 4800 + lat + 480, 4800 + lat + 1440); }; // 10-30 ms after the note-off
      expect (run (false) > 0.05, "without CHOKE the kick ignores the note-off (" + juce::String (run (false), 4) + ")");
      // The rest is the DC blocker settling after the cut (any high-pass does that): at least 15 dB down.
      expect (run (true) < run (false) * 0.18, "CHOKE: note-off cuts the kick (" + juce::String (run (true), 5) + " vs " + juce::String (run (false), 4) + ")"); }

    // Hand-off: a hit after the previous one has ended starts on time; one on top of a sounding
    // hit starts after the 1 ms fade of the old body, never later.
    { auto onset = [] (int secondAt)
      { auto p = preset ("Hard Techno Core"); p.rumble = 0.0f; p.outputDb = -12.0f; p.decay = 0.2f;
        dsp::KickEngine e; e.prepare (48000, 64); juce::AudioBuffer<float> out (2, 48000);
        for (int pos = 0; pos < 48000; pos += 64)
        { juce::MidiBuffer m;
          if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
          if (pos == secondAt) m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
          e.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), 64, m, 0, p, 140, false); }
        // first sample of the second hit: compare with a render without it
        dsp::KickEngine f; f.prepare (48000, 64); juce::AudioBuffer<float> ref (2, 48000);
        for (int pos = 0; pos < 48000; pos += 64)
        { juce::MidiBuffer m; if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
          f.process (ref.getWritePointer (0, pos), ref.getWritePointer (1, pos), 64, m, 0, p, 140, false); }
        for (int i = secondAt; i < 48000; ++i)
          if (std::abs (out.getSample (0, i) - ref.getSample (0, i)) > 1.0e-4f) return i - secondAt;
        return -1; };
      const int ended = onset (24000), sounding = onset (2432); // block-aligned positions
      expect (ended >= 0 && ended <= 8, "a hit after the previous one ended starts on time (" + juce::String (ended) + " samples + latency)");
      expect (sounding >= 0 && sounding <= ended + 49, "a hit on a sounding one starts within 1 ms (" + juce::String (sounding) + " samples)"); }

    // Reverb rumble: smoother tail than the comb rumble, sub still mono.
    { auto ripple = [] (const juce::AudioBuffer<float>& x)
      { std::vector<double> v; for (int i = (int) (0.35 * 48000); i + 480 < std::min (x.getNumSamples(), (int) (0.8 * 48000)); i += 480) v.push_back (db (rms (x, i, i + 480)));
        double m = 0; for (double d : v) m += d; m /= (double) v.size(); double q = 0; for (double d : v) q += (d - m) * (d - m); return std::sqrt (q / (double) v.size()); };
      auto p = preset ("Industrial Rumble"); p.rumble = 0.8f; p.outputDb = -12.0f;
      const auto classic = renderKick (p, 48000.0, 140.0, true);
      p.rumbleMode = 1; const auto reverb = renderKick (p, 48000.0, 140.0, true);
      expect (ripple (reverb) < ripple (classic) - 0.5, "REVERB rumble tail is smoother (" + juce::String (ripple (reverb), 2) + " vs " + juce::String (ripple (classic), 2) + " dB)");
      expect (stats (reverb).finite && stats (reverb).peak <= 1.0, "REVERB rumble renders cleanly"); }
}

void testVelocity()
{
    auto p = preset ("Classic Punch");
    const auto loud = renderKick (p, 48000.0, 140.0, false, 1.0f);
    const auto soft = renderKick (p, 48000.0, 140.0, false, 0.4f);
    expect (stats (soft).peak < stats (loud).peak * 0.9, "velocity changes level");
}

void testLegacyPresetMigration()
{
    // A preset saved before bodyTone / pitchFall / splitFreq existed must sound as it did:
    // missing new parameters take their neutral value, not today's defaults.
    const juce::String xml = "<PowerKickPreset format=\"1\" name=\"Old\" category=\"User\">"
                             "<Param id=\"tune\" value=\"2\"/><Param id=\"drive\" value=\"0.4\"/></PowerKickPreset>";
    Preset p;
    juce::String err;
    expect (presetFromXml (xml, p, err), "old preset parses");
    expect (p.values.count (id::bodyTone) && std::abs (p.values[id::bodyTone] - 1.0f) < 1e-6f, "old preset: bodyTone neutral (1)");
    expect (p.values.count (id::pitchFall) && std::abs (p.values[id::pitchFall]) < 1e-6f, "old preset: pitchFall neutral (0)");
    expect (p.values.count (id::splitFreq) && std::abs (p.values[id::splitFreq] - 1000.0f) < 1e-3f, "old preset: SPLIT at the old fixed 1 kHz");
}

void testPresetRoundTripAndWav()
{
    Snapshot s = factoryPresets()[21].values;
    s[id::width] = 73.5f;
    s[id::delay] = 4.25f;
    const auto xml = presetToXml (juce::String::fromUTF8 ("Ünïcode Test"), "User", s);
    Preset back;
    juce::String err;
    expect (presetFromXml (xml, back, err), "preset XML parses: " + err);
    bool all = true;
    for (const auto& [k, v] : s) all = all && back.values.count (k) && std::abs (back.values.at (k) - v) < 1.0e-4f;
    expect (all, "preset keeps every parameter");
    expect (std::abs (back.values[id::width] - 73.5f) < 1e-4f && std::abs (back.values[id::delay] - 4.25f) < 1e-4f,
            "preset stores Stereo Width and Delay");
    expect (back.name == juce::String::fromUTF8 ("Ünïcode Test"), "preset name round-trips");
    Preset bad;
    expect (! presetFromXml ("<garbage/>", bad, err), "rejects a foreign file");
    expect (! presetFromXml ("not xml at all", bad, err), "rejects a corrupt file");

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("powerkick-test-" + juce::String (juce::Random::getSystemRandom().nextInt()));
    {
        PresetLibrary lib (dir);
        expect (lib.saveUser ("My Kick", s, err), "save user preset: " + err);
        int idx = lib.indexOf ("My Kick");
        expect (idx >= 26, "user preset listed after factory");
        expect (lib.rename (idx, "Renamed", err), "rename: " + err);
        idx = lib.indexOf ("Renamed");
        expect (idx >= 0 && lib.indexOf ("My Kick") < 0, "renamed preset listed");
        auto exported = dir.getChildFile ("exported.pkpreset");
        expect (lib.exportTo (exported, "Renamed", s, err), "export: " + err);
        juce::String imported;
        expect (lib.importFile (exported, imported, err), "import: " + err);
        expect (imported == "Renamed 2", "import avoids name clash (" + imported + ")");
        expect (! lib.remove (0, err), "factory presets cannot be deleted");
        expect (lib.remove (lib.indexOf ("Renamed"), err), "delete user preset: " + err);
        expect (lib.indexOf ("Renamed") < 0, "deleted preset gone");
    }
    // WAV export: 24-bit, channels as requested.
    for (bool st : { true, false })
    {
        auto audio = renderKick (paramsFromSnapshot (s), 44100.0, 140.0, st);
        auto wavFile = dir.getChildFile (st ? "st.wav" : "mono.wav");
        expect (writeWav24 (wavFile, audio, 44100.0, err), "write wav: " + err);
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::AudioFormatReader> r (fmt.createReaderFor (wavFile.createInputStream().release(), true));
        expect (r != nullptr && r->bitsPerSample == 24 && (int) r->numChannels == (st ? 2 : 1) && r->sampleRate == 44100.0,
                "WAV is 24-bit with the requested channels");
    }
    dir.deleteRecursively();
}
}

int main (int argc, char** argv)
{
    // No GUI initialisation: the tests are pure DSP and file I/O (and run under Wine).
    if (argc > 1 && juce::String (argv[1]) == "--fingerprint")
    {
        // Sound fingerprints used by testExistingSoundUnchanged (energy, peak, two sample values).
        for (const char* name : { "Hard Techno Core", "Classic Punch", "Classic Schranz", "Industrial Rumble", "Gabba Lite" })
        {
            const auto a = renderKick (preset (name), 48000.0, 140.0, true);
            double e = 0;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < a.getNumSamples(); ++i) e += (double) a.getSample (c, i) * a.getSample (c, i);
            std::printf ("{ \"%s\", %d, %.17g, %.9g, %.9g },\n", name, a.getNumSamples(), e,
                         (double) a.getSample (0, 2000), (double) a.getSample (1, 9000));
        }
        return 0;
    }
    if (argc > 2 && juce::String (argv[1]) == "--cut")
    {
        // Diagnostic: how long the output keeps sounding after the voice is cut (note-off, CHOKE).
        auto base = preset (argv[2]);
        auto run = [] (const char* what, KickParams p)
        {
            p.choke = true; p.rumble = 0.0f; p.outputDb = -12.0f;
            dsp::KickEngine e; e.prepare (48000.0, 64);
            const int n = 9600;
            juce::AudioBuffer<float> out (2, n);
            for (int pos = 0; pos < n; pos += 64)
            {
                juce::MidiBuffer m;
                if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
                if (pos == 4800) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                e.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), 64, m, 0, p, 140.0, false);
            }
            const int c = 4800 + e.getLatencySamples();
            const double before = rms (out, c - 960, c);
            std::printf ("%-18s", what);
            const double w[6][2] = { { 4, 6 }, { 6, 8 }, { 8, 12 }, { 12, 20 }, { 20, 40 }, { 40, 80 } };
            for (auto& x : w)
                std::printf ("  %2.0f-%2.0f ms %6.1f", x[0], x[1], db (rms (out, c + (int) (x[0] * 48), c + (int) (x[1] * 48))) - db (before));
            std::printf ("  dB\n");
        };
        {
            // Millisecond view around the cut: output vs the voice's own (pre-processing) signal.
            auto p = base; p.choke = true; p.rumble = 0.0f; p.outputDb = -12.0f; p.drive = 0.0f; p.character = 0.0f; p.bodyTone = 1.0f; p.width = 0.0f; p.click = 0.0f;
            dsp::KickEngine e; e.prepare (48000.0, 64);
            juce::AudioBuffer<float> out (2, 9600);
            for (int pos = 0; pos < 9600; pos += 64)
            {
                juce::MidiBuffer m;
                if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
                if (pos == 4800) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                e.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), 64, m, 0, p, 140.0, false);
            }
            std::printf ("latency %d samples\n", e.getLatencySamples());
            std::printf ("onset (first |x| > 1e-4): sample %d\n", [&] { for (int i = 0; i < 9600; ++i) if (std::abs (out.getSample (0, i)) > 1.0e-4f) return i; return -1; }());
            std::printf ("ms after cut, peak per ms:");
            for (int k = -4; k < 24; ++k)
                std::printf (" %d:%.3f", k, out.getMagnitude (0, 4800 + k * 48, 48));
            std::printf ("\n");
            // Same render without CHOKE: the difference is what the cut removed.
            p.choke = false;
            dsp::KickEngine f; f.prepare (48000.0, 64);
            juce::AudioBuffer<float> ref (2, 9600);
            for (int pos = 0; pos < 9600; pos += 64)
            {
                juce::MidiBuffer m;
                if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
                if (pos == 4800) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                f.process (ref.getWritePointer (0, pos), ref.getWritePointer (1, pos), 64, m, 0, p, 140.0, false);
            }
            std::printf ("without choke, peak per ms:");
            for (int k = -4; k < 24; ++k)
                std::printf (" %d:%.3f", k, ref.getMagnitude (0, 4800 + k * 48, 48));
            std::printf ("\nfirst sample that differs: %d\n", [&] { for (int i = 0; i < 9600; ++i) if (std::abs (out.getSample (0, i) - ref.getSample (0, i)) > 1.0e-6f) return i; return -1; }());
        }
        run ("as is", base);
        { auto q = base; q.character = 0.0f; run ("no character", q); }
        { auto q = base; q.drive = 0.0f; run ("no drive", q); }
        { auto q = base; q.bodyTone = 1.0f; run ("no body scoop", q); }
        { auto q = base; q.width = 0.0f; run ("width 0", q); }
        { auto q = base; q.character = 0.0f; q.drive = 0.0f; q.bodyTone = 1.0f; q.width = 0.0f; q.click = 0.0f; run ("all off", q); }
        return 0;
    }
    if (argc > 2 && juce::String (argv[1]) == "--retrigger")
    {
        // Diagnostic: where the extra peak of a roll comes from.
        auto p = preset (argv[2]);
        auto show = [] (const char* what, const KickParams& q)
        {
            std::printf ("%-22s single %6.2f  1/16 %6.2f  1/32 %6.2f  | full velocity 1/16 %6.2f  1/32 %6.2f dBFS\n", what,
                         db (retriggerPeak (q, 1 << 30, 150.0)), db (retriggerPeak (q, 4800, 150.0)), db (retriggerPeak (q, 2400, 150.0)),
                         db (retriggerPeak (q, 4800, 150.0, true)), db (retriggerPeak (q, 2400, 150.0, true)));
        };
        show ("as is", p);
        for (double bpm : { 120.0, 140.0, 150.0, 170.0 })
            std::printf ("  single hit at %.0f BPM: %.2f dBFS\n", bpm, db (retriggerPeak (p, 1 << 30, bpm)));
        {
            // Where the roll peak sits relative to its hit, and the single hit at the same offset.
            auto q = p; q.outputDb -= 24.0f;
            for (int step : { 4800, 2400 })
            {
                dsp::KickEngine e; e.prepare (48000.0, 512);
                juce::AudioBuffer<float> out (2, 96256);
                int hit = 0;
                for (int pos = 0; pos < 96256; pos += 512)
                {
                    juce::MidiBuffer m;
                    for (int t = pos; t < pos + 512; ++t)
                        if (t % step == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (hit++ % 2) == 0 ? 1.0f : 0.8f), t - pos);
                    e.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), 512, m, 0, q, 150.0, false);
                }
                int pi = 0; float pv = 0;
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 96000; ++i) if (std::abs (out.getSample (c, i)) > pv) { pv = std::abs (out.getSample (c, i)); pi = i; }
                std::printf ("  step %d: peak %.2f dBFS at hit %d + %.2f ms\n", step, db (pv) + 24.0, pi / step, (pi % step) / 48.0);
                // per-hit peaks
                std::printf ("  per-hit peaks:");
                for (int h = 0; h < 96000 / step && h < 24; ++h)
                    std::printf (" %.1f", db (std::max (out.getMagnitude (0, h * step, step), out.getMagnitude (1, h * step, step))) + 24.0);
                std::printf ("\n");
            }
        }
        { auto q = p; q.rumble = 0.0f; show ("no rumble", q); }
        { auto q = p; q.width = 0.0f; show ("width 0", q); }
        { auto q = p; q.click = 0.0f; show ("no click", q); }
        { auto q = p; q.drive = 0.0f; q.character = 0.0f; show ("no drive", q); }
        {
            // Identical hits (no click noise, no velocity): roll hit 3 minus an isolated hit.
            auto q = p; q.click = 0.0f; q.velSens = 0.0f; q.outputDb -= 24.0f;
            auto render = [&] (int step, int hits)
            {
                dsp::KickEngine e; e.prepare (48000.0, 512);
                juce::AudioBuffer<float> out (2, 48128);
                for (int pos = 0; pos < 48128; pos += 512)
                {
                    juce::MidiBuffer m;
                    for (int t = pos; t < pos + 512; ++t)
                        if (t % step == 0 && t / step < hits) m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), t - pos);
                    e.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), 512, m, 0, q, 150.0, false);
                }
                return out;
            };
            for (int step : { 4800, 2400 })
            {
                const auto roll = render (step, 4), single = render (step, 1);
                const int start = 3 * step + 48; // hand-off: the hit starts 1 ms after its note
                const float g = std::pow (10.0f, 24.0f / 20.0f);
                std::printf ("  step %d, hit 3 vs isolated hit (dBFS-equivalent peaks per 2 ms):\n   roll  ", step);
                for (int k = 0; k < 12; ++k) std::printf (" %6.1f", db (roll.getMagnitude (0, start + k * 96, 96) * g));
                std::printf ("\n   single");
                for (int k = 0; k < 12; ++k) std::printf (" %6.1f", db (single.getMagnitude (0, k * 96, 96) * g));
                std::printf ("\n   diff  ");
                for (int k = 0; k < 12; ++k)
                {
                    float d = 0.0f;
                    for (int i = k * 96; i < (k + 1) * 96; ++i) d = std::max (d, std::abs (roll.getSample (0, start + i) - single.getSample (0, i)));
                    std::printf (" %6.1f", db (d * g));
                }
                std::printf ("\n");
            }
        }
        { auto q = p; q.bodyTone = 1.0f; show ("no body scoop", q); }
        { auto q = p; q.bodyTone = 1.0f; q.character = 0.0f; show ("no scoop, no char", q); }
        { auto q = p; q.bodyTone = 1.0f; q.click = 0.0f; show ("no scoop, no click", q); }
        { auto q = p; q.bodyTone = 1.0f; q.click = 0.0f; q.velSens = 0.0f; show ("no scoop/click/vel", q); }
        { auto q = p; q.bodyTone = 1.0f; q.click = 0.0f; q.velSens = 0.0f; q.character = 0.0f; show ("+ no character", q); }
        { auto q = p; q.character = 0.0f; show ("no character", q); }
        { auto q = p; q.punch = 0.0f; show ("no punch", q); }
        { auto q = p; q.velSens = 0.0f; show ("no velocity", q); }
        { auto q = p; q.pitchFall = 0.0f; show ("no pitch fall", q); }
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "--dump-presets")
    {
        // name|category|id=value ... for Tools/fit_kicks.py
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
        // Render server for Tools/fit_kicks.py. Each stdin line: "paramId=value ... _len=0.6 _sr=48000".
        // Unlisted parameters use defaults. Writes len*sr stereo frames of float32 (interleaved) to stdout.
        std::string line;
        std::vector<float> out;
        while (std::getline (std::cin, line))
        {
            Snapshot s;
            double len = 0.6, sr = 48000.0, bpm = 140.0;
            int step = 0; // _step: roll, a hit every _step samples (with _bpm)
            float velocity = 1.0f; bool alternate = false; // _vel: hit velocity, _alt=1: 1.0 / _vel alternating
            std::istringstream ss (line);
            std::string tok;
            while (ss >> tok)
            {
                const auto eq = tok.find ('=');
                if (eq == std::string::npos) continue;
                const auto key = juce::String (tok.substr (0, eq));
                const float value = std::stof (tok.substr (eq + 1));
                if (key == "_len") len = value;
                else if (key == "_sr") sr = value;
                else if (key == "_step") step = (int) value;
                else if (key == "_bpm") bpm = value;
                else if (key == "_vel") velocity = value;
                else if (key == "_alt") alternate = value > 0.5f;
                else s[key] = value;
            }
            const auto audio = stream (paramsFromSnapshot (s), sr, 512, std::max (1, (int) std::ceil (len)), step, bpm, velocity, alternate);
            const int frames = std::min (audio.getNumSamples(), (int) (len * sr));
            out.resize ((size_t) frames * 2);
            for (int i = 0; i < frames; ++i)
            {
                out[(size_t) i * 2] = audio.getSample (0, i);
                out[(size_t) i * 2 + 1] = audio.getSample (1, i);
            }
            std::fwrite (out.data(), sizeof (float), out.size(), stdout);
            std::fflush (stdout);
        }
        return 0;
    }
    const bool report = argc > 1 && juce::String (argv[1]) == "--report";
    if (argc > 2 && juce::String (argv[1]) == "--export")
    {
        // Renders every factory preset to 24-bit stereo WAV for listening.
        juce::File dir (juce::String::fromUTF8 (argv[2]));
        dir.createDirectory();
        int n = 1;
        for (const auto& pr : factoryPresets())
        {
            juce::String err;
            const auto audio = renderKick (paramsFromSnapshot (pr.values), 48000.0, 140.0, true);
            const auto name = juce::String (n++).paddedLeft ('0', 2) + " " + pr.category.replace ("/", "-") + " - " + pr.name + ".wav";
            if (! writeWav24 (dir.getChildFile (name), audio, 48000.0, err))
                std::cout << err << "\n";
        }
        return 0;
    }
    testPresetLibrary();
    testPresetsRender (report);
    testWidthZeroIsTrueMono();
    testMonoSumKeepsKick();
    testSampleRatesAndBlocks();
    testNoClicksOnParameterJumps();
    testCharacterAudible();
    testPitchFall();
    testPresetsStayInKickRegister();
    testRetriggerPeaks();
    testWholeChainHandoff();
    testDriveSweepIsSmooth();
    testStereoControlsAreAudible();
    testStereoSplit();
    testExistingSoundUnchanged();
    testAdvancedFeatures();
    testSettledPitch();
    testGatedBody();
    testBodyTone();
    testVelocity();
    testLegacyPresetMigration();
    testPresetRoundTripAndWav();
    std::cout << (failures == 0 ? "PASS " : "FAILED ") << (checks - failures) << "/" << checks << " checks\n";
    return failures == 0 ? 0 : 1;
}
