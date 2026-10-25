#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Filters.h"
#include "Params.h"

namespace pb::dsp
{
inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }

// One-pole parameter smoother (~15 ms), ticked at the base sample rate.
struct Smooth
{
    double v = 0.0, coef = 0.0;
    void prepare (double sr, double seconds = 0.015) { coef = std::exp (-1.0 / (seconds * sr)); }
    void snap (double x) noexcept { v = x; }
    double tick (double target) noexcept { v = target + coef * (v - target); return v; }
};

// PolyBLEP residual for band-limited discontinuities.
inline double polyBlep (double t, double dt) noexcept
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

//==============================================================================
// Monophonic bass voice and the whole signal chain.
//   pitch (glide, bend, punch) -> analog osc (morph sine/tri/saw/square, phase-
//   modulated by FM) + 2-op FM sine + sub -> [filter] -> distortion with sub
//   protection -> [filter] -> amp env      (all of that oversampled 2x/4x/8x)
//   -> DC block -> DARK/BITE EQ -> stereo (mono below SPLIT) -> master -> peak safety
class BassEngine
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        blockSize = std::max (16, maxBlock);
        for (int i = 0; i < 3; ++i)
        {
            os[i] = std::make_unique<juce::dsp::Oversampling<float>> (
                1, (size_t) (i + 1), juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
            os[i]->initProcessing ((size_t) blockSize);
        }
        mono.setSize (1, blockSize);
        for (auto* s : { &sShape, &sSub, &sOscDrive, &sFm, &sMix, &sCut, &sRes, &sEnvAmt, &sDrive, &sTone, &sDistMix,
                         &sDark, &sBite, &sMaster, &sWidth, &sSplit })
            s->prepare (sr);
        sSplit.prepare (sr, 0.0005); // ticked once per 32 samples: ~16 ms glide
        dc.set (10.0, sr);
        decor.prepare (sr);
        haas.prepare ((int) (0.016 * sr) + 8);
        reset();
    }

    void prepare (double sampleRate, int maxBlock, const SynthParams& initialParams)
    {
        prepare (sampleRate, maxBlock);
        snapTo (initialParams); // the host needs the selected oversampler's latency before audio starts
    }

    void reset()
    {
        for (auto& o : os) if (o) o->reset();
        activeOs = -1;
        noteCount = 0;
        gate = false;
        ampEnv = fltEnv = fmEnv = 0.0;
        stage = Stage::idle;
        phase = subPhase = modPhase = 0.0;
        prevMod = 0.0;
        logFreq = targetLogFreq = std::log (110.0);
        bend = 0.0;
        punchT = 1.0e9;
        dc.reset();
        for (auto& b : eq) b.reset();
        stereoReset();
        fadeGain = 1.0;
        postCountdown = 0;
        firstBlock = true;
    }

    int getLatencySamples() const
    {
        return activeOs >= 0 ? (int) std::lround (os[(size_t) activeOs]->getLatencyInSamples()) : 0;
    }

    // Renders numSamples; MIDI events at positions [midiOffset, midiOffset + numSamples)
    // of 'midi' are applied, so a host block can be rendered in parts without copying.
    void process (float* outL, float* outR, int numSamples, const juce::MidiBuffer& midi, const SynthParams& p,
                  int midiOffset = 0)
    {
        if (firstBlock)
            snapTo (p);
        auto it = midi.findNextSamplePosition (midiOffset);
        for (int done = 0; done < numSamples;)
        {
            const int len = std::min (blockSize, numSamples - done);
            renderChunk (outL + done, outR + done, len, midi, it, midiOffset + done, p);
            done += len;
        }
    }

    bool isSilent() const noexcept { return stage == Stage::idle; }

