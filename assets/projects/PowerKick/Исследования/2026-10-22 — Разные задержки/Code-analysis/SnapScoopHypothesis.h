#pragma once

#include <juce_dsp/juce_dsp.h>
#include "/Users/roman/Library/Mobile Documents/com~apple~CloudDocs/Главная рабочая папка ИИ/Проекты/PowerKick/Source/DSP/Filters.h"
#include "/Users/roman/Library/Mobile Documents/com~apple~CloudDocs/Главная рабочая папка ИИ/Проекты/PowerKick/Source/Params.h"

namespace pk::snap_scoop
{
using namespace pk::dsp;
inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }

//==============================================================================
// One kick: pitch-swept sine body with punch transient, plus a click layer.
// Sound parameters are captured at note-on, so knob moves never cause zipper
// noise inside a hit; the next hit picks them up.
class KickVoice
{
public:
    void prepare (double sampleRate) { sr = sampleRate; active = false; }

    void start (const KickParams& p, int note, float velocity, unsigned int seed, double bpm = 140.0)
    {
        const double keyOffset = p.keyTrack ? (note - p.keyRoot) : 0.0;
        f0 = baseFrequency (p.tune) * std::pow (2.0, keyOffset / 12.0);
        depth = p.pitchDepth * (0.55 + 0.45 * p.punch);
        pitchTau = std::max (0.002, p.pitchTime * 0.001);
        fall = p.pitchFall;
        fallTau = juce::jlimit (0.03, 0.2, 0.25 * decayMs (p.decay) * 0.001);
        punch = p.punch;
        body = p.body;
        decaySec = decayMs (p.decay) * 0.001;
        if (p.beatLength > 0) // BEAT LENGTH: end within a note value at the host tempo
        {
            static constexpr double beats[4] = { 0.0, 1.0, 0.5, 0.25 };
            const double maxSec = beats[juce::jlimit (0, 3, p.beatLength)] * 60.0 / juce::jlimit (40.0, 300.0, bpm) - 0.01;
            decaySec = std::max (0.03, std::min (decaySec, maxSec));
            fallTau = juce::jlimit (0.03, 0.2, 0.25 * decaySec);
        }
        hold = decaySec * (0.06 + 0.54 * body);
        curveShape = p.pitchCurve;
        subLevel = p.subLevel;
        // High BODY holds the level and then stops, like measured hard-techno kicks
        // (tail only 1-2 dB under the hit); low BODY decays smoothly.
        curve = 0.35 + 3.65 * std::pow (1.0 - body, 1.4);
        satGain = 1.0 + 2.4 * body;
        satNorm = 1.0 / std::tanh (satGain);

        const double v = juce::jlimit (0.0, 1.0, (double) velocity);
        velGain = std::pow (std::max (v, 0.001), 1.5 * p.velSens);
        const double velClick = std::pow (std::max (v, 0.001), 2.0 * p.velSens);

        clickType = p.clickType;
        clickTone = p.clickTone;
        clickGain = 0.75 * std::pow (p.click, 1.6) * velClick;
        clickTau = clickType == ClickType::soft ? 0.0035 : clickType == ClickType::sharp ? 0.0013 : 0.006 + 0.012 * clickTone;
        clickFilter.reset();
        if (clickType == ClickType::soft)  clickFilter.set (1400.0 + 3200.0 * clickTone, 0.8, sr);
        if (clickType == ClickType::sharp) clickFilter.set (2500.0 + 6000.0 * clickTone, 0.9, sr);
        if (clickType == ClickType::metal) clickFilter.set (3000.0 + 4500.0 * clickTone, 1.4, sr);
        blipFreq = 700.0 + 1500.0 * clickTone;
        const double metalBase = 900.0 + 1700.0 * clickTone;
        static constexpr double ratios[6] = { 1.0, 1.4717, 1.7932, 2.5310, 2.6142, 3.9071 };
        for (int i = 0; i < 6; ++i) { metalInc[i] = metalBase * ratios[i] / sr; metalPhase[i] = 0.0; }

        if (clickType == ClickType::noise909) clickFilter.set (3500.0 + 3000.0 * clickTone, 0.8, sr);
        if (clickType == ClickType::noise909) clickTau = 0.006;
        if (clickType == ClickType::fmBlip) { clickTau = 0.004; blipFreq = 1800.0 + 1800.0 * clickTone; }

        rng.seed (seed);
        t = 0.0;
        phase = 0.25 * juce::jlimit (0.0f, 1.0f, p.startPhase); // START PHASE: 0..90 degrees
        fade = 1.0;
        fadeStep = 0.0;
        lastAmp = 0.0;
        active = true;
    }

    // Short fade so a hand-off or a note-off never cuts a waveform mid-cycle.
    void release (double fadeSeconds) noexcept
    {
        if (active && fadeStep == 0.0)
            fadeStep = 1.0 / (fadeSeconds * sr);
    }

    bool isActive() const noexcept { return active; }
    double level() const noexcept { return active ? lastAmp * fade : 0.0; } // current body envelope

