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

constexpr int kMaxSpots = 16;                 // EN-MS spot mics (ribbon + cardioid)
constexpr int kPanCh = 6 + 2 * kMaxSpots;     // pan channels: 3 AB mics, 3 AER, spots, spots' right side (stereo M/S)
constexpr int kNumIn = 9 + 2 * kMaxSpots;     // engine inputs: 6 AB (X, Y), 3 AER, spots (cardio, ribbon)

// Everything that requires rebuilding filters (done off the audio thread).
struct CrossoverConfig {
    double fs = 48000;
    bool linearPhase = true;
    bool exportQuality = false;
    Slope slope = Slope::dB12;
    std::array<double, 3> fc { 700, 700, 700 };
    bool aerEqLinear = false;
    std::array<double, kMaxSpots> spotFc = [] { std::array<double, kMaxSpots> a {}; a.fill (700); return a; }();

    bool operator== (const CrossoverConfig& o) const
    {
        return fs == o.fs && linearPhase == o.linearPhase && exportQuality == o.exportQuality
            && slope == o.slope && fc == o.fc && aerEqLinear == o.aerEqLinear && spotFc == o.spotFc;
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
        const double fc = cfg.fc[(size_t) mic];
        build (cfg, fc, cfg.linearPhase ? designLinearPhaseLowpass (fc, cfg.fs, (int) cfg.slope, firLength (cfg.fs, cfg.exportQuality))
                                        : std::vector<double>());
    }

