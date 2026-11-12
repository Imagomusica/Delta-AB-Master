// Click test: every parameter swept from min to max (and back) while a sine plays through the real plugin, block by
// block as a host does when a knob is turned. Reports the worst discontinuity (residual after removing the smooth
// part) relative to the same render without the sweep, and the slowest block (CPU spike) per parameter.
#include "PluginProcessor.h"
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <cstdlib>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

struct R { double click = 0, maxMs = 0; int at = 0; };

static R run (juce::RangedAudioParameter* prm, int blk)
{
    std::unique_ptr<juce::AudioProcessor> ap (createPluginFilter());
    auto& p = static_cast<DeltaCompProcessor&> (*ap);
    p.setProcessingPrecision (juce::AudioProcessor::doublePrecision);
    p.prepareToPlay (48000, blk);
    juce::RangedAudioParameter* q = prm ? p.apvts.getParameter (prm->paramID) : nullptr;
    const double fs = 48000; const int n = (int) (6 * fs), sweepFrom = (int) (2 * fs), sweepTo = (int) (5 * fs);
    std::vector<double> out ((size_t) n);
    juce::AudioBuffer<double> b (2, blk); juce::MidiBuffer midi;
    R r;
    for (int s0 = 0; s0 < n; s0 += blk)
    {
        if (q && s0 >= sweepFrom && s0 < sweepTo)
        {   // up then down, as a hand on a knob
            const double t = (double) (s0 - sweepFrom) / (sweepTo - sweepFrom);
            q->setValueNotifyingHost ((float) (t < 0.5 ? 2 * t : 2 - 2 * t));
        }
        for (int i = 0; i < blk; ++i) { const double x = 0.2 * std::sin (2 * M_PI * 220.0 * (s0 + i) / fs); b.setSample (0, i, x); b.setSample (1, i, 0.9 * x); }
        const auto t0 = std::chrono::steady_clock::now();
        p.processBlock (b, midi);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        if (s0 > (int) fs) r.maxMs = std::max (r.maxMs, ms);
        for (int i = 0; i < blk && s0 + i < n; ++i) out[(size_t) (s0 + i)] = b.getSample (0, i);
    }
    // for a sine of any amplitude, d3 y[n] = -4 sin^2 (w/2) d1 y[n+1]: the residual of that relation is ~0 for a
    // steady or slowly changing sine and stands out on a step, a kink or a gap
    const double k = 4 * std::pow (std::sin (M_PI * 220.0 / fs), 2.0);
    for (int i = sweepFrom; i < n - 3; ++i)
    {
        const double d3 = out[(size_t) i + 3] - 3 * out[(size_t) i + 2] + 3 * out[(size_t) i + 1] - out[(size_t) i];
        const double d1 = out[(size_t) i + 2] - out[(size_t) i + 1];
        const double v = std::abs (d3 + k * d1);
        if (v > r.click) { r.click = v; r.at = i; }
    }
    if (std::getenv ("CLICK_DUMP") && prm)
        for (int i = std::max (0, r.at - 40); i < std::min (n, r.at + 40); ++i) std::printf ("%d %.6f\n", i, out[(size_t) i]);
    return r;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const int blk = 512;
    const R base = run (nullptr, blk);
    std::printf ("baseline: residual %.2e, slowest block %.2f ms (block = %.2f ms)\n", base.click, base.maxMs, blk / 48.0);
    std::unique_ptr<juce::AudioProcessor> ap (createPluginFilter());
    auto& p = static_cast<DeltaCompProcessor&> (*ap);
    for (auto* prm : p.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (const char* only = std::getenv ("CLICK_ONLY")) if (rp->paramID != juce::String (only)) continue;
            const R r = run (rp, blk);
            const double ratio = 20 * std::log10 ((r.click + 1e-12) / (base.click + 1e-12));
            std::printf ("%-12s click %+6.1f dB  slowest %6.2f ms %s%s\n", rp->paramID.toRawUTF8(), ratio, r.maxMs,
                         ratio > 6 ? " CLICK" : "", r.maxMs > blk / 48.0 * 0.5 ? " CPU" : "");
            std::fflush (stdout);
        }
}
