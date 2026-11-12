// Parameter audit: every parameter from its minimum to its maximum, through the real plugin (APVTS), on a
// music-like stereo signal mastered to about -1 dBFS peak: how much does the output change? A control that
// changes the output by less than -40 dB (relative to the output) is reported as INERT.
#include "PluginProcessor.h"
#include <random>
#include <cstdio>
#include <map>
#include <cstdlib>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

static const double fs = 48000;

// classical-like: strings (detuned saws, soft attack), hall reverb, a crescendo pp -> ff with timpani and a voice,
// then a decay into the room. Peak normalised to -1 dBFS.
static std::vector<double> classical (int n)
{
    std::mt19937 rng (5); std::normal_distribution<double> nd (0, 1);
    std::vector<double> L ((size_t) n), R ((size_t) n);
    std::vector<double> c1 (3001, 0), c2 (3517, 0), c3 (4093, 0), c4 (4441, 0); size_t i1 = 0, i2 = 0, i3 = 0, i4 = 0;
    double lpL = 0, lpR = 0, tim = 0, timPh = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        // dynamic contour in dB: pp 0-4 s, mp 4-8, mf 8-12, ff 12-16, then silence (tails only)
        const double dyn = t < 4 ? -40 : t < 8 ? -30 : t < 12 ? -20 : t < 16 ? -8 : -200;
        double g = std::pow (10.0, dyn / 20.0) * (t < 16 ? std::min (1.0, std::fmod (t, 4.0) / 0.3) : 0.0);
        if (t >= 16.3 && t < 16.38) g = std::pow (10.0, -8 / 20.0) * (1.0 - (t - 16.3) / 0.08);   // final staccato chord into the hall
        double sL = 0, sR = 0;
        for (double f : { 196.0, 246.9, 293.7, 392.0 })
        {
            sL += (2 * std::fmod (f * t + 0.1 * std::sin (2 * M_PI * 5.1 * t), 1.0) - 1);
            sR += (2 * std::fmod (f * 1.002 * t + 0.1 * std::sin (2 * M_PI * 4.7 * t), 1.0) - 1);
        }
        lpL += 0.08 * (sL - lpL); lpR += 0.08 * (sR - lpR);   // soft strings
        double voice = 0;
        if (t > 12 && t < 16) voice = 0.6 * std::sin (2 * M_PI * (440 + 6 * std::sin (2 * M_PI * 5.5 * t)) * t) * (0.5 + 0.5 * std::sin (2 * M_PI * 0.5 * t));
        if (t > 12 && t < 16 && std::fmod (t, 1.0) < 1.0 / fs) { tim = 1.0; timPh = 0; }
        timPh += 2 * M_PI * 70 / fs; tim *= std::exp (-1.0 / (0.35 * fs));
        const double timp = 1.4 * tim * (std::sin (timPh) + 0.3 * nd (rng) * tim);
        const double dryL = g * (0.25 * lpL + 0.2 * voice) + g * timp, dryR = g * (0.25 * lpR + 0.2 * voice) + g * timp;
        const double send = 0.5 * (dryL + dryR);
        const double a = c1[i1], b = c2[i2], c = c3[i3], d = c4[i4];
        c1[i1] = send + 0.86 * a; c2[i2] = send + 0.87 * b; c3[i3] = send + 0.88 * c; c4[i4] = send + 0.85 * d;
        i1 = (i1 + 1) % c1.size(); i2 = (i2 + 1) % c2.size(); i3 = (i3 + 1) % c3.size(); i4 = (i4 + 1) % c4.size();
        L[(size_t) i] = dryL + 0.06 * (a - c); R[(size_t) i] = dryR + 0.06 * (b - d);
    }
    double pk = 0; for (int i = 0; i < n; ++i) pk = std::max ({ pk, std::abs (L[(size_t) i]), std::abs (R[(size_t) i]) });
    const double g = std::pow (10.0, -1.0 / 20) / pk;
    std::vector<double> out ((size_t) (2 * n));
    for (int i = 0; i < n; ++i) { out[(size_t) i] = L[(size_t) i] * g; out[(size_t) (n + i)] = R[(size_t) i] * g; }
    return out;
}

