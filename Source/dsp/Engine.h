#pragma once
// Delta AB engine: 3 hybrid mics (omni O + ribbon R), mini Decca triangle.
//   per mic : routing -> O/R level match -> crossover (switch: which capsule takes the lows)
//             -> Bloom (low capsule) / Focus (high capsule) -> mono sum -> delay -> pan to L/C/R
//   buses   : faders L, C, R with 6 dB/oct low cut -> stereo sum (C sent to L and R) -> output
// Mic 1 = left, mic 2 = right, mic 3 = centre (the only mic whose delay is user-set).
#include "Convolver.h"
#include "Filters.h"
#include "AerEq.h"
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
    bool aerEqLinear = false;

    bool operator== (const CrossoverConfig& o) const
    {
        return fs == o.fs && linearPhase == o.linearPhase && exportQuality == o.exportQuality
            && slope == o.slope && fc == o.fc && aerEqLinear == o.aerEqLinear;
    }
    bool operator!= (const CrossoverConfig& o) const { return ! (*this == o); }
};

// FIR length scales with the sample rate, so the time span stays the same.
inline int firLength (double fs, bool exportQuality)
{
    const double base = exportQuality ? 65536.0 : 4096.0;   // at 48 kHz
    return (1 << ceilLog2 ((int) std::lround (base * fs / 48000.0))) - 1;
}


// Hilbert length for phase panning: about 43 ms (listen) or 340 ms (export) of filter.
constexpr double kHfPanSplitHz = 700.0;   // HF pan: below this the image stays nearer the middle
inline int hilbertLength (double fs, bool exportQuality)
{
    const double base = exportQuality ? 16384.0 : 2048.0;   // at 48 kHz
    return (1 << ceilLog2 ((int) std::lround (base * fs / 48000.0))) - 1;
}

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
            conv.prepare (h, convBlock (fs));
            lat = convBlock (fs) + (N - 1) / 2;
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
    // pan stage for 6 channels: 0..2 Delta AB mics, 3..5 Delta AER channels
    std::array<FifoConvolver, 6> hilbert, splitLow, splitLowH;
    std::array<IntDelay, 6> hilbertDirect;
    std::array<IntDelay, 3> abPad, aerPad;     // align AB crossover and AER EQ latencies
    int core = 0, latency = 0;
    void build (const CrossoverConfig& c)
    {
        cfg = c;
        for (int m = 0; m < 3; ++m) mic[(size_t) m].build (c, m);
        const int HN = hilbertLength (c.fs, c.exportQuality);
        const auto h = designHilbert (HN);
        const auto lp = designSplitLowpass (kHfPanSplitHz, c.fs, 2, HN);
        const auto hlp = designHilbertLowpass (kHfPanSplitHz, c.fs, 2, HN);
        const int hl = convBlock (c.fs) + ((int) h.size() - 1) / 2;
        for (int k = 0; k < 6; ++k)
        {
            hilbert[(size_t) k].prepare (h, convBlock (c.fs));
            splitLow[(size_t) k].prepare (lp, convBlock (c.fs));
            splitLowH[(size_t) k].prepare (hlp, convBlock (c.fs));
            hilbertDirect[(size_t) k].prepare (hl);
        }
        const int abLat = mic[0].latency();
        const int aerLat = aerEqLatency (c);
        core = std::max (abLat, aerLat);
        for (auto& d : abPad) d.prepare (core - abLat);
        for (auto& d : aerPad) d.prepare (core - aerLat);
        latency = core + hl;
    }
    static int aerEqLatency (const CrossoverConfig& c)
    {
        return c.aerEqLinear ? convBlock (c.fs) + (firLength (c.fs, c.exportQuality) - 1) / 2 : 0;
    }
};

// Real-time parameters (read every block, smoothed where needed).
struct PanParams {
    double pan = 0;              // -1 = L, 0 = C, +1 = R
    double panLevel = 1.0;       // 0..1 share of level panning
    double panDelay = 0.0;       // 0..1 share of time panning (far bus delayed)
    double panPhase = 0.0;       // 0..1 share of phase panning (constant phase offset, no filtering)
    double panHF = 0.0;          // 0..1 HF pan: the band below 700 Hz is panned less (linear-phase split, exact sum)
};

