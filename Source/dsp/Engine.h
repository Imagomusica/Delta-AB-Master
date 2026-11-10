#pragma once
// Delta AB engine: 3 hybrid mics (omni O + ribbon R), mini Decca triangle.
//   per mic : routing -> O/R level match -> crossover (switch: which capsule takes the lows)
//             -> Bloom (low capsule) / Focus (high capsule) -> mono sum -> delay -> pan to L/C/R
//   buses   : faders L, C, R with 6 dB/oct low cut -> stereo sum (C sent to L and R) -> output
// Mic 1 = left, mic 2 = right, mic 3 = centre (the only mic whose delay is user-set).
#include "Convolver.h"
#include "Filters.h"
#include <array>
#include <atomic>
#include <memory>

namespace dab {

enum class Slope { dB6 = 1, dB12 = 2, dB24 = 4 };

// Everything that requires rebuilding filters (done off the audio thread).
struct CrossoverConfig {
    double fs = 48000;
    bool linearPhase = true;
    bool exportQuality = false;
    Slope slope = Slope::dB12;
    std::array<double, 3> fc { 700, 700, 700 };

    bool operator== (const CrossoverConfig& o) const
    {
        return fs == o.fs && linearPhase == o.linearPhase && exportQuality == o.exportQuality
            && slope == o.slope && fc == o.fc;
    }
    bool operator!= (const CrossoverConfig& o) const { return ! (*this == o); }
};

// FIR length scales with the sample rate, so the time span stays the same.
inline int firLength (double fs, bool exportQuality)
{
    const double base = exportQuality ? 65536.0 : 4096.0;   // at 48 kHz
    return (1 << ceilLog2 ((int) std::lround (base * fs / 48000.0))) - 1;
}
constexpr int kConvBlock = 512;

// One mic's crossover: out = LP(low) + HP(high).
class MicCrossover {
public:
    void build (const CrossoverConfig& cfg, int mic)
    {
        linear = cfg.linearPhase;
        const double fc = cfg.fc[(size_t) mic], fs = cfg.fs;
        if (linear)
        {
            const int N = firLength (fs, cfg.exportQuality);
            const auto h = designLinearPhaseLowpass (fc, fs, (int) cfg.slope, N);
            conv.prepare (h, kConvBlock);
            lat = kConvBlock + (N - 1) / 2;
            direct.prepare (lat);
        }
        else
        {
            lat = 0;
            nStages = 0; sign = 1.0;
            switch (cfg.slope)
            {
                case Slope::dB6:   // 1st order: LP + HP = 1 exactly
                    lp[0] = Biquad::lowpass1 (fc, fs); hp[0] = Biquad::highpass1 (fc, fs); nStages = 1; break;
                case Slope::dB12:  // Linkwitz-Riley 2nd order: LP - HP is all-pass
                    lp[0] = lp[1] = Biquad::lowpass1 (fc, fs); hp[0] = hp[1] = Biquad::highpass1 (fc, fs);
                    nStages = 2; sign = -1.0; break;
                case Slope::dB24:  // Linkwitz-Riley 4th order: LP + HP is all-pass
                    lp[0] = lp[1] = Biquad::lowpass2 (fc, fs, std::sqrt (0.5));
                    hp[0] = hp[1] = Biquad::highpass2 (fc, fs, std::sqrt (0.5));
                    nStages = 2; break;
            }
        }
    }

    int latency() const { return lat; }