static std::vector<double> music (int ch, int n)
{
    std::mt19937 rng (11); std::normal_distribution<double> nd (0, 1);
    std::vector<double> L ((size_t) n), R ((size_t) n);
    // simple decorrelated "room": two comb + allpass chains
    std::vector<double> cl (2311, 0), cr (2797, 0); size_t il = 0, ir = 0;
    double hp = 0, lp = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs, beat = std::fmod (t, 0.5), half = std::fmod (t + 0.25, 0.5), q = std::fmod (t, 0.125);
        const double sect = (t > 3.0 && t < 5.5) ? 0.18 : 1.0;          // quiet passage with tails
        const double kick = 0.6 * std::exp (-beat / 0.15) * std::sin (2 * M_PI * (50 + 60 * std::exp (-beat / 0.03)) * beat);
        const double nz = nd (rng);
        const double snare = 0.35 * std::exp (-half / 0.08) * (0.7 * nz + 0.3 * std::sin (2 * M_PI * 200 * half));
        hp = 0.6 * (hp + nz - lp); lp = nz;
        const double hat = 0.08 * std::exp (-q / 0.02) * hp;
        const double bassF = std::fmod (t, 2.0) < 1.0 ? 55.0 : 73.4;
        const double bass = 0.25 * std::sin (2 * M_PI * bassF * t);
        double padL = 0, padR = 0;
        for (double f : { 220.0, 277.2, 329.6 })
        {
            padL += 0.05 * (2 * std::fmod (f * t, 1.0) - 1) * (0.8 + 0.2 * std::sin (2 * M_PI * 0.3 * t));
            padR += 0.05 * (2 * std::fmod (f * 1.003 * t, 1.0) - 1) * (0.8 + 0.2 * std::sin (2 * M_PI * 0.37 * t));
        }
        const double send = snare + 0.5 * (padL + padR);
        const double rl = cl[il], rr = cr[ir];
        cl[il] = send + 0.82 * rl; cr[ir] = send + 0.83 * rr; il = (il + 1) % cl.size(); ir = (ir + 1) % cr.size();
        const double dry = sect * (kick + snare + hat + bass);
        L[(size_t) i] = dry + sect * (padL + 0.12 * rl);
        R[(size_t) i] = dry + sect * (padR + 0.12 * rr);
    }
    double pk = 0; for (int i = 0; i < n; ++i) pk = std::max ({ pk, std::abs (L[(size_t) i]), std::abs (R[(size_t) i]) });
    const double g = std::pow (10.0, -1.0 / 20) / pk;
    std::vector<double> out ((size_t) (2 * n));
    for (int i = 0; i < n; ++i) { out[(size_t) i] = L[(size_t) i] * g; out[(size_t) (n + i)] = R[(size_t) i] * g; }
    (void) ch;
    return out;
}

struct Result { std::vector<double> l, r; };

struct Zones { double a[6][5] {}, mx[6][5] {}; int cnt[5] {}; };   // mean for the RMS stages, max for peak/limiter   // per 4 s section: amb, up, ll, dn, pk, lim
static Result render (DeltaCompProcessor& p, const std::vector<double>& in, int n, Zones* z = nullptr)
{
    p.prepareToPlay (fs, 512);
    Result res; res.l.resize ((size_t) n); res.r.resize ((size_t) n);
    juce::AudioBuffer<double> b (2, 512); juce::MidiBuffer m;
    for (int s = 0; s < n; s += 512)
    {
        const int k = std::min (512, n - s);
        b.setSize (2, k, false, false, true);
        for (int i = 0; i < k; ++i) { b.setSample (0, i, in[(size_t) (s + i)]); b.setSample (1, i, in[(size_t) (n + s + i)]); }
        static_cast<juce::AudioProcessor&> (p).processBlock (b, m);
        for (int i = 0; i < k; ++i) { res.l[(size_t) (s + i)] = b.getSample (0, i); res.r[(size_t) (s + i)] = b.getSample (1, i); }
        if (z != nullptr)
        {
            const int sec = std::min (4, (int) (s / (4 * fs)));
            if (std::fmod (s, 4 * fs) < 1.5 * fs) continue;   // let each section settle
            auto& e = p.engine;
            const double v[6] = { e.meterAmb.load(), e.meterA.load(), e.meterB.load(), e.meterC.load(), e.meterD.load(), e.meterLim.load() };
            for (int j = 0; j < 4; ++j) z->a[j][sec] += v[j];
            for (int j = 4; j < 6; ++j) z->mx[j][sec] = std::min (z->mx[j][sec], v[j]);
            z->cnt[sec]++;
        }
    }
    return res;
}