    // lpFir: the linear-phase low-pass for this fc (shared between channels with the same frequency).
    void build (const CrossoverConfig& cfg, double fc, const std::vector<double>& lpFir)
    {
        linear = cfg.linearPhase;
        const double fs = cfg.fs;
        if (linear)
        {
            const int N = (int) lpFir.size();
            conv.prepare (lpFir, convBlock (fs, N));
            lat = convBlock (fs, N) + (N - 1) / 2;
            direct.prepare (lat);
            bypassD.prepare (lat);
            bypassR_.prepare (lat);
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

    // No crossover: same latency as the crossover, no filtering (EN-MS spots in M/S mode).
    void bypass (double* x, int n) { if (linear) for (int i = 0; i < n; ++i) x[i] = bypassD.process (x[i]); }
    void bypassR (double* x, int n) { if (linear) for (int i = 0; i < n; ++i) x[i] = bypassR_.process (x[i]); }

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
    IntDelay direct, bypassD, bypassR_;
    Biquad lp[2], hp[2];
    int nStages = 0;
    double sign = 1.0;
};

struct CrossoverSet {
    CrossoverConfig cfg;
    std::array<MicCrossover, 3> mic;
    std::array<MicCrossover, kMaxSpots> spot;
    // pan stage: 0..2 Delta AB mics, 3..5 Delta AER channels, 6.. EN-MS spots
    std::array<FifoConvolver, kPanCh> hilbert, splitLow, splitLowH;
    std::array<IntDelay, kPanCh> hilbertDirect;
    std::array<IntDelay, 3> abPad, aerPad;     // align AB crossover and AER EQ latencies
    std::array<IntDelay, kMaxSpots> spotPad, spotPadR;
    int core = 0, latency = 0;
    void build (const CrossoverConfig& c)
    {
        cfg = c;
        // one low-pass design per distinct frequency
        std::vector<std::pair<double, std::vector<double>>> lps;
        auto lpFor = [&] (double fc) -> const std::vector<double>& {
            for (auto& e : lps) if (e.first == fc) return e.second;
            lps.emplace_back (fc, c.linearPhase ? designLinearPhaseLowpass (fc, c.fs, (int) c.slope, firLength (c.fs, c.exportQuality))
                                                : std::vector<double>());
            return lps.back().second;
        };
        for (int m = 0; m < 3; ++m) mic[(size_t) m].build (c, c.fc[(size_t) m], lpFor (c.fc[(size_t) m]));
        for (int k = 0; k < kMaxSpots; ++k) spot[(size_t) k].build (c, c.spotFc[(size_t) k], lpFor (c.spotFc[(size_t) k]));
        const int HN = hilbertLength (c.fs, c.exportQuality);
        const auto h = designHilbert (HN);
        const auto lp = designSplitLowpass (kHfPanSplitHz, c.fs, 2, HN);
        const auto hlp = designHilbertLowpass (kHfPanSplitHz, c.fs, 2, HN);
        const int hb = convBlock (c.fs, HN);
        const int hl = hb + ((int) h.size() - 1) / 2;
        for (int k = 0; k < kPanCh; ++k)
        {
            hilbert[(size_t) k].prepare (h, hb);
            splitLow[(size_t) k].prepare (lp, hb);
            splitLowH[(size_t) k].prepare (hlp, hb);
            hilbertDirect[(size_t) k].prepare (hl);
        }
        const int abLat = mic[0].latency();
        const int aerLat = aerEqLatency (c);
        core = std::max (abLat, aerLat);
        for (auto& d : abPad) d.prepare (core - abLat);
        for (auto& d : aerPad) d.prepare (core - aerLat);
        for (auto& d : spotPad) d.prepare (core - abLat);
        for (auto& d : spotPadR) d.prepare (core - abLat);   // spots use the same crossover type as the AB mics
        latency = core + hl;
    }
    static int aerEqLatency (const CrossoverConfig& c)
    {
        const int N = firLength (c.fs, c.exportQuality);
        return c.aerEqLinear ? convBlock (c.fs, N) + (N - 1) / 2 : 0;
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
    double levelDb = 0;          // mic output level (after the sum, before the pan)
};

struct AerParams : PanParams {
    AerEqParams eq;
    double delayMs = 0;          // -10 .. +100 ms
    double faderDb = 0;
};

// EN-MS spot: cardioid (Mid) + ribbon figure-8 aimed sideways (Side).
struct SpotParams : PanParams {
    bool crossover = false;      // false: M/S (M + S summed, the ribbon polarity picks the side); true: crossover as in Delta AB
    double matchDb = 0;          // ribbon gain before M/S or crossover
    bool ribbonInLows = false;   // crossover mode
    bool ribbonInvert = false;   // M/S mode: S polarity, aims the virtual mic to the other side
    bool stereo = false;         // M/S mode: false = M + S summed (mono), true = decoded stereo (M + S, M - S)
    double width = 0.5;          // stereo: the two sides sit at pan -+ width (on the -1 .. +1 scale)
    double cardioDb = 0, ribbonDb = 0;   // level of each branch after M/S or crossover
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
    std::array<SpotParams, kMaxSpots> spot;
    int numSpots = 8;
    double spotsOutDb = 0;
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
    // The phase cue needs the Hilbert signal, the HF cue needs the low band; both cost a convolution each.
    bool needsHilbert() const { for (auto& s : si) if (s.current() != 0.0 || s.target() != 0.0) return true; return false; }
    bool needsLowBand() const
    {
        for (int b = 0; b < 3; ++b)
            if (g[(size_t) b].current() != gl[(size_t) b].current() || g[(size_t) b].target() != gl[(size_t) b].target()) return true;
        return false;
    }

    // full / hFull: signal and its Hilbert; low / hLow: band below 700 Hz and its Hilbert.
    void process (const double* full, const double* hFull, const double* low, const double* hLow, double* const* dst, int n)
    {
        for (int b = 0; b < 3; ++b)
        {
            auto& G = g[(size_t) b]; auto& GL = gl[(size_t) b]; auto& C = co[(size_t) b]; auto& S = si[(size_t) b];
            auto& D = pd[(size_t) b];
            double* out = dst[b];
            if (G.isSilent() && GL.isSilent())   // this bus gets nothing: keep the delay line fed with the zeros it would get
            {
                D.pushSilence (n);
                for (int i = 0; i < n; ++i) { C.next(); S.next(); }
                continue;
            }
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
        spotHi.assign ((size_t) maxN, 0.0);
        for (auto& b : flowTmp) b.assign ((size_t) maxN, 0.0);
        cSend.assign ((size_t) maxN, 0.0);
        for (auto& b : bus) b.assign ((size_t) maxN, 0.0);
        baseDelay = (int) std::ceil (kMaxDelayMs * 1e-3 * fs) + FractionalDelay::kHalf;
        for (auto& d : delay) d.prepare (2 * baseDelay + 4);
        for (auto& d : aerDelay) d.prepare (baseDelay + (int) std::ceil (kMaxAerDelayMs * 1e-3 * fs) + 4);
        for (auto& d : spotDelayR) d.prepare (baseDelay + (int) std::ceil (kMaxAerDelayMs * 1e-3 * fs) + 4);
        for (auto& d : spotDelay) d.prepare (baseDelay + (int) std::ceil (kMaxAerDelayMs * 1e-3 * fs) + 4);
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

    // in (kNumIn): 6 AB channels (mic1 X,Y  mic2 X,Y  mic3 X,Y), 3 AER channels (L, C, R), then per spot cardio, ribbon.
    // out: main stereo; aerOut / spotsOut: separate stereo outputs, or nullptr to mix into the main output.
    // eq: the 3 linear-phase AER EQ filters (used when set.cfg.aerEqLinear; entries may be nullptr).
    // flows (optional, 2 * kPanCh pointers, entries may be nullptr): each channel's own stereo output after its pan
    // (centre folded into L and R with the centre send), before any bus processing, for summing in the DAW.
    // Channel order: AB mic 1..3, AER G C D, spots.
    void process (CrossoverSet& set, AerEqFilter* const* eq, const EngineParams& p, const double* const* in,
                  double* const* out, double* const* aerOut, double* const* spotsOut, int n,
                  double* const* flows = nullptr)
    {
        int done = 0;
        while (done < n)
        {
            const int k = std::min (maxN, n - done);
            const double* inPtr[kNumIn];
            for (int c = 0; c < kNumIn; ++c) inPtr[c] = in[c] + done;
            double* outPtr[2] = { out[0] + done, out[1] + done };
            double* aerPtr[2] = { nullptr, nullptr };
            double* spPtr[2] = { nullptr, nullptr };
            if (aerOut != nullptr) { aerPtr[0] = aerOut[0] + done; aerPtr[1] = aerOut[1] + done; }
            if (spotsOut != nullptr) { spPtr[0] = spotsOut[0] + done; spPtr[1] = spotsOut[1] + done; }
            for (int c = 0; c < 2 * kPanCh; ++c)
            {
                flowPtr[(size_t) c] = flows != nullptr && flows[c] != nullptr ? flows[c] + done : nullptr;
                if (flowPtr[(size_t) c] != nullptr) std::fill (flowPtr[(size_t) c], flowPtr[(size_t) c] + k, 0.0);   // flows add up
            }
            processChunk (set, eq, p, inPtr, outPtr, aerOut != nullptr ? aerPtr : nullptr, spotsOut != nullptr ? spPtr : nullptr, k);
            done += k;
        }
    }

    // AB-only convenience (AER inputs silent, AER mixed into main).
    void process (CrossoverSet& set, const EngineParams& p, const double* const* in, double* const* out, int n)
    {
        if (silence.size() < (size_t) n) silence.assign ((size_t) n, 0.0);
        const double* all[kNumIn];
        for (int c = 0; c < kNumIn; ++c) all[c] = c < 6 ? in[c] : silence.data();
        process (set, nullptr, p, all, out, nullptr, nullptr, n);
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
    static constexpr int kSpotG = 13;      // spot cardio / ribbon levels: 2 per spot
    static constexpr int kSpotFader = kSpotG + 2 * kMaxSpots;
    static constexpr int kMicLevel = kSpotFader + kMaxSpots;   // AB mic output levels: 3
    static constexpr int kNumSmoothers = kMicLevel + 3;

    void processChunk (CrossoverSet& set, AerEqFilter* const* eq, const EngineParams& p, const double* const* in,
                       double* const* out, double* const* aerOut, double* const* spotsOut, int n)
    {
        // smoothed targets
        auto setT = [&] (int i, double v) { if (firstBlock) sm (i).setImmediate (v); else sm (i).setTarget (v); };
        for (int m = 0; m < 3; ++m)
        {
            const auto& mp = p.mic[(size_t) m];
            setT (kFB + m * 2, dbToGain (mp.focusDb));
            setT (kFB + m * 2 + 1, dbToGain (mp.bloomDb));
            setT (kMicLevel + m, dbToGain (mp.levelDb));
            pan[(size_t) m].setTargets (panTargets (mp, p.panLawDb, p.panDelayMaxMs));
            delay[(size_t) m].setDelay (baseDelay + std::clamp (p.delayMs[(size_t) m], -kMaxDelayMs, kMaxDelayMs) * 1e-3 * fs);

            const auto& ap = p.aer[(size_t) m];
            setT (kAerFader + m, dbToGain (ap.faderDb));
            pan[(size_t) (3 + m)].setTargets (panTargets (ap, p.panLawDb, p.panDelayMaxMs));
            aerDelay[(size_t) m].setDelay (baseDelay + std::clamp (ap.delayMs, -kMaxDelayMs, kMaxAerDelayMs) * 1e-3 * fs);
        }
        const int numSpots = std::clamp (p.numSpots, 0, kMaxSpots);
        for (int k = 0; k < numSpots; ++k)
        {
            const auto& sp = p.spot[(size_t) k];
            setT (kSpotG + 2 * k, dbToGain (sp.cardioDb));
            setT (kSpotG + 2 * k + 1, dbToGain (sp.ribbonDb) * (! sp.crossover && sp.ribbonInvert ? -1.0 : 1.0));
            setT (kSpotFader + k, dbToGain (sp.faderDb));
            const bool st = sp.stereo && ! sp.crossover;
            PanParams pl = sp, pr = sp;
            if (st) { pl.pan = std::clamp (sp.pan - sp.width, -1.0, 1.0); pr.pan = std::clamp (sp.pan + sp.width, -1.0, 1.0); }
            pan[(size_t) (6 + k)].setTargets (panTargets (pl, p.panLawDb, p.panDelayMaxMs));
            pan[(size_t) (6 + kMaxSpots + k)].setTargets (panTargets (pr, p.panLawDb, p.panDelayMaxMs));
            const double dl = baseDelay + std::clamp (sp.delayMs, -kMaxDelayMs, kMaxAerDelayMs) * 1e-3 * fs;
            spotDelay[(size_t) k].setDelay (dl);
            spotDelayR[(size_t) k].setDelay (dl);
        }
        for (int b = 0; b < 3; ++b) setT (kFader + b, dbToGain (p.faderDb[(size_t) b]));
        setT (kCSend, dbToGain (p.centreSendDb));
        firstBlock = false;
        { auto& cs = sm (kCSend); for (int i = 0; i < n; ++i) cSend[(size_t) i] = cs.next(); }

        for (auto& b : bus) std::fill (b.begin(), b.begin() + n, 0.0);
        double* abBus[3]  = { bus[0].data(), bus[1].data(), bus[2].data() };
        double* aerBus[3] = { bus[3].data(), bus[4].data(), bus[5].data() };
        double* spotBus[3] = { bus[6].data(), bus[7].data(), bus[8].data() };

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
            auto& lv = sm (kMicLevel + m);
            for (int i = 0; i < n; ++i) mo[i] = d.process (pad.process (mo[i])) * lv.next();
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

        // ---- EN-MS spots (spots beyond numSpots are off and cost nothing)
        for (int k = 0; k < numSpots; ++k)
        {
            const auto& sp = p.spot[(size_t) k];
            const double* cIn = in[9 + 2 * k];
            const double* rIn = in[10 + 2 * k];
            const double match = dbToGain (sp.matchDb);
            double* mo = mono[(size_t) (6 + k)].data();
            auto& gC = sm (kSpotG + 2 * k); auto& gR = sm (kSpotG + 2 * k + 1);
            if (sp.crossover)
            {
                double* low = lo[0].data();     // AB scratch, free here
                double* high = spotHi.data();
                for (int i = 0; i < n; ++i)
                {
                    const double c = cIn[i] * gC.next(), r = rIn[i] * match * gR.next();
                    low[i]  = sp.ribbonInLows ? r : c;
                    high[i] = sp.ribbonInLows ? c : r;
                }
                set.spot[(size_t) k].process (low, high, mo, n);
            }
            else if (! sp.stereo)
            {
                for (int i = 0; i < n; ++i) mo[i] = cIn[i] * gC.next() + rIn[i] * match * gR.next();
                set.spot[(size_t) k].bypass (mo, n);
            }
            else
            {
                // decoded M/S: left = M + S, right = M - S, each panned as its own source
                double* moR = mono[(size_t) (6 + kMaxSpots + k)].data();
                for (int i = 0; i < n; ++i)
                {
                    const double m = cIn[i] * gC.next(), sd = rIn[i] * match * gR.next();
                    mo[i] = m + sd; moR[i] = m - sd;
                }
                set.spot[(size_t) k].bypass (mo, n);
                set.spot[(size_t) k].bypassR (moR, n);
            }
            const bool st = sp.stereo && ! sp.crossover;
            auto& pad = set.spotPad[(size_t) k];
            auto& d = spotDelay[(size_t) k];
            auto& f = sm (kSpotFader + k);
            if (st)
            {
                double* moR = mono[(size_t) (6 + kMaxSpots + k)].data();
                auto& padR = set.spotPadR[(size_t) k];
                auto& dR = spotDelayR[(size_t) k];
                for (int i = 0; i < n; ++i)
                {
                    const double g = f.next();
                    mo[i] = d.process (pad.process (mo[i])) * g;
                    moR[i] = dR.process (padR.process (moR[i])) * g;
                }
                panStage (set, 6 + kMaxSpots + k, spotBus, n);
            }
            else
                for (int i = 0; i < n; ++i) mo[i] = d.process (pad.process (mo[i])) * f.next();
            panStage (set, 6 + k, spotBus, n);
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
        const double outG = dbToGain (p.outputDb), aerG = dbToGain (p.aerOutDb), spG = dbToGain (p.spotsOutDb);
        for (int i = 0; i < n; ++i)
        {
            const double k = cSend[(size_t) i];
            const double c = bus[1][(size_t) i] * k, ca = bus[4][(size_t) i] * k;
            const double aL = (bus[3][(size_t) i] + ca) * aerG, aR = (bus[5][(size_t) i] + ca) * aerG;
            double L = bus[0][(size_t) i] + c, R = bus[2][(size_t) i] + c;
            if (aerOut != nullptr) { aerOut[0][i] = aL; aerOut[1][i] = aR; }
            else { L += aL; R += aR; }
            const double cs2 = bus[7][(size_t) i] * k;
            const double sL = (bus[6][(size_t) i] + cs2) * spG, sR = (bus[8][(size_t) i] + cs2) * spG;
            if (spotsOut != nullptr) { spotsOut[0][i] = sL; spotsOut[1][i] = sR; }
            else { L += sL; R += sR; }
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
        // Convolutions whose result is multiplied by zero are skipped (and cleared, so they restart clean).
        auto run = [&] (FifoConvolver& c, bool need, bool& was, double* dst)
        {
            if (need) { if (! was) c.reset(); c.process (mo, dst, n); }
            else c.skip (dst, n);
            was = need;
        };
        const bool needH = pan[(size_t) k].needsHilbert(), needL = pan[(size_t) k].needsLowBand();
        run (set.hilbert[(size_t) k], needH, usedH[(size_t) k], h);
        run (set.splitLow[(size_t) k], needL, usedL[(size_t) k], lb);
        run (set.splitLowH[(size_t) k], needL && needH, usedLH[(size_t) k], hlb);
        auto& hd = set.hilbertDirect[(size_t) k];
        for (int i = 0; i < n; ++i) mo[i] = hd.process (mo[i]);
        double* const* fl = flowPtr.data() + 2 * k;
        if (fl[0] != nullptr && fl[1] != nullptr)
        {
            double* t[3] = { flowTmp[0].data(), flowTmp[1].data(), flowTmp[2].data() };
            for (int b = 0; b < 3; ++b) std::fill (t[b], t[b] + n, 0.0);
            pan[(size_t) k].process (mo, h, lb, hlb, t, n);
            for (int b = 0; b < 3; ++b) for (int i = 0; i < n; ++i) dst[b][i] += t[b][i];
            for (int i = 0; i < n; ++i)
            {
                const double c = t[1][i] * cSend[(size_t) i];
                fl[0][i] += t[0][i] + c;
                fl[1][i] += t[2][i] + c;
            }
        }
        else
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
    std::array<std::vector<double>, kPanCh> mono, hx, lowBand, lowBandH;
    std::array<std::vector<double>, 9> bus;   // AB L C R, AER L C R, spots L C R
    std::vector<double> spotHi;
    std::array<FractionalDelay, 3> delay, aerDelay;
    std::array<FractionalDelay, kMaxSpots> spotDelay, spotDelayR;
    std::array<PanChannel, kPanCh> pan;
    std::array<bool, kPanCh> usedH {}, usedL {}, usedLH {};
    std::array<double*, 2 * kPanCh> flowPtr {};
    std::array<std::vector<double>, 3> flowTmp;
    std::vector<double> cSend;   // smoothed centre send, per sample of the chunk
    std::array<Smoother, kNumSmoothers> smooth;
    std::array<Biquad, 3> lowCut;
    std::array<double, 3> lastLowCut { -1, -1, -1 };
    std::array<Analyser, 3> analysers;
    std::array<MinEq, 3> aerMinEq;
    std::vector<double> silence;
};

} // namespace dab
