// Delta Comp: mastering dynamics chain, no JUCE dependency (tested on its own in tests/dsp_tests.cpp).
//
// Chain (all in double, mono or stereo):
//
//   in ── T transients (round off / accentuate) ─┬─ A upward low level ── B downward low level ── C downward double threshold (RMS / opto)
//       │      ── D downward peak (threshold = C threshold 1 + crest) ──┐
//       │                                                               ├─ + ── auto makeup ── mix ── out gain ── E limiter ── out
//       └─ Density: heavily compressed parallel path (lifts low level) ─┘
//                                                       (dry for the mix is the input itself)
//   M/S end section (before the limiter): side width, and a discreet internal tail reverb on Mid, Side
//   or both, sent mostly when the level is low (follows stage A's detector) to lengthen the tails.
//   Ambiance (ambient retrieval, K-Stereo inspired): the side signal is extracted, pre-delayed past the
//   early reflections, decorrelated, and added back to the side by an upward expander: the lower the
//   level under its threshold, the more the tails come up. Mid is untouched, so it stays mono compatible.
//   Auto mode estimates where the early reflections end (after each clear onset) and sets the pre-delay.
//
// Detection of A, B, C: K-weighted (BS.1770) RMS, in LUFS, thresholds relative to the target loudness.
// D: peak envelope in dBFS. E: look-ahead limiter, optionally oversampled 2/4/8x, true-peak or
// sample-peak detection, channel link for transients and for release separately.
// Latency is constant whatever the settings: 5 ms (max look-ahead) + 16 samples (oversampling filters).
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

namespace dcomp
{
constexpr int kMaxCh = 2;
constexpr double kPi = 3.14159265358979323846;

inline double dbToLin (double db) { return std::pow (10.0, db / 20.0); }
inline double linToDb (double x)  { return 20.0 * std::log10 (std::max (x, 1e-12)); }
inline double powToLufs (double p) { return -0.691 + 10.0 * std::log10 (std::max (p, 1e-20)); }
inline double lufsToPow (double l) { return std::pow (10.0, (l + 0.691) / 10.0); }

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double process (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void highpass (double fs, double f, double q = 0.7071)
    {
        const double w = 2 * kPi * f / fs, al = std::sin (w) / (2 * q), cw = std::cos (w), a0 = 1 + al;
        b0 = (1 + cw) / 2 / a0; b1 = -(1 + cw) / a0; b2 = b0; a1 = -2 * cw / a0; a2 = (1 - al) / a0;
    }
    // RBJ cookbook: type 1 = low shelf, 2 = high shelf, 3 = bell
    void eq (int type, double fs, double f, double gainDb, double q)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * kPi * f / fs, cw = std::cos (w), sw = std::sin (w);
        double B0, B1, B2, A0, A1, A2;
        if (type == 3)
        {
            const double al = sw / (2 * q);
            B0 = 1 + al * A; B1 = -2 * cw; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * cw; A2 = 1 - al / A;
        }
        else
        {
            const double al = sw / 2 * std::sqrt (2.0), sa = 2 * std::sqrt (A) * al;
            if (type == 1) { B0 = A * ((A + 1) - (A - 1) * cw + sa); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - sa);
                             A0 = (A + 1) + (A - 1) * cw + sa; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - sa; }
            else           { B0 = A * ((A + 1) + (A - 1) * cw + sa); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - sa);
                             A0 = (A + 1) - (A - 1) * cw + sa; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - sa; }
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    void onePoleHighpass (double fs, double f)
    {
        const double k = std::tan (kPi * f / fs);
        b0 = 1 / (1 + k); b1 = -b0; b2 = 0; a1 = (k - 1) / (k + 1); a2 = 0;
    }
};

// BS.1770 K-weighting (high shelf + RLB high-pass) for any sample rate.
struct KWeighting
{
    Biquad shelf, hp;
    void prepare (double fs)
    {
        double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        double K = std::tan (kPi * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        double a0 = 1.0 + K / Q + K * K;
        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0; shelf.b1 = 2.0 * (K * K - Vh) / a0; shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0;       shelf.a2 = (1.0 - K / Q + K * K) / a0;
        f0 = 38.13547087602444; Q = 0.5003270373238773; K = std::tan (kPi * f0 / fs);
        a0 = 1.0 + K / Q + K * K;
        hp.b0 = 1; hp.b1 = -2; hp.b2 = 1;
        hp.a1 = 2.0 * (K * K - 1.0) / a0; hp.a2 = (1.0 - K / Q + K * K) / a0;
    }
    double process (double x) { return hp.process (shelf.process (x)); }
    void reset() { shelf.reset(); hp.reset(); }
};

// 4x interpolated peak estimator (BS.1770-4 annex 2 style), delayed by kDelay base samples.
struct TruePeak
{
    static constexpr int kTaps = 48, kPhase = 4, kPerPhase = kTaps / kPhase;
    static constexpr int kDelay = 6;  // (kTaps - 1) / 2 / 4 = 5.875
    std::array<std::array<double, kPerPhase>, kPhase> h {};
    std::array<double, kPerPhase> hist {};   // hist[k] = x[n - k]
    TruePeak()
    {
        for (int p = 0; p < kPhase; ++p)
        {
            double sum = 0;
            for (int k = 0; k < kPerPhase; ++k)
            {
                const int i = k * kPhase + p;
                const double t = (i - (kTaps - 1) * 0.5) / kPhase;
                const double sinc = std::abs (t) < 1e-12 ? 1.0 : std::sin (kPi * t) / (kPi * t);
                const double w = 0.42 - 0.5 * std::cos (2 * kPi * (i + 0.5) / kTaps) + 0.08 * std::cos (4 * kPi * (i + 0.5) / kTaps);
                h[(size_t) p][(size_t) k] = sinc * w; sum += sinc * w;
            }
            for (auto& c : h[(size_t) p]) c /= sum;
        }
    }
    void reset() { hist.fill (0); }
    double process (double x)
    {
        for (int k = kPerPhase - 1; k > 0; --k) hist[(size_t) k] = hist[(size_t) (k - 1)];
        hist[0] = x;
        double m = 0;
        for (int p = 0; p < kPhase; ++p)
        {
            double acc = 0;
            for (int k = 0; k < kPerPhase; ++k) acc += h[(size_t) p][(size_t) k] * hist[(size_t) k];
            m = std::max (m, std::abs (acc));
        }
        return m;
    }
};

// Fixed-capacity building blocks: allocate in reserve(), change length without allocating.
struct Delay
{
    std::vector<double> buf; int pos = 0, len = 0;
    void reserve (int cap) { buf.assign ((size_t) std::max (1, cap), 0.0); }
    void setLength (int l) { len = std::clamp (l, 0, (int) buf.size()); reset(); }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0); pos = 0; }
    double process (double x) { if (len == 0) return x; const double y = buf[(size_t) pos]; buf[(size_t) pos] = x; pos = (pos + 1) % len; return y; }
};

struct MinHold   // sliding minimum over the last len samples (monotonic queue)
{
    std::vector<double> val; std::vector<long long> at; int head = 0, tail = 0, cap = 3, len = 1; long long n = 0;
    void reserve (int maxLen) { cap = maxLen + 2; val.assign ((size_t) cap, 1.0); at.assign ((size_t) cap, 0); }
    void setLength (int l) { len = std::clamp (l, 1, cap - 2); reset(); }
    void reset() { head = tail = 0; n = 0; }
    double process (double x)
    {
        while (tail != head && val[(size_t) ((tail - 1 + cap) % cap)] >= x) tail = (tail - 1 + cap) % cap;
        val[(size_t) tail] = x; at[(size_t) tail] = n; tail = (tail + 1) % cap;
        while (at[(size_t) head] <= n - len) head = (head + 1) % cap;
        ++n;
        return val[(size_t) head];
    }
};

