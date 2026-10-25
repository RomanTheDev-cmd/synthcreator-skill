#pragma once

#include <cmath>
#include <algorithm>
#include <vector>
#include <initializer_list>

namespace pb::dsp
{
constexpr double pi = 3.14159265358979323846;

// Topology-preserving-transform state variable filter (Zavalishin). Stable under
// fast coefficient changes, which matters for the pitch-swept click filters.
struct Svf
{
    double ic1 = 0.0, ic2 = 0.0, g = 0.0, k = 1.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;

    void set (double freq, double q, double sr) noexcept
    {
        freq = std::clamp (freq, 5.0, sr * 0.49);
        g = std::tan (pi * freq / sr);
        k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() noexcept { ic1 = ic2 = 0.0; }

    struct Out { double low, band, high; };
    Out tick (double x) noexcept
    {
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return { v2, v1, x - k * v1 - v2 };
    }
};

// RBJ biquad, transposed direct form II.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0.0; }
    double tick (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setNormalised (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }

    void peak (double f, double q, double gainDb, double sr) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * pi * std::min (f, sr * 0.45) / sr;
        const double al = std::sin (w) / (2.0 * q), c = std::cos (w);
        setNormalised (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void shelf (bool high, double f, double gainDb, double sr) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * pi * std::min (f, sr * 0.45) / sr;
        const double c = std::cos (w), al = std::sin (w) / 2.0 * std::sqrt (2.0), sA = 2.0 * std::sqrt (A) * al;
        if (high)
            setNormalised (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
                           (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
        else
            setNormalised (A * ((A + 1) - (A - 1) * c + sA), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sA),
                           (A + 1) + (A - 1) * c + sA, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sA);
    }
    void highpass (double f, double q, double sr) noexcept
    {
        const double w = 2.0 * pi * f / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        setNormalised ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void lowpass (double f, double q, double sr) noexcept
    {
        const double w = 2.0 * pi * f / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        setNormalised ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
};

// 4th-order Linkwitz-Riley crossover. low + high is an allpass of the input, so the
// bands recombine without magnitude dips. allpass() applies exactly that phase to
// another signal, for aligning the other bands of a three-way split.
struct LR4
{
    Biquad l1, l2, h1, h2;
    Biquad al1, al2, ah1, ah2;

    void set (double f, double sr) noexcept
    {
        const double q = std::sqrt (0.5);
        l1.lowpass (f, q, sr);
        h1.highpass (f, q, sr);
        copyCoefficients (l2, l1); copyCoefficients (al1, l1); copyCoefficients (al2, l1);
        copyCoefficients (h2, h1); copyCoefficients (ah1, h1); copyCoefficients (ah2, h1);
    }
    void reset() noexcept
    {
        for (auto* b : { &l1, &l2, &h1, &h2, &al1, &al2, &ah1, &ah2 })
            b->reset();
    }
    void split (double x, double& low, double& high) noexcept
    {
        low = l2.tick (l1.tick (x));
        high = h2.tick (h1.tick (x));
    }
    double allpass (double x) noexcept { return al2.tick (al1.tick (x)) + ah2.tick (ah1.tick (x)); }

private:
    static void copyCoefficients (Biquad& d, const Biquad& s) noexcept
    {
        d.b0 = s.b0; d.b1 = s.b1; d.b2 = s.b2; d.a1 = s.a1; d.a2 = s.a2;
    }
};

// One-pole DC blocker.
struct DcBlock
{
    double x1 = 0, y1 = 0, r = 0.999;
    void set (double cutoff, double sr) noexcept { r = std::exp (-2.0 * pi * cutoff / sr); }
    void reset() noexcept { x1 = y1 = 0; }
    double tick (double x) noexcept { const double y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
};

// Fractional delay line, cubic (Hermite) interpolation.
struct DelayLine
{
    std::vector<double> buf;
    int w = 0, mask = 0;

    void prepare (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 4) size <<= 1;
        buf.assign ((size_t) size, 0.0);
        mask = size - 1;
        w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0); w = 0; }
    void push (double x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // Read 'd' samples behind the most recently pushed sample.
    double read (double d) const noexcept
    {
        d = std::max (d, 0.0);
        const int di = (int) d;
        const double f = d - di;
        auto at = [this] (int k) { return buf[(size_t) ((w - 1 - k) & mask)]; };
        const double xm1 = at (di - 1 < 0 ? 0 : di - 1), x0 = at (di), x1 = at (di + 1), x2 = at (di + 2);
        if (di == 0) // no future sample available: linear
            return x0 + f * (x1 - x0);
        const double c1 = 0.5 * (x1 - xm1), c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2, c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
};

// Linear parameter ramp, set per block.
struct Ramp
{
    double cur = 0, target = 0, step = 0;
    int left = 0;
    void reset (double v) noexcept { cur = target = v; left = 0; step = 0; }
    void setTarget (double v, int samples) noexcept
    {
        if (v == target) return;
        target = v;
        left = std::max (1, samples);
        step = (target - cur) / left;
    }
    double next() noexcept
    {
        if (left > 0) { cur += step; if (--left == 0) cur = target; }
        return cur;
    }
};

struct Rng
{
    unsigned int s = 0x9E3779B9u;
    void seed (unsigned int v) noexcept { s = v ? v : 1u; }
    double white() noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (double) s / 2147483648.0 - 1.0;
    }
};
}
