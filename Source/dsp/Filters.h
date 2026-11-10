#pragma once
// IIR biquads, delays, smoothing and the linear-phase crossover FIR design.
#include "FFT.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace dab {

// ---------------------------------------------------------------- Biquad (TDF-II, double)
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    inline double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    // Bilinear transform with prewarping, so the cutoff lands exactly on fc.
    static Biquad lowpass1 (double fc, double fs)
    {
        const double K = std::tan (kPi * fc / fs);
        Biquad q; q.b0 = q.b1 = K / (1 + K); q.a1 = (K - 1) / (K + 1); return q;
    }
    static Biquad highpass1 (double fc, double fs)
    {
        const double K = std::tan (kPi * fc / fs);
        Biquad q; q.b0 = 1 / (1 + K); q.b1 = -1 / (1 + K); q.a1 = (K - 1) / (K + 1); return q;
    }
    static Biquad lowpass2 (double fc, double fs, double Q)
    {
        const double K = std::tan (kPi * fc / fs), n = 1 / (1 + K / Q + K * K);
        Biquad q; q.b0 = K * K * n; q.b1 = 2 * q.b0; q.b2 = q.b0;
        q.a1 = 2 * (K * K - 1) * n; q.a2 = (1 - K / Q + K * K) * n; return q;
    }
    static Biquad highpass2 (double fc, double fs, double Q)
    {
        const double K = std::tan (kPi * fc / fs), n = 1 / (1 + K / Q + K * K);
        Biquad q; q.b0 = n; q.b1 = -2 * n; q.b2 = n;
        q.a1 = 2 * (K * K - 1) * n; q.a2 = (1 - K / Q + K * K) * n; return q;
    }
    // Constant 0 dB peak band-pass (used only by the level analyser).
    static Biquad bandpass (double fc, double fs, double Q)
    {
        const double K = std::tan (kPi * fc / fs), n = 1 / (1 + K / Q + K * K);
        Biquad q; q.b0 = K / Q * n; q.b1 = 0; q.b2 = -q.b0;
        q.a1 = 2 * (K * K - 1) * n; q.a2 = (1 - K / Q + K * K) * n; return q;
    }
};

// ---------------------------------------------------------------- Integer delay
class IntDelay {
public:
    void prepare (int delaySamples)
    {
        d = std::max (0, delaySamples);
        buf.assign ((size_t) d + 1, 0.0);
        w = 0;
    }
    inline double process (double x)
    {
        if (d == 0) return x;
        buf[(size_t) w] = x;
        int r = w - d; if (r < 0) r += (int) buf.size();
        const double y = buf[(size_t) r];
        if (++w == (int) buf.size()) w = 0;
        return y;
    }
private:
    int d = 0, w = 0;
    std::vector<double> buf;
};

// ---------------------------------------------------------------- Fractional delay
// Windowed-sinc (Kaiser) interpolation. A whole-sample delay is an exact pass-through.
class FractionalDelay {
public:
    static constexpr int kTaps = 128;             // even
    static constexpr int kHalf = kTaps / 2 - 1;   // minimum integer delay

    void prepare (int maxDelaySamples)
    {
        int size = 1; while (size < maxDelaySamples + kTaps + 2) size <<= 1;
        buf.assign ((size_t) size, 0.0);
        mask = size - 1;
        w = 0;
        setDelay ((double) kHalf);
    }

    // delay in samples, must be >= kHalf
    void setDelay (double samples)
    {
        samples = std::max ((double) kHalf, samples);
        if (samples == current) return;
        current = samples;
        I = (int) std::floor (samples);
        const double f = samples - I;
        if (f == 0.0)
        {
            std::fill (c, c + kTaps, 0.0);
            c[kHalf] = 1.0;
            return;
        }
        const double beta = 14.0, i0b = besselI0 (beta);
        double sum = 0;
        for (int k = 0; k < kTaps; ++k)
        {
            const double t = k - kHalf - f;
            const double s = std::abs (t) < 1e-12 ? 1.0 : std::sin (kPi * t) / (kPi * t);
            const double r = t / (kTaps / 2.0);
            const double win = std::abs (r) >= 1 ? 0.0 : besselI0 (beta * std::sqrt (1 - r * r)) / i0b;
            c[k] = s * win; sum += c[k];
        }
        for (int k = 0; k < kTaps; ++k) c[k] /= sum;
    }