struct MicParams : PanParams {
    bool swapInputs = false;     // false: input 1 = X, input 2 = Y
    double matchDb = 0;          // gain applied to Y before the crossover
    bool ribbonInLows = false;   // true: Y in the lows, X in the highs
    double focusDb = 0;          // source in the highs
    double bloomDb = 0;          // source in the lows
    bool analyse = false;        // measure X/Y level around the crossover frequency
};

struct AerParams : PanParams {
    AerEqParams eq;
    double delayMs = 0;          // -10 .. +100 ms
    double faderDb = 0;
};

struct EngineParams {
    std::array<MicParams, 3> mic;
    std::array<double, 3> delayMs { 0, 0, 0 };   // per mic, -10 .. +10 ms around a fixed 10 ms base
    std::array<double, 3> faderDb { 0, 0, 0 };          // buses L, C, R
    std::array<bool, 3> lowCutOn { false, false, false };
    std::array<double, 3> lowCutHz { 40, 40, 40 };
    double centreSendDb = -3.0;  // C bus into L and R
    double panLawDb = -3.0;      // level at the middle between two buses: -3, -4.5 or -6 dB
    double panDelayMaxMs = 0.7;  // max inter-bus delay: 0.7 .. 11.2 ms, or 30 ms (Haas)
    double outputDb = 0;
    std::array<AerParams, 3> aer;          // Delta AER channels: L, C, R
    double aerOutDb = 0;
};

// Pan: per bus gains (full band and low band), constant phase rotation, pure delay.
class PanChannel {
public:
    void prepare (double sampleRate, int maxPanSamples)
    {
        fs = sampleRate;
        for (auto& d : pd) d.prepare (maxPanSamples);
        for (auto* a : { &g, &gl, &co, &si }) for (auto& s : *a) s.prepare (fs);
        first = true;
    }
    template <typename Targets>
    void setTargets (const Targets& t)
    {
        for (int b = 0; b < 3; ++b)
        {
            if (first)
            {
                g[(size_t) b].setImmediate (t.g[b]); gl[(size_t) b].setImmediate (t.gLow[b]);
                co[(size_t) b].setImmediate (std::cos (t.theta[b])); si[(size_t) b].setImmediate (std::sin (t.theta[b]));
            }
            else
            {
                g[(size_t) b].setTarget (t.g[b]); gl[(size_t) b].setTarget (t.gLow[b]);
                co[(size_t) b].setTarget (std::cos (t.theta[b])); si[(size_t) b].setTarget (std::sin (t.theta[b]));
            }
            pd[(size_t) b].setDelay (FractionalDelay::kHalf + t.tauMs[b] * 1e-3 * fs);
        }
        first = false;
    }
    // full / hFull: signal and its Hilbert; low / hLow: band below 700 Hz and its Hilbert.
    void process (const double* full, const double* hFull, const double* low, const double* hLow, double* const* dst, int n)
    {
        for (int b = 0; b < 3; ++b)
        {
            auto& G = g[(size_t) b]; auto& GL = gl[(size_t) b]; auto& C = co[(size_t) b]; auto& S = si[(size_t) b];
            auto& D = pd[(size_t) b];
            double* out = dst[b];
            for (int i = 0; i < n; ++i)
            {
                const double gH = G.next(), gL = GL.next();
                const double a  = gH * full[i]  + (gL - gH) * low[i];
                const double ha = gH * hFull[i] + (gL - gH) * hLow[i];
                out[i] += D.process (C.next() * a - S.next() * ha);   // phase advance by theta
            }
        }
    }
private:
    double fs = 48000;
    bool first = true;
    std::array<FractionalDelay, 3> pd;
    std::array<Smoother, 3> g, gl, co, si;
};