    // Returns body and click separately; env receives the amplitude envelope
    // (used to duck the rumble under each hit).
    void tick (double& bodyOut, double& clickOut, double& env, double& subOut) noexcept
    {
        bodyOut = clickOut = env = subOut = 0.0;
        if (! active)
            return;

        // Fast sweep, the punch blip, and a slow fall through the tail (time constant 25% of
        // DECAY, 30..200 ms), all ending on TUNE. PITCH CURVE blends the exponential sweep
        // towards a rounder, slower 1 / (1 + x^2) shape.
        const double x = t / pitchTau;
        const double shape = curveShape > 0.0 ? (1.0 - curveShape) * std::exp (-x) + curveShape / (1.0 + x * x) : std::exp (-x);
        const double sweep = depth * shape + punch * 14.0 * std::exp (-t / 0.004)
                             + fall * std::exp (-t / fallTau);
        const double freq = std::min (f0 * std::exp2 (sweep / 12.0), sr * 0.45);
        phase += freq / sr;
        phase -= std::floor (phase);
        const double s = std::tanh (satGain * std::sin (2.0 * pi * phase)) * satNorm;

        double a;
        if (t < hold)
            a = 1.0;
        else
        {
            const double u = (t - hold) / (decaySec - hold);
            a = u >= 1.0 ? 0.0 : std::pow (1.0 - u, curve);
        }
        const double attack = std::min (1.0, t / 0.0004) * std::min (1.0, (decaySec - t) / 0.004); // 4 ms end fade
        const double transient = 1.0 + punch * 0.9 * std::exp (-t / 0.016);
        a *= attack * fade;
        env = a;
        lastAmp = a * transient * velGain;
        bodyOut = s * a * transient * velGain * (0.92 / (1.0 + 0.45 * punch));
        // SUB LAYER: the same pitch as a clean, unsaturated sine; mixed after the drive.
        if (subLevel > 0.0)
            subOut = std::sin (2.0 * pi * phase) * a * velGain * 0.8 * subLevel;

        clickOut = renderClick() * fade * clickGain;

        t += 1.0 / sr;
        if (fadeStep > 0.0)
            fade = std::max (0.0, fade - fadeStep);
        if (fade <= 0.0 || t > decaySec + 0.01)
            active = false;
    }

private:
    double renderClick() noexcept
    {
        if (clickGain <= 0.0 || t > clickTau * 12.0)
            return 0.0;
        const double attack = std::min (1.0, t / 0.0002);
        const double e = std::exp (-t / clickTau) * attack;
        switch (clickType)
        {
            case ClickType::soft:
            {
                const double blip = std::sin (2.0 * pi * blipFreq * t) * std::exp (-t / 0.0025);
                return (clickFilter.tick (rng.white()).low * 0.9 + blip * 0.6) * e;
            }
            case ClickType::sharp:
                return clickFilter.tick (rng.white()).high * 1.3 * e;
            case ClickType::metal:
            {
                double osc[6];
                for (int i = 0; i < 6; ++i)
                {
                    metalPhase[i] += metalInc[i];
                    metalPhase[i] -= std::floor (metalPhase[i]);
                    osc[i] = std::sin (2.0 * pi * metalPhase[i]);
                }
                // Ring-modulated sine pairs: inharmonic metal, band-limited by construction.
                const double ring = osc[0] * osc[1] + osc[2] * osc[3] + osc[4] * osc[5];
                return (clickFilter.tick (ring).band * 1.6 + rng.white() * 0.08) * e;
            }
            case ClickType::noise909: // bright band-passed noise burst, 909-style
                return clickFilter.tick (rng.white()).band * 2.6 * e;
            case ClickType::fmBlip:   // short FM "tick": carrier with a fast-decaying 3.5x modulator
            {
                const double index = 4.0 * std::exp (-t / 0.002);
                return std::sin (2.0 * pi * blipFreq * t + index * std::sin (2.0 * pi * 3.5 * blipFreq * t)) * 2.4 * e;
            }
        }
        return 0.0;
    }

    double sr = 48000.0, t = 0.0, phase = 0.0;
    double f0 = 43.65, depth = 30.0, pitchTau = 0.03, fall = 0.0, fallTau = 0.1, punch = 0.5, body = 0.5;
    double curveShape = 0.0, subLevel = 0.0;
    double decaySec = 0.4, hold = 0.05, curve = 2.0, satGain = 2.0, satNorm = 1.0, velGain = 1.0;
    ClickType clickType = ClickType::sharp;
    double clickTone = 0.5, clickGain = 0.0, clickTau = 0.002, blipFreq = 1000.0;
    double metalPhase[6] {}, metalInc[6] {};
    double fade = 1.0, fadeStep = 0.0, lastAmp = 0.0;
    bool active = false;
    Svf clickFilter;
    Rng rng;
};