    // low/high are scratch buffers and may be overwritten.
    void process (double* low, double* high, double* out, int n)
    {
        if (linear)
        {
            // LP*low + (z^-L - LP)*high = LP*(low - high) + z^-L * high : one convolution, exact reconstruction
            for (int i = 0; i < n; ++i) low[i] -= high[i];
            conv.process (low, out, n);
            for (int i = 0; i < n; ++i) out[i] += direct.process (high[i]);
        }
        else
        {
            for (int i = 0; i < n; ++i)
            {
                double l = low[i], h = high[i];
                for (int s = 0; s < nStages; ++s) { l = lp[s].process (l); h = hp[s].process (h); }
                out[i] = l + sign * h;
            }
        }
    }

private:
    bool linear = true;
    int lat = 0;
    FifoConvolver conv;
    IntDelay direct;
    Biquad lp[2], hp[2];
    int nStages = 0;
    double sign = 1.0;
};

struct CrossoverSet {
    CrossoverConfig cfg;
    std::array<MicCrossover, 3> mic;
    int latency = 0;
    void build (const CrossoverConfig& c)
    {
        cfg = c;
        for (int m = 0; m < 3; ++m) mic[(size_t) m].build (c, m);
        latency = mic[0].latency();
    }
};

// Real-time parameters (read every block, smoothed where needed).
struct MicParams {
    bool swapInputs = false;     // false: input 1 = omni, input 2 = ribbon
    double matchDb = 0;          // gain applied to the ribbon before the crossover
    bool ribbonInLows = false;   // false: omni in the lows, ribbon in the highs
    double focusDb = 0;          // capsule in the highs
    double bloomDb = 0;          // capsule in the lows
    double pan = 0;              // -1 = L, 0 = C, +1 = R
    bool analyse = false;        // measure O/R level around the crossover frequency
};

struct EngineParams {
    std::array<MicParams, 3> mic;
    double centreDelayMs = 0;    // mic 3, -10 .. +10 ms (negative = L and R delayed instead)
    std::array<double, 3> faderDb { 0, 0, 0 };          // buses L, C, R
    std::array<bool, 3> lowCutOn { false, false, false };
    std::array<double, 3> lowCutHz { 40, 40, 40 };
    double centreSendDb = -3.0;  // C bus into L and R
    double outputDb = 0;
};

class Engine {
public:
    static constexpr double kMaxDelayMs = 10.0;

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        maxN = std::max (1, maxBlock);
        for (auto* v : { &o, &r, &lo, &hi, &mono })
            for (auto& b : *v) b.assign ((size_t) maxN, 0.0);
        for (auto& b : bus) b.assign ((size_t) maxN, 0.0);
        baseDelay = (int) std::ceil (kMaxDelayMs * 1e-3 * fs) + FractionalDelay::kHalf;
        for (auto& d : delay) d.prepare (2 * baseDelay + 4);
        for (auto& s : smooth) s.prepare (fs);
        for (int i = 0; i < kNumSmoothers; ++i) smooth[(size_t) i].setImmediate (i < 6 ? 1.0 : 0.0);
        firstBlock = true;
        for (auto& a : analysers) a = {};
        lastLowCut.fill (-1);
    }

    int extraLatency() const { return baseDelay; }

    // Total plugin latency for a given crossover set.
    int totalLatency (const CrossoverSet& set) const { return set.latency + baseDelay; }

    // in: 6 channels (mic1 O,R  mic2 O,R  mic3 O,R as routed), out: 2 channels.
    void process (CrossoverSet& set, const EngineParams& p, const double* const* in, double* const* out, int n)
    {
        int done = 0;
        while (done < n)
        {
            const int k = std::min (maxN, n - done);
            const double* inPtr[6];
            for (int c = 0; c < 6; ++c) inPtr[c] = in[c] + done;
            double* outPtr[2] = { out[0] + done, out[1] + done };
            processChunk (set, p, inPtr, outPtr, k);
            done += k;
        }
    }

    // Level analyser results (dB to add to the ribbon so it matches the omni).
    struct Analyser {
        Biquad bpO, bpR;
        double eO = 0, eR = 0;
        bool active = false;
        double fc = 0;
    };
    std::array<std::atomic<double>, 3> measuredMatchDb {};
    std::array<std::atomic<bool>, 3> measureValid {};

private:
    static constexpr int kNumSmoothers = 6 + 9 + 3 + 1; // focus/bloom x3, pan gains 3x3, faders 3, centre send

    void processChunk (CrossoverSet& set, const EngineParams& p, const double* const* in, double* const* out, int n)
    {
        // smoothed targets
        for (int m = 0; m < 3; ++m)
        {
            const auto& mp = p.mic[(size_t) m];
            sm (m * 2).setTarget (dbToGain (mp.focusDb));
            sm (m * 2 + 1).setTarget (dbToGain (mp.bloomDb));
            double gL, gC, gR; panGains (mp.pan, gL, gC, gR);
            sm (6 + m * 3).setTarget (gL); sm (7 + m * 3).setTarget (gC); sm (8 + m * 3).setTarget (gR);
        }
        for (int b = 0; b < 3; ++b) sm (15 + b).setTarget (dbToGain (p.faderDb[(size_t) b]));
        sm (18).setTarget (dbToGain (p.centreSendDb));
        if (firstBlock) { snapSmoothers (p); firstBlock = false; }

        // delays: base latency on every mic, centre mic offset by the user delay
        const double dSamples = p.centreDelayMs * 1e-3 * fs;
        const double cDelay = baseDelay + std::max (0.0, dSamples);
        const double sideDelay = baseDelay + std::max (0.0, -dSamples);
        delay[0].setDelay (sideDelay);
        delay[1].setDelay (sideDelay);
        delay[2].setDelay (cDelay);

        for (auto& b : bus) std::fill (b.begin(), b.begin() + n, 0.0);

        for (int m = 0; m < 3; ++m)
        {
            const auto& mp = p.mic[(size_t) m];
            const double* oIn = in[m * 2 + (mp.swapInputs ? 1 : 0)];
            const double* rIn = in[m * 2 + (mp.swapInputs ? 0 : 1)];
            const double match = dbToGain (mp.matchDb);
            analyse (m, mp.analyse, set.cfg.fc[(size_t) m], oIn, rIn, n);

            double* low = lo[(size_t) m].data();
            double* high = hi[(size_t) m].data();
            auto& sF = sm (m * 2); auto& sB = sm (m * 2 + 1);
            for (int i = 0; i < n; ++i)
            {
                const double om = oIn[i], rb = rIn[i] * match;
                const double gF = sF.next(), gB = sB.next();
                low[i]  = (mp.ribbonInLows ? rb : om) * gB;
                high[i] = (mp.ribbonInLows ? om : rb) * gF;
            }
            double* mo = mono[(size_t) m].data();
            set.mic[(size_t) m].process (low, high, mo, n);

            auto& d = delay[(size_t) m];
            auto& pL = sm (6 + m * 3); auto& pC = sm (7 + m * 3); auto& pR = sm (8 + m * 3);
            for (int i = 0; i < n; ++i)
            {
                const double x = d.process (mo[i]);
                bus[0][(size_t) i] += x * pL.next();
                bus[1][(size_t) i] += x * pC.next();
                bus[2][(size_t) i] += x * pR.next();
            }
        }

        // bus faders + low cut
        for (int b = 0; b < 3; ++b)
        {
            const double hz = p.lowCutHz[(size_t) b];
            if (hz != lastLowCut[(size_t) b])
            {
                const auto st = lowCut[(size_t) b];
                lowCut[(size_t) b] = Biquad::highpass1 (hz, fs);
                lowCut[(size_t) b].z1 = st.z1; lowCut[(size_t) b].z2 = st.z2;
                lastLowCut[(size_t) b] = hz;
            }
            auto& f = sm (15 + b);
            double* x = bus[(size_t) b].data();
            for (int i = 0; i < n; ++i)
            {
                double v = x[i] * f.next();
                const double hpv = lowCut[(size_t) b].process (v);   // keep state running so switching is clean
                x[i] = p.lowCutOn[(size_t) b] ? hpv : v;
            }
        }

        const double outG = dbToGain (p.outputDb);
        auto& cs = sm (18);
        for (int i = 0; i < n; ++i)
        {
            const double c = bus[1][(size_t) i] * cs.next();
            out[0][i] = (bus[0][(size_t) i] + c) * outG;
            out[1][i] = (bus[2][(size_t) i] + c) * outG;
        }
    }

