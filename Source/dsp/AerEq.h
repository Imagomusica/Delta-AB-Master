#pragma once
// Delta AER channel EQ: low cut, high cut (6/12/24 dB/oct), 3 bells with Q, low and high shelf with Q.
// Minimum phase: a chain of biquads. Linear phase: one FIR whose amplitude is exactly the
// amplitude of that same chain (frequency sampling), so both modes have the same curve.
#include "Convolver.h"
#include "Filters.h"
#include <array>

namespace dab {

struct Band { double f = 1000, g = 0, q = 0.707; bool operator== (const Band& o) const { return f == o.f && g == o.g && q == o.q; } };

struct AerEqParams {
    bool lcOn = false; double lcHz = 40;   int lcSlope = 2;   // slope: 1, 2, 4 (6/12/24 dB/oct)
    bool hcOn = false; double hcHz = 18000; int hcSlope = 2;
    std::array<Band, 3> bell { Band { 250, 0, 1.0 }, Band { 1000, 0, 1.0 }, Band { 4000, 0, 1.0 } };
    Band lowShelf { 120, 0, 0.707 }, highShelf { 8000, 0, 0.707 };

    bool operator== (const AerEqParams& o) const
    {
        return lcOn == o.lcOn && lcHz == o.lcHz && lcSlope == o.lcSlope && hcOn == o.hcOn && hcHz == o.hcHz
            && hcSlope == o.hcSlope && bell == o.bell && lowShelf == o.lowShelf && highShelf == o.highShelf;
    }
    bool operator!= (const AerEqParams& o) const { return ! (*this == o); }
};

inline Biquad peaking (double f, double fs, double gDb, double Q)
{
    const double A = std::pow (10.0, gDb / 40), w = 2 * kPi * f / fs, al = std::sin (w) / (2 * Q), c = std::cos (w);
    const double a0 = 1 + al / A;
    Biquad q; q.b0 = (1 + al * A) / a0; q.b1 = -2 * c / a0; q.b2 = (1 - al * A) / a0; q.a1 = -2 * c / a0; q.a2 = (1 - al / A) / a0;
    return q;
}

inline Biquad shelf (double f, double fs, double gDb, double Q, bool high)
{
    const double A = std::pow (10.0, gDb / 40), w = 2 * kPi * f / fs, al = std::sin (w) / (2 * Q), c = std::cos (w);
    const double sA = 2 * std::sqrt (A) * al;
    Biquad q; double a0;
    if (! high)
    {
        a0 = (A + 1) + (A - 1) * c + sA;
        q.b0 = A * ((A + 1) - (A - 1) * c + sA) / a0; q.b1 = 2 * A * ((A - 1) - (A + 1) * c) / a0;
        q.b2 = A * ((A + 1) - (A - 1) * c - sA) / a0; q.a1 = -2 * ((A - 1) + (A + 1) * c) / a0;
        q.a2 = ((A + 1) + (A - 1) * c - sA) / a0;
    }
    else
    {
        a0 = (A + 1) - (A - 1) * c + sA;
        q.b0 = A * ((A + 1) + (A - 1) * c + sA) / a0; q.b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0;
        q.b2 = A * ((A + 1) + (A - 1) * c - sA) / a0; q.a1 = 2 * ((A - 1) - (A + 1) * c) / a0;
        q.a2 = ((A + 1) - (A - 1) * c - sA) / a0;
    }
    return q;
}

// Butterworth sections for a cut filter of slope 1 (6 dB), 2 (12 dB) or 4 (24 dB).
inline void addCut (std::vector<Biquad>& v, double f, double fs, int slope, bool highPass)
{
    if (slope == 1) { v.push_back (highPass ? Biquad::highpass1 (f, fs) : Biquad::lowpass1 (f, fs)); return; }
    if (slope == 2) { v.push_back (highPass ? Biquad::highpass2 (f, fs, std::sqrt (0.5)) : Biquad::lowpass2 (f, fs, std::sqrt (0.5))); return; }
    for (double Q : { 0.54119610, 1.30656296 })
        v.push_back (highPass ? Biquad::highpass2 (f, fs, Q) : Biquad::lowpass2 (f, fs, Q));
}

// Fills v (cleared first; no allocation if v has capacity for 9 sections).
inline void eqSections (const AerEqParams& p, double fs, std::vector<Biquad>& v)
{
    v.clear();
    const double nyq = fs * 0.49;
    if (p.lcOn) addCut (v, std::min (p.lcHz, nyq), fs, p.lcSlope, true);
    if (p.hcOn) addCut (v, std::min (p.hcHz, nyq), fs, p.hcSlope, false);
    if (p.lowShelf.g != 0) v.push_back (shelf (std::min (p.lowShelf.f, nyq), fs, p.lowShelf.g, p.lowShelf.q, false));
    if (p.highShelf.g != 0) v.push_back (shelf (std::min (p.highShelf.f, nyq), fs, p.highShelf.g, p.highShelf.q, true));
    for (const auto& b : p.bell)
        if (b.g != 0) v.push_back (peaking (std::min (b.f, nyq), fs, b.g, b.q));
}

inline std::vector<Biquad> eqSections (const AerEqParams& p, double fs)
{
    std::vector<Biquad> v;
    eqSections (p, fs, v);
    return v;
}

inline double sectionsMagnitude (const std::vector<Biquad>& v, double f, double fs)
{
    const cd z1 = std::polar (1.0, -2 * kPi * f / fs), z2 = z1 * z1;
    cd h = 1.0;
    for (const auto& q : v) h *= (q.b0 + q.b1 * z1 + q.b2 * z2) / (1.0 + q.a1 * z1 + q.a2 * z2);
    return std::abs (h);
}

// Linear-phase FIR with the same amplitude as the minimum-phase chain. Odd length N, delay (N - 1) / 2.
inline std::vector<double> designLinearPhaseEq (const AerEqParams& p, double fs, int N)
{
    if (N % 2 == 0) ++N;
    const int D = (N - 1) / 2;
    const int order = ceilLog2 (4 * N);
    const int M = 1 << order;
    const auto sec = eqSections (p, fs);
    std::vector<cd> spec ((size_t) M);
    for (int k = 0; k <= M / 2; ++k)
    {
        const double mag = sectionsMagnitude (sec, (double) k * fs / M, fs);
        spec[(size_t) k] = mag;
        if (k > 0 && k < M / 2) spec[(size_t) (M - k)] = mag;
    }
    FFT fft (order);
    fft.inverse (spec.data());
    std::vector<double> h ((size_t) N);
    const double beta = 4.0, i0b = FractionalDelay::besselI0 (beta);
    for (int i = 0; i < N; ++i)
    {
        const int m = i - D;
        const double r = (double) m / (D + 1);
        h[(size_t) i] = spec[(size_t) ((m + M) % M)].real()
                      * FractionalDelay::besselI0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / i0b;
    }
    return h;
}

// Linear-phase EQ filter for one AER channel, built off the audio thread when its EQ changes.
struct AerEqFilter {
    AerEqParams params;
    double fs = 0;
    int N = 0;
    FifoConvolver conv;
    void build (const AerEqParams& p, double sampleRate, int length)
    {
        params = p; fs = sampleRate; N = length;
        conv.prepare (designLinearPhaseEq (p, sampleRate, length), convBlock (sampleRate, length));
    }
};

} // namespace dab