public:
    enum class Stage { idle, attack, decay, sustain, release };

    //==========================================================================
    void snapTo (const SynthParams& p)
    {
        sShape.snap (p.shape); sSub.snap (p.sub); sOscDrive.snap (p.oscDrive); sFm.snap (p.fmAmount); sMix.snap (p.oscMix);
        sCut.snap (std::log (p.cutoffHz)); sRes.snap (p.resonance); sEnvAmt.snap (p.envAmount); sDrive.snap (p.distDrive);
        sTone.snap (p.distTone); sDistMix.snap (p.distMix); sDark.snap (p.dark); sBite.snap (p.bite);
        sMaster.snap (dbToGain (p.masterDb)); sWidth.snap (p.width); sSplit.snap (std::log (p.splitHz));
        lastSplitTarget = p.splitHz;
        applyDiscrete (p);
        updateEq();
        updateSplit();
        firstBlock = false;
    }

    // Discrete settings (distortion type, filter mode/position, oversampling) switch
    // inside a 4 ms fade so preset changes never click.
    bool discreteDiffers (const SynthParams& p) const noexcept
    {
        return p.distType != actDist || p.filterMode != actFilter || p.filterPostDist != actPost || p.oversampleIndex != activeOs;
    }
    void applyDiscrete (const SynthParams& p)
    {
        if (p.oversampleIndex != activeOs)
        {
            activeOs = p.oversampleIndex;
            os[(size_t) activeOs]->reset();
            osRate = sr * (double) (2 << activeOs);
        }
        actDist = p.distType;
        actFilter = p.filterMode;
        actPost = p.filterPostDist;
        f1.reset(); f2.reset();
        xo.set (120.0, osRate); xo.reset();
        toneLp.reset();
        crushHold = 0.0; crushCount = 0;
    }

    //==========================================================================
    void handleMidi (const juce::MidiMessage& m, const SynthParams& p)
    {
        if (m.isNoteOn())
        {
            const bool wasHeld = noteCount > 0;
            removeNote (m.getNoteNumber());
            if (noteCount < (int) std::size (notes))
                notes[noteCount++] = m.getNoteNumber();
            velocity = m.getFloatVelocity();
            targetLogFreq = noteLog (m.getNoteNumber());
            const bool glideNow = p.glideMs > 0.5f && (p.glideMode == GlideMode::mono ? stage != Stage::idle : wasHeld);
            if (! glideNow)
                logFreq = targetLogFreq;
            if (! wasHeld || p.glideMode == GlideMode::mono)
                trigger (p);
        }
        else if (m.isNoteOff())
        {
            removeNote (m.getNoteNumber());
            if (noteCount > 0)
            {
                targetLogFreq = noteLog (notes[noteCount - 1]);
                if (p.glideMs <= 0.5f) logFreq = targetLogFreq;
            }
            else
                releaseGate();
        }
        else if (m.isPitchWheel())
            bend = (m.getPitchWheelValue() - 8192) / 8192.0 * p.bendRange;
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            noteCount = 0;
            releaseGate();
        }
    }

    static double noteLog (int note) { return std::log (440.0) + (note - 69) * std::log (2.0) / 12.0; }

    void removeNote (int note)
    {
        int w = 0;
        for (int i = 0; i < noteCount; ++i)
            if (notes[i] != note) notes[w++] = notes[i];
        noteCount = w;
    }

    void trigger (const SynthParams& p)
    {
        // Phases restart only from silence, so retriggers never break the waveform.
        if (stage == Stage::idle || ampEnv < 1.0e-4)
        {
            phase = 0.0; subPhase = 0.0; modPhase = 0.0; prevMod = 0.0; phaseUp = 0.0; phaseDown = 0.0;
        }
        gate = true;
        stage = Stage::attack;
        attackStart = ampEnv;
        attackPos = 0.0;
        fltEnv = 1.0;
        fmEnv = 1.0;
        punchT = 0.0;
        velGain = (1.0 - p.velSens) + p.velSens * velocity;
    }

    void releaseGate() noexcept
    {
        gate = false;
        if (stage != Stage::idle) stage = Stage::release;
    }

    void tickEnvelopes (const SynthParams& p) noexcept
    {
        const double dt = 1.0 / sr;
        switch (stage)
        {
            case Stage::attack:
                attackPos += dt / std::max (0.0005, p.attackMs * 0.001);
                ampEnv = attackStart + (1.0 - attackStart) * std::min (1.0, attackPos);
                if (attackPos >= 1.0) stage = Stage::decay;
                break;
            case Stage::decay:
            {
                const double c = std::exp (-dt / std::max (0.001, p.decayMs * 0.001 / 4.0));
                ampEnv = p.sustain + c * (ampEnv - p.sustain);
                if (std::abs (ampEnv - p.sustain) < 1.0e-4) stage = Stage::sustain;
                break;
            }
            case Stage::sustain:
                ampEnv = p.sustain + 0.999 * (ampEnv - p.sustain); // follows sustain changes smoothly
                break;
            case Stage::release:
            {
                const double c = std::exp (-dt / std::max (0.001, p.releaseMs * 0.001 / 5.0));
                ampEnv *= c;
                if (ampEnv < 1.0e-5) { ampEnv = 0.0; stage = Stage::idle; }
                break;
            }
            case Stage::idle:
                ampEnv = 0.0;
                break;
        }
        fltEnv *= std::exp (-dt / std::max (0.002, p.fltDecayMs * 0.001 / 3.0));
        fmEnv *= std::exp (-dt / std::max (0.002, p.fmDecayMs * 0.001 / 3.0));
        punchT += dt;
    }

    //==========================================================================
    void renderChunk (float* outL, float* outR, int len, const juce::MidiBuffer& midi, juce::MidiBufferIterator& it,
                      int chunkStart, const SynthParams& p)
    {
        const bool wantSwitch = discreteDiffers (p);
        lastSplitTarget = p.splitHz;
        auto& over = *os[(size_t) activeOs];
        const int factor = 2 << activeOs;

        auto* base = mono.getWritePointer (0);
        std::fill (base, base + len, 0.0f);
        juce::dsp::AudioBlock<float> block (mono.getArrayOfWritePointers(), 1, (size_t) len);
        auto up = over.processSamplesUp (block);
        float* u = up.getChannelPointer (0);

        const double glideCoef = p.glideMs > 0.5f ? std::exp (-1.0 / (p.glideMs * 0.001 / 3.0 * sr)) : 0.0;
        const double fmRatio = fmRatios[p.fmRatio];

        for (int i = 0; i < len; ++i)
        {
            while (it != midi.cend() && (*it).samplePosition <= chunkStart + i)
            {
                handleMidi ((*it).getMessage(), p);
                ++it;
            }
            tickEnvelopes (p);

            // Control-rate values (base rate), held across the oversampled samples.
            const double shape = sShape.tick (p.shape), subLvl = sSub.tick (p.sub), oscDrive = sOscDrive.tick (p.oscDrive);
            const double fmAmt = sFm.tick (p.fmAmount), mix = sMix.tick (p.oscMix);
            const double dark = sDark.tick (p.dark), bite = sBite.tick (p.bite);
            const double logCut = sCut.tick (std::log (p.cutoffHz)), res = sRes.tick (p.resonance), envAmt = sEnvAmt.tick (p.envAmount);
            const double drive = sDrive.tick (p.distDrive), tone = sTone.tick (p.distTone), distMix = sDistMix.tick (p.distMix);

            logFreq = targetLogFreq + glideCoef * (logFreq - targetLogFreq);
            const double punchSemis = p.punch * 12.0 * std::exp (-punchT / 0.006);
            const double freq = std::exp (logFreq) * std::exp2 ((bend + punchSemis) / 12.0);
            const double inc = std::min (freq / osRate, 0.2);
            // The level hit rises over 1 ms, so a retrigger on a sounding note never clicks.
            const double punchAmp = 1.0 + p.punch * 0.6 * std::exp (-punchT / 0.012) * std::min (1.0, punchT / 0.001);

            // FM: modulation index decays from the attack to a sustained floor.
            const double fmIndex = std::pow (fmAmt, 1.5) * 7.0 * (0.18 + 0.82 * fmEnv);
            const double fb = 0.1 + 0.6 * bite;

            // Filter: envelope, DARK pulls it down, BITE adds resonance, mild key tracking.
            const double keyTrack = 0.3 * (logFreq - std::log (110.0)) / std::log (2.0);
            const double cutOct = envAmt * fltEnv * 6.0 - dark * 1.3 + keyTrack;
            const double cut = juce::jlimit (25.0, osRate * 0.45, std::exp (logCut) * std::exp2 (cutOct));
            const double q = 0.6 + std::pow (std::min (1.0, res + 0.15 * bite), 2.0) * 13.0;
            setFilter (cut, q);

            const double g = dbToGain (drive * 36.0 + bite * 9.0);
            // Cached: Destroy's peak search is only redone when the gain really moves.
            if (actDist != normType || std::abs (g - normG) > 0.002 * normG)
            {
                normG = g; normType = actDist; normValue = shaperNorm (g, actDist);
            }
            const double norm = normValue;
            // TONE: one gentle 12 dB/oct low-pass, 600 Hz .. ~22 kHz. Measured reference
            // basses keep their "air" (-21 dB at 3-8 kHz), so no steep second stage.
            const double toneHz = 600.0 * std::exp2 (juce::jlimit (0.0, 1.0, tone - 0.3 * dark + 0.2 * bite) * 5.2);
            toneLp.set (toneHz, 0.65, osRate);
            const double oscG = 1.0 + 6.0 * (oscDrive + 0.3 * dark), oscNorm = 1.0 / std::tanh (oscG);
            const double amp = ampEnv * velGain * punchAmp;

            // DETUNE (reese): two extra voices at +/- up to 30 cents, level-normalised.
            const double detuneW = std::min (1.0, p.detune * 3.0);
            const double detuneRatio = std::exp2 (p.detune * 30.0 / 1200.0);
            const double detuneNorm = 1.0 / std::sqrt (1.0 + 2.0 * detuneW * detuneW);

            // Waveform morph weights.
            const int si = juce::jlimit (0, 2, (int) shape);
            const double sf = juce::jlimit (0.0, 1.0, shape - si);

            for (int k = 0; k < factor; ++k)
            {
                double y = 0.0;
                if (stage != Stage::idle)
                {
                    modPhase += inc * fmRatio;
                    modPhase -= std::floor (modPhase);
                    const double mod = std::sin (2.0 * pi * modPhase + fb * prevMod);
                    prevMod = mod;
                    const double pm = fmIndex * mod / (2.0 * pi);

                    phase += inc;
                    phase -= std::floor (phase);
                    subPhase += inc * 0.5;
                    subPhase -= std::floor (subPhase);

                    double ph = phase + pm;
                    ph -= std::floor (ph);
                    double analog = morph (ph, inc, si, sf);
                    if (detuneW > 0.0)
                    {
                        phaseUp += inc * detuneRatio;   phaseUp -= std::floor (phaseUp);
                        phaseDown += inc / detuneRatio; phaseDown -= std::floor (phaseDown);
                        double pu = phaseUp + pm, pd = phaseDown + pm;
                        pu -= std::floor (pu); pd -= std::floor (pd);
                        analog = (analog + detuneW * (morph (pu, inc * detuneRatio, si, sf) + morph (pd, inc / detuneRatio, si, sf))) * detuneNorm;
                    }
                    const double fm = std::sin (2.0 * pi * phase + fmIndex * mod);
                    double osc = std::tanh (oscG * (analog * (1.0 - mix) + fm * mix)) * oscNorm * 0.8;
                    osc += subLvl * 0.85 * std::sin (2.0 * pi * subPhase);

                    if (! actPost) osc = filter (osc);
                    double d = distort (osc, g, norm, distMix);
                    if (actPost) d = filter (d);
                    y = d * amp;
                }
                u[i * factor + k] = (float) y;
            }
        }
        over.processSamplesDown (block);

        // Base-rate post chain.
        for (int i = 0; i < len; ++i)
        {
            if (wantSwitch)
            {
                fadeGain = std::max (0.0, fadeGain - 1.0 / (0.004 * sr));
            }
            else
                fadeGain = std::min (1.0, fadeGain + 1.0 / (0.004 * sr));

            if (--postCountdown <= 0) // fixed 32-sample steps, independent of host buffer size
            {
                postCountdown = 32;
                updateEq();
                updateSplit();
            }
            double y = dc.tick (base[i]);
            for (auto& b : eq) y = b.tick (y);
            const double gain = sMaster.tick (dbToGain (p.masterDb)) * fadeGain;
            double l, r;
            delayRamp.setTarget (juce::jlimit (0.0, 15.0, (double) p.stereoDelayMs) * 0.001 * sr, (int) (0.05 * sr));
            stereo (y, sWidth.tick (p.width), delayRamp.next(), l, r);
            l *= gain;
            r *= gain;
            // Linked peak safety: one gain for both channels, so limiting never
            // invents a side signal (and never pushes sub into the stereo field).
            const double pk = std::max (std::abs (l), std::abs (r));
            const double lim = pk > 0.9 ? safety (pk) / pk : 1.0;
            outL[i] = (float) (l * lim);
            outR[i] = (float) (r * lim);
        }
        if (wantSwitch && fadeGain <= 0.0)
            applyDiscrete (p);
    }

    //==========================================================================
    double morph (double ph, double dt, int si, double sf) const noexcept
    {
        auto wave = [ph, dt] (int w)
        {
            switch (w)
            {
                case 0: return std::sin (2.0 * pi * ph);
                case 1: return ph < 0.25 ? 4.0 * ph : (ph < 0.75 ? 2.0 - 4.0 * ph : 4.0 * ph - 4.0);
                case 2: return (1.0 - 2.0 * ph + polyBlep (ph, dt)) * 0.8;
                default:
                {
                    double ph2 = ph + 0.5; ph2 -= std::floor (ph2);
                    return ((ph < 0.5 ? 1.0 : -1.0) + polyBlep (ph, dt) - polyBlep (ph2, dt)) * 0.7;
                }
            }
        };
        return sf <= 0.0 ? wave (si) : wave (si) * (1.0 - sf) + wave (si + 1) * sf;
    }

    void setFilter (double cut, double q) noexcept
    {
        switch (actFilter)
        {
            case FilterMode::lp24: f1.set (cut, 0.54, osRate); f2.set (cut, q, osRate); break;
            case FilterMode::lp12: f1.set (cut, q, osRate); break;
            case FilterMode::bp:   f1.set (cut, std::max (0.7, q * 0.7), osRate); break;
            case FilterMode::hp:   f1.set (cut, q, osRate); break;
        }
    }

    double filter (double x) noexcept
    {
        switch (actFilter)
        {
            case FilterMode::lp24: return f2.tick (f1.tick (x).low).low;
            case FilterMode::lp12: return f1.tick (x).low;
            case FilterMode::bp:   return f1.tick (x).band * f1.k * 1.4;
            case FilterMode::hp:   return f1.tick (x).high;
        }
        return x;
    }