    void snapSmoothers (const EngineParams& p)
    {
        for (int m = 0; m < 3; ++m)
        {
            const auto& mp = p.mic[(size_t) m];
            sm (m * 2).setImmediate (dbToGain (mp.focusDb));
            sm (m * 2 + 1).setImmediate (dbToGain (mp.bloomDb));
            double gL, gC, gR; panGains (mp.pan, gL, gC, gR);
            sm (6 + m * 3).setImmediate (gL); sm (7 + m * 3).setImmediate (gC); sm (8 + m * 3).setImmediate (gR);
        }
        for (int b = 0; b < 3; ++b) sm (15 + b).setImmediate (dbToGain (p.faderDb[(size_t) b]));
        sm (18).setImmediate (dbToGain (p.centreSendDb));
    }

    void analyse (int m, bool on, double fc, const double* oIn, const double* rIn, int n)
    {
        auto& a = analysers[(size_t) m];
        if (on && ! a.active)
        {
            a = {};
            a.active = true; a.fc = fc;
            a.bpO = Biquad::bandpass (fc, fs, std::sqrt (2.0));   // about one octave wide
            a.bpR = a.bpO;
        }
        if (! on) { a.active = false; return; }
        for (int i = 0; i < n; ++i)
        {
            const double xo = a.bpO.process (oIn[i]), xr = a.bpR.process (rIn[i]);
            a.eO += xo * xo; a.eR += xr * xr;
        }
        if (a.eO > 1e-20 && a.eR > 1e-20)
        {
            measuredMatchDb[(size_t) m].store (10.0 * std::log10 (a.eO / a.eR));
            measureValid[(size_t) m].store (true);
        }
    }

public:
    // 3-way pan, constant power between neighbours: -1 = L, 0 = C, +1 = R.
    static void panGains (double p, double& gL, double& gC, double& gR)
    {
        p = std::clamp (p, -1.0, 1.0);
        if (p <= 0) { const double t = (p + 1) * kPi / 2; gL = std::cos (t); gC = std::sin (t); gR = 0; }
        else        { const double t = p * kPi / 2;       gC = std::cos (t); gR = std::sin (t); gL = 0; }
        if (std::abs (gL) < 1e-15) gL = 0;
        if (std::abs (gC) < 1e-15) gC = 0;
        if (std::abs (gR) < 1e-15) gR = 0;
    }

private:
    Smoother& sm (int i) { return smooth[(size_t) i]; }

    double fs = 48000;
    int maxN = 512, baseDelay = 0;
    bool firstBlock = true;
    std::array<std::vector<double>, 3> o, r, lo, hi, mono, bus;
    std::array<FractionalDelay, 3> delay;
    std::array<Smoother, kNumSmoothers> smooth;
    std::array<Biquad, 3> lowCut;
    std::array<double, 3> lastLowCut { -1, -1, -1 };
    std::array<Analyser, 3> analysers;
};

} // namespace dab