class Engine {
public:
    static constexpr double kMaxDelayMs = 10.0;
    static constexpr double kMaxAerDelayMs = 100.0;
    static constexpr double kMaxPanDelayMs = 30.0;  // Haas range

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        maxN = std::max (1, maxBlock);
        for (auto* v : { &lo, &hi })
            for (auto& b : *v) b.assign ((size_t) maxN, 0.0);
        for (auto* v : { &mono, &hx, &lowBand, &lowBandH })
            for (auto& b : *v) b.assign ((size_t) maxN, 0.0);
        for (auto& b : bus) b.assign ((size_t) maxN, 0.0);
        baseDelay = (int) std::ceil (kMaxDelayMs * 1e-3 * fs) + FractionalDelay::kHalf;
        for (auto& d : delay) d.prepare (2 * baseDelay + 4);
        for (auto& d : aerDelay) d.prepare (baseDelay + (int) std::ceil (kMaxAerDelayMs * 1e-3 * fs) + 4);
        const int maxPan = FractionalDelay::kHalf + (int) std::ceil (kMaxPanDelayMs * 1e-3 * fs) + 4;
        for (auto& pc : pan) pc.prepare (fs, maxPan);
        for (auto& s : smooth) s.prepare (fs);
        firstBlock = true;
        for (auto& a : analysers) a = {};
        lastLowCut.fill (-1);
        for (auto& e : aerMinEq) { e.params = {}; e.valid = false; e.sec.clear(); e.sec.reserve (16); e.tmp.reserve (16); }
    }

    int extraLatency() const { return baseDelay; }

    // Total plugin latency for a given crossover set.
    int totalLatency (const CrossoverSet& set) const { return set.latency + baseDelay + FractionalDelay::kHalf; }

    // in: 6 AB channels (mic1 X,Y  mic2 X,Y  mic3 X,Y) then 3 AER channels (L, C, R).
    // out: main stereo; aerOut: separate AER stereo, or nullptr to mix the AER into the main output.
    // eq: the 3 linear-phase AER EQ filters (used when set.cfg.aerEqLinear; entries may be nullptr).
    void process (CrossoverSet& set, AerEqFilter* const* eq, const EngineParams& p, const double* const* in,
                  double* const* out, double* const* aerOut, int n)
    {
        int done = 0;
        while (done < n)
        {
            const int k = std::min (maxN, n - done);
            const double* inPtr[9];
            for (int c = 0; c < 9; ++c) inPtr[c] = in[c] + done;
            double* outPtr[2] = { out[0] + done, out[1] + done };
            double* aerPtr[2] = { nullptr, nullptr };
            if (aerOut != nullptr) { aerPtr[0] = aerOut[0] + done; aerPtr[1] = aerOut[1] + done; }
            processChunk (set, eq, p, inPtr, outPtr, aerOut != nullptr ? aerPtr : nullptr, k);
            done += k;
        }
    }

    // AB-only convenience (AER inputs silent, AER mixed into main).
    void process (CrossoverSet& set, const EngineParams& p, const double* const* in, double* const* out, int n)
    {
        if (silence.size() < (size_t) n) silence.assign ((size_t) n, 0.0);
        const double* in9[9];
        for (int c = 0; c < 6; ++c) in9[c] = in[c];
        for (int c = 6; c < 9; ++c) in9[c] = silence.data();
        process (set, nullptr, p, in9, out, nullptr, n);
    }

    // Level analyser results (dB to add to Y so it matches X).
    struct Analyser {
        Biquad bpO, bpR;
        double eO = 0, eR = 0;
        bool active = false;
        double fc = 0;
    };
    std::array<std::atomic<double>, 3> measuredMatchDb {};
    std::array<std::atomic<bool>, 3> measureValid {};