struct Box       // moving average over len samples
{
    std::vector<double> buf; int pos = 0, len = 1, sinceExact = 0; double sum = 1;
    void reserve (int maxLen) { buf.assign ((size_t) std::max (1, maxLen), 1.0); }
    void setLength (int l) { len = std::clamp (l, 1, (int) buf.size()); reset(); }
    void reset() { std::fill (buf.begin(), buf.end(), 1.0); sum = len; pos = 0; }
    double process (double x)
    {
        sum += x - buf[(size_t) pos]; buf[(size_t) pos] = x; pos = (pos + 1) % len;
        if (++sinceExact > 8192) { sinceExact = 0; sum = 0; for (int i = 0; i < len; ++i) sum += buf[(size_t) i]; }
        return sum / len;
    }
};

// Linear-phase polyphase oversampler, factor 1/2/4/8. Filter length 16F+1, so up + down = exactly 16 base samples.
struct Oversampler
{
    static constexpr int kMaxF = 8, kPer = 17, kDelayBase = 16;
    int F = 1, N = 1;
    std::array<double, kPer * kMaxF> h {};
    std::array<double, kPer> xh {};
    std::array<double, kPer * kMaxF * 2> ring {}; int rpos = 0;
    void setFactor (int f)
    {
        F = std::clamp (f, 1, kMaxF); N = 16 * F + 1;
        h.fill (0);
        if (F > 1)
        {
            const double fc = 0.47 / F;   // of the high rate
            double sum = 0;
            for (int i = 0; i < N; ++i)
            {
                const double t = i - (N - 1) * 0.5;
                const double s = std::abs (t) < 1e-12 ? 2 * fc : std::sin (2 * kPi * fc * t) / (kPi * t);
                const double w = 0.42 - 0.5 * std::cos (2 * kPi * i / (N - 1)) + 0.08 * std::cos (4 * kPi * i / (N - 1));
                h[(size_t) i] = s * w; sum += s * w;
            }
            for (int i = 0; i < N; ++i) h[(size_t) i] /= sum;
        }
        reset();
    }
    void reset() { xh.fill (0); ring.fill (0); rpos = 0; }
    void up (double x, double* out)
    {
        for (int k = kPer - 1; k > 0; --k) xh[(size_t) k] = xh[(size_t) (k - 1)];
        xh[0] = x;
        for (int p = 0; p < F; ++p)
        {
            double acc = 0;
            for (int k = 0; k * F + p < N; ++k) acc += h[(size_t) (k * F + p)] * xh[(size_t) k];
            out[p] = acc * F;
        }
    }
    double down (const double* v)   // v: the F high-rate samples of this base period
    {
        const int L = kPer * kMaxF;
        for (int p = 0; p < F; ++p) { ring[(size_t) rpos] = ring[(size_t) (rpos + L)] = v[p]; rpos = (rpos + 1) % L; }
        // output aligned on the first high-rate sample of the period: newest - (F - 1)
        const int newest = (rpos - 1 + L) % L + L;
        double acc = 0;
        for (int j = 0; j < N; ++j) acc += h[(size_t) j] * ring[(size_t) (newest - (F - 1) - j)];
        return acc;
    }
};

// Ambiance side extraction: high-pass, pre-delay (past the early reflections), two allpasses.
struct AmbExtract;
// Small 4-line feedback delay network for discreet tails.
struct TailVerb
{
    std::array<std::vector<double>, 4> line; std::array<int, 4> len {}, pos {}; std::array<double, 4> g {}, lp {};
    std::vector<double> pre; int preW = 0; double preD = 0, damp = 0.5;
    double fs = 48000;
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const double ms[4] = { 29.7, 37.1, 41.1, 43.7 };
        for (size_t i = 0; i < 4; ++i) { len[i] = (int) std::lround (ms[i] * 0.001 * fs); line[i].assign ((size_t) len[i], 0.0); }
        pre.assign ((size_t) (0.1 * fs) + 2, 0.0);
        damp = std::exp (-2 * kPi * 6000.0 / fs);
        reset();
    }
    void reset() { for (auto& l : line) std::fill (l.begin(), l.end(), 0.0); pos.fill (0); lp.fill (0); std::fill (pre.begin(), pre.end(), 0.0); preW = 0; }
    void setDecay (double t60) { for (size_t i = 0; i < 4; ++i) g[i] = std::pow (10.0, -3.0 * len[i] / (std::max (0.1, t60) * fs)); }
    void setPreDelay (double ms)    // glides one sample at a time
    {
        const double want = std::clamp (ms * 0.001 * fs, 0.0, (double) pre.size() - 2);
        if (std::abs (want - preD) >= 1.0) preD += want > preD ? 1.0 : -1.0;
    }
    void process (double in, double& outA, double& outB)
    {
        const size_t P = pre.size();
        pre[(size_t) preW] = in;
        const double x = pre[((size_t) preW + P - (size_t) preD) % P];
        preW = (int) (((size_t) preW + 1) % P);
        double o[4];
        for (size_t i = 0; i < 4; ++i) { o[i] = line[i][(size_t) pos[i]]; lp[i] = o[i] + damp * (lp[i] - o[i]); }
        // Hadamard mix
        const double a = lp[0] + lp[1], b = lp[0] - lp[1], c = lp[2] + lp[3], d = lp[2] - lp[3];
        const double m[4] = { 0.5 * (a + c), 0.5 * (b + d), 0.5 * (a - c), 0.5 * (b - d) };
        for (size_t i = 0; i < 4; ++i) { line[i][(size_t) pos[i]] = x + g[i] * m[i]; pos[i] = (pos[i] + 1) % len[i]; }
        outA = 0.5 * (o[0] + o[1] - o[2] + o[3]);
        outB = 0.5 * (o[0] - o[1] + o[2] + o[3]);
    }
};

// Linear-phase Kaiser low-pass applied to gain signals (anti-aliasing of the gain modulation):
// stopband at (Nyquist - 20 kHz), -140 dB, so a gain change can never fold anything below 20 kHz.
struct GainFir
{
    std::vector<double> h; int N = 1, half = 0;
    static double i0 (double x) { double s = 1, t = 1; for (int k = 1; k < 60; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; if (t < 1e-18 * s) break; } return s; }
    // Kaiser linear-phase low-pass, 140 dB stopband from fsb, passband to fsb / 4, unity DC gain.
    // half is rounded up to a multiple of 'mult' (oversampled use needs a whole number of base samples).
    void design (double fs, double fsb, int mult = 1)
    {
        const double fp = fsb * 0.25, A = 140.0;
        const double dw = 2 * kPi * (fsb - fp) / fs, beta = 0.1102 * (A - 8.7);
        half = (int) std::ceil ((A - 8.0) / (2.285 * dw) * 0.5);
        half = ((half + mult - 1) / mult) * mult; N = 2 * half + 1;
        h.assign ((size_t) N, 0.0);
        const double fc = 0.5 * (fp + fsb) / fs; double sum = 0;
        for (int i = 0; i < N; ++i)
        {
            const double t = i - half, r = (double) t / half;
            const double sinc = t == 0 ? 2 * fc : std::sin (2 * kPi * fc * t) / (kPi * t);
            h[(size_t) i] = sinc * i0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / i0 (beta); sum += h[(size_t) i];
        }
        for (auto& v : h) v /= sum;
    }
};
struct FirLine   // one gain stream through a GainFir (doubled ring buffer, contiguous dot product)
{
    std::vector<double> ring; int pos = 0, N = 1;
    void prepare (int n, double init) { N = n; ring.assign ((size_t) (2 * N), init); pos = 0; }
    void reserve (int n) { ring.reserve ((size_t) (2 * n)); }   // then prepare (m <= n) never allocates
    void reset (double init) { std::fill (ring.begin(), ring.end(), init); pos = 0; }
    double process (double x, const std::vector<double>& h)
    {
        ring[(size_t) pos] = ring[(size_t) (pos + N)] = x; pos = (pos + 1) % N;
        const double* w = &ring[(size_t) pos];   // oldest .. newest
        const double* k = h.data();
        double a0 = 0, a1 = 0, a2 = 0, a3 = 0; int i = 0;
        for (; i + 3 < N; i += 4) { a0 += k[i] * w[i]; a1 += k[i + 1] * w[i + 1]; a2 += k[i + 2] * w[i + 2]; a3 += k[i + 3] * w[i + 3]; }
        for (; i < N; ++i) a0 += k[i] * w[i];
        return (a0 + a1) + (a2 + a3);
    }
};