//==============================================================================
// Techno rumble: the kick is delayed by a tempo-synced amount, smeared by a
// damped comb bank, low-passed, saturated and ducked under each new hit.
class Rumble
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        pre.prepare ((int) (sr * 1.2));
        static constexpr double combMs[4] = { 29.7, 37.1, 41.1, 43.7 };
        for (int i = 0; i < 4; ++i)
        {
            combLen[i] = combMs[i] * 0.001 * sr;
            comb[i].prepare ((int) combLen[i] + 8);
            damp[i] = 0.0;
        }
        dampCoef = std::exp (-2.0 * pi * 320.0 / sr);
        // Reverb mode: longer mutually prime lines, darker damping, two diffusers.
        static constexpr double revMs[4] = { 53.7, 61.3, 71.1, 79.9 };
        for (int i = 0; i < 4; ++i)
        {
            revLen[i] = revMs[i] * 0.001 * sr;
            rev[i].prepare ((int) revLen[i] + 8);
            revDamp[i] = 0.0;
        }
        revDampCoef = std::exp (-2.0 * pi * 220.0 / sr);
        static constexpr double difMs[2] = { 5.1, 1.7 };
        for (int i = 0; i < 2; ++i) { difLen[i] = difMs[i] * 0.001 * sr; dif[i].prepare ((int) difLen[i] + 8); }
        lp.set (165.0, 0.85, sr);
        hp.highpass (32.0, 0.7, sr);
        duckAtt = std::exp (-1.0 / (0.0008 * sr));
        duckRel = std::exp (-1.0 / (0.045 * sr));
        reset();
    }

    void reset()
    {
        pre.reset();
        for (auto& c : comb) c.reset();
        for (auto& d : damp) d = 0.0;
        for (auto& c : rev) c.reset();
        for (auto& d : revDamp) d = 0.0;
        for (auto& d : dif) d.reset();
        lp.reset(); hp.reset();
        duck = 0.0; gateTime = 1.0e9; gateLen = 0.0;
    }

    void setBlockParams (const KickParams& p, double bpm) noexcept
    {
        const double beat = 60.0 / juce::jlimit (40.0, 300.0, bpm);
        static constexpr double syncBeats[3] = { 0.25, 0.5, 0.75 };
        preDelay = juce::jlimit (1.0, sr * 1.15, syncBeats[juce::jlimit (0, 2, p.rumbleSync)] * beat * sr);
        feedback = 0.55 + 0.38 * p.rumbleTime;
        gateLen = (0.5 + 1.5 * p.rumbleTime) * beat;
        amount = 1.5 * std::pow ((double) p.rumble, 1.3);
        reverbMode = p.rumbleMode == 1;
    }

    void trigger() noexcept { gateTime = 0.0; }

    // in: kick body signal, kickEnv: summed voice envelope.
    double tick (double in, double kickEnv) noexcept
    {
        pre.push (in);
        const double x = pre.read (preDelay);

        double sum = 0.0;
        if (! reverbMode)
            for (int i = 0; i < 4; ++i)
            {
                const double y = comb[i].read (combLen[i] - 1.0);
                damp[i] = y + dampCoef * (damp[i] - y);
                comb[i].push (x + damp[i] * feedback);
                sum += y;
            }
        else
        {
            // Diffuse the input first so the tail has no audible comb periodicity.
            double d = x;
            for (int k = 0; k < 2; ++k)
            {
                const double delayed = dif[k].read (difLen[k] - 1.0);
                const double v = d + 0.6 * delayed;
                dif[k].push (v);
                d = delayed - 0.6 * v;
            }
            const double fb = std::min (0.93, feedback + 0.08);
            for (int i = 0; i < 4; ++i)
            {
                const double y = rev[i].read (revLen[i] - 1.0);
                revDamp[i] = y + revDampCoef * (revDamp[i] - y);
                rev[i].push (d + revDamp[i] * fb * (i % 2 == 0 ? 1.0 : -1.0));
                sum += y;
            }
            sum *= 0.8;
        }
        double r = lp.tick (sum * 0.3).low;
        r = std::tanh (2.5 * r);
        r = hp.tick (r);

        const double target = std::min (1.0, kickEnv);
        duck = target > duck ? target + duckAtt * (duck - target) : target + duckRel * (duck - target);

        // Gate opens when the delayed copy arrives and fades out after gateLen.
        double gate = 0.0;
        const double since = gateTime - preDelay / sr;
        if (since >= 0.0)
            gate = since < gateLen ? 1.0 : std::exp (-(since - gateLen) / 0.06);
        gateTime += 1.0 / sr;

        const double ducked = 1.0 - duck;
        return r * amount * gate * ducked * ducked;
    }

private:
    double sr = 48000.0, preDelay = 1000.0, feedback = 0.7, gateLen = 0.3, amount = 0.0;
    double combLen[4] {}, damp[4] {}, dampCoef = 0.9, duck = 0.0, duckAtt = 0.9, duckRel = 0.99, gateTime = 1.0e9;
    DelayLine pre, comb[4];
    bool reverbMode = false;
    double revLen[4] {}, revDamp[4] {}, revDampCoef = 0.97, difLen[2] {};
    DelayLine rev[4], dif[2];
    Svf lp;
    Biquad hp;
};

//==============================================================================
// Character EQ: a fixed four-band topology (low shelf, two peaks, high shelf)
// whose targets come from the selected mode scaled by CHARACTER. Coefficients
// glide every 32 samples so mode and knob changes never click.
class CharacterEq
{
public:
    struct Band { double freq, q, gain; };
    struct Mode { Band b[4]; double makeupDb; double extraDriveDb; };