public:
    static double shape (double x, double g, DistType t) noexcept
    {
        const double v = x * g;
        switch (t)
        {
            case DistType::saturate: return std::tanh (v + 0.12) - std::tanh (0.12);
            case DistType::hardClip: return juce::jlimit (-1.0, 1.0, v);
            case DistType::fold:     return std::sin (1.25 * v);
            case DistType::destroy:
            {
                const double a = std::tanh (v);
                const double b = juce::jlimit (-1.0, 1.0, 1.8 * a + 0.25) - 0.25 * (1.0 - a * a);
                const double c = std::sin (2.3 * b);
                return std::tanh (1.6 * (c + 0.5 * a));
            }
            case DistType::crush:    return std::tanh (v);
        }
        return v;
    }
    // True peak of the shaper over inputs in [-1, 1]. Monotonic curves peak at the edge; Fold
    // (a sine) reaches 1 as soon as its argument passes pi/2; Destroy is sampled densely.
    // (The old 4-point estimate could land on Fold's zeros: ~11 dB jumps, found by Codex.)
    static double shaperPeak (double g, DistType t) noexcept
    {
        switch (t)
        {
            case DistType::fold:
                return 1.25 * g >= pi * 0.5 ? 1.0 : std::sin (1.25 * g);
            case DistType::destroy:
            {
                double peak = 0.0;
                for (int i = 1; i <= 128; ++i)
                {
                    const double x = i / 128.0;
                    peak = std::max ({ peak, std::abs (shape (x, g, t)), std::abs (shape (-x, g, t)) });
                }
                return peak;
            }
            default:
                return std::max (std::abs (shape (1.0, g, t)), std::abs (shape (-1.0, g, t)));
        }
    }
    static double shaperNorm (double g, DistType t) noexcept
    {
        return juce::jlimit (0.2, 4.0, 0.85 / std::max (shaperPeak (g, t), 1.0e-6));
    }