// Schroeder allpass for the ambiance decorrelator.
struct Allpass
{
    std::vector<double> buf; int pos = 0, len = 1; double g = 0.5;
    void prepare (int l, double gain) { len = std::max (1, l); buf.assign ((size_t) len, 0.0); pos = 0; g = gain; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0); pos = 0; }
    double process (double x) { const double d = buf[(size_t) pos]; const double y = -g * x + d; buf[(size_t) pos] = x + g * y; pos = (pos + 1) % len; return y; }
};

struct Params
{
    // global
    double target = -14.0;     // LUFS, thresholds of A, B, C are relative to it (LU)
    bool autoGain = false;     // slow automatic makeup towards the target
    double windowMs = 300.0;   // RMS window of A, B, C
    double link = 1.0;         // stereo link of the compressors 0..1
    double knee = 6.0;         // dB
    double density = 0.0;      // 0..1 parallel low-level lift
    double mix = 1.0;          // dry / wet
    double outGainDb = 0.0;    // before the limiter
    // Ambiance (ambient retrieval), placed after stage 1 = A, 2 = B, 3 = C, 4 = D
    bool ambOn = false; double ambiance = 0.5;   // amount 0..1
    double ambThr = -20, ambRatio = 2, ambAtt = 20, ambRel = 400, ambPreMs = 20;
    bool ambAuto = true; int ambStage = 3; double ambTr = 0.5;   // ambTr: transients removed from the lifted ambiance, 0..1

    // T transient shaper (Bittersweet spirit): -1 rounds the attacks off, +1 accentuates them
    double transient = 0.0; double trDurMs = 30.0;
    // A upward low level
    bool upOn = true;  double upThr = -14, upRatio = 1.5, upRange = 4, upAtt = 50, upRel = 600;
    // B downward low level (gentle, capped by range)
    bool llOn = false; double llThr = -24, llRatio = 1.5, llRange = 3, llAtt = 30, llRel = 400;
    // C downward, double threshold
    bool dnOn = true;  bool opto = false;
    double thr1 = 0, ratio1 = 1.5, att1 = 30, rel1 = 300;
    double thr2 = 4, ratio2 = 4,   att2 = 10, rel2 = 150;
    double hyst = 0;           // dB: release only starts when the gain has more than this to recover (C and D)
    // D downward peak
    bool pkOn = true;  double crest = 12, pkRatio = 4, pkAtt = 0.5, pkRel = 80;
    // M/S end section
    double widthDb = 0.0;          // side gain
    double centreDb = 0.0;         // centre (mid) level, 0.1 dB steps
    int centreFilter = 0;          // 0 = broadband, 1 = low shelf, 2 = high shelf, 3 = bell
    double centreFreq = 1000.0;
    double sideLowCut = 0.0;       // Hz, 0 = off: the side is high-passed (bass in mono)
    int sideSlope = 12;            // 6 / 12 / 24 dB/oct
    bool revOn = false; int revTarget = 1;   // 0 = Mid, 1 = Side, 2 = both
    double revSendDb = -24, revDecay = 1.8, revPreMs = 20; bool revPreAuto = true;
    double revDyn = 1.0;           // 0 = constant send, 1 = send only when the level is low (under stage A threshold)
    // E limiter
    bool limOn = true; double ceiling = -1, limRel = 60, lookMs = 2; int os = 1; bool truePeak = true;
    double linkT = 1.0, linkR = 1.0;
};

struct OutputMeter   // accumulated by the audio thread, taken by the editor
{
    double peak[kMaxCh] {}, tp[kMaxCh] {}, sumSq[kMaxCh] {};
    double inPeak[kMaxCh] {}, inSumSq[kMaxCh] {};
    int n = 0;
};