    inline double process (double x)
    {
        buf[(size_t) w] = x;
        const int base = w - (I - kHalf);
        double y = 0;
        for (int k = 0; k < kTaps; ++k) y += c[k] * buf[(size_t) ((base - k) & mask)];
        w = (w + 1) & mask;
        return y;
    }

    static double besselI0 (double x)
    {
        double sum = 1, term = 1, k = 1;
        while (term > 1e-17 * sum) { term *= (x / (2 * k)) * (x / (2 * k)); sum += term; k += 1; }
        return sum;
    }

private:
    std::vector<double> buf;
    int mask = 0, w = 0, I = kHalf;
    double current = -1;
    double c[kTaps] {};
};

// ---------------------------------------------------------------- Linear ramp smoother
class Smoother {
public:
    void prepare (double fs, double seconds = 0.02) { len = std::max (1, (int) (fs * seconds)); }
    void setImmediate (double v) { cur = tgt = v; steps = 0; }
    void setTarget (double v)
    {
        if (v == tgt) return;
        tgt = v; steps = len; inc = (tgt - cur) / len;
    }
    inline double next()
    {
        if (steps > 0) { cur += inc; if (--steps == 0) cur = tgt; }
        return cur;
    }
private:
    double cur = 0, tgt = 0, inc = 0;
    int len = 1, steps = 0;
};

inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }

// ---------------------------------------------------------------- Linear-phase crossover design
// Low-pass target |H(f)| = 1 / (1 + (f/fc)^n), n = 1, 2 or 4 (6, 12, 24 dB/oct).
// The high-pass is (delay - low-pass), so the two branches always sum back to the input exactly,
// and the target magnitudes themselves sum to 1 (-6 dB each at fc).
// Returns an odd-length symmetric FIR; its delay is (N - 1) / 2 samples.
inline std::vector<double> designLinearPhaseLowpass (double fc, double fs, int n, int N)
{
    if (N % 2 == 0) ++N;
    const int D = (N - 1) / 2;
    const int order = ceilLog2 (8 * N);
    const int M = 1 << order;
    std::vector<cd> spec ((size_t) M);
    for (int k = 0; k <= M / 2; ++k)
    {
        const double f = (double) k * fs / M;
        const double mag = 1.0 / (1.0 + std::pow (f / fc, (double) n));
        spec[(size_t) k] = mag;
        if (k > 0 && k < M / 2) spec[(size_t) (M - k)] = mag;
    }
    FFT fft (order);
    fft.inverse (spec.data());

    std::vector<double> h ((size_t) N);
    const double beta = 10.0, i0b = FractionalDelay::besselI0 (beta);
    double sum = 0;
    for (int i = 0; i < N; ++i)
    {
        const int m = i - D;
        const double v = spec[(size_t) ((m + M) % M)].real();
        const double r = (double) m / (D + 1);
        const double win = FractionalDelay::besselI0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / i0b;
        h[(size_t) i] = v * win;
        sum += h[(size_t) i];
    }
    for (auto& x : h) x /= sum;   // exact unity gain at DC, so the high-pass is exactly zero at DC
    return h;
}

// ---------------------------------------------------------------- Hilbert transformer (FIR, linear phase)
// 90 degree phase shift at every frequency of its band, flat amplitude, no colouring.
// Odd length N, delay (N - 1) / 2. Rolls off only near DC and Nyquist.
inline std::vector<double> designHilbert (int N)
{
    if (N % 2 == 0) ++N;
    const int D = (N - 1) / 2;
    std::vector<double> h ((size_t) N, 0.0);
    const double beta = 8.0, i0b = FractionalDelay::besselI0 (beta);
    for (int i = 0; i < N; ++i)
    {
        const int m = i - D;
        if (m % 2 == 0) continue;
        const double r = (double) m / (D + 1);
        const double win = FractionalDelay::besselI0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / i0b;
        h[(size_t) i] = 2.0 / (kPi * m) * win;
    }
    return h;
}

} // namespace dab