    static const Mode& mode (CharMode m) noexcept
    {
        static const Mode modes[5] = {
            { { { 65, 0.7, 7 },  { 280, 0.9, -5 }, { 2500, 0.7, -2 }, { 7000, 0.7, -4 } },  -2.5, 0.0 }, // DEEP
            { { { 45, 0.7, -1 }, { 125, 1.2, 6 },  { 420, 1.0, -4 },  { 3500, 0.7, 4.5 } }, -2.5, 0.0 }, // PUNCHY
            { { { 60, 0.7, 4 },  { 190, 0.8, 2 },  { 1500, 0.7, -5 }, { 3000, 0.7, -10 } }, -1.0, 0.0 }, // DARK
            { { { 60, 0.7, 2 },  { 300, 1.0, -3 }, { 1800, 0.8, 6 },  { 6000, 0.7, 3 } },   -2.5, 9.0 }, // HARD
            { { { 60, 0.7, 0 },  { 180, 1.0, -2 }, { 900, 1.3, 7 },   { 5000, 0.7, 6 } },   -3.0, 7.0 }, // RAW
        };
        return modes[(int) m];
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        initialised = false;
        for (auto& f : filt) f.reset();
    }

    void setTarget (CharMode m, double intensity)
    {
        const auto& md = mode (m);
        for (int i = 0; i < 4; ++i)
        {
            tgt[i] = { std::log (md.b[i].freq), md.b[i].q, md.b[i].gain * intensity };
            if (! initialised) cur[i] = tgt[i];
        }
        tgtMakeup = md.makeupDb * intensity;
        if (! initialised) { curMakeup = tgtMakeup; updateCoefficients(); }
        initialised = true;
    }

    double tick (double x) noexcept
    {
        if (--countdown <= 0)
        {
            countdown = 32;
            const double k = 1.0 - std::exp (-32.0 / (0.03 * sr)); // ~30 ms glide
            for (int i = 0; i < 4; ++i)
            {
                cur[i].freq += (tgt[i].freq - cur[i].freq) * k;
                cur[i].q += (tgt[i].q - cur[i].q) * k;
                cur[i].gain += (tgt[i].gain - cur[i].gain) * k;
            }
            curMakeup += (tgtMakeup - curMakeup) * k;
            updateCoefficients();
        }
        for (auto& f : filt) x = f.tick (x);
        return x * makeupGain;
    }

private:
    void updateCoefficients() noexcept
    {
        filt[0].shelf (false, std::exp (cur[0].freq), cur[0].gain, sr);
        filt[1].peak (std::exp (cur[1].freq), cur[1].q, cur[1].gain, sr);
        filt[2].peak (std::exp (cur[2].freq), cur[2].q, cur[2].gain, sr);
        filt[3].shelf (true, std::exp (cur[3].freq), cur[3].gain, sr);
        makeupGain = dbToGain (curMakeup);
    }

    double sr = 48000.0, curMakeup = 0.0, tgtMakeup = 0.0, makeupGain = 1.0;
    Band cur[4] {}, tgt[4] {};
    Biquad filt[4];
    int countdown = 0;
    bool initialised = false;
};