class Engine
{
public:
    static constexpr double kMaxLookMs = 5.0;
    static constexpr double kSilenceLufs = -70.0;   // upward gain fades out between -60 and -70 LUFS

    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate; nch = std::clamp (numChannels, 1, kMaxCh);
        lookMax = std::max (1, (int) std::lround (kMaxLookMs * 0.001 * fs));
        // Alias-free budget: a gain with bandwidth B shifts the audio by at most B. Up to three gain
        // modulations follow each other on the way out (dynamics, reverb send, 1x limiter), so each one is
        // band-limited to a third of (fs/2 - 20 kHz): nothing can fold back below 20 kHz.
        gfir.design (fs, std::clamp ((fs * 0.5 - 20000.0) / 3.0, 300.0, 8000.0));
        const int hf = gfir.half;
        // oversampled limiter: its gain only has to stay below (F - 1) fs / 2 (the rest is removed when coming down)
        int tailOs = 0, nMax = gfir.N, hoMax = 0;
        for (int k = 1; k <= 3; ++k)
        {
            const int F = 1 << k;
            gfirOs[(size_t) k].design (F * fs, (F - 1) * fs * 0.5, F);
            tailOs = std::max (tailOs, Oversampler::kDelayBase + 2 * gfirOs[(size_t) k].half / F);
            nMax = std::max (nMax, gfirOs[(size_t) k].N); hoMax = std::max (hoMax, 2 * gfirOs[(size_t) k].half);
        }
        limTail = std::max (TruePeak::kDelay + 2 * hf, tailOs);
        latencySamples = hf + lookMax + limTail;
        for (int c = 0; c < kMaxCh; ++c)
        {
            for (auto& k : kw[(size_t) c]) k.prepare (fs);
            wetKw[(size_t) c].prepare (fs); outKw[(size_t) c].prepare (fs); inKw[(size_t) c].prepare (fs);
            limDelay[(size_t) c].reserve ((lookMax + 1) * Oversampler::kMaxF + 2 * hf + hoMax + 16);
            xDelay[(size_t) c].reserve (hf + 1); xDelay[(size_t) c].setLength (hf);
            firMain[(size_t) c].prepare (gfir.N, 1.0); firAmbC[(size_t) c].prepare (gfir.N, 0.0);
            limFir[(size_t) c].reserve (nMax); limFir[(size_t) c].prepare (gfir.N, 1.0);
            spDelay[(size_t) c].reserve (TruePeak::kDelay + 1); spDelay[(size_t) c].setLength (TruePeak::kDelay);
            hold[(size_t) c].reserve ((lookMax + 2) * Oversampler::kMaxF + 2 * hf + hoMax);
            box[(size_t) c].reserve ((lookMax + 2) * Oversampler::kMaxF);
            pad[(size_t) c].reserve (latencySamples + 2);
        }
        firRev.prepare (gfir.N, 0.0);
        ambHp.highpass (fs, 150.0); ambHpA.highpass (fs, 150.0);
        ambRingA.assign ((size_t) (0.1 * fs) + 4, 0.0); ambRingG.assign (ambRingA.size(), 1.0); ambRingT.assign (ambRingA.size(), 0.0);
        insRing[0].assign ((size_t) (2 * fs) + 8, 0.0); insRing[1].assign (insRing[0].size(), 0.0); insW = 0;
        laAmb = std::round (0.002 * fs); cAmbDuck = std::exp (-1.0 / (0.06 * fs));
        ap1A.prepare ((int) std::lround (0.0053 * fs), 0.6); ap2A.prepare ((int) std::lround (0.0079 * fs), 0.55);
        ambRing.assign ((size_t) (0.1 * fs) + 4, 0.0); ambW = 0; ambD = (double) std::lround (0.02 * fs);
        ambKw.prepare (fs); verb.prepare (fs);
        frameLen = std::max (1, (int) std::lround (0.001 * fs));
        ap1.prepare ((int) std::lround (0.0053 * fs), 0.6); ap2.prepare ((int) std::lround (0.0079 * fs), 0.55);
        blockLen = std::max (1, (int) std::lround (0.1 * fs));
        curOs = -1; curLook = -1;
        setParams (p);
        reset();
    }

    void reset()
    {
        for (size_t c = 0; c < (size_t) kMaxCh; ++c)
        {
            for (auto& k : kw[c]) k.reset();
            wetKw[c].reset(); outKw[c].reset(); inKw[c].reset(); spDelay[c].reset(); tpLim[c].reset(); tpMeter[c].reset(); pad[c].reset();
            os[c].reset(); limDelay[c].reset(); hold[c].reset(); box[c].reset(); limEnv[c] = 1;
            for (auto& r : rms[c]) r = 1e-10;
            pkEnv[c] = 0;
            gA[c] = gB[c] = gC1[c] = gC2[c] = gD[c] = gDen[c] = 0; relC1[c] = relC2[c] = relD[c] = false; optoMem[c] = 0;
        }
        for (size_t c = 0; c < (size_t) kMaxCh; ++c) { xDelay[c].reset(); firMain[c].reset (1.0); firAmbC[c].reset (0.0); limFir[c].reset (1.0); }
        firRev.reset (0.0); ambHpA.reset(); ap1A.reset(); ap2A.reset(); std::fill (ambRingA.begin(), ambRingA.end(), 0.0); std::fill (ambRingG.begin(), ambRingG.end(), 1.0); std::fill (ambRingT.begin(), ambRingT.end(), 0.0); for (auto& v : insRing) std::fill (v.begin(), v.end(), 0.0); ambDuck = 0; ambWA = 0;
        ambHp.reset(); std::fill (ambRing.begin(), ambRing.end(), 0.0); ap1.reset(); ap2.reset(); ambKw.reset(); verb.reset(); lvlA = -100;
        ambRms = 1e-10; gAmb = 0; frameAcc = 0; frameCount = 0; frameIdx = 0; recLeft = 0; sinceOnset = 100000;
        frames.fill (-120); erMs = 20; meterEr.store ((float) erMs);
        wetLoud = lufsToPow (p.target); makeupDb = 0;
        mixS = std::clamp (p.mix, 0.0, 1.0); outS = dbToLin (p.outGainDb); denS = p.density; ambS = p.ambiance; trS = p.transient; trFast = trSlow = 0;
        acc = {};
        resetLoudness();
    }

    void resetLoudness()
    {
        hopAcc = 0; hopCount = 0; hopIdx = 0; hopFill = 0; hops.fill (0);
        histPow.fill (0); histN.fill (0);
        meterM.store (-100); meterS.store (-100); meterI.store (-100);
    }

    void setParams (const Params& np)
    {
        p = np;
        auto co = [this] (double ms) { return std::exp (-1.0 / (std::max (ms, 0.01) * 0.001 * fs)); };
        cWin = co (p.windowMs);
        cAa = co (p.upAtt); cAr = co (p.upRel); cBa = co (p.llAtt); cBr = co (p.llRel);
        cC1a = co (p.att1); cC1r = co (p.rel1); cC2a = co (p.att2); cC2r = co (p.rel2);
        cDa = co (p.pkAtt); cDr = co (p.pkRel); cPkDet = co (5.0);
        cDenA = co (3.0); cDenR = co (120.0);
        cOptoUp = co (400.0); cOptoDn = co (3000.0);
        cTrFa = co (0.5); cTrFr = co (p.trDurMs); cTrSa = co (p.trDurMs * 0.7); cTrSr = 0;
        cRevSend = co (150.0); cFast = co (50.0);
        cAmbA = co (p.ambAtt); cAmbR = co (p.ambRel); cAmbWin = co (50.0);
        verb.setDecay (p.revDecay);
        if (p.centreFilter >= 1 && p.centreFilter <= 3) { centreEq.eq (p.centreFilter, fs, std::clamp (p.centreFreq, 20.0, 0.45 * fs), p.centreDb, 0.8); }
        if (p.sideLowCut > 0)
        {
            const double f = std::clamp (p.sideLowCut, 10.0, 0.45 * fs);
            if (p.sideSlope <= 6) { sideHp1.onePoleHighpass (fs, f); sideHp2 = Biquad(); }
            else if (p.sideSlope <= 12) { sideHp1.highpass (fs, f, 0.7071); sideHp2 = Biquad(); }
            else { sideHp1.highpass (fs, f, 0.5412); sideHp2.highpass (fs, f, 1.3066); }
        }
        if (p.centreFilter != lastCentreFilter || (p.sideLowCut > 0) != lastLowCutOn || p.sideSlope != lastSlope)
        { centreEq.reset(); sideHp1.reset(); sideHp2.reset(); lastCentreFilter = p.centreFilter; lastLowCutOn = p.sideLowCut > 0; lastSlope = p.sideSlope; }
        cLoud = co (8000.0); cMakeup = co (1500.0); cSmooth = co (20.0);
        const int f = p.os <= 1 ? 1 : p.os <= 2 ? 2 : p.os <= 4 ? 4 : 8;
        cLimRel = std::exp (-1.0 / (std::max (p.limRel, 0.1) * 0.001 * fs * f));
        ceiling = dbToLin (std::min (0.0, p.ceiling));
        wantOs = f;
        wantLook = std::clamp ((int) std::lround (p.lookMs * 0.001 * fs), 1, lookMax);
    }

    int latency() const { return latencySamples; }
    const Params& params() const { return p; }

    // ---- static curves (dB), exposed for tests and the display
    double shape (double over) const
    {
        const double k = std::max (0.0, p.knee * (p.opto ? 1.5 : 1.0));
        if (k <= 0) return std::max (0.0, over);
        if (over <= -k * 0.5) return 0.0;
        if (over >= k * 0.5) return over;
        const double t = over + k * 0.5; return t * t / (2 * k);
    }
    double curveA (double L) const
    {
        if (! p.upOn || p.upRange <= 0 || p.upRatio <= 1) return 0;
        double up = (1.0 - 1.0 / p.upRatio) * shape (p.target + p.upThr - L);
        up = p.upRange * std::tanh (up / p.upRange);
        return up * std::clamp ((L - kSilenceLufs) / 10.0, 0.0, 1.0);
    }
    double curveB (double L) const
    {
        if (! p.llOn || p.llRange <= 0 || p.llRatio <= 1) return 0;
        const double dn = (1.0 - 1.0 / p.llRatio) * shape (L - (p.target + p.llThr));
        return -p.llRange * std::tanh (dn / p.llRange);
    }
    double curveC1 (double L) const { return p.dnOn ? -(1.0 - 1.0 / std::max (1.0, p.ratio1)) * shape (L - thr1Abs()) : 0; }
    double curveC2 (double L) const
    {
        if (! p.dnOn) return 0;
        const double extra = std::max (0.0, 1.0 / std::max (1.0, p.ratio1) - 1.0 / std::max (1.0, p.ratio2));
        return -extra * shape (L - thr2Abs());
    }
    double curveD (double peakDb) const { return p.pkOn ? -(1.0 - 1.0 / std::max (1.0, p.pkRatio)) * shape (peakDb - peakThr()) : 0; }
    double thr1Abs() const { return p.target + p.thr1; }
    double thr2Abs() const { return std::max (thr1Abs(), p.target + p.thr2); }
    double peakThr() const { return std::min (0.0, thr1Abs() + p.crest); }   // dBFS

    template <typename T>
    void process (T* const* io, int numCh, int n)
    {
        const int C = std::min (numCh, nch);
        const double cs = (double) C;
        if (wantOs != curOs || wantLook != curLook) configureLimiter();
        const double mixT = std::clamp (p.mix, 0.0, 1.0), outT = dbToLin (p.outGainDb);
        double mRev = 0, mT = 0, mA = 0, mB = 0, mC = 0, mD = 0, mL = 1, mDen = 0, mAmb = 0, lvlC = -100;
        const int F = curOs;
        for (int i0 = 0; i0 < n; i0 += kChunk)
        {
        const int nb = std::min (kChunk, n - i0);
        for (int i = i0; i < i0 + nb; ++i)
        {
            mixS = mixT + cSmooth * (mixS - mixT); outS = outT + cSmooth * (outS - outT);
            trS = p.transient + cSmooth * (trS - p.transient);
            widthS = dbToLin (p.widthDb) + cSmooth * (widthS - dbToLin (p.widthDb));
            centreS = dbToLin (p.centreDb) + cSmooth * (centreS - dbToLin (p.centreDb));
            denS = p.density + cSmooth * (denS - p.density); ambS = p.ambiance + cSmooth * (ambS - p.ambiance);
            double x[kMaxCh] {}, s[kMaxCh] {}, gPre[kMaxCh] { 1, 1 }, gPost[kMaxCh] { 1, 1 }, dk[kMaxCh] {};
            bool afterAmb = false; ambUsed = false; ambK = 0;
            auto mulGain = [&] (int c, double gl) { if (afterAmb) gPost[c] *= gl; else gPre[c] *= gl; };
            for (int c = 0; c < C; ++c) s[c] = x[c] = (double) io[c][i];
            {   // input metering (sand, crest, dynamic range)
                double ip = 0;
                for (int c = 0; c < C; ++c)
                {
                    const double k = inKw[(size_t) c].process (x[c]); ip += k * k;
                    acc.inPeak[c] = std::max (acc.inPeak[c], std::abs (x[c])); acc.inSumSq[c] += x[c] * x[c];
                }
                if (C == 1) ip *= 2;
                inFast = ip + cFast * (inFast - ip);
            }

            // T: transients = fast envelope over slow envelope (linked), turned into a gain
            {
                double a = 0;
                for (int c = 0; c < C; ++c) a = std::max (a, std::abs (s[c]));
                trFast = a > trFast ? a + cTrFa * (trFast - a) : a + cTrFr * (trFast - a);
                trSlow = trFast + cTrSa * (trSlow - trFast);   // slow = fast envelope smoothed: equal on steady tones
                const double tr = std::clamp (linToDb (trFast + 1e-9) - linToDb (trSlow + 1e-9), 0.0, 12.0);
                trDb = tr;
                const double g = trS * tr;
                mT = std::abs (g) > std::abs (mT) ? g : mT;
                if (std::abs (g) > 1e-6) { const double gl = dbToLin (g); for (int c = 0; c < C; ++c) { s[c] *= gl; mulGain (c, gl); } }
            }

            // A, B, C: RMS stages in series, each detects its own input
            auto rmsLevels = [&] (size_t st, const double* in, double* lc) {
                double sum = 0;
                for (int c = 0; c < C; ++c)
                {
                    auto& r = rms[(size_t) c][st];
                    const double k = kw[(size_t) c][st].process (in[c]);
                    r = k * k + cWin * (r - k * k);
                    sum += r;
                }
                const double linked = powToLufs (sum);
                for (int c = 0; c < C; ++c) lc[c] = p.link * linked + (1.0 - p.link) * powToLufs (rms[(size_t) c][st] * cs);
                return linked;
            };
            double L[kMaxCh] {};
            lvlA = rmsLevels (0, s, L);
            for (int c = 0; c < C; ++c) { auto& g = gA[(size_t) c]; smooth (g, curveA (L[c]), cAa, cAr); s[c] *= dbToLin (g); mulGain (c, dbToLin (g)); mA = std::max (mA, g); }
            if (p.ambStage == 1) { ambiance (s, C, mAmb); afterAmb = ambUsed; }
            rmsLevels (1, s, L);
            for (int c = 0; c < C; ++c) { auto& g = gB[(size_t) c]; smooth (g, curveB (L[c]), cBa, cBr); s[c] *= dbToLin (g); mulGain (c, dbToLin (g)); mB = std::min (mB, g); }
            if (p.ambStage == 2) { ambiance (s, C, mAmb); afterAmb = ambUsed; }
            lvlC = rmsLevels (2, s, L);
            for (int c = 0; c < C; ++c)
            {
                const size_t u = (size_t) c;
                double r1 = cC1r, r2 = cC2r;
                if (p.opto)   // opto: the longer and deeper it compressed, the slower it lets go
                {
                    const double depth = std::min (1.0, -(gC1[u] + gC2[u]) / 6.0);
                    optoMem[u] = depth + (depth > optoMem[u] ? cOptoUp : cOptoDn) * (optoMem[u] - depth);
                    const double slow = 1.0 + 4.0 * optoMem[u];
                    r1 = std::pow (cC1r, 1.0 / slow); r2 = std::pow (cC2r, 1.0 / slow);
                }
                smoothHyst (gC1[u], curveC1 (L[c]), cC1a, r1, relC1[u]);
                smoothHyst (gC2[u], curveC2 (L[c]), cC2a, r2, relC2[u]);
                s[c] *= dbToLin (gC1[u] + gC2[u]); mulGain (c, dbToLin (gC1[u] + gC2[u])); mC = std::min (mC, gC1[u] + gC2[u]);
            }
            if (p.ambStage == 3) { ambiance (s, C, mAmb); afterAmb = ambUsed; }

            // D: peak
            double pk = 0, pkc[kMaxCh] {};
            for (int c = 0; c < C; ++c)
            {
                auto& e = pkEnv[(size_t) c];
                const double a = std::abs (s[c]);
                e = a > e ? a : a + cPkDet * (e - a);
                pkc[c] = e; pk = std::max (pk, e);
            }
            for (int c = 0; c < C; ++c)
            {
                const double lv = linToDb (p.link * pk + (1.0 - p.link) * pkc[c]);
                auto& g = gD[(size_t) c];
                smoothHyst (g, curveD (lv), cDa, cDr, relD[(size_t) c]);
                s[c] *= dbToLin (g); mulGain (c, dbToLin (g)); mD = std::min (mD, g);
            }
            if (p.ambStage == 4) { ambiance (s, C, mAmb); afterAmb = ambUsed; }

            // Density: parallel, heavily compressed (thr target - 30 LU, 10:1), made up to the target level
            if (denS > 1e-4)
            {
                rmsLevels (3, x, L);
                const double thr = p.target - 30.0, makeup = 30.0 * (1.0 - 1.0 / 10.0);
                for (int c = 0; c < C; ++c)
                {
                    auto& g = gDen[(size_t) c];
                    smooth (g, -(1.0 - 1.0 / 10.0) * std::max (0.0, L[c] - thr), cDenA, cDenR);
                    dk[c] = 0.5 * denS * dbToLin (g + makeup) * std::clamp ((L[c] - kSilenceLufs) / 10.0, 0.0, 1.0);
                    s[c] += dk[c] * x[c];
                    mDen = std::min (mDen, g);
                }
            }

            // auto makeup (on the processed signal), mix, output gain
            double wetPow = 0;
            for (int c = 0; c < C; ++c) { const double k = wetKw[(size_t) c].process (s[c]); wetPow += k * k; }
            if (p.autoGain)
            {
                if (powToLufs (wetPow) > -60.0) wetLoud = wetPow + cLoud * (wetLoud - wetPow);
                const double want = std::clamp (p.target - powToLufs (wetLoud), -12.0, 24.0);
                makeupDb = want + cMakeup * (makeupDb - want);
            }
            else makeupDb *= cMakeup;
            const double mk = dbToLin (makeupDb);

            // audio path: the input delayed by the gain FIR, times ONE band-limited gain per channel that holds
            // every stage, the density, the makeup, the mix and the output gain (a product of separately filtered
            // gains would widen the band again). The ambiance is a second, additive band-limited term.
            double xd[kMaxCh] {}, y[kMaxCh] {};
            const bool same = C == 2 && p.link >= 1.0;
            double G[kMaxCh] {};
            for (int c = 0; c < C; ++c)
            {
                xd[c] = xDelay[(size_t) c].process (x[c]);
                if (same && c == 1) { G[1] = G[0]; continue; }
                G[c] = firMain[(size_t) c].process (outS * ((1.0 - mixS) + mixS * mk * (gPre[c] * gPost[c] + dk[c])), gfir.h);
            }
            for (int c = 0; c < C; ++c) y[c] = xd[c] * G[c];
            if (C == 2)
            {
                // decorrelated side of the (unprocessed) input, pre-delayed like the control path; the gain applied
                // before the ambiance on the control path is delayed by the same pre-delay
                const size_t R = ambRingA.size(), rd = (ambWA + R - (size_t) ambD) % R;
                ambRingA[ambWA] = ambHpA.process (0.5 * (xd[0] - xd[1]));
                ambRingG[ambWA] = 0.5 * (gPre[0] + gPre[1]);
                ambRingT[ambWA] = trDb;
                const double del = ambRingA[rd], gDel = ambRingG[rd];
                // transients out of the lifted ambiance: the transient measure, read 2 ms ahead of the pre-delayed
                // ambiance, holds a fast-attack / 60 ms release duck (smeared attacks of the decorrelator included)
                const double tAhead = ambRingT[(ambWA + R - (size_t) std::max (0.0, ambD - laAmb)) % R];
                ambDuck = tAhead > ambDuck ? tAhead : tAhead + cAmbDuck * (ambDuck - tAhead);
                const double kDuck = dbToLin (-p.ambTr * 2.5 * ambDuck);
                ambWA = (ambWA + 1) % R;
                const double e = ap2A.process (ap1A.process (del));
                double ka[kMaxCh] {};
                for (int c = 0; c < 2; ++c)
                {
                    if (same && c == 1) { ka[1] = ka[0]; continue; }
                    ka[c] = firAmbC[(size_t) c].process (outS * mixS * mk * gDel * ambK * kDuck * gPost[c], gfir.h);
                }
                y[0] += ka[0] * e; y[1] -= ka[1] * e;
            }

            // M/S end section: width, centre level / filter, side low cut
            if (C == 2 && (std::abs (p.widthDb) > 1e-6 || std::abs (p.centreDb) > 1e-6 || p.sideLowCut > 0))
            {
                double m = 0.5 * (y[0] + y[1]), sd = 0.5 * (y[0] - y[1]);
                sd *= widthS;
                if (p.centreFilter == 0) m *= centreS; else m = centreEq.process (m);
                if (p.sideLowCut > 0) sd = sideHp2.process (sideHp1.process (sd));
                y[0] = m + sd; y[1] = m - sd;
            }

            for (int c = 0; c < C; ++c) yBuf[(size_t) c][(size_t) (i - i0)] = y[c];
        }

        // insert (external plugin, e.g. a reverb) on Mid, Side or L/R, before the limiter. The part of M/S that does
        // not go through it is delayed by the plugin's latency, and so is everything while the insert is off.
        if (const int lat = insLat.load (std::memory_order_relaxed); lat > 0 || insertFn != nullptr)
        {
            const size_t R = insRing[0].size();
            const bool run = insertFn != nullptr && C == 2 && p.revOn;
            double dl[kChunk], dr[kChunk];
            for (int j = 0; j < nb; ++j)
            {
                insRing[0][insW] = yBuf[0][(size_t) j]; insRing[1][insW] = C == 2 ? yBuf[1][(size_t) j] : 0.0;
                const size_t rd = (insW + R - (size_t) std::min (lat, (int) R - 1)) % R;
                dl[j] = insRing[0][rd]; dr[j] = insRing[1][rd]; insW = (insW + 1) % R;
            }
            if (run)
            {
                auto& a = insA; auto& b = insB;
                for (int j = 0; j < nb; ++j)
                {
                    const double l = yBuf[0][(size_t) j], r = yBuf[1][(size_t) j], m = 0.5 * (l + r), sd = 0.5 * (l - r);
                    if (p.revTarget == 0) { a[(size_t) j] = m; b[(size_t) j] = m; }
                    else if (p.revTarget == 1) { a[(size_t) j] = sd; b[(size_t) j] = -sd; }
                    else { a[(size_t) j] = l; b[(size_t) j] = r; }
                }
                insertFn (insertCtx, a.data(), b.data(), nb);
                for (int j = 0; j < nb; ++j)
                {
                    double mo = 0.5 * (dl[j] + dr[j]), so = 0.5 * (dl[j] - dr[j]);   // delayed, untouched part
                    if (p.revTarget == 0) mo = 0.5 * (a[(size_t) j] + b[(size_t) j]);
                    else if (p.revTarget == 1) so = 0.5 * (a[(size_t) j] - b[(size_t) j]);
                    else { mo = 0.5 * (a[(size_t) j] + b[(size_t) j]); so = 0.5 * (a[(size_t) j] - b[(size_t) j]); }
                    yBuf[0][(size_t) j] = mo + so; yBuf[1][(size_t) j] = mo - so;
                }
            }
            else
                for (int j = 0; j < nb; ++j) { yBuf[0][(size_t) j] = dl[j]; if (C == 2) yBuf[1][(size_t) j] = dr[j]; }
        }

        for (int i = i0; i < i0 + nb; ++i)
        {
            double y[kMaxCh] {};
            for (int c = 0; c < C; ++c) y[c] = yBuf[(size_t) c][(size_t) (i - i0)];
            // E: limiter
            double o[kMaxCh] {};
            if (F > 1)
            {
                double up[kMaxCh][Oversampler::kMaxF] {}, dn[kMaxCh][Oversampler::kMaxF] {};
                for (int c = 0; c < C; ++c) os[(size_t) c].up (y[c], up[c]);
                for (int q = 0; q < F; ++q)
                {
                    double a[kMaxCh] {}, v[kMaxCh] {};
                    for (int c = 0; c < C; ++c) { a[c] = std::abs (up[c][q]); v[c] = up[c][q]; }
                    limitSample (a, v, C, mL);
                    for (int c = 0; c < C; ++c) dn[c][q] = v[c];
                }
                for (int c = 0; c < C; ++c) o[c] = os[(size_t) c].down (dn[c]);
            }
            else
            {
                double a[kMaxCh] {}, v[kMaxCh] {};
                for (int c = 0; c < C; ++c)
                {
                    const double tpv = tpLim[(size_t) c].process (y[c]), spv = std::abs (spDelay[(size_t) c].process (y[c]));
                    a[c] = p.truePeak ? tpv : spv; v[c] = y[c];
                }
                limitSample (a, v, C, mL);
                for (int c = 0; c < C; ++c) o[c] = v[c];
            }

            // constant latency padding, write out, meter
            double op = 0;
            for (int c = 0; c < C; ++c)
            {
                const double out = pad[(size_t) c].process (o[c]);
                io[c][i] = (T) out;
                const double ko = outKw[(size_t) c].process (out);
                op += ko * ko;
                acc.peak[c] = std::max (acc.peak[c], std::abs (out));
                acc.tp[c] = std::max (acc.tp[c], tpMeter[(size_t) c].process (out));
                acc.sumSq[c] += out * out;
            }
            ++acc.n;
            if (C == 1) { acc.peak[1] = acc.peak[0]; acc.tp[1] = acc.tp[0]; acc.sumSq[1] = acc.sumSq[0]; }
            outFast = (C == 1 ? 2 * op : op) + cFast * (outFast - (C == 1 ? 2 * op : op));
            if (C == 1) { acc.inPeak[1] = acc.inPeak[0]; acc.inSumSq[1] = acc.inSumSq[0]; }
            loudnessSample (op);
        }
        }
        if (resetIntegrated.exchange (false)) resetLoudness();
        meterLevel.store ((float) lvlC);
        meterIn.store ((float) powToLufs (inFast)); meterOut.store ((float) powToLufs (outFast));
        meterA.store ((float) mA); meterB.store ((float) mB); meterC.store ((float) mC); meterD.store ((float) mD);
        meterLim.store ((float) linToDb (mL)); meterMakeup.store ((float) makeupDb); meterDensity.store ((float) mDen); meterAmb.store ((float) mAmb); meterT.store ((float) mT); meterRev.store ((float) linToDb (mRev + 1e-9));
        if (meterLock.try_lock())
        {
            for (int c = 0; c < kMaxCh; ++c)
            {
                shared.peak[c] = std::max (shared.peak[c], acc.peak[c]); shared.tp[c] = std::max (shared.tp[c], acc.tp[c]);
                shared.sumSq[c] += acc.sumSq[c];
                shared.inPeak[c] = std::max (shared.inPeak[c], acc.inPeak[c]); shared.inSumSq[c] += acc.inSumSq[c];
            }
            shared.n += acc.n;
            meterLock.unlock();
            acc = {};
        }
    }

    OutputMeter takeMeter() { std::lock_guard<std::mutex> l (meterLock); auto m = shared; shared = {}; return m; }

    std::atomic<float> meterLevel { -100 }, meterA { 0 }, meterB { 0 }, meterC { 0 }, meterD { 0 }, meterLim { 0 },
                       meterMakeup { 0 }, meterDensity { 0 }, meterAmb { 0 }, meterEr { 20 }, meterT { 0 }, meterRev { -100 }, meterIn { -100 }, meterOut { -100 }, meterM { -100 }, meterS { -100 }, meterI { -100 };
    std::atomic<bool> resetIntegrated { false };

