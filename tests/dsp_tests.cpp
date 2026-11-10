// Measurement tests for the Delta AB DSP engine (no JUCE needed).
#include "../Source/dsp/Engine.h"
#include <cstdio>
#include <random>
#include <string>

using namespace dab;

static int failures = 0;
static void check (bool ok, const std::string& what, double value)
{
    std::printf ("[%s] %-62s %.6g\n", ok ? " OK " : "FAIL", what.c_str(), value);
    if (! ok) ++failures;
}

static double magAt (const std::vector<double>& h, double f, double fs)
{
    cd s = 0;
    for (size_t i = 0; i < h.size(); ++i) s += h[i] * std::polar (1.0, -2 * kPi * f / fs * (double) i);
    return std::abs (s);
}
static double db (double g) { return 20 * std::log10 (g); }

static std::vector<double> noise (int n, unsigned seed)
{
    std::mt19937 rng (seed);
    std::normal_distribution<double> d (0, 0.25);
    std::vector<double> v ((size_t) n);
    for (auto& x : v) x = d (rng);
    return v;
}

int main()
{
    const double fs = 48000;

    // 1. Linear-phase crossover: target magnitude
    for (int n : { 1, 2, 4 })
    {
        for (double fc : { 350.0, 700.0, 1500.0, 2200.0 })
        {
            const auto h = designLinearPhaseLowpass (fc, fs, n, firLength (fs, true));
            const double atFc = db (magAt (h, fc, fs));
            check (std::abs (atFc + 6.0206) < 0.05, "LP " + std::to_string (6 * n) + " dB/oct, " + std::to_string ((int) fc) + " Hz: -6.02 dB at fc", atFc);
            const double f3 = fc * 8, want = db (1.0 / (1.0 + std::pow (8.0, n)));
            const double got = db (magAt (h, f3, fs));
            check (std::abs (got - want) < 0.2, "   3 octaves above fc follows the target slope (dB)", got);
        }
    }

    // 2. Linear-phase reconstruction: LP + HP == delayed input, both qualities, all slopes
    for (bool exportQ : { false, true })
        for (Slope s : { Slope::dB6, Slope::dB12, Slope::dB24 })
        {
            CrossoverConfig cfg; cfg.fs = fs; cfg.linearPhase = true; cfg.exportQuality = exportQ; cfg.slope = s;
            MicCrossover xo; xo.build (cfg, 0);
            const int L = xo.latency(), N = L + 20000;
            auto x = noise (N, 1);
            std::vector<double> a = x, b = x, y ((size_t) N);
            for (int i = 0; i < N; i += 333) { const int k = std::min (333, N - i); xo.process (a.data() + i, b.data() + i, y.data() + i, k); }
            double err = 0;
            for (int i = L; i < N; ++i) err = std::max (err, std::abs (y[(size_t) i] - x[(size_t) (i - L)]));
            check (err < 1e-12, std::string ("Reconstruction ") + (exportQ ? "export" : "listen") + ", slope " + std::to_string (6 * (int) s) + " dB/oct: max error", err);
        }

    // 2b. Independent signals: partitioned FFT convolution == direct LP*a + (delta - LP)*b
    {
        CrossoverConfig cfg; cfg.fs = fs; cfg.linearPhase = true; cfg.slope = Slope::dB12;
        MicCrossover xo; xo.build (cfg, 0);
        const auto h = designLinearPhaseLowpass (700, fs, 2, firLength (fs, false));
        const int L = xo.latency(), D = ((int) h.size() - 1) / 2, B = L - D, N = L + 6000;
        auto a = noise (N, 3), b = noise (N, 4);
        std::vector<double> a2 = a, b2 = b, y ((size_t) N);
        for (int i = 0; i < N; i += 777) { const int k = std::min (777, N - i); xo.process (a2.data() + i, b2.data() + i, y.data() + i, k); }
        double err = 0;
        for (int n = L; n < N; n += 97)
        {
            const int m = n - B;   // output of the plain FIR at time m
            double ref = b[(size_t) (m - D)];
            for (int k = 0; k < (int) h.size() && k <= m; ++k) ref += h[(size_t) k] * (a[(size_t) (m - k)] - b[(size_t) (m - k)]);
            err = std::max (err, std::abs (y[(size_t) n] - ref));
        }
        check (err < 1e-12, "FFT convolution matches direct LP(O) + HP(R): max error", err);
    }

    // 3. Minimum phase: flat magnitude sum
    for (Slope s : { Slope::dB6, Slope::dB12, Slope::dB24 })
    {
        CrossoverConfig cfg; cfg.fs = fs; cfg.linearPhase = false; cfg.slope = s;
        MicCrossover xo; xo.build (cfg, 0);
        const int N = 1 << 16;
        std::vector<double> a ((size_t) N, 0.0), b ((size_t) N, 0.0), y ((size_t) N);
        a[0] = b[0] = 1;
        xo.process (a.data(), b.data(), y.data(), N);
        std::vector<cd> sp (y.begin(), y.end());
        FFT f (16); f.forward (sp.data());
        double dev = 0;
        for (int k = 1; k < N / 2; ++k) dev = std::max (dev, std::abs (db (std::abs (sp[(size_t) k]))));
        check (dev < 1e-6, "Minimum phase " + std::to_string (6 * (int) s) + " dB/oct: sum is flat (max dB deviation)", dev);
    }

    // 4. Fractional delay accuracy (1 kHz sine, 10.37 + 31 samples)
    {
        FractionalDelay d; d.prepare (300);
        const double D = 100.37; d.setDelay (D);
        double err = 0;
        for (int i = 0; i < 4000; ++i)
        {
            const double y = d.process (std::sin (2 * kPi * 1000 * i / fs));
            if (i > 300) err = std::max (err, std::abs (y - std::sin (2 * kPi * 1000 * (i - D) / fs)));
        }
        check (err < 1e-6, "Fractional delay 100.37 samples, 1 kHz: max error", err);
    }

    // 5. Whole engine: impulse on every input, identical capsules, all gains 0 dB
    for (bool linear : { true, false })
    {
        Engine e; e.prepare (fs, 256);
        CrossoverConfig cfg; cfg.fs = fs; cfg.linearPhase = linear;
        CrossoverSet set; set.build (cfg);
        EngineParams p;
        p.mic[0].pan = -1; p.mic[1].pan = 1; p.mic[2].pan = 0;
        const int lat = e.totalLatency (set), N = lat + 4096;
        std::vector<std::vector<double>> in (6, std::vector<double> ((size_t) N, 0.0));
        for (auto& c : in) c[0] = 1;
        std::vector<double> L ((size_t) N), R ((size_t) N);
        const double* ip[6]; for (int c = 0; c < 6; ++c) ip[c] = in[(size_t) c].data();
        double* op[2] = { L.data(), R.data() };
        e.process (set, p, ip, op, N);
        int peak = 0; for (int i = 0; i < N; ++i) if (std::abs (L[(size_t) i]) > std::abs (L[(size_t) peak])) peak = i;
        const double want = 1 + dbToGain (-3.0);
        if (linear)
        {
            check (peak == lat, "Engine linear: output peak at the reported latency (samples)", peak);
            check (std::abs (L[(size_t) lat] - want) < 1e-12, "Engine linear: L = mic1 + centre at -3 dB, exactly", L[(size_t) lat]);
            double rest = 0; for (int i = 0; i < N; ++i) if (i != lat) rest = std::max (rest, std::abs (L[(size_t) i]));
            check (rest < 1e-12, "Engine linear: nothing outside the impulse", rest);
        }
        else
        {
            double sumL = 0; for (auto v : L) sumL += v;
            check (std::abs (sumL - want) < 1e-3, "Engine min phase: DC gain = mic1 + centre at -3 dB", sumL);
        }
    }

    // 6. Centre delay: +2 ms and -2 ms move the centre relative to L/R
    for (double ms : { 2.0, -2.0 })
    {
        Engine e; e.prepare (fs, 512);
        CrossoverConfig cfg; cfg.fs = fs;
        CrossoverSet set; set.build (cfg);
        EngineParams p; p.mic[0].pan = -1; p.mic[1].pan = 1; p.mic[2].pan = 0; p.centreDelayMs = ms;
        p.centreSendDb = -1.5;
        const int lat = e.totalLatency (set), N = lat + 4096;
        std::vector<std::vector<double>> in (6, std::vector<double> ((size_t) N, 0.0));
        for (auto& c : in) c[0] = 1;
        std::vector<double> L ((size_t) N), R ((size_t) N);
        const double* ip[6]; for (int c = 0; c < 6; ++c) ip[c] = in[(size_t) c].data();
        double* op[2] = { L.data(), R.data() };
        e.process (set, p, ip, op, N);
        const int shift = (int) std::lround (std::abs (ms) * 1e-3 * fs);
        const int side = ms > 0 ? lat : lat + shift, centre = ms > 0 ? lat + shift : lat;
        check (std::abs (L[(size_t) side] - 1) < 1e-12 && std::abs (L[(size_t) centre] - dbToGain (-1.5)) < 1e-12,
               "Centre delay " + std::to_string ((int) ms) + " ms: centre offset by " + std::to_string (shift) + " samples", L[(size_t) centre]);
    }

    // 7. Level analyser: ribbon 1.94 dB lower than omni
    {
        Engine e; e.prepare (fs, 512);
        CrossoverConfig cfg; cfg.fs = fs;
        CrossoverSet set; set.build (cfg);
        EngineParams p; p.mic[0].analyse = true;
        const int N = (int) fs * 2;
        auto x = noise (N, 7);
        std::vector<std::vector<double>> in (6, std::vector<double> ((size_t) N, 0.0));
        in[0] = x; for (int i = 0; i < N; ++i) in[1][(size_t) i] = 0.8 * x[(size_t) i];
        std::vector<double> L ((size_t) N), R ((size_t) N);
        const double* ip[6]; for (int c = 0; c < 6; ++c) ip[c] = in[(size_t) c].data();
        double* op[2] = { L.data(), R.data() };
        e.process (set, p, ip, op, N);
        const double m = e.measuredMatchDb[0].load();
        check (std::abs (m - 20 * std::log10 (1 / 0.8)) < 0.01, "Analyser: ribbon correction found (want +1.938 dB)", m);
    }

    // 8. Focus / Bloom: with Focus +6 dB the highs rise by 6 dB, the lows stay
    {
        CrossoverConfig cfg; cfg.fs = fs; cfg.linearPhase = true; cfg.slope = Slope::dB12;
        Engine e; e.prepare (fs, 1024);
        CrossoverSet set; set.build (cfg);
        EngineParams p; p.mic[0].pan = -1; p.mic[1].pan = -1; p.mic[2].pan = -1; p.mic[0].focusDb = 6;
        p.mic[1].focusDb = 6; p.mic[2].focusDb = 6;
        const int lat = e.totalLatency (set), N = lat + 8192;
        std::vector<std::vector<double>> in (6, std::vector<double> ((size_t) N, 0.0));
        for (auto& c : in) c[0] = 1.0 / 3.0;
        std::vector<double> L ((size_t) N), R ((size_t) N);
        const double* ip[6]; for (int c = 0; c < 6; ++c) ip[c] = in[(size_t) c].data();
        double* op[2] = { L.data(), R.data() };
        e.process (set, p, ip, op, N);
        std::vector<double> h (L.begin(), L.end());
        const double lowG = db (magAt (h, 50, fs)), highG = db (magAt (h, 12000, fs));
        check (std::abs (lowG) < 0.05 && std::abs (highG - 6.0206) < 0.05, "Focus +6 dB: 50 Hz stays 0 dB, 12 kHz at +6 dB (12 kHz value)", highG);
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