//==============================================================================
// Frequency-dependent stereo. The mono kick is split (phase-matched LR4) into
// SUB < 120 Hz, BODY 120-500 Hz and HIGH > 500 Hz. SUB and BODY stay in the
// centre. The side signal S is built from HIGH: a decorrelated copy (WIDTH) plus a
// real inter-channel time offset (DELAY):
//   L = centre + S,  R = centre - S
// so L + R contains no side at all: the mono sum is the mono kick, whatever WIDTH and
// DELAY are, and never combs.
class StereoEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        xoLow.set (120.0, sr);
        // 8th-order Butterworth high-pass at 150 Hz on the side only (Q of the 4 sections).
        static constexpr double q8[4] = { 0.5098, 0.6013, 0.9000, 2.5629 };
        for (int k = 0; k < 4; ++k) sideHp[k].highpass (150.0, q8[k], sr);
        xoHigh.set (500.0, sr); // kick highs above 1 kHz are only ~-20 dB: widen from 500 Hz
        static constexpr double apMs[3] = { 1.31, 2.87, 4.73 };
        for (int i = 0; i < 3; ++i)
        {
            apLen[i] = apMs[i] * 0.001 * sr;
            ap[i].prepare ((int) apLen[i] + 8);
        }
        haas.prepare ((int) (0.016 * sr) + 8);
        reset();
    }

    void reset()
    {
        xoLow.reset(); xoHigh.reset();
        for (auto& h : sideHp) h.reset();
        for (auto& a : ap) a.reset();
        haas.reset();
        width.reset (targetWidth);
        delay.reset (targetDelay);
    }

    void setTargets (double width01, double delayMs, double splitHz, bool snap = false) noexcept
    {
        targetSplit = juce::jlimit (150.0, 2000.0, splitHz);
        if (snap) { logSplit = std::log (targetSplit); applySplit (targetSplit); }
        targetWidth = juce::jlimit (0.0, 1.0, width01);
        targetDelay = juce::jlimit (0.0, 15.0, delayMs) * 0.001 * sr;
        if (snap) { width.reset (targetWidth); delay.reset (targetDelay); }
        const int ramp = (int) (0.05 * sr);
        width.setTarget (targetWidth, ramp);
        delay.setTarget (targetDelay, ramp);
    }

    void tick (double x, double& l, double& r) noexcept
    {
        if (--splitCountdown <= 0) // SPLIT glides (~20 ms) in fixed 32-sample steps: no clicks
        {
            splitCountdown = 32;
            logSplit += (std::log (targetSplit) - logSplit) * (1.0 - std::exp (-32.0 / (0.02 * sr)));
            const double f = std::exp (logSplit);
            if (std::abs (f - appliedSplit) > 0.5) applySplit (f);
        }
        double sub, rest, body, high;
        xoLow.split (x, sub, rest);
        xoHigh.split (rest, body, high);
        juce::ignoreUnused (sub);

        // WIDTH: a decorrelated copy of HIGH (plus a trace of BODY, as in measured schranz
        // kicks), same spectrum, as the side signal.
        double d = high + 0.15 * body;
        for (int i = 0; i < 3; ++i)
        {
            const double delayed = ap[i].read (apLen[i] - 1.0);
            const double v = d + 0.6 * delayed;
            ap[i].push (v);
            d = delayed - 0.6 * v;
        }
        // DELAY: a real time offset between the channels, built as a side signal
        // (HIGH - HIGH delayed) / 2. Left gets the direct sound first, right the delayed copy;
        // in the mono sum the term cancels exactly, so mono never combs. Zero at DELAY 0.
        haas.push (high);
        const double w = width.next();
        const double dl = delay.next();
        const double haasSide = dl > 0.5 ? 0.5 * (high - haas.read (dl)) : 0.0;
        // The centre is never reduced: the mono sum is exactly the mono kick at any WIDTH/DELAY.
        // (The side of a kick is ~20 dB under the whole, so widening adds < 0.1 dB of level.)
        // Extra high-pass on the side only: no sub in the stereo field.
        double sideSrc = d + 2.0 * haasSide;
        for (auto& h : sideHp) sideSrc = h.tick (sideSrc);
        const double side = w * 0.9 * sideSrc;
        // The centre is the input itself: no band split in the main path, so the lows are not
        // delayed against the attack (the bands only feed the side signal).
        const double centre = x;
        l = centre + side;
        r = centre - side;
    }

private:
    void applySplit (double f) noexcept { xoHigh.set (f, sr); appliedSplit = f; }

    double sr = 48000.0, targetWidth = 0.0, targetDelay = 0.0;
    double targetSplit = 500.0, logSplit = std::log (500.0), appliedSplit = 500.0;
    int splitCountdown = 0;
    double apLen[3] {};
    LR4 xoLow, xoHigh;
    Biquad sideHp[4];
    DelayLine ap[3], haas;
    Ramp width, delay;
};

//==============================================================================
class KickEngine
{
public:
    static constexpr int maxVoices = 2;