private:
    static void smooth (double& g, double gt, double ca, double cr) { g = gt + (gt < g ? ca : cr) * (g - gt); }
    void smoothHyst (double& g, double gt, double ca, double cr, bool& releasing) const
    {
        if (gt < g) { g = gt + ca * (g - gt); releasing = false; return; }
        if (! releasing && gt - g > p.hyst) releasing = true;
        if (releasing) g = gt + cr * (g - gt);
    }

public:
    double curveAmb (double L) const
    {
        if (p.ambRatio <= 1) return 0;
        const double up = (1.0 - 1.0 / p.ambRatio) * shape (p.target + p.ambThr - L);
        return 12.0 * std::tanh (up / 12.0) * std::clamp ((L - kSilenceLufs) / 10.0, 0.0, 1.0);
    }
private:
    void ambiance (double* s, int C, double& meter)
    {
        if (C < 2) return;
        const double m = 0.5 * (s[0] + s[1]), sd = 0.5 * (s[0] - s[1]);
        // level of the mid (K-weighted, 50 ms) drives the upward expander
        const double k = ambKw.process (m);
        ambRms = 2 * k * k + cAmbWin * (ambRms - 2 * k * k);
        const double L = powToLufs (ambRms);
        smooth (gAmb, p.ambOn ? curveAmb (L) : 0.0, cAmbA, cAmbR);
        meter = std::max (meter, gAmb);
        if (p.ambAuto) analyseReflections (m);
        // pre-delay glides one sample at a time towards its target (no clicks when auto moves it)
        const double want = std::clamp ((p.ambAuto ? erMs : p.ambPreMs) * 0.001 * fs, 1.0, (double) ambRing.size() - 2);
        if (std::abs (want - ambD) >= 1.0) ambD += want > ambD ? 1.0 : -1.0;
        const size_t R = ambRing.size();
        ambRing[ambW] = ambHp.process (sd);
        const double delayed = ambRing[(ambW + R - (size_t) ambD) % R];
        ambW = (ambW + 1) % R;
        const double e = ap2.process (ap1.process (delayed));
        const double amt = p.ambOn ? ambS : 0.0;
        ambUsed = true; ambK = amt * (dbToLin (gAmb) - 1.0);
        if (amt <= 0) return;
        const double s2 = sd + amt * (dbToLin (gAmb) - 1.0) * e;
        s[0] = m + s2; s[1] = m - s2;
    }

    // Early reflection estimate: after a clear onset, the 1 ms energy envelope (dB) of the mid is recorded for
    // 200 ms; the end of the early reflections is where the decay becomes a straight line in dB (diffuse tail):
    // the first time from which a 60 ms line fit deviates by less than 1 dB rms.
    void analyseReflections (double m)
    {
        frameAcc += m * m;
        if (++frameCount < frameLen) return;
        const double e = 10.0 * std::log10 (frameAcc / frameCount + 1e-14);
        frameAcc = 0; frameCount = 0;
        ++sinceOnset;
        if (recLeft > 0)
        {
            frames[(size_t) frameIdx++] = e;
            if (--recLeft == 0) estimateEr();
            return;
        }
        backAvg = 0.98 * backAvg + 0.02 * e;
        if (e > backAvg + 9.0 && e > -60.0 && sinceOnset > 300) { frameIdx = 0; frames[(size_t) frameIdx++] = e; recLeft = kFrames - 1; sinceOnset = 0; }
    }
    void estimateEr()
    {
        // 3 ms smoothed envelope in dB (1 ms frames of noise-like tails fluctuate too much)
        std::array<double, kFrames> d {};
        for (int i = 0; i < kFrames; ++i)
        {
            double s = 0; int c = 0;
            for (int j = std::max (0, i - 1); j <= std::min (kFrames - 1, i + 1); ++j) { s += std::pow (10.0, frames[(size_t) j] / 10.0); ++c; }
            d[(size_t) i] = 10.0 * std::log10 (s / c + 1e-14);
        }
        int pk = 0;
        for (int i = 1; i < 10; ++i) if (d[(size_t) i] > d[(size_t) pk]) pk = i;
        const int W = 60;
        for (int t = pk + 3; t + W < kFrames; ++t)
        {
            double sx = 0, sy = 0, sxx = 0, sxy = 0;
            for (int j = 0; j < W; ++j) { const double y = d[(size_t) (t + j)]; sx += j; sy += y; sxx += j * j; sxy += j * y; }
            const double b = (W * sxy - sx * sy) / (W * sxx - sx * sx), a = (sy - b * sx) / W;
            double dev = 0;
            for (int j = 0; j < W; ++j) { const double r = d[(size_t) (t + j)] - (a + b * j); dev += r * r; }
            if (std::sqrt (dev / W) < 1.0)
            {
                if (b > -0.01) return;    // flat: no decay after this onset, not usable
                const double ms = std::clamp ((double) (t - pk), 5.0, 80.0);
                erMs = 0.7 * erMs + 0.3 * ms;
                meterEr.store ((float) erMs);
                return;
            }
        }
    }

    void configureLimiter()
    {
        curOs = wantOs; curLook = wantLook;
        const int Lh = curLook * curOs;
        const int hf = gfir.half, tail = limTail;
        const GainFir& lf = curOs > 1 ? gfirOs[(size_t) (curOs == 2 ? 1 : curOs == 4 ? 2 : 3)] : gfir;
        const int hl = lf.half;   // limiter gain FIR half length, at the limiter's rate
        const int padLen = curOs > 1 ? lookMax - curLook + tail - Oversampler::kDelayBase - 2 * hl / curOs
                                     : lookMax - curLook + tail - TruePeak::kDelay - 2 * hf;
        for (size_t c = 0; c < (size_t) kMaxCh; ++c)
        {
            os[c].setFactor (curOs);
            // the gain goes through a band-limiting FIR (window 2 * hl); at 1x detection is also TruePeak::kDelay late
            limDelay[c].setLength (curOs > 1 ? Lh + 2 * hl : curLook + TruePeak::kDelay + 2 * hl);
            hold[c].setLength ((curOs > 1 ? Lh : curLook) + 2 * hl + 1);
            limFir[c].prepare (lf.N, 1.0);
            box[c].setLength (curOs > 1 ? Lh : curLook);
            pad[c].setLength (padLen);
            limEnv[c] = 1; tpLim[c].reset(); spDelay[c].reset();
        }
    }

    const std::vector<double>& limH() const { return curOs > 1 ? gfirOs[(size_t) (curOs == 2 ? 1 : curOs == 4 ? 2 : 3)].h : gfir.h; }

    // one limiter step at the current rate: a = detected peak per channel (already aligned), v = audio in / out
    void limitSample (const double* a, double* v, int C, double& meterMin)
    {
        if (! p.limOn)
        {
            for (int c = 0; c < C; ++c) { const size_t u = (size_t) c; hold[u].process (1.0); box[u].process (1.0); limFir[u].process (1.0, limH()); limEnv[u] = 1; v[c] = limDelay[u].process (v[c]); }
            return;
        }
        double want[kMaxCh] {}, wmin = 1.0;
        for (int c = 0; c < C; ++c) { want[c] = a[c] > ceiling ? ceiling / a[c] : 1.0; wmin = std::min (wmin, want[c]); }
        double emin = 1.0;
        for (int c = 0; c < C; ++c)
        {
            const size_t u = (size_t) c;
            const double w = p.linkT * wmin + (1.0 - p.linkT) * want[c];
            const double h = hold[u].process (w);
            limEnv[u] = h < limEnv[u] ? h : h + cLimRel * (limEnv[u] - h);
            emin = std::min (emin, limEnv[u]);
        }
        for (int c = 0; c < C; ++c)
        {
            const size_t u = (size_t) c;
            const double e = p.linkR * emin + (1.0 - p.linkR) * limEnv[u];
            const double gb = box[u].process (e);
            const double g = limFir[u].process (gb, limH());   // band-limited limiter gain
            v[c] = limDelay[u].process (v[c]) * g;
            meterMin = std::min (meterMin, g);
        }
    }

    // BS.1770 loudness: 100 ms hops, momentary = 4 hops, short-term = 30, integrated = gated histogram
    void loudnessSample (double pw)
    {
        hopAcc += pw;
        if (++hopCount < blockLen) return;
        hops[(size_t) hopIdx] = hopAcc / hopCount; hopIdx = (hopIdx + 1) % 30; hopFill = std::min (hopFill + 1, 30);
        hopAcc = 0; hopCount = 0;
        auto meanLast = [&] (int k) {
            double s = 0; const int m = std::min (k, hopFill);
            for (int j = 1; j <= m; ++j) s += hops[(size_t) ((hopIdx - j + 30) % 30)];
            return s / std::max (1, m);
        };
        const double mPow = meanLast (4);
        meterM.store ((float) powToLufs (mPow)); meterS.store ((float) powToLufs (meanLast (30)));
        if (hopFill >= 4)
        {
            const double l = powToLufs (mPow);
            if (l > -70.0) { const size_t b = (size_t) std::clamp ((int) ((l + 70.0) * 10.0), 0, kBins - 1); histPow[b] += mPow; histN[b] += 1; }
            double sp = 0; long long sn = 0;
            for (size_t b = 0; b < (size_t) kBins; ++b) { sp += histPow[b]; sn += histN[b]; }
            if (sn > 0)
            {
                const double gate = powToLufs (sp / (double) sn) - 10.0;
                const size_t gb = (size_t) std::clamp ((int) ((gate + 70.0) * 10.0), 0, kBins - 1);
                sp = 0; sn = 0;
                for (size_t b = gb; b < (size_t) kBins; ++b) { sp += histPow[b]; sn += histN[b]; }
                if (sn > 0) meterI.store ((float) powToLufs (sp / (double) sn));
            }
        }
    }

    Params p;
    double fs = 48000; int nch = 2, lookMax = 240, latencySamples = 0;
    int wantOs = 1, curOs = -1, wantLook = 96, curLook = -1;
    double cWin = 0, cAa = 0, cAr = 0, cBa = 0, cBr = 0, cC1a = 0, cC1r = 0, cC2a = 0, cC2r = 0, cDa = 0, cDr = 0, cPkDet = 0;
    double cDenA = 0, cDenR = 0, cOptoUp = 0, cOptoDn = 0, cLoud = 0, cMakeup = 0, cSmooth = 0, cLimRel = 0, ceiling = 1;
    double mixS = 1, outS = 1, denS = 0, ambS = 0, trS = 0, trFast = 0, trSlow = 0;
    Biquad centreEq, sideHp1, sideHp2; int lastCentreFilter = -1, lastSlope = -1; bool lastLowCutOn = false; double centreS = 1;
    std::array<KWeighting, kMaxCh> inKw; double inFast = 0, outFast = 0, cFast = 0;
    double widthS = 1, revSendS = 0, cRevSend = 0, lvlA = -100; TailVerb verb;
    double cTrFa = 0, cTrFr = 0, cTrSa = 0, cTrSr = 0;
    std::array<std::array<KWeighting, 4>, kMaxCh> kw;
    std::array<std::array<double, 4>, kMaxCh> rms {};
    std::array<KWeighting, kMaxCh> wetKw, outKw;
    std::array<double, kMaxCh> pkEnv {}, gA {}, gB {}, gC1 {}, gC2 {}, gD {}, gDen {}, optoMem {}, limEnv {};
    std::array<bool, kMaxCh> relC1 {}, relC2 {}, relD {};
    std::array<Oversampler, kMaxCh> os;
    std::array<TruePeak, kMaxCh> tpLim, tpMeter;
    std::array<Delay, kMaxCh> limDelay, spDelay, pad;
    std::array<MinHold, kMaxCh> hold;
    std::array<Box, kMaxCh> box;
    Biquad ambHp; Allpass ap1, ap2; KWeighting ambKw;
    // audio path (the gains computed on the control path, band-limited by the gain FIR, applied to the delayed input)
    GainFir gfir; std::array<GainFir, 4> gfirOs; int limTail = 16; std::array<FirLine, kMaxCh> firMain, firAmbC, limFir; FirLine firRev;
    std::array<Delay, kMaxCh> xDelay;
    Biquad ambHpA; Allpass ap1A, ap2A; std::vector<double> ambRingA, ambRingG, ambRingT; size_t ambWA = 0;
    double trDb = 0, ambDuck = 0, laAmb = 96, cAmbDuck = 0.9997;
    static constexpr int kChunk = 256;
    std::array<std::array<double, kChunk>, kMaxCh> yBuf {};
    std::array<double, kChunk> insA {}, insB {};
