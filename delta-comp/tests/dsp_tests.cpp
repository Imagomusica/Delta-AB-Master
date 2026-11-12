#include <chrono>
// Measurement tests for the Delta Comp engine (no JUCE).
#include "../Source/dsp/Compressor.h"
#include <cstdio>
#include <random>
#include <complex>

using namespace dcomp;
static int fails = 0;
static void check (bool ok, const char* what, double v) { std::printf ("[%s] %-66s %g\n", ok ? " OK " : "FAIL", what, v); if (! ok) ++fails; }

static Params neutral()
{
    Params p; p.upOn = p.llOn = p.dnOn = p.pkOn = p.limOn = p.ambOn = p.kstOn = false; p.autoGain = false; p.knee = 0; p.density = 0;
    return p;
}

// Stereo sine for `seconds`, returns output RMS (peak-equivalent dB) over the last 0.5 s.
static double runSine (Engine& e, double fs, double ampDb, double seconds, double freq = 997.0)
{
    const int n = (int) (seconds * fs), blk = 512, tail = (int) (0.5 * fs);
    std::vector<double> l (blk), r (blk);
    double* io[2] = { l.data(), r.data() };
    const double a = dbToLin (ampDb);
    double acc = 0; int cnt = 0;
    for (int s = 0; s < n; s += blk)
    {
        const int m = std::min (blk, n - s);
        for (int i = 0; i < m; ++i) l[(size_t) i] = r[(size_t) i] = a * std::sin (2 * kPi * freq * (s + i) / fs);
        e.process (io, 2, m);
        for (int i = 0; i < m; ++i) if (s + i >= n - tail) { acc += l[(size_t) i] * l[(size_t) i]; ++cnt; }
    }
    return 10 * std::log10 (acc / cnt) + 3.0103;
}

static double maxTruePeakDb (const std::vector<double>& l, const std::vector<double>& r)
{
    TruePeak t1, t2; double m = 0;
    for (size_t i = 0; i < l.size(); ++i) m = std::max ({ m, t1.process (l[i]), t2.process (r[i]) });
    return linToDb (m);
}