    void prepare (double sampleRate, int maxBlock, int initialOversampleIndex = 1)
    {
        activeOs = juce::jlimit (0, 2, initialOversampleIndex);
        sr = sampleRate;
        blockSize = std::max (16, maxBlock);
        for (auto& v : voices) v.prepare (sr);
        rumble.prepare (sr);
        eq.prepare (sr);
        stereo.prepare (sr);
        lpD.set (90.0, sr); lpC.set (90.0, sr);
        dc.set (12.0, sr);
        mono.setSize (1, blockSize);
        clean.assign ((size_t) blockSize, 0.0);
        noteTime.assign ((size_t) blockSize, 1.0e9);
        handoffGain.assign ((size_t) blockSize, 1.0);
        bodyToneCoef = std::exp (-1.0 / (0.02 * sr));
        for (int i = 0; i < 3; ++i) // 2x / 4x / 8x
        {
            oversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>> (
                1, (size_t) (i + 1), juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
            oversamplers[i]->initProcessing ((size_t) blockSize);
        }
        subBuf.assign ((size_t) blockSize, 0.0);
        reset();
    }

    void reset()
    {
        for (auto& v : voices) v = KickVoice(), v.prepare (sr);
        rumble.reset();
        stereo.reset();
        lpD.reset(); lpC.reset();
        dc.reset();
        scoop.reset();
        scoopDb = 0.0;
        bodyScoop.reset();
        bodyScoopDb = 0.0;
        sinceNote = 1.0e9;
        scoopCountdown = 0;
        for (auto& o : oversamplers) if (o) o->reset();
        cleanDelay.reset(); subDelay.reset();
        modWheel = 0.0; lastVel = 1.0;
        pending = {};
        handoffFramesLeft = 0;
        postHandoffGain = 1.0;
        firstBlock = true;
    }

    int getLatencySamples() const { return osLatency(); }

    // Latency of an oversampling choice (0 = 2x, 1 = 4x, 2 = 8x) once prepared, -1 before.
    int getLatencySamplesFor (int index) const
    {
        const auto& o = oversamplers[(size_t) juce::jlimit (0, 2, index)];
        return o ? (int) std::lround (o->getLatencyInSamples()) : -1;
    }

    // A new hit on top of a still-sounding one is a clean hand-off: the old body fades out in 1 ms
    // and the new hit starts right after it. The complete old path fades too: retained filter
    // energy must not add to the next attack.
    // The 1 ms delay only applies when the previous hit is still audible (not in a 4/4 pattern
    // whose kicks have ended).
    void noteOn (const KickParams& p, int note, float velocity, double bpm = 140.0)
    {
        lastVel = juce::jlimit (0.0, 1.0, (double) velocity);
        double sounding = 0.0;
        for (auto& v : voices) sounding = std::max (sounding, v.level());
        if (sounding > 0.01 || handoffFramesLeft > 0) // finish an existing fade before replacing its pending hit
        {
            for (auto& v : voices) v.release (handOffSeconds);
            if (handoffFramesLeft == 0) handoffFramesLeft = (int) std::ceil (handOffSeconds * sr);
            pending = { true, note, velocity, bpm, handoffFramesLeft };
            return;
        }
        pending.active = false;
        for (auto& v : voices) v.release (0.004);
        startVoice (p, note, velocity, bpm);
    }

    // CHOKE: a MIDI note-off cuts the kick (4 ms fade), so the note length sets the kick length.
    void noteOff (const KickParams& p)
    {
        if (! p.choke) return;
        pending.active = false;
        for (auto& v : voices) v.release (0.004);
    }

    // Renders numSamples into out (2 channels). Events must be pre-sorted.
    // monoCheck folds the output to (L+R)/2 on both channels.
    void process (float* outL, float* outR, int numSamples, const juce::MidiBuffer& midi, int midiOffset,
                  const KickParams& p, double bpm, bool monoCheck)
    {
        int done = 0;
        auto it = midi.findNextSamplePosition (midiOffset);
        while (done < numSamples)
        {
            int len = std::min (blockSize, numSamples - done);
            const int start = midiOffset + done;
            // A note-on opens its own chunk for the hand-off reset. Mapped mod wheel changes also
            // need a boundary: drive is set per chunk. Other events are applied per sample without
            // splitting DSP, so dense unmapped controllers do not create one-sample chunks.
            for (auto e = it; e != midi.cend() && (*e).samplePosition < start + len; ++e)
                if (const auto msg = (*e).getMessage(); msg.isNoteOn()
                    || (p.modDrive > 0.0f && msg.isController() && msg.getControllerNumber() == 1))
                {
                    len = std::min (len, std::max (1, (*e).samplePosition - start));
                    break;
                }
            if (handoffFramesLeft > 1)
                len = std::min (len, handoffFramesLeft - 1);
            renderChunk (outL + done, outR + done, len, midi, it, start, p, bpm, monoCheck);
            done += len;
        }
    }

private:
    static constexpr double handOffSeconds = 0.001;
    struct Pending { bool active = false; int note = 60; float velocity = 1.0f; double bpm = 140.0; int countdown = 0; };
    Pending pending;
    int handoffFramesLeft = 0;
    double postHandoffGain = 1.0;

    void resetSignalPath()
    {
        for (auto& v : voices) v.prepare (sr);
        rumble.reset();
        oversamplers[activeOs]->reset(); // the others are reset when they are switched in
        cleanDelay.reset(); subDelay.reset();
        lpD.reset(); lpC.reset(); dc.reset();
        scoop.reset(); scoopDb = 0.0; scoopCountdown = 0;
        scoop.peak (480.0, 0.6, 0.0, sr);
        bodyScoop = SvfBell(); bodyScoopDb = 0.0;
        eq.prepare (sr);
        stereo.reset();
        firstBlock = true;
    }

    void startVoice (const KickParams& p, int note, float velocity, double bpm)
    {
        sinceNote = 0.0;
        postHandoffGain = 1.0;
        // Prefer a free slot; otherwise steal the older (already fading) one.
        auto& v = ! voices[next].isActive() ? voices[next] : voices[1 - next];
        // The click noise is the same on every hit (as in a sample, and as the presets were
        // fitted and levelled): a random burst would peak differently from hit to hit.
        v.start (p, note, velocity, clickSeed, bpm);
        next = 1 - next;
        rumble.trigger();
    }

    void renderChunk (float* outL, float* outR, int len, const juce::MidiBuffer& midi, juce::MidiBufferIterator& it,
                      int chunkStart, const KickParams& p, double bpm, bool monoCheck)
    {
        // Clear the old signal only between complete DSP chunks, after its output faded.
        if (handoffFramesLeft == 1)
            resetSignalPath();
        rumble.setBlockParams (p, bpm);
        eq.setTarget (p.charMode, p.character);
        stereo.setTargets (p.width * 0.01, p.delayMs, p.splitHz, firstBlock);

        // Oversampling choice (2x/4x/8x); switching resets the new stage.
        const int osWanted = juce::jlimit (0, 2, p.oversampleIndex);
        if (osWanted != activeOs) { activeOs = osWanted; oversamplers[activeOs]->reset(); }
        auto& oversampler = oversamplers[activeOs];

        const auto& md = CharacterEq::mode (p.charMode);
        // VELOCITY DRIVE (softer notes cleaner) and MOD WHEEL DRIVE: both 0 by default.
        const double driveDb = 30.0 * p.drive + md.extraDriveDb * p.character
                               + 18.0 * p.velDrive * (lastVel - 1.0) + 24.0 * p.modDrive * modWheel;
        const double raw = p.charMode == CharMode::raw ? p.character : 0.0;
        const int ramp = std::max (1, (int) (0.02 * sr));
        if (firstBlock)
        {
            driveGain.reset (dbToGain (driveDb));
            outGain.reset (dbToGain (p.outputDb));
            bodyToneSm = p.bodyTone;
            scoopDb = -14.0 * p.drive; scoop.peak(480.0,0.6,scoopDb,sr);
            firstBlock = false;
        }
        driveGain.setTarget (dbToGain (driveDb), ramp);
        outGain.setTarget (dbToGain (p.outputDb), ramp);

        // 1. Voices + rumble -> mono pre-drive signal.
        auto* m = mono.getWritePointer (0);
        for (int i = 0; i < len; ++i)
        {
            while (it != midi.cend() && (*it).samplePosition <= chunkStart + i)
            {
                const auto msg = (*it).getMessage();
                if (msg.isNoteOn())
                    noteOn (p, msg.getNoteNumber(), msg.getFloatVelocity(), bpm);
                else if (msg.isNoteOff())
                    noteOff (p);
                else if (msg.isController() && msg.getControllerNumber() == 1)
                    modWheel = msg.getControllerValue() / 127.0;
                else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                {
                    pending.active = false;
                    for (auto& v : voices) v.release (0.004);
                }
                ++it;
            }
            if (handoffFramesLeft > 0)
                postHandoffGain = (double) --handoffFramesLeft / std::ceil (handOffSeconds * sr);
            if (pending.active && --pending.countdown <= 0)
            {
                pending.active = false;
                startVoice (p, pending.note, pending.velocity, pending.bpm);
            }
            handoffGain[(size_t) i] = postHandoffGain;
            double bodySum = 0.0, clickSum = 0.0, envSum = 0.0, subSum = 0.0;
            for (auto& v : voices)
            {
                double b, c, e, sb;
                v.tick (b, c, e, sb);
                bodySum += b; clickSum += c; envSum += e; subSum += sb;
            }
            subBuf[(size_t) i] = subSum;
            const double x = bodySum + clickSum + rumble.tick (bodySum, envSum);
            noteTime[(size_t) i] = sinceNote;
            sinceNote += 1.0 / sr;
            clean[(size_t) i] = x;
            m[i] = (float) x;
        }

        // 2. Drive, 4x oversampled.
        juce::dsp::AudioBlock<float> block (mono.getArrayOfWritePointers(), 1, (size_t) len);
        auto up = oversampler->processSamplesUp (block);
        auto* u = up.getChannelPointer (0);
        const int upLen = (int) up.getNumSamples();
        const int factor = upLen / len;
        for (int i = 0; i < upLen; ++i)
        {
            if (i % factor == 0)
            {
                currentDrive = driveGain.next();
                // DRIVE ATTACK: up to +24 dB more drive on the attack, decaying over ~30 ms.
                attackDrive = p.driveEnv > 0.0f ? dbToGain (24.0 * p.driveEnv * std::exp (-noteTime[(size_t) (i / factor)] / 0.03)) : 1.0;
            }
            u[i] = (float) shape ((double) u[i], currentDrive * attackDrive, p.driveMode, raw);
        }
        oversampler->processSamplesDown (block);
        const double norm = shaperNorm (currentDrive, p.driveMode, raw) * dbToGain (-4.0 * p.drive);
        // Harmonic control: distortion piles energy into 300-800 Hz ("honk"). Measured
        // hard-techno kicks sit ~20 dB down there, so scoop it as drive rises.
        const double scoopTarget = -14.0 * p.drive;

        // 3. Sub protection, Character EQ, output, stereo.
        const int lat = osLatency();
        for (int i = 0; i < len; ++i)
        {
            // Align the clean path with the oversampler's integer latency.
            cleanDelay.push (clean[(size_t) i]);
            const double cleanAligned = cleanDelay.read ((double) lat);

            double dLow, dHigh, cLow;
            if (--scoopCountdown <= 0) // fixed 32-sample steps: independent of host buffer size
            {
                scoopCountdown = 32;
                scoopDb += (scoopTarget - scoopDb) * (1.0 - std::exp (-32.0 / (0.03 * sr)));
                scoop.peak (480.0, 0.6, scoopDb, sr);
            }
            // Sub protection with a subtractive one-pole split (low + high = input exactly):
            // a Linkwitz-Riley split here delayed the whole low end by ~5 ms against the attack.
            const double dIn = scoop.tick (m[i] * norm);
            dLow = lpD.tick (dIn);
            dHigh = dIn - dLow;
            // BODY TONE: measured hard-techno kicks have a bright, distorted attack and a body
            // with strong lows, scooped mids (~1 kHz) and some top "fizz". After the first 4 ms
            // a wide mid scoop deepens towards the body tone (0 = -24 dB, 1 = no scoop).
            bodyToneSm = p.bodyTone + bodyToneCoef * (bodyToneSm - p.bodyTone);
            {
                const double tn = noteTime[(size_t) i];
                const double open = tn < 0.004 ? 1.0 : std::exp (-(tn - 0.004) / 0.012);
                const double depth = -24.0 * (1.0 - bodyToneSm) * (1.0 - open);
                if (std::abs (depth - bodyScoopDb) > 0.05)
                {
                    bodyScoopDb = depth;
                    bodyScoop.set (1000.0, 0.9, bodyScoopDb, sr); // state-variable bell: no spike when it opens on a retrigger
                }
                dHigh = bodyScoop.tick (dHigh);
            }
            cLow = lpC.tick (cleanAligned);
            const double protect = 0.75 * std::sqrt (p.drive);
            double y = dHigh + dLow + (cLow - dLow) * protect;
            // SUB LAYER: clean sine under the driven body, aligned with the oversampler latency.
            subDelay.push (subBuf[(size_t) i]);
            y += subDelay.read ((double) lat);

            y = eq.tick (y);
            y = dc.tick (y) * outGain.next();

            double l, r;
            stereo.tick (y, l, r);
            if (monoCheck) l = r = 0.5 * (l + r);
            // Linked emergency ceiling: one gain for both channels, never a side signal.
            l *= handoffGain[(size_t) i];
            r *= handoffGain[(size_t) i];
            const double pk = std::max (std::abs (l), std::abs (r));
            const double lim = pk > 0.9 ? safety (pk) / pk : 1.0;
            outL[i] = (float) (l * lim);
            outR[i] = (float) (r * lim);
        }
    }

public:
    static double shape (double x, double g, DriveMode mode, double raw) noexcept
    {
        double v = x * g;
        double y;
        switch (mode)
        {
            case DriveMode::clip:
                y = juce::jlimit (-1.0, 1.0, v);
                break;
            case DriveMode::industrial:
            {
                // Folding plus even-order asymmetry: grinding industrial overtones.
                const double t = std::tanh (v);
                y = 0.62 * t + 0.38 * std::sin (1.8 * v) * std::tanh (0.6 * v * v) + 0.18 * t * t;
                break;
            }
            case DriveMode::saturate:
            default:
                y = std::tanh (v + 0.08) - std::tanh (0.08);
                break;
        }
        return y + raw * 0.22 * y * y;
    }

    // Keeps the peak of a full-scale hit near constant as drive rises. The true peak over
    // inputs up to 0.9: Saturate/Clip (also with RAW) are monotonic, so the edge is the peak;
    // Industrial folds, so it is searched densely (a 3-point estimate could hit its zeros and
    // jump the level by >2 dB -- same class of bug Codex found in PowerBass).
    static double shaperNorm (double g, DriveMode mode, double raw) noexcept
    {
        double peak = 0.0;
        if (mode == DriveMode::industrial)
        {
            for (int i = 1; i <= 128; ++i)
            {
                const double x = 0.9 * i / 128.0;
                peak = std::max ({ peak, std::abs (shape (x, g, mode, raw)), std::abs (shape (-x, g, mode, raw)) });
            }
        }
        else
            peak = std::max (std::abs (shape (0.9, g, mode, raw)), std::abs (shape (-0.9, g, mode, raw)));
        return juce::jlimit (0.25, 4.0, 0.9 / std::max (peak, 1.0e-6));
    }

private:
    int osLatency() const
    {
        const auto& o = oversamplers[activeOs];
        return o ? (int) std::lround (o->getLatencyInSamples()) : 0;
    }

    // Emergency ceiling only: presets keep their peaks below it, so it never shapes the sound.
    static double safety (double x) noexcept
    {
        const double a = std::abs (x);
        if (a <= 0.9) return x;
        return std::copysign (0.9 + 0.099 * std::tanh ((a - 0.9) / 0.099), x);
    }

    double sr = 48000.0, currentDrive = 1.0;
    int blockSize = 512, next = 0;
    static constexpr unsigned int clickSeed = 12345u + 0x6F4F2A1Bu; // the seed the first hit always had
    bool firstBlock = true;
    KickVoice voices[maxVoices];
    Rumble rumble;
    CharacterEq eq;
    StereoEngine stereo;
    struct Lp1 { double z = 0.0, a = 0.0; void set (double f, double rate) { a = 1.0 - std::exp (-2.0 * pi * f / rate); } void reset() { z = 0.0; } double tick (double x) { z += a * (x - z); return z; } };
    Lp1 lpD, lpC;
    DcBlock dc;
    Biquad scoop;
    double scoopDb = 0.0;
    SvfBell bodyScoop;
    double bodyScoopDb = 0.0;
    double bodyToneSm = 1.0, bodyToneCoef = 0.999, sinceNote = 1.0e9;
    std::vector<double> noteTime;
    std::vector<double> handoffGain;
    int scoopCountdown = 0;
    Ramp driveGain, outGain;
    DelayLine cleanDelay = makeDelay();
    juce::AudioBuffer<float> mono;
    std::vector<double> clean;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversamplers[3];
    int activeOs = 1;
    std::vector<double> subBuf;
    DelayLine subDelay = makeDelay();
    double modWheel = 0.0, lastVel = 1.0, attackDrive = 1.0;

    static DelayLine makeDelay() { DelayLine d; d.prepare (256); return d; }
};
}