public:
    // Distortion with frequency-dependent processing: below 120 Hz only gentle
    // saturation, so the sub stays solid instead of turning into a drone.
    double distort (double x, double g, double norm, double mix) noexcept
    {
        double low, high;
        xo.split (x, low, high);
        double wet = shape (high, g, actDist) * norm;
        if (actDist == DistType::crush)
        {
            // Deliberate digital destruction: sample-rate and bit reduction.
            const int holdN = 1 + (int) (std::pow (std::min (1.0, (g - 1.0) / 60.0), 0.7) * osRate / 3500.0);
            if (++crushCount >= holdN) { crushCount = 0; crushHold = wet; }
            const double bits = 12.0 - 9.0 * std::min (1.0, (g - 1.0) / 60.0);
            const double qz = std::exp2 (bits);
            wet = std::round (crushHold * qz) / qz;
        }
        wet = toneLp.tick (wet).low;
        wet += std::tanh (1.4 * low) / 1.4;
        const double dry = x;
        return dry + (wet - dry) * mix;
    }

    void updateEq() noexcept
    {
        const double dark = sDark.v, bite = sBite.v;
        eq[0].peak (160.0, 0.8, 4.0 * dark, sr);              // weight above the sub, not more sub
        eq[1].peak (1800.0, 0.9, 6.0 * bite - 2.0 * dark, sr); // bite presence
        eq[2].shelf (true, 6000.0, -6.0 * dark + 2.0 * bite, sr);
    }

    //==========================================================================
    // Stereo: LR4 split at SPLIT FREQ, everything below stays mono. Above it, a
    // decorrelated copy forms the side signal: L = M + S, R = M - S, so the mono
    // sum never combs. WIDTH 0 gives bit-identical channels.
    struct Decorrelator
    {
        DelayLine ap[4];
        double len[4] {};
        void prepare (double sr)
        {
            static constexpr double msTimes[4] = { 0.61, 1.37, 2.93, 4.41 };
            for (int i = 0; i < 4; ++i) { len[i] = msTimes[i] * 0.001 * sr; ap[i].prepare ((int) len[i] + 8); }
        }
        void reset() { for (auto& a : ap) a.reset(); }
        double tick (double x) noexcept
        {
            for (int i = 0; i < 4; ++i)
            {
                const double d = ap[i].read (len[i] - 1.0);
                const double v = x + 0.6 * d;
                ap[i].push (v);
                x = d - 0.6 * v;
            }
            return x;
        }
    };

    void stereoReset() { split.reset(); decor.reset(); haas.reset(); delayRamp.reset (0.0); for (auto& h : sideHp) h.reset(); }
    void updateSplit() noexcept
    {
        const double f = std::exp (sSplit.tick (std::log (lastSplitTarget)));
        if (std::abs (f - splitApplied) > 0.5)
        {
            split.set (f, sr);
            // 8th-order Butterworth high-pass on the side (Q values of the 4 sections).
            static constexpr double q8[4] = { 0.5098, 0.6013, 0.9000, 2.5629 };
            for (int k = 0; k < 4; ++k) sideHp[k].highpass (f, q8[k], sr);
            splitApplied = f;
        }
    }
    void stereo (double x, double width, double delaySamples, double& l, double& r) noexcept
    {
        double low, high;
        split.split (x, low, high);
        const double theta = width * 0.65;
        // DELAY: a real time offset between the channels above SPLIT, built as a side term
        // (HIGH - HIGH delayed) / 2 -- it cancels exactly in the mono sum. Zero at DELAY 0.
        haas.push (high);
        const double haasSide = delaySamples > 0.5 ? 0.5 * (high - haas.read (delaySamples)) : 0.0;
        // The side signal gets its own extra 8th-order high-pass (mono sum unaffected): the sub
        // stays out of the stereo field even with a resonant peak right above SPLIT.
        double side = decor.tick (high) + 2.0 * haasSide;
        for (auto& h : sideHp) side = h.tick (side);
        side *= std::sin (theta);
        // The crossover feeds only the side: SPLIT must not delay the mono signal.
        const double centre = x;
        l = centre + side;
        r = centre - side;
    }

    static double safety (double x) noexcept
    {
        const double a = std::abs (x);
        if (a <= 0.9) return x;
        return std::copysign (0.9 + 0.099 * std::tanh ((a - 0.9) / 0.099), x);
    }

    //==========================================================================
    double sr = 48000.0, osRate = 192000.0;
    int blockSize = 512, activeOs = -1;
    std::unique_ptr<juce::dsp::Oversampling<float>> os[3];
    juce::AudioBuffer<float> mono;

    int notes[16] {}, noteCount = 0;
    float velocity = 1.0f;
    double velGain = 1.0;
    bool gate = false, firstBlock = true;
    Stage stage = Stage::idle;
    double ampEnv = 0.0, attackStart = 0.0, attackPos = 0.0, fltEnv = 0.0, fmEnv = 0.0, punchT = 1.0e9;
    double phase = 0.0, subPhase = 0.0, modPhase = 0.0, prevMod = 0.0, phaseUp = 0.0, phaseDown = 0.0;
    double logFreq = 0.0, targetLogFreq = 0.0, bend = 0.0;

    DistType actDist = DistType::saturate;
    FilterMode actFilter = FilterMode::lp24;
    bool actPost = false;
    Svf f1, f2, toneLp;
    ComplementarySplit xo;
    double crushHold = 0.0;
    int crushCount = 0;
    double fadeGain = 1.0;
    int postCountdown = 0;
    double normG = -1.0, normValue = 1.0;
    DistType normType = DistType::saturate;

    DcBlock dc;
    Biquad eq[3];
    LR4 split;
    Biquad sideHp[4];
    DelayLine haas;
    Ramp delayRamp;
    Decorrelator decor;
    double lastSplitTarget = 300.0, splitApplied = -1.0;
    Smooth sShape, sSub, sOscDrive, sFm, sMix, sCut, sRes, sEnvAmt, sDrive, sTone, sDistMix, sDark, sBite, sMaster, sWidth, sSplit;
};
}