// Aliasing: a 21 kHz tone rides along with band-limited 100 Hz bursts that make every stage work hard.
// Without aliasing nothing can appear between 5 and 16 kHz (the bursts stay below ~4 kHz even with their
// gain modulation, the tone's sidebands stay above 17 kHz). Returns that band's power relative to the tone, dB.
static double aliasBandDb (double fs, int os)
{
    Engine e; e.prepare (fs, 2);
    Params p = neutral();
    p.pkOn = true; p.pkAtt = 0.05; p.pkRel = 30; p.crest = 0; p.dnOn = true; p.att1 = 1; p.att2 = 0.5; p.thr1 = -10; p.ratio1 = 3;
    p.thr2 = -4; p.ratio2 = 8; p.upOn = true; p.limOn = true; p.ceiling = -6; p.transient = -1; p.autoGain = true; p.os = os;
    p.ambOn = true; p.ambiance = 1; p.density = 0.5;
    e.setParams (p);
    const int N = 1 << 16, skip = (int) fs + e.latency(), total = skip + N;
    std::vector<double> l ((size_t) total), r ((size_t) total);
    const int period = (int) (0.125 * fs); const double sig = 0.004 * fs;
    for (int i = 0; i < total; ++i)
    {
        const double t = (double) (i % period) - 0.5 * period, env = 0.9 * std::exp (-0.5 * t * t / (sig * sig));
        const double b = env * std::sin (2 * kPi * 100.0 * i / fs), h = 0.25 * std::sin (2 * kPi * 21000.0 * i / fs);
        l[(size_t) i] = b + h; r[(size_t) i] = 0.7 * b + h;
    }
    for (int s = 0; s < total; s += 512) { double* io[2] = { &l[(size_t) s], &r[(size_t) s] }; e.process (io, 2, std::min (512, total - s)); }
    std::vector<std::complex<double>> a ((size_t) N);
    for (int i = 0; i < N; ++i)
    {
        const double t = 2.0 * i / (N - 1) - 1, w = GainFir::i0 (20 * std::sqrt (std::max (0.0, 1 - t * t))) / GainFir::i0 (20.0);
        a[(size_t) i] = l[(size_t) (skip + i)] * w;
    }
    for (size_t i = 1, j = 0; i < (size_t) N; ++i) { size_t bit = (size_t) N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= (size_t) N; len <<= 1)
    {
        const std::complex<double> wl (std::cos (-2 * kPi / (double) len), std::sin (-2 * kPi / (double) len));
        for (size_t i = 0; i < (size_t) N; i += len) { std::complex<double> w (1); for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
    double band = 0, tone = 0;
    for (int k = 1; k < N / 2; ++k)
    {
        const double f = k * fs / N, pw = std::norm (a[(size_t) k]);
        if (f > 5000 && f < 16000) band += pw;
        if (f > 20500 && f < 21500) tone += pw;
    }
    return 10 * std::log10 (band / tone + 1e-300);
}

int main()
{
    const double fs = 48000;
    {
        TruePeak tp; double m = 0, sp = 0;
        for (int i = 0; i < 2000; ++i) { const double x = std::sin (2 * kPi * 0.25 * i + kPi / 4); sp = std::max (sp, std::abs (x)); m = std::max (m, tp.process (x)); }
        check (m > 0.98 && m < 1.02, "true peak of a fs/4 sine at 45 deg (samples at 0.707) = 1.0", m);
    }
    {   // static curves
        Engine e; e.prepare (fs, 2);
        Params p = neutral(); p.dnOn = p.upOn = p.llOn = p.pkOn = true; p.target = -14;
        p.thr1 = 0; p.ratio1 = 2; p.thr2 = 6; p.ratio2 = 4; p.upThr = -10; p.upRatio = 2; p.upRange = 100;
        p.llThr = -30; p.llRatio = 2; p.llRange = 3; p.crest = 10; p.pkCrest = true; p.pkRatio = 4; p.upSpectral = false;
        e.setParams (p);
        check (std::abs (e.curveC1 (-10) + 2) < 1e-9, "C: 4 dB over threshold 1, 2:1 -> -2 dB", e.curveC1 (-10));
        const double tot = e.curveC1 (-4) + e.curveC2 (-4);   // 10 over thr1, 4 over thr2: -(10/2) - (1/2-1/4)*4 = -6
        check (std::abs (tot + 6) < 1e-9, "C: double threshold 2:1 then 4:1 -> -6 dB at +10", tot);
        check (std::abs (e.curveA (-30) - 100 * std::tanh (3.0 / 100)) < 1e-9, "A: 6 dB under upward threshold, 1:2 -> +3 dB", e.curveA (-30));
        check (e.curveA (-110) == 0.0, "A: silence (-110 LUFS) is not lifted", e.curveA (-110));
        check (std::abs (e.curveB (-14) + 3 * std::tanh (15.0 / 3)) < 1e-9, "B: low-level downward capped by its range (3 dB)", e.curveB (-14));
        e.pkRmsDb = -14;
        check (std::abs (e.peakThr() - (-4)) < 1e-9, "D: crest mode, threshold = running RMS -14 + crest 10 = -4 dBFS", e.peakThr());
        check (std::abs (e.curveD (0) + 3) < 1e-9, "D: peak 4 dB over, 4:1 -> -3 dB", e.curveD (0));
    }
    {   // transparency, latency
        Engine e; e.prepare (fs, 2); e.setParams (neutral()); e.reset();
        std::vector<double> l (32768, 0.0), r (32768, 0.0); l[0] = 0.5; r[0] = -0.25;
        double* io[2] = { l.data(), r.data() };
        e.process (io, 2, 32768);
        double err = 0;
        for (int i = 0; i < 32768; ++i) if (i != e.latency()) err = std::max (err, std::abs (l[(size_t) i]) + std::abs (r[(size_t) i]));
        check (e.latency() > 256 && e.latency() < 16000, "latency at 48 kHz: look-ahead + gain FIR, constant", e.latency());
        err = std::max ({ err, std::abs (l[(size_t) e.latency()] - 0.5), std::abs (r[(size_t) e.latency()] + 0.25) });
        check (err < 1e-12, "all stages off: transparent (< -240 dB error), delayed by latency()", err);
    }
    for (int f : { 2, 4, 8 })
    {   // oversampled path keeps the same latency and is transparent in band
        Engine e; e.prepare (fs, 2); Params p = neutral(); p.os = f; e.setParams (p);
        const double out = runSine (e, fs, -6, 1, 1000);
        char msg[96]; std::snprintf (msg, sizeof msg, "oversampling %dx: 1 kHz passes at unity (+-0.05 dB)", f);
        check (std::abs (out + 6) < 0.05, msg, out);
        std::vector<double> l (32768, 0.0), r (32768, 0.0); l[0] = 1;
        double* io[2] = { l.data(), r.data() };
        e.process (io, 2, 32768);
        int pk = 0; for (int i = 0; i < 32768; ++i) if (std::abs (l[(size_t) i]) > std::abs (l[(size_t) pk])) pk = i;
        std::snprintf (msg, sizeof msg, "oversampling %dx: impulse peak at latency()", f);
        check (pk == e.latency(), msg, pk);
    }
    {   // steady downward through C
        Engine e; e.prepare (fs, 2); Params p = neutral(); p.dnOn = true; p.target = -14; p.thr1 = 2; p.ratio1 = 4; p.thr2 = 20; e.setParams (p);
        const double out = runSine (e, fs, -6, 5);
        check (std::abs (out + 10.5) < 0.3, "C: -6 in, thr -12, 4:1 -> -10.5 dBFS out", out);
    }
    {   // opto releases slower after long compression than RMS mode
        auto relTime = [&] (bool opto) {
            Engine e; e.prepare (fs, 2); Params p = neutral(); p.dnOn = true; p.opto = opto; p.thr1 = -6; p.ratio1 = 4; p.rel1 = 200; p.thr2 = 20; e.setParams (p);
            runSine (e, fs, -6, 4);
            const int blk = 480; std::vector<double> l (blk), r (blk); double* io[2] = { l.data(), r.data() };
            const double a = dbToLin (-30.0);
            for (int b = 0; b < 400; ++b)
            {
                for (int i = 0; i < blk; ++i) l[(size_t) i] = r[(size_t) i] = a * std::sin (2 * kPi * 997 * (b * blk + i) / fs);
                e.process (io, 2, blk);
                if (e.meterC.load() > -1.0f) return b * 10.0;
            }
            return 4000.0;
        };
        const double tr = relTime (false), to = relTime (true);
        check (to > tr * 1.5, "opto: recovery to -1 dB slower than RMS after 4 s of compression (ms)", to);
    }
    {   // hysteresis: with 3 dB hysteresis, a 2 dB drop of level does not release
        Engine e; e.prepare (fs, 2); Params p = neutral(); p.dnOn = true; p.thr1 = -8; p.ratio1 = 3; p.thr2 = 20; p.hyst = 3; p.rel1 = 50; e.setParams (p);
        runSine (e, fs, -6, 3);
        const float before = e.meterC.load();
        runSine (e, fs, -8, 2);
        const float after = e.meterC.load();
        check (std::abs (after - before) < 0.05, "hysteresis 3 dB: small level drop keeps the gain (dB change)", after - before);
        runSine (e, fs, -40, 2);
        check (e.meterC.load() > -0.1f, "hysteresis: a big drop releases fully", e.meterC.load());
    }
    {   // upward
        Engine e; e.prepare (fs, 2); Params p = neutral(); p.upOn = true; p.upSpectral = false; p.target = -14; p.upThr = -6; p.upRatio = 2; p.upRange = 20; e.setParams (p);
        const double out = runSine (e, fs, -30, 8);
        check (std::abs (out - (-30 + 20 * std::tanh (5.0 / 20))) < 0.3, "A: -30 in, thr -20, 1:2 -> -25.1 dBFS out", out);
    }
    {   // spectral pp: a quiet 100 Hz "cello" under a loud 2 kHz tone is lifted, the loud tone is not
        auto run = [&] (bool spectral, bool on, double& lo, double& hi) {
            Engine e; e.prepare (fs, 2); Params p = neutral(); p.upOn = on; p.upSpectral = spectral; p.target = -14; p.upThr = -6;
            p.upRatio = 2; p.upRange = 20; p.knee = 0; e.setParams (p);
            const int n = (int) (6 * fs), blk = 512, from = n - (int) fs / 2;
            std::vector<double> l (blk), r (blk); double* io[2] = { l.data(), r.data() };
            std::complex<double> a1, a2; const double al = dbToLin (-60), ah = dbToLin (-20);
            for (int s0 = 0; s0 < n; s0 += blk)
            {
                for (int i = 0; i < blk; ++i) { const double t = (s0 + i) / fs; l[(size_t) i] = r[(size_t) i] = al * std::sin (2 * kPi * 100 * t) + ah * std::sin (2 * kPi * 2000 * t); }
                e.process (io, 2, blk);
                for (int i = 0; i < blk; ++i) if (s0 + i >= from && s0 + i < n)
                {
                    const double t = (s0 + i) / fs;
                    a1 += l[(size_t) i] * std::polar (1.0, -2 * kPi * 100 * t); a2 += l[(size_t) i] * std::polar (1.0, -2 * kPi * 2000 * t);
                }
            }
            lo = linToDb (2 * std::abs (a1) / (fs / 2)); hi = linToDb (2 * std::abs (a2) / (fs / 2));
        };
        double lo0, hi0, lo1, hi1, lo2, hi2;
        run (true, false, lo0, hi0); run (true, true, lo1, hi1); run (false, true, lo2, hi2);
        check (std::abs (lo0 + 60) < 0.01 && std::abs (hi0 + 20) < 0.01, "pp spectral off: exact (tones at -60 / -20 dB)", lo0 + 60);
        check (lo1 - lo0 > 8, "pp spectral: quiet 100 Hz under a loud 2 kHz lifted > 8 dB", lo1 - lo0);
        check (std::abs (hi1 - hi0) < 0.3, "pp spectral: loud 2 kHz untouched (< 0.3 dB)", hi1 - hi0);
        check (lo2 - lo0 < 0.5, "pp broadband: the loud tone holds the quiet one down (no lift)", lo2 - lo0);
    }
    {   // density lifts low level more than high level
        auto lift = [&] (double lvl) {
            Engine a; a.prepare (fs, 2); a.setParams (neutral());
            Engine b; b.prepare (fs, 2); Params p = neutral(); p.density = 1; b.setParams (p);
            return runSine (b, fs, lvl, 3) - runSine (a, fs, lvl, 3);
        };
        const double lo = lift (-40), hi = lift (-8);
        check (lo > hi + 6, "density: lift at -40 dBFS much larger than at -8 dBFS (dB)", lo - hi);
    }
    {   // auto gain
        Engine e; e.prepare (fs, 2); Params p = neutral(); p.autoGain = true; p.target = -16; e.setParams (p);
        runSine (e, fs, -30, 60);
        check (std::abs (e.meterS.load() + 16) < 0.5, "auto gain brings -30 LUFS to the -16 LUFS target", e.meterS.load());
    }
    {   // loudness meters
        Engine e; e.prepare (fs, 2); e.setParams (neutral());
        runSine (e, fs, -20, 5);
        check (std::abs (e.meterS.load() + 20) < 0.15, "short-term: -20 dBFS stereo sine = -20 LUFS", e.meterS.load());
        check (std::abs (e.meterI.load() + 20) < 0.15, "integrated (gated) = -20 LUFS", e.meterI.load());
        check (std::abs (e.meterM.load() + 20) < 0.15, "momentary = -20 LUFS", e.meterM.load());
    }
    // limiter: noise with hot spikes, every mode
    for (int mode = 0; mode < 5; ++mode)
    {
        Engine e; e.prepare (fs, 2); Params p = neutral(); p.limOn = true; p.ceiling = -1;
        p.truePeak = mode != 0; p.os = mode <= 1 ? 1 : (mode == 2 ? 2 : mode == 3 ? 4 : 8);
        e.setParams (p);
        std::mt19937 rng (7); std::normal_distribution<double> nd (0, 0.2);
        const int n = (int) (fs * 3);
        std::vector<double> l ((size_t) n), r ((size_t) n);
        for (int i = 0; i < n; ++i) { l[(size_t) i] = nd (rng); r[(size_t) i] = nd (rng); if (i % 4800 == 100) l[(size_t) i] = 2.0; }
        for (int s = 0; s < n; s += 512) { double* io[2] = { &l[(size_t) s], &r[(size_t) s] }; e.process (io, 2, std::min (512, n - s)); }
        double sp = 0; for (int i = 0; i < n; ++i) sp = std::max ({ sp, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]) });
        const double tp = maxTruePeakDb (l, r);
        const char* names[] = { "limiter sample peak: no sample above -1 dBFS", "limiter true peak (1x): output true peak <= -0.8 dBTP",
                                "limiter 2x oversampled: true peak <= -0.7 dBTP", "limiter 4x oversampled: true peak <= -0.7 dBTP", "limiter 8x oversampled: true peak <= -0.7 dBTP" };
        if (mode == 0) check (linToDb (sp) <= -1.0 + 1e-6, names[mode], linToDb (sp));
        else check (tp <= (mode == 1 ? -0.8 : -0.7), names[mode], tp);
    }
    {   // unlinked limiter: a spike on L leaves R untouched
        Engine e; e.prepare (fs, 2); Params p = neutral(); p.limOn = true; p.linkT = 0; p.linkR = 0; p.ceiling = -1; e.setParams (p);
        const int n = 9600; std::vector<double> l ((size_t) n, 0.0), r ((size_t) n);
        for (int i = 0; i < n; ++i) { r[(size_t) i] = 0.3 * std::sin (2 * kPi * 500 * i / fs); l[(size_t) i] = 0.3 * std::sin (2 * kPi * 500 * i / fs); }
        l[4000] = 2.0;
        std::vector<double> rIn = r;
        double* io[2] = { l.data(), r.data() };
        e.process (io, 2, n);
        double err = 0; for (int i = e.latency(); i < n; ++i) err = std::max (err, std::abs (r[(size_t) i] - rIn[(size_t) (i - e.latency())]));
        check (err < 1e-12, "unlinked limiter: right channel untouched by a left spike", err);
    }
    {   // transients on a soft orchestral attack (40 ms rise, strings/brass-like), not only on drums
        auto attack = [&] (double amount) {
            Engine e; e.prepare (fs, 2); Params p = neutral(); p.transient = amount; e.setParams (p);
            const int n = (int) (3 * fs), t0 = (int) fs, rise = (int) (0.04 * fs);
            std::vector<double> l ((size_t) n, 0.0), r;
            for (int i = t0; i < n; ++i)
            {
                const double env = i - t0 < rise ? 0.5 - 0.5 * std::cos (kPi * (i - t0) / rise) : 1.0;
                const double t = i / fs;
                l[(size_t) i] = 0.3 * env * (std::sin (2 * kPi * 196 * t) + 0.5 * std::sin (2 * kPi * 392 * t) + 0.3 * std::sin (2 * kPi * 588 * t));
            }
            r = l; double* io[2] = { l.data(), r.data() };
            for (int s0 = 0; s0 < n; s0 += 512) { double* b[2] = { io[0] + s0, io[1] + s0 }; e.process (b, 2, std::min (512, n - s0)); }
            const int L = e.latency(); double a = 0, b = 0;   // energy in the first 60 ms vs 0.5..1 s later
            for (int i = t0; i < t0 + (int) (0.06 * fs); ++i) a += l[(size_t) (i + L)] * l[(size_t) (i + L)];
            for (int i = t0 + (int) (0.5 * fs); i < t0 + (int) fs; ++i) b += l[(size_t) (i + L)] * l[(size_t) (i + L)];
            return 10 * std::log10 ((a / 0.06) / (b / 0.5));
        };
        const double a0 = attack (0), am = attack (-1), ap = attack (1);
        check (am - a0 < -4, "transients -100%: soft 40 ms attack softened by more than 4 dB", am - a0);
        check (ap - a0 > 4, "transients +100%: soft 40 ms attack accentuated by more than 4 dB", ap - a0);
    }
    {   // ambiance: mono compatible, lifts the side in quiet passages, auto finds early reflections
        Engine e; e.prepare (fs, 2); Params p = neutral(); p.ambOn = true; p.ambiance = 1; p.ambThr = -8; p.ambRatio = 3; p.ambAuto = true; p.ambStage = 3;
        e.setParams (p);
        // synthetic room: clicks every 0.5 s with 3 early reflections (4, 9, 15 ms) then an exponential diffuse tail from 25 ms
        std::mt19937 rng (3); std::normal_distribution<double> nd (0, 1);
        const int n = (int) (fs * 12);
        std::vector<double> l ((size_t) n, 0.0), r ((size_t) n, 0.0), mono ((size_t) n, 0.0);
        for (int t0 = 0; t0 + 24000 <= n; t0 += 24000)
        {
            l[(size_t) t0] += 0.5; r[(size_t) t0] += 0.5;
            const int er[3] = { 192, 432, 720 }; const double eg[3] = { 0.3, 0.25, 0.2 };
            for (int k = 0; k < 3; ++k) { l[(size_t) (t0 + er[k])] += eg[k]; r[(size_t) (t0 + er[k] + 7)] += eg[k] * 0.9; }
            for (int i = 1200; i < 24000; ++i) { const double g = 0.05 * std::exp (-(i - 1200) / (0.25 * fs)); l[(size_t) (t0 + i)] += g * nd (rng); r[(size_t) (t0 + i)] += g * nd (rng); }
        }
        for (int i = 0; i < n; ++i) mono[(size_t) i] = l[(size_t) i] + r[(size_t) i];
        for (int s = 0; s < n; s += 512) { double* io[2] = { &l[(size_t) s], &r[(size_t) s] }; e.process (io, 2, std::min (512, n - s)); }
        double err = 0; for (int i = e.latency(); i < n; ++i) err = std::max (err, std::abs (l[(size_t) i] + r[(size_t) i] - mono[(size_t) (i - e.latency())]));
        check (err < 1e-9, "ambiance: L+R (mono) unchanged", err);
        check (e.meterAmb.load() > 1.0f, "ambiance: tails lifted (dB of upward on the side)", e.meterAmb.load());
        const float er = e.meterEr.load();
        check (er > 22 && er < 32, "ambiance auto: early reflections end found (synthetic 25 ms, starts at 20)", er);
    }
    {   // transients: round off lowers the attack of a drum-like burst, keeps its body
        auto burst = [&] (double amount, double& attackPk, double& bodyRms) {
            Engine e; e.prepare (fs, 2); Params p = neutral(); p.transient = amount; e.setParams (p);
            const int n = (int) (2 * fs); std::vector<double> l ((size_t) n, 0.0), r;
            for (int t0 = 0; t0 < n; t0 += 12000)
                for (int i = 0; i < 6000; ++i) l[(size_t) (t0 + i)] = 0.5 * std::exp (-i / 600.0) * std::sin (2 * kPi * 200 * i / fs) + 0.1 * std::sin (2 * kPi * 200 * i / fs);
            r = l;
            for (int s = 0; s < n; s += 512) { double* io[2] = { &l[(size_t) s], &r[(size_t) s] }; e.process (io, 2, std::min (512, n - s)); }
            const int t0 = 3 * 12000 + e.latency();
            attackPk = 0; for (int i = 0; i < 300; ++i) attackPk = std::max (attackPk, std::abs (l[(size_t) (t0 + i)]));
            bodyRms = 0; for (int i = 4000; i < 6000; ++i) bodyRms += l[(size_t) (t0 + i)] * l[(size_t) (t0 + i)];
        };
        double a0, b0, a1, b1; burst (0, a0, b0); burst (-1, a1, b1);
        check (linToDb (a1 / a0) < -3, "transients -100%: attack peak lowered by more than 3 dB", linToDb (a1 / a0));
        check (std::abs (10 * std::log10 (b1 / b0)) < 1.0, "transients -100%: body within 1 dB", 10 * std::log10 (b1 / b0));
    }
    {   // external insert on Side / Mid: the other part of M/S stays untouched, blocks never exceed the limit
        struct Fake { int maxN = 0; static void run (void* c, double* a, double* b, int n) { auto* f = (Fake*) c; f->maxN = std::max (f->maxN, n); for (int i = 0; i < n; ++i) { a[i] *= 0.5; b[i] *= 0.5; } } };
        for (int target : { 0, 1 })
        {
            Fake fk; Engine e; e.prepare (fs, 2); e.setInsert (&Fake::run, &fk);
            Params p = neutral(); p.revOn = true; p.revTarget = target; e.setParams (p); e.reset();
            std::mt19937 rng (5); std::normal_distribution<double> nd (0, 1);
            const int n = (int) fs;
            std::vector<double> l ((size_t) n), r ((size_t) n), mono ((size_t) n), side ((size_t) n);
            for (int i = 0; i < n; ++i) { l[(size_t) i] = 0.2 * nd (rng); r[(size_t) i] = 0.2 * nd (rng); mono[(size_t) i] = l[(size_t) i] + r[(size_t) i]; side[(size_t) i] = l[(size_t) i] - r[(size_t) i]; }
            for (int s0 = 0; s0 < n; s0 += 1000) { double* io[2] = { &l[(size_t) s0], &r[(size_t) s0] }; e.process (io, 2, std::min (1000, n - s0)); }
            double eKeep = 0, eHalf = 0;
            for (int i = e.latency(); i < n; ++i)
            {
                const size_t k = (size_t) i, j = (size_t) (i - e.latency());
                const double keep = target == 0 ? (l[k] - r[k]) - side[j] : (l[k] + r[k]) - mono[j];
                const double half = target == 0 ? (l[k] + r[k]) - 0.5 * mono[j] : (l[k] - r[k]) - 0.5 * side[j];
                eKeep = std::max (eKeep, std::abs (keep)); eHalf = std::max (eHalf, std::abs (half));
            }
            check (eKeep < 1e-9 && eHalf < 1e-9, target == 0 ? "insert on Mid: mid processed, side untouched" : "insert on Side: side processed, mono sum untouched", std::max (eKeep, eHalf));
            check (fk.maxN <= Engine::maxInsertBlock(), "insert blocks never exceed the declared size", fk.maxN);
        }
    }
    {   // ambiance transients: with the knob up, the lifted ambiance carries much less of the attacks, tails stay
        auto run = [&] (double tr, double& att, double& tail) {
            Engine e; e.prepare (fs, 2); Params p = neutral(); p.ambOn = true; p.ambiance = 1; p.ambThr = 0; p.ambRatio = 3;
            p.ambAuto = false; p.ambPreMs = 10; p.ambStage = 3; p.ambTr = tr; e.setParams (p);
            Engine e0; e0.prepare (fs, 2); Params p0 = p; p0.ambOn = false; e0.setParams (p0);
            std::mt19937 rng (9); std::normal_distribution<double> nd (0, 1);
            const int n = (int) (fs * 4), per = 24000;
            std::vector<double> l ((size_t) n, 0.0), r ((size_t) n, 0.0);
            for (int t0 = 0; t0 + per <= n; t0 += per)
                for (int i = 0; i < per; ++i) { const double g = (i < 240 ? 0.6 : 0.0) + 0.03 * std::exp (-i / (0.3 * fs)); l[(size_t) (t0 + i)] += g * nd (rng); r[(size_t) (t0 + i)] += g * nd (rng); }
            auto l0 = l, r0 = r;
            for (int s0 = 0; s0 < n; s0 += 512) { double* io[2] = { &l[(size_t) s0], &r[(size_t) s0] }; e.process (io, 2, std::min (512, n - s0)); double* io0[2] = { &l0[(size_t) s0], &r0[(size_t) s0] }; e0.process (io0, 2, std::min (512, n - s0)); }
            att = 0; tail = 0;
            for (int t0 = per * 2; t0 + per <= n; t0 += per)
                for (int i = 0; i < per; ++i)
                {
                    const size_t k = (size_t) (t0 + i + e.latency());
                    if (k >= (size_t) n) break;
                    const double d = (l[k] - r[k]) - (l0[k] - r0[k]);
                    if (i < (int) (0.04 * fs)) att += d * d; else if (i > (int) (0.15 * fs) && i < (int) (0.45 * fs)) tail += d * d;
                }
        };
        double a0, t0, a1, t1; run (0, a0, t0); run (1, a1, t1);
        check (10 * std::log10 (a1 / a0) < -6, "ambiance transients 100%: attacks in the lifted ambiance down > 6 dB", 10 * std::log10 (a1 / a0));
        check (std::abs (10 * std::log10 (t1 / t0)) < 1.5, "ambiance transients 100%: lifted tails within 1.5 dB", 10 * std::log10 (t1 / t0));
    }
    {   // M/S: centre +1.5 dB broadband on a mono signal, side low cut 24 dB/oct at 120 Hz on a 40 Hz side
        Engine e; e.prepare (fs, 2); Params p = neutral(); p.centreDb = 1.5; e.setParams (p);
        const double out = runSine (e, fs, -12, 1);
        check (std::abs (out + 10.5) < 0.02, "centre +1.5 dB (broadband) on a mono sine", out);
        Engine e2; e2.prepare (fs, 2); p = neutral(); p.sideLowCut = 120; p.sideSlope = 24; e2.setParams (p);
        const int n = (int) fs; std::vector<double> l ((size_t) n), r ((size_t) n);
        for (int i = 0; i < n; ++i) { const double s = 0.25 * std::sin (2 * kPi * 40 * i / fs); l[(size_t) i] = s; r[(size_t) i] = -s; }
        for (int s = 0; s < n; s += 512) { double* io[2] = { &l[(size_t) s], &r[(size_t) s] }; e2.process (io, 2, std::min (512, n - s)); }
        double pk = 0; for (int i = n / 2; i < n; ++i) pk = std::max (pk, std::abs (l[(size_t) i]));
        check (linToDb (pk / 0.25) < -30, "side low cut 120 Hz 24 dB/oct: 40 Hz side down > 30 dB", linToDb (pk / 0.25));
        Engine e3; e3.prepare (fs, 2); p = neutral(); p.centreDb = 3; p.centreFilter = 1; p.centreFreq = 200; e3.setParams (p);
        const double lo = runSine (e3, fs, -12, 1, 50), hi = runSine (e3, fs, -12, 1, 5000);
        check (std::abs (lo + 9) < 0.2 && std::abs (hi + 12) < 0.1, "centre low shelf +3 dB at 200 Hz: 50 Hz +3, 5 kHz 0", lo);
    }
    {   // float mono
        Engine e; e.prepare (44100, 1); e.setParams (Params());
        std::vector<float> m (1024, 0.1f); float* io[1] = { m.data() };
        e.process (io, 1, 1024);
        check (std::isfinite (m[1023]), "float mono processing runs, default settings", m[1023]);
    }
    {   // aliasing: nothing folds back below 20 kHz
        check (aliasBandDb (48000, 1) < -130, "no aliasing 48k 1x: 5-16 kHz band vs 21 kHz tone (dB)", aliasBandDb (48000, 1));
        check (aliasBandDb (44100, 1) < -130, "no aliasing 44.1k 1x: 5-16 kHz band vs 21 kHz tone (dB)", aliasBandDb (44100, 1));
        // oversampled, the gains are fast (up to fs/2): a 21 kHz tone then gets real modulation sidebands across the
        // band (what any fast compressor does), so only report it; fold-back itself is checked with the stages off
        std::printf ("       oversampled 4x / 40x, 21 kHz tone, all stages hard: 5-16 kHz band %.1f / %.1f dB (modulation, not fold-back)\n", aliasBandDb (48000, 4), aliasBandDb (48000, 40));
    }
    {   // oversampling: flat to 20 kHz (the old filter lost 3.5 dB at 20 kHz), latency unchanged
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (int f : { 2, 8, 40 })
            {
                double worst = 0; int lat0 = 0, lat1 = 0;
                for (double fr : { 1000.0, 10000.0, 18000.0, 20000.0 })
                {
                    Engine a; a.prepare (rate, 2); a.setParams (neutral()); a.reset(); lat0 = a.latency();
                    Engine b; b.prepare (rate, 2); Params p = neutral(); p.os = f; b.setParams (p); b.reset(); lat1 = b.latency();
                    worst = std::max (worst, std::abs (runSine (b, rate, -12, 1, fr) - runSine (a, rate, -12, 1, fr)));
                }
                char what[120]; std::snprintf (what, sizeof what, "oversampling %dx at %.1f kHz: flat 1..20 kHz (< 0.01 dB), same latency", f, rate / 1000);
                check (worst < 0.01 && lat0 == lat1, what, worst);
            }
    }
    {   // oversampling filter by hand: minimum phase keeps the magnitude and removes the pre-ringing
        std::vector<double> lin, mn; std::vector<std::complex<double>> w; int lo, hi;
        Oversampler::design (2, 128, fs, 24000, 0.85, 1.0, lin, lo, hi, w);
        Oversampler::design (2, 128, fs, 24000, 0.85, 0.0, mn, lo, hi, w);
        const int c = (int) (lin.size() - 1) / 2; double preL = 0, preM = 0, totL = 0, totM = 0;
        for (int i = 0; i < (int) lin.size(); ++i) { totL += lin[(size_t) i] * lin[(size_t) i]; totM += mn[(size_t) i] * mn[(size_t) i]; if (i < c) { preL += lin[(size_t) i] * lin[(size_t) i]; preM += mn[(size_t) i] * mn[(size_t) i]; } }
        check (10 * std::log10 (preM / totM + 1e-30) < -100, "OS filter minimum phase: energy before the centre < -100 dB", 10 * std::log10 (preM / totM + 1e-30));
        double postL = 0; for (int i = c + 1; i < (int) lin.size(); ++i) postL += lin[(size_t) i] * lin[(size_t) i];
        check (std::abs (10 * std::log10 (preL / postL)) < 0.01, "OS filter linear phase: as much ringing before as after (dB)", 10 * std::log10 (preL / postL));
        double worst = 0;
        for (double fr : { 1000.0, 10000.0, 18000.0, 20000.0 })
        {
            Engine a; a.prepare (fs, 2); a.setParams (neutral()); a.reset();
            Engine b; b.prepare (fs, 2); Params p = neutral(); p.os = 8; p.osPre = 0; b.setParams (p); b.reset();
            worst = std::max (worst, std::abs (runSine (b, fs, -12, 1, fr) - runSine (a, fs, -12, 1, fr)));
        }
        check (worst < 0.05, "OS 8x minimum phase: flat 1..20 kHz (< 0.05 dB)", worst);
        Engine g; g.prepare (fs, 2); Params p = neutral(); p.os = 8; p.osCut = 19000; p.osSteep = 0; g.setParams (p); g.reset();
        Engine a; a.prepare (fs, 2); a.setParams (neutral()); a.reset();
        const double d = runSine (g, fs, -12, 1, 18000) - runSine (a, fs, -12, 1, 18000);
        check (d < -1.0, "OS cutoff 19 kHz, gentle slope: 18 kHz audibly rolled off (dB)", d);
    }
    {   // real-time cost at 40x, every stage on (seconds of CPU per second of audio)
        for (int f : { 1, 8, 40 }) {
        Engine e; e.prepare (fs, 2); Params p; p.os = f; e.setParams (p); e.reset();
        const int n = (int) fs * 2; std::vector<double> l ((size_t) n), r ((size_t) n);
        for (int i = 0; i < n; ++i) l[(size_t) i] = r[(size_t) i] = 0.3 * std::sin (2 * kPi * 440 * i / fs);
        const auto t0 = std::chrono::steady_clock::now();
        for (int s0 = 0; s0 < n; s0 += 512) { double* b[2] = { &l[(size_t) s0], &r[(size_t) s0] }; e.process (b, 2, std::min (512, n - s0)); }
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("       CPU at %dx (48 kHz stereo, all on): %.3f s per s of audio\n", f, sec / 2.0); }
    }
    {   // oversampling: the peak stage reacts faster in time (gain no longer limited to a few hundred Hz)
        auto firstMs = [&] (int f) {
            Engine e; e.prepare (fs, 2); Params p = neutral(); p.pkOn = true; p.pkCrest = false; p.pkThr = -20; p.pkRatio = 20; p.pkAtt = 0.05; p.pkRel = 50; p.os = f;
            e.setParams (p); e.reset();
            const int n = (int) (1.0 * fs), t0 = (int) (0.25 * fs);
            std::vector<double> l ((size_t) n, 0.0), r;
            for (int i = t0; i < n; ++i) l[(size_t) i] = 0.5 * std::sin (2 * kPi * 3000 * (i - t0) / fs);
            r = l; double* io[2] = { l.data(), r.data() };
            for (int s0 = 0; s0 < n; s0 += 512) { double* b[2] = { io[0] + s0, io[1] + s0 }; e.process (b, 2, std::min (512, n - s0)); }
            double pk = 0; const int L = e.latency();
            for (int i = t0; i < t0 + (int) (0.0003 * fs); ++i) pk = std::max (pk, std::abs (l[(size_t) (i + L)]));
            return linToDb (pk / 0.5);
        };
        const double g1 = firstMs (1), g8 = firstMs (8);
        std::printf ("       peak stage, first 0.3 ms of a hard onset: 1x %.2f dB, 8x %.2f dB\n", g1, g8);
        check (g8 < g1 - 1.0, "oversampling 8x: peak stage clamps the onset faster than 1x (dB lower)", g8 - g1);
    }
    {   // tilt: exact magnitude in linear and minimum phase; Mid or Side alone leaves the other untouched
        for (int mp = 0; mp < 2; ++mp)
        {
            TiltEq t; t.prepare (fs); t.design (t.H[1], 1.5, 1000.0, mp == 1);
            const auto& h = t.lastTaps();
            double worst = 0;
            for (double f : { 30.0, 100.0, 440.0, 1000.0, 3000.0, 10000.0, 18000.0 })
            {
                std::complex<double> H (0);
                for (size_t n = 0; n < h.size(); ++n) H += h[n] * std::polar (1.0, -2 * kPi * f * (double) n / fs);
                const double e = std::abs (20 * std::log10 (std::abs (H)) - TiltEq::targetDb (f, 1.5, 1000.0));
                worst = std::max (worst, e);
            }
            check (worst < 0.01, mp ? "tilt min phase: magnitude error 30 Hz..18 kHz < 0.01 dB" : "tilt linear: magnitude error 30 Hz..18 kHz < 0.01 dB", worst);
        }
        TiltEq t; t.prepare (fs); t.setTarget (1.5, 1000.0, false, 2); t.mixM = 0; t.mixS = 1;
        const int n = 30000; double err = 0; std::vector<double> in ((size_t) n);
        for (int i = 0; i < n; ++i) in[(size_t) i] = std::sin (2 * kPi * 300 * i / fs) + 0.3 * std::sin (2 * kPi * 7000 * i / fs);
        for (int i = 0; i < n; ++i)
        {
            double v[2] = { in[(size_t) i], in[(size_t) i] };   // mono: all Mid
            t.process (v, 2);
            if (i > 20000) err = std::max (err, std::abs (v[0] - in[(size_t) (i - TiltEq::latency())]));
        }
        check (err < 1e-12, "tilt on SIDE: a mono (Mid) signal comes out only delayed", err);
    }
    std::printf (fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
