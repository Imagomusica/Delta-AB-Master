#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include <algorithm>
#include <juce_core/juce_core.h>

namespace dab {

// Master meter feed (audio thread -> UI): block statistics accumulated until the UI takes them,
// a 4x oversampled true peak (ITU-R BS.1770 style, 48-tap polyphase interpolator) and a decimated
// stereo stream for the scope. Lock-free: atomics and a single-producer / single-consumer FIFO.
class MasterMeter
{
public:
    static constexpr int kPhases = 4, kTapsPerPhase = 12, kScope = 8192;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        decim = std::max (1, (int) std::lround (fs / 48000.0));
        // windowed sinc, cutoff at the original Nyquist, Kaiser beta 8
        const int N = kPhases * kTapsPerPhase;
        const double beta = 8.0, i0b = bessel0 (beta);
        for (int k = 0; k < N; ++k)
        {
            const double t = (k - (N - 1) / 2.0) / kPhases;
            const double sinc = t == 0 ? 1.0 : std::sin (juce::MathConstants<double>::pi * t) / (juce::MathConstants<double>::pi * t);
            const double r = 2.0 * k / (N - 1) - 1.0;
            const double w = bessel0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / i0b;
            coef[(size_t) (k % kPhases)][(size_t) (k / kPhases)] = sinc * w;
        }
        for (auto& h : hist) h.fill (0.0);
        pos = 0; dcount = 0;
        for (int c = 0; c < 2; ++c) { peak[c].store (0); truePeak[c].store (0); sumSq[c].store (0); }
        sumLR.store (0); count.store (0);
        fifo.reset();
    }

    void process (const double* L, const double* R, int n)
    {
        const double* x[2] = { L, R };
        double pk[2] = {}, tp[2] = {}, ss[2] = {}, lr = 0;
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                const double v = x[c][i];
                pk[c] = std::max (pk[c], std::abs (v));
                ss[c] += v * v;
                auto& h = hist[(size_t) c];
                h[(size_t) pos] = v;
                for (int ph = 0; ph < kPhases; ++ph)
                {
                    double acc = 0;
                    for (int t = 0; t < kTapsPerPhase; ++t) acc += coef[(size_t) ph][(size_t) t] * h[(size_t) ((pos - t + kTapsPerPhase) % kTapsPerPhase)];
                    tp[c] = std::max (tp[c], std::abs (acc));
                }
            }
            lr += L[i] * R[i];
            pos = (pos + 1) % kTapsPerPhase;
            if (++dcount >= decim)
            {
                dcount = 0;
                int s1, n1, s2, n2;
                fifo.prepareToWrite (1, s1, n1, s2, n2);
                if (n1 > 0) { sl[(size_t) s1] = (float) L[i]; sr[(size_t) s1] = (float) R[i]; fifo.finishedWrite (1); }
            }
        }
        for (int c = 0; c < 2; ++c)
        {
            maxStore (peak[c], (float) pk[c]);
            maxStore (truePeak[c], (float) std::max (tp[c], pk[c]));
            add (sumSq[c], ss[c]);
        }
        add (sumLR, lr);
        count.fetch_add (n);
    }

    struct Stats { float peak[2] {}, truePeak[2] {}; double meanSq[2] {}; double corr = 0; int n = 0; };
    Stats take()
    {
        Stats s;
        s.n = count.exchange (0);
        double lr = sumLR.exchange (0);
        for (int c = 0; c < 2; ++c)
        {
            s.peak[c] = peak[c].exchange (0);
            s.truePeak[c] = truePeak[c].exchange (0);
            const double q = sumSq[c].exchange (0);
            s.meanSq[c] = s.n > 0 ? q / s.n : 0;
        }
        const double den = std::sqrt (s.meanSq[0] * s.meanSq[1]) * s.n;
        s.corr = den > 1e-20 ? lr / den : 0;
        return s;
    }

    // UI side: pop up to max scope samples
    int popScope (float* l, float* r, int max)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (std::min (max, fifo.getNumReady()), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) { l[i] = sl[(size_t) (s1 + i)]; r[i] = sr[(size_t) (s1 + i)]; }
        for (int i = 0; i < n2; ++i) { l[n1 + i] = sl[(size_t) (s2 + i)]; r[n1 + i] = sr[(size_t) (s2 + i)]; }
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

private:
    static double bessel0 (double x)
    {
        double s = 1, t = 1;
        for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; }
        return s;
    }
    static void maxStore (std::atomic<float>& a, float v)
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }
    static void add (std::atomic<double>& a, double v)
    {
        double cur = a.load (std::memory_order_relaxed);
        while (! a.compare_exchange_weak (cur, cur + v, std::memory_order_relaxed)) {}
    }

    double fs = 48000;
    int decim = 1, dcount = 0, pos = 0;
    std::array<std::array<double, kTapsPerPhase>, kPhases> coef {};
    std::array<std::array<double, kTapsPerPhase>, 2> hist {};
    std::atomic<float> peak[2] {}, truePeak[2] {};
    std::atomic<double> sumSq[2] {}, sumLR { 0 };
    std::atomic<int> count { 0 };
    juce::AbstractFifo fifo { kScope };
    std::array<float, kScope> sl {}, sr {};
};

} // namespace dab