// difference against the reference, relative to the reference energy of each 4 s section (pp, mp, mf, ff,
// tails), the largest of the five: a control made for the quiet zones counts as much as one for the loud ones
static double diffDb (const Result& a, const Result& ref, int from)
{
    double worst = -300;
    const size_t sec = (size_t) (4 * fs);
    for (size_t s0 = 0; s0 < ref.l.size(); s0 += sec)
    {
        double d = 0, e = 0;
        for (size_t i = std::max (s0, (size_t) from); i < std::min (ref.l.size(), s0 + sec); ++i)
        {
            d += (a.l[i] - ref.l[i]) * (a.l[i] - ref.l[i]) + (a.r[i] - ref.r[i]) * (a.r[i] - ref.r[i]);
            e += ref.l[i] * ref.l[i] + ref.r[i] * ref.r[i];
        }
        if (e > 1e-12) worst = std::max (worst, 10 * std::log10 (d / e + 1e-30));
    }
    return worst;
}
static double rmsDb (const Result& a, int from)
{
    double e = 0; for (size_t i = (size_t) from; i < a.l.size(); ++i) e += a.l[i] * a.l[i] + a.r[i] * a.r[i];
    return 10 * std::log10 (e / (2.0 * (a.l.size() - (size_t) from)) + 1e-30);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const bool pop = std::getenv ("AUDIT_POP") != nullptr;
    const int n = (int) (fs * (pop ? 8 : 20));
    const auto in = pop ? music (2, n) : classical (n);
    {   // section loudness of the input (K-weighting skipped: plain RMS per 4 s)
        for (int k = 0; k * 4 * fs < n; ++k)
        {
            double e = 0; const int a = (int) (k * 4 * fs), b = std::min (n, (int) ((k + 1) * 4 * fs));
            for (int i = a; i < b; ++i) e += in[(size_t) i] * in[(size_t) i] + in[(size_t) (n + i)] * in[(size_t) (n + i)];
            std::printf ("input %2d-%2d s: %.1f dB RMS\n", k * 4, (k + 1) * 4, 10 * std::log10 (e / (b - a) + 1e-30));
        }
    }
    std::unique_ptr<juce::AudioProcessor> ap (createPluginFilter());
    auto& p = static_cast<DeltaCompProcessor&> (*ap);
    p.setProcessingPrecision (juce::AudioProcessor::doublePrecision);
    if (std::getenv ("AUDIT_TILT"))
    {   // tilt check: sine level out at 100 Hz and 10 kHz, tilt at 0 and at +1.5 dB/oct, everything else default
        for (float t : { 0.0f, 3.0f, -3.0f })
        {
            auto* tp = p.apvts.getParameter ("tilt"); tp->setValueNotifyingHost (tp->convertTo0to1 (t));
            std::printf ("tilt %+.1f:", t);
            for (double f : { 100.0, 1000.0, 10000.0 })
            {
                std::vector<double> sn ((size_t) (2 * n));
                for (int i = 0; i < n; ++i) sn[(size_t) i] = sn[(size_t) (n + i)] = 0.05 * std::sin (2 * M_PI * f * i / fs);
                const Result r = render (p, sn, n);
                std::printf ("  %g Hz %.2f dB", f, rmsDb (r, n / 2));
            }
            std::printf ("\n");
        }
        return 0;
    }
    // base: defaults with every module switched on
    const char* ons[] = { "trOn", "upOn", "llOn", "dnOn", "pkOn", "ambOn", "msOn", "tiltOn", "limOn", "active" };
    for (auto* id : ons) p.apvts.getParameter (id)->setValueNotifyingHost (1.0f);
    const auto base = p.apvts.copyState();
    Zones z;
    const Result ref = render (p, in, n, &z);
    const int from = (int) fs + p.getLatencySamples();
    std::printf ("reference: output RMS %.1f dBFS, latency %d\n", rmsDb (ref, from), p.getLatencySamples());
    {
        const char* rows[] = { "ambiance", "upward", "bas niv.", "double", "peak", "limiteur" };
        std::printf ("\nmean gain per section (dB)   %s\n", pop ? "0-4  4-8" : "pp      mp      mf      ff      queues");
        for (int j = 0; j < 6; ++j)
        {
            std::printf ("  %-10s", rows[j]);
            for (int k = 0; k < 5; ++k) if (z.cnt[k]) std::printf ("%+7.1f ", j < 4 ? z.a[j][k] / z.cnt[k] : z.mx[j][k]);
            if (j >= 4) std::printf (" (max)");
            std::printf ("\n");
        }
        // output RMS per section
        std::printf ("  %-10s", "sortie RMS");
        for (int k = 0; k * 4 * fs < n; ++k)
        {
            double e = 0; const int a0 = (int) (k * 4 * fs + 1.5 * fs) + p.getLatencySamples(), b0 = std::min (n, (int) ((k + 1) * 4 * fs) + p.getLatencySamples());
            for (int i = a0; i < b0; ++i) e += ref.l[(size_t) i] * ref.l[(size_t) i] + ref.r[(size_t) i] * ref.r[(size_t) i];
            std::printf ("%7.1f ", 10 * std::log10 (e / std::max (1, b0 - a0) + 1e-30));
        }
        std::printf ("\n\n");
        std::printf ("auto early-reflection end: %.1f ms\n\n", (double) p.engine.meterEr.load());
        if (std::getenv ("AUDIT_ZONES")) return 0;
    }
    int inert = 0;
    for (auto* prm : p.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm);
        if (rp == nullptr) continue;
        const juce::String id = rp->getParameterID();
        if (argc > 1 && id != argv[1]) continue;
        if (id == "revOn" || id == "revTarget") { std::printf ("%-6s %-12s (needs a plugin loaded in the insert)\n", "skip", id.toRawUTF8()); continue; }
        // a few controls only matter once their parent control is away from neutral
        static const std::map<juce::String, std::vector<std::pair<const char*, float>>> ctx = {
            { "trDur", { { "transient", -60 } } }, { "trTarget", { { "transient", -60 } } }, { "trOn", { { "transient", -60 } } },
            { "msOn", { { "width", 3 } } }, { "tiltOn", { { "tilt", 3 } } }, { "tiltPivot", { { "tilt", 3 } } }, { "tiltPhase", { { "tilt", 3 } } }, { "tiltPos", { { "tilt", 3 } } }, { "tiltTarget", { { "tilt", 3 } } },
            { "centreFilter", { { "centre", 3 } } }, { "centreFreq", { { "centre", 3 }, { "centreFilter", 3 } } },
            { "sideSlope", { { "sideLowCut", 150 } } }, { "ambPre", { { "ambAuto", 0 } } }, { "crest", { { "pkMode", 1 } } },
            { "pkThr", { { "pkMode", 0 } } },
            // the limiter only works on overs: lower the ceiling so its own controls have something to act on
            { "limOn", { { "ceiling", -6 } } }, { "look", { { "ceiling", -6 } } }, { "truePeak", { { "ceiling", -6 } } }, { "linkT", { { "ceiling", -6 } } },
            { "linkR", { { "ceiling", -6 } } }, { "limRel", { { "ceiling", -6 } } }, { "os", { { "ceiling", -6 } } } };
        double worst = -300; juce::String txt;
        for (float v : { 0.0f, 1.0f })
        {
            p.apvts.replaceState (base.createCopy());
            juce::String note;
            if (auto it = ctx.find (id); it != ctx.end())
                for (auto& [cid, val] : it->second)
                {
                    auto* cp = p.apvts.getParameter (cid);
                    cp->setValueNotifyingHost (cp->convertTo0to1 (val));
                }
            const Result rc = ctx.count (id) ? render (p, in, n) : ref;
            rp->setValueNotifyingHost (v);
            const Result r = render (p, in, n);
            const double d = diffDb (r, rc, from);
            worst = std::max (worst, d);
            txt << rp->getCurrentValueAsText() << ": " << juce::String (d, 1) << " dB  ";
        }
        const bool dead = worst < -40;
        inert += dead ? 1 : 0;
        std::printf ("%-6s %-12s %s\n", dead ? "INERT" : "ok", id.toRawUTF8(), txt.toRawUTF8());
    }
    std::printf ("\n%d inert parameter(s)\n", inert);
    return 0;
}