public:
    // external insert (plugin host side): processes two channels in place, n <= kChunk, on the audio thread
    using InsertFn = void (*) (void* ctx, double* a, double* b, int n);
    void setInsert (InsertFn f, void* ctx) { insertFn = f; insertCtx = ctx; }
    void setInsertLatency (int samples) { insLat.store (std::clamp (samples, 0, (int) insRing[0].size() - 1)); }
    static constexpr int maxInsertBlock() { return kChunk; }
private:
    InsertFn insertFn = nullptr; void* insertCtx = nullptr;
    std::array<std::vector<double>, 2> insRing; size_t insW = 0; std::atomic<int> insLat { 0 };
    double ambK = 0; bool ambUsed = false;
    std::vector<double> ambRing; size_t ambW = 0; double ambD = 960, ambRms = 1e-10, gAmb = 0;
    double cAmbA = 0, cAmbR = 0, cAmbWin = 0;
    static constexpr int kFrames = 200;
    std::array<double, kFrames> frames {}; int frameLen = 48, frameCount = 0, frameIdx = 0, recLeft = 0, sinceOnset = 100000;
    double frameAcc = 0, backAvg = -100, erMs = 20;
    double wetLoud = 0, makeupDb = 0;
    static constexpr int kBins = 800;
    int blockLen = 4800, hopCount = 0, hopIdx = 0, hopFill = 0; double hopAcc = 0;
    std::array<double, 30> hops {};
    std::array<double, kBins> histPow {}; std::array<long long, kBins> histN {};
    OutputMeter acc, shared; std::mutex meterLock;
};
} // namespace dcomp
