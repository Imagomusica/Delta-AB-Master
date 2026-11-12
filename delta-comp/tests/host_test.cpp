// Loads the built VST3 the way a DAW does: layout, constant latency, float + double processing,
// compression actually happening, parameter count, state save / restore, editor opens.
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>

static int fails = 0;
static void check (bool ok, const char* what, double v) { std::printf ("[%s] %-60s %g\n", ok ? " OK " : "FAIL", what, v); if (! ok) ++fails; }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf ("usage: host_test <plugin.vst3>\n"); return 2; }
    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> descs;
    fmt.findAllTypesForFile (descs, juce::String (argv[1]));
    check (descs.size() == 1, "VST3 found and scanned", descs.size());
    if (descs.isEmpty()) return 1;
    juce::String err;
    auto p = fmt.createInstanceFromDescription (*descs[0], 48000, 512, err);
    check (p != nullptr, "instance created", 0);
    if (! p) { std::printf ("%s\n", err.toRawUTF8()); return 1; }

    check (p->getTotalNumInputChannels() == 2 && p->getTotalNumOutputChannels() == 2, "stereo in / out", p->getTotalNumInputChannels());
    check (p->supportsDoublePrecisionProcessing(), "64-bit processing supported", 1);
    std::printf ("parameters: %d\n", p->getParameters().size());
    check (p->getParameters().size() >= 60, "all parameters exposed", p->getParameters().size());
    // hosted parameters only speak normalised values and text
    auto find = [&] (const char* name) -> juce::AudioProcessorParameter* {
        for (auto* prm : p->getParameters()) if (prm->getName (128) == juce::String::fromUTF8 (name)) return prm;
        check (false, name, 0); return nullptr;
    };
    auto set = [&] (const char* name, float plain) { if (auto* r = find (name)) r->setValueNotifyingHost (r->getValueForText (juce::String (plain))); };
    auto setB = [&] (const char* name, bool on) { if (auto* r = find (name)) r->setValueNotifyingHost (on ? 1.0f : 0.0f); };
    auto get = [&] (const char* name) { auto* r = find (name); return r ? r->getText (r->getValue(), 32).getDoubleValue() : -999.0; };

    // loud sine, only the double-threshold stage, 4:1 above -12 LUFS
    setB ("Upward actif", false); setB ("Peak actif", false); setB ("Limiteur actif", false); setB ("Gain auto", false);
    set ("Cible", -14); set ("Seuil 1", 2); set ("Ratio 1", 4); set ("Seuil 2", 18); set ("Genou", 0);
    check (std::abs (get ("Ratio 1") - 4) < 0.02 && std::abs (get ("Seuil 1") - 2) < 0.02, "parameters set through the host", get ("Ratio 1"));
    for (int dbl = 0; dbl < 2; ++dbl)
    {
        p->setProcessingPrecision (dbl ? juce::AudioProcessor::doublePrecision : juce::AudioProcessor::singlePrecision);
        p->prepareToPlay (48000, 512);
        check (p->getLatencySamples() == 13452, dbl ? "latency 13452 samples at 48 kHz (double)" : "latency 13452 samples at 48 kHz (float)", p->getLatencySamples());
        juce::MidiBuffer midi; double acc = 0; int cnt = 0;
        for (int blk = 0; blk < 400; ++blk)
        {
            juce::AudioBuffer<double> b (2, 512); juce::AudioBuffer<float> f (2, 512);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i)
            { const double x = 0.5 * std::sin (2 * juce::MathConstants<double>::pi * 997.0 * (blk * 512 + i) / 48000.0); b.setSample (c, i, x); f.setSample (c, i, (float) x); }
            if (dbl) p->processBlock (b, midi); else p->processBlock (f, midi);
            if (blk >= 350) for (int i = 0; i < 512; ++i) { const double v = dbl ? b.getSample (0, i) : f.getSample (0, i); acc += v * v; ++cnt; }
        }
        const double outDb = 10 * std::log10 (acc / cnt) + 3.0103;
        check (std::abs (outDb - (-11.25)) < 0.4, dbl ? "-6 dBFS sine compressed to -11.25 with the default zones (double)" : "-6 dBFS sine compressed to -11.25 with the default zones (float)", outDb);
        p->releaseResources();
    }
    // every oversampling setting keeps the latency
    for (int os = 0; os < 4; ++os)
    {
        if (auto* r = find ("Oversampling")) r->setValueNotifyingHost ((float) os / 3.0f);
        p->prepareToPlay (48000, 512);
        check (p->getLatencySamples() == 13452, "latency constant for each oversampling setting", os);
        p->releaseResources();
    }

    juce::MemoryBlock st; p->getStateInformation (st);
    set ("Ratio 1", 1);
    p->setStateInformation (st.getData(), (int) st.getSize());
    const double back = get ("Ratio 1");
    check (std::abs (back - 4) < 0.02, "state restore (ratio 1 back to 4)", back);

    if (p->hasEditor()) { std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditorIfNeeded()); check (ed != nullptr && ed->getWidth() > 0, "editor opens", ed ? ed->getWidth() : 0); if (ed) p->editorBeingDeleted (ed.get()); }

    p.reset();
    // default settings, as opened in a DAW: the transient knob must clearly soften / accentuate a soft attack
    auto softAttack = [&] (float amount) {
        auto q = fmt.createInstanceFromDescription (*descs[0], 48000, 512, err);
        for (auto* prm : q->getParameters()) if (prm->getName (128) == "Transitoires") prm->setValueNotifyingHost (prm->getValueForText (juce::String (amount)));
        q->setProcessingPrecision (juce::AudioProcessor::doublePrecision);
        q->prepareToPlay (48000, 512);
        const int L = q->getLatencySamples(), n = 48000 * 4, t0 = 48000 * 2, rise = 1920;
        std::vector<double> out ((size_t) n);
        juce::MidiBuffer midi;
        for (int s0 = 0; s0 < n; s0 += 512)
        {
            juce::AudioBuffer<double> b (2, 512);
            for (int i = 0; i < 512; ++i)
            {
                const int k = s0 + i; const double t = k / 48000.0, pi2 = 2 * juce::MathConstants<double>::pi;
                const double env = k < t0 ? 0.0 : k - t0 < rise ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (k - t0) / rise) : 1.0;
                const double x = 0.15 * env * (std::sin (pi2 * 196 * t) + 0.5 * std::sin (pi2 * 392 * t) + 0.3 * std::sin (pi2 * 588 * t));
                b.setSample (0, i, x); b.setSample (1, i, x);
            }
            q->processBlock (b, midi);
            for (int i = 0; i < 512; ++i) out[(size_t) (s0 + i)] = b.getSample (0, i);
        }
        double a = 0, c = 0;
        for (int i = t0; i < t0 + 2880; ++i) a += out[(size_t) (i + L)] * out[(size_t) (i + L)];
        for (int i = t0 + 24000; i < t0 + 48000 && i + L < n; ++i) c += out[(size_t) (i + L)] * out[(size_t) (i + L)];
        q->releaseResources();
        return 10 * std::log10 ((a / 2880) / (c / 24000));
    };
    const double t0 = softAttack (0), tm = softAttack (-100), tp = softAttack (100);
    check (tm - t0 < -4, "defaults: Transitoires -100 softens a 40 ms attack > 4 dB", tm - t0);
    check (tp - t0 > 4, "defaults: Transitoires +100 accentuates a 40 ms attack > 4 dB", tp - t0);
    std::printf (fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