private:
    // smoother indices
    static constexpr int kFB = 0;          // focus/bloom: 6
    static constexpr int kFader = 6;       // AB bus faders: 3
    static constexpr int kCSend = 9;       // centre send: 1
    static constexpr int kAerFader = 10;   // AER channel faders: 3
    static constexpr int kNumSmoothers = 13;

    void processChunk (CrossoverSet& set, AerEqFilter* const* eq, const EngineParams& p, const double* const* in,
                       double* const* out, double* const* aerOut, int n)
    {
        // smoothed targets
        auto setT = [&] (int i, double v) { if (firstBlock) sm (i).setImmediate (v); else sm (i).setTarget (v); };
        for (int m = 0; m < 3; ++m)
        {
            const auto& mp = p.mic[(size_t) m];
            setT (kFB + m * 2, dbToGain (mp.focusDb));
            setT (kFB + m * 2 + 1, dbToGain (mp.bloomDb));
            pan[(size_t) m].setTargets (panTargets (mp, p.panLawDb, p.panDelayMaxMs));
            delay[(size_t) m].setDelay (baseDelay + std::clamp (p.delayMs[(size_t) m], -kMaxDelayMs, kMaxDelayMs) * 1e-3 * fs);

            const auto& ap = p.aer[(size_t) m];
            setT (kAerFader + m, dbToGain (ap.faderDb));
            pan[(size_t) (3 + m)].setTargets (panTargets (ap, p.panLawDb, p.panDelayMaxMs));
            aerDelay[(size_t) m].setDelay (baseDelay + std::clamp (ap.delayMs, -kMaxDelayMs, kMaxAerDelayMs) * 1e-3 * fs);
        }
        for (int b = 0; b < 3; ++b) setT (kFader + b, dbToGain (p.faderDb[(size_t) b]));
        setT (kCSend, dbToGain (p.centreSendDb));
        firstBlock = false;

        for (auto& b : bus) std::fill (b.begin(), b.begin() + n, 0.0);
        double* abBus[3]  = { bus[0].data(), bus[1].data(), bus[2].data() };
        double* aerBus[3] = { bus[3].data(), bus[4].data(), bus[5].data() };

        // ---- Delta AB
        for (int m = 0; m < 3; ++m)
        {
            const auto& mp = p.mic[(size_t) m];
            const double* oIn = in[m * 2 + (mp.swapInputs ? 1 : 0)];
            const double* rIn = in[m * 2 + (mp.swapInputs ? 0 : 1)];
            const double match = dbToGain (mp.matchDb);
            analyse (m, mp.analyse, set.cfg.fc[(size_t) m], oIn, rIn, n);

            double* low = lo[(size_t) m].data();
            double* high = hi[(size_t) m].data();
            auto& sF = sm (kFB + m * 2); auto& sB = sm (kFB + m * 2 + 1);
            for (int i = 0; i < n; ++i)
            {
                const double om = oIn[i], rb = rIn[i] * match;
                const double gF = sF.next(), gB = sB.next();
                low[i]  = (mp.ribbonInLows ? rb : om) * gB;
                high[i] = (mp.ribbonInLows ? om : rb) * gF;
            }
            double* mo = mono[(size_t) m].data();
            set.mic[(size_t) m].process (low, high, mo, n);
            auto& pad = set.abPad[(size_t) m];
            auto& d = delay[(size_t) m];
            for (int i = 0; i < n; ++i) mo[i] = d.process (pad.process (mo[i]));
            panStage (set, m, abBus, n);
        }

        // ---- Delta AER
        for (int c = 0; c < 3; ++c)
        {
            const auto& ap = p.aer[(size_t) c];
            double* x = mono[(size_t) (3 + c)].data();
            const double* src = in[6 + c];
            if (set.cfg.aerEqLinear)
            {
                // a filter built for another rate or length would break the alignment: stay silent until the right one arrives
                auto* f = eq != nullptr ? eq[c] : nullptr;
                if (f != nullptr && f->fs == set.cfg.fs && f->N == firLength (set.cfg.fs, set.cfg.exportQuality))
                    f->conv.process (src, x, n);
                else
                    std::fill (x, x + n, 0.0);
            }
            else
            {
                auto& e = aerMinEq[(size_t) c];
                if (! e.valid || e.params != ap.eq)
                {
                    eqSections (ap.eq, fs, e.tmp);   // no allocation: capacity reserved in prepare
                    if (e.tmp.size() == e.sec.size()) for (size_t k = 0; k < e.sec.size(); ++k) { e.tmp[k].z1 = e.sec[k].z1; e.tmp[k].z2 = e.sec[k].z2; }
                    std::swap (e.sec, e.tmp); e.params = ap.eq; e.valid = true;
                }
                for (int i = 0; i < n; ++i)
                {
                    double v = src[i];
                    for (auto& q : e.sec) v = q.process (v);
                    x[i] = v;
                }
            }
            auto& pad = set.aerPad[(size_t) c];
            auto& d = aerDelay[(size_t) c];
            auto& f = sm (kAerFader + c);
            for (int i = 0; i < n; ++i) x[i] = d.process (pad.process (x[i])) * f.next();
            panStage (set, 3 + c, aerBus, n);
        }

        // ---- AB bus faders + low cut
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
            auto& f = sm (kFader + b);
            double* x = bus[(size_t) b].data();
            for (int i = 0; i < n; ++i)
            {
                double v = x[i] * f.next();
                const double hpv = lowCut[(size_t) b].process (v);   // keep state running so switching is clean
                x[i] = p.lowCutOn[(size_t) b] ? hpv : v;
            }
        }

        // ---- stereo sums
        const double outG = dbToGain (p.outputDb), aerG = dbToGain (p.aerOutDb);
        auto& cs = sm (kCSend);
        for (int i = 0; i < n; ++i)
        {
            const double k = cs.next();
            const double c = bus[1][(size_t) i] * k, ca = bus[4][(size_t) i] * k;
            const double aL = (bus[3][(size_t) i] + ca) * aerG, aR = (bus[5][(size_t) i] + ca) * aerG;
            double L = bus[0][(size_t) i] + c, R = bus[2][(size_t) i] + c;
            if (aerOut != nullptr) { aerOut[0][i] = aL; aerOut[1][i] = aR; }
            else { L += aL; R += aR; }
            out[0][i] = L * outG;
            out[1][i] = R * outG;
        }
    }

    // Hilbert pair + low band split, then the pan into three buses. Uses mono[k] as input.
    void panStage (CrossoverSet& set, int k, double* const* dst, int n)
    {
        double* mo = mono[(size_t) k].data();
        double* h = hx[(size_t) k].data();
        double* lb = lowBand[(size_t) k].data();
        double* hlb = lowBandH[(size_t) k].data();
        set.hilbert[(size_t) k].process (mo, h, n);
        set.splitLow[(size_t) k].process (mo, lb, n);
        set.splitLowH[(size_t) k].process (mo, hlb, n);
        auto& hd = set.hilbertDirect[(size_t) k];
        for (int i = 0; i < n; ++i) mo[i] = hd.process (mo[i]);
        pan[(size_t) k].process (mo, h, lb, hlb, dst, n);
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
    // Pan between the two neighbouring buses (L-C or C-R). Cues, each with its own amount:
    //  level : pan law gains (-3 / -4.5 / -6 dB at the middle)
    //  delay : the far bus is delayed, up to maxMs (pure delay, flat amplitude; 30 ms = Haas)
    //  phase : constant phase offset between the two buses, up to +-45 deg (FIR Hilbert, flat amplitude)
    //  HF    : the band below 700 Hz is panned less (linear-phase split, exact sum)
    // A channel sitting exactly on a bus feeds only that bus, with no delay and no phase change (HF pan aside).
    struct PanTargets { double g[3] {}; double gLow[3] {}; double theta[3] {}; double tauMs[3] {}; };

    static PanTargets panTargets (const PanParams& mp, double lawDb, double maxMs)
    {
        PanTargets t;
        const double p = std::clamp (mp.pan, -1.0, 1.0);
        const int A = p <= 0 ? 0 : 1, B = A + 1;
        const double s = p <= 0 ? 2 * p + 1 : 2 * p - 1;          // -1 = on A, +1 = on B
        const double a = std::clamp (mp.panLevel, 0.0, 1.0);
        const double x = (s < 0 ? -1.0 : 1.0) * std::pow (std::abs (s), 1.0 + 3.0 * (1.0 - a));
        const double e = lawDb / (20.0 * std::log10 (0.5));
        t.g[A] = std::pow ((1 - x) / 2, e);
        t.g[B] = std::pow ((1 + x) / 2, e);
        const double xl = x * (1.0 - std::clamp (mp.panHF, 0.0, 1.0));
        t.gLow[A] = std::pow ((1 - xl) / 2, e);
        t.gLow[B] = std::pow ((1 + xl) / 2, e);
        const double tau = std::clamp (mp.panDelay, 0.0, 1.0) * std::clamp (maxMs, 0.0, kMaxPanDelayMs) * std::abs (s);
        if (s < 0) t.tauMs[B] = tau; else t.tauMs[A] = tau;
        const double phi = -std::clamp (mp.panPhase, 0.0, 1.0) * (kPi / 4) * std::sin (kPi * s);
        t.theta[A] = phi; t.theta[B] = -phi;
        return t;
    }

private:
    Smoother& sm (int i) { return smooth[(size_t) i]; }

    struct MinEq { AerEqParams params; std::vector<Biquad> sec, tmp; bool valid = false; };

    double fs = 48000;
    int maxN = 512, baseDelay = 0;
    bool firstBlock = true;
    std::array<std::vector<double>, 3> lo, hi;
    std::array<std::vector<double>, 6> mono, hx, lowBand, lowBandH, bus;
    std::array<FractionalDelay, 3> delay, aerDelay;
    std::array<PanChannel, 6> pan;
    std::array<Smoother, kNumSmoothers> smooth;
    std::array<Biquad, 3> lowCut;
    std::array<double, 3> lastLowCut { -1, -1, -1 };
    std::array<Analyser, 3> analysers;
    std::array<MinEq, 3> aerMinEq;
    std::vector<double> silence;
};

} // namespace dab
