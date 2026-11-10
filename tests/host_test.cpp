// Loads the built VST3 the way a DAW does and checks: layout, latency, processing (float and double),
// latency change when switching quality, parameter count and state save / restore.
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

    check (p->getTotalNumInputChannels() == 47, "47 inputs (3 mics x O1 O2 R1 R2, AER G C D, 16 spots x 2)", p->getTotalNumInputChannels());
    check (p->getTotalNumOutputChannels() == 2, "stereo output (AER output off by default)", p->getTotalNumOutputChannels());
    check (p->supportsDoublePrecisionProcessing(), "64-bit processing supported", 1);
    check (p->getParameters().size() == 380, "parameter count", p->getParameters().size());
    auto setParam = [&] (const char* name, float v01) {
        for (auto* prm : p->getParameters()) if (prm->getName (128) == juce::String::fromUTF8 (name)) { prm->setValueNotifyingHost (v01); return; }
        check (false, name, 0);
    };
    // Generic run: impulse on the given input channels, returns the peak position and value on output channel outCh.
    auto impulse = [&] (bool dbl, std::vector<int> inCh, int outCh, double& peak, int nCh) -> double {
        p->setProcessingPrecision (dbl ? juce::AudioProcessor::doublePrecision : juce::AudioProcessor::singlePrecision);
        p->prepareToPlay (48000, 512);
        const int lat = p->getLatencySamples();
        juce::MidiBuffer midi;
        double peakPos = -1; int pos = 0; peak = 0;
        for (int blk = 0; blk < (lat + 2048) / 512 + 2; ++blk)
        {
            juce::AudioBuffer<double> b (nCh, 512); b.clear();
            juce::AudioBuffer<float> f (nCh, 512); f.clear();
            if (blk == 0) for (int c : inCh) { b.setSample (c, 0, 1.0); f.setSample (c, 0, 1.0f); }
            if (dbl) p->processBlock (b, midi); else p->processBlock (f, midi);
            for (int i = 0; i < 512; ++i)
            {
                const double v = dbl ? b.getSample (outCh, i) : f.getSample (outCh, i);
                if (std::abs (v) > peak) { peak = std::abs (v); peakPos = pos + i; }
            }
            pos += 512;
        }
        p->releaseResources();
        return peakPos - lat;
    };

    auto run = [&] (bool dbl) -> double {
        p->setProcessingPrecision (dbl ? juce::AudioProcessor::doublePrecision : juce::AudioProcessor::singlePrecision);
        p->prepareToPlay (48000, 512);
        const int lat = p->getLatencySamples();
        juce::MidiBuffer midi;
        double peakPos = -1, peak = 0; int pos = 0;
        for (int blk = 0; blk < (lat + 2048) / 512 + 2; ++blk)
        {
            if (dbl)
            {
                juce::AudioBuffer<double> b (47, 512); b.clear();
                if (blk == 0) for (int m = 0; m < 3; ++m) { b.setSample (m * 4, 0, 1.0); b.setSample (m * 4 + 2, 0, 1.0); }
                p->processBlock (b, midi);
                for (int i = 0; i < 512; ++i) if (std::abs (b.getSample (0, i)) > peak) { peak = std::abs (b.getSample (0, i)); peakPos = pos + i; }
            }
            else
            {
                juce::AudioBuffer<float> b (47, 512); b.clear();
                if (blk == 0) for (int m = 0; m < 3; ++m) { b.setSample (m * 4, 0, 1.0f); b.setSample (m * 4 + 2, 0, 1.0f); }
                p->processBlock (b, midi);
                for (int i = 0; i < 512; ++i) if (std::abs (b.getSample (0, i)) > peak) { peak = std::abs (b.getSample (0, i)); peakPos = pos + i; }
            }
            pos += 512;
        }
        p->releaseResources();
        check (peakPos == lat, dbl ? "double: impulse arrives at reported latency" : "float: impulse arrives at reported latency", peakPos);
        check (std::abs (peak - (1 + std::pow (10.0, -3.0 / 20))) < (dbl ? 1e-9 : 1e-6), dbl ? "double: L = mic1 + centre at -3 dB" : "float: L = mic1 + centre at -3 dB", peak);
        return lat;
    };
    const double latListen = run (false);
    run (true);
    check (latListen > 0, "latency in listen mode (samples)", latListen);

    // Delta AER: impulse on AER G only arrives on L at the same latency (min-phase EQ, then linear-phase EQ)
    for (int lin = 0; lin < 2; ++lin)
    {
        setParam ("AER · Phase des EQ", lin ? 1.0f : 0.0f);
        p->prepareToPlay (48000, 512); p->releaseResources();
        double pk = 0;
        const double off = impulse (true, { 12 }, 0, pk, 47);
        check (off == 0, lin ? "AER lin EQ: impulse on L at latency (offset)" : "AER min EQ: impulse on L at latency (offset)", off);
        check (std::abs (pk - 1) < 1e-6, lin ? "AER lin EQ: flat EQ gives unity" : "AER min EQ: flat EQ gives unity", pk);
        if (lin) check (p->getLatencySamples() == latListen, "AER lin EQ: same latency as the linear crossover", p->getLatencySamples());
    }
    setParam ("AER · Phase des EQ", 0.0f);

    // EN-MS spots: spot 1 on L, cardio in 16, ribbon in 17 (0-based 15, 16)
    {
        setParam ("Spot 1 · Pan G-C-D", 0.0f);
        double pk = 0;
        double off = impulse (true, { 15 }, 0, pk, 47);
        check (off == 0 && std::abs (pk - 1) < 1e-9, "Spot 1 M/S: cardio alone on L at latency, unity", pk);
        off = impulse (true, { 15, 16 }, 0, pk, 47);
        check (off == 0 && std::abs (pk - 2) < 1e-9, "Spot 1 M/S: cardio + ribbon = M + S (2.0)", pk);
        setParam ("Spot 1 · Mode", 1.0f);
        p->prepareToPlay (48000, 512); p->releaseResources();
        off = impulse (true, { 15, 16 }, 0, pk, 47);
        check (off == 0 && std::abs (pk - 1) < 1e-9, "Spot 1 crossover: same impulse on both = exact reconstruction (1.0)", pk);
        setParam ("Spot 1 · Mode", 0.0f);
        impulse (true, { 15 + 2 * 8 }, 0, pk, 47);
        double pk2 = 0; impulse (true, { 15 + 2 * 8 }, 1, pk2, 47);
        check (pk < 1e-12 && pk2 < 1e-12, "Spot 9 is off until added (8 spots by default)", pk + pk2);
    }

    // Separate AER output: enable the second output bus, AER goes to channels 2-3 and leaves the main output
    {
        auto layout = p->getBusesLayout();
        layout.outputBuses.getReference (1) = juce::AudioChannelSet::stereo();
        const bool ok = p->setBusesLayout (layout);
        if (! ok) for (auto& b : layout.inputBuses) std::printf ("   in bus: %s (%d)\n", b.getDescription().toRawUTF8(), b.size());
        check (ok, "AER separate output can be enabled", 1);
        check (p->getTotalNumOutputChannels() == 4, "outputs with AER separate", p->getTotalNumOutputChannels());
        double pkMain = 0, pkAer = 0;
        impulse (false, { 12 }, 0, pkMain, 47);
        const double off = impulse (false, { 12 }, 2, pkAer, 47);
        check (pkMain < 1e-9, "AER separate: nothing on main L", pkMain);
        check (off == 0 && std::abs (pkAer - 1) < 1e-6, "AER separate: AER G on output 3 at latency", pkAer);
        layout.outputBuses.getReference (1) = juce::AudioChannelSet::disabled();
        p->setBusesLayout (layout);
    }

    // switch to Export quality via the host parameter and let the plugin rebuild
    for (auto* prm : p->getParameters())
        if (prm->getName (64).contains ("Qualit")) prm->setValueNotifyingHost (1.0f);
    p->prepareToPlay (48000, 512);
    check (p->getLatencySamples() > latListen, "export mode reports a longer latency (samples)", p->getLatencySamples());
    p->releaseResources();

    // state round trip
    juce::MemoryBlock st; p->getStateInformation (st);
    auto p2 = fmt.createInstanceFromDescription (*descs[0], 48000, 512, err);
    p2->setStateInformation (st.getData(), (int) st.getSize());
    bool same = true;
    for (int i = 0; i < p->getParameters().size(); ++i)
        same &= std::abs (p->getParameters()[i]->getValue() - p2->getParameters()[i]->getValue()) < 1e-6f;
    check (same, "state save / restore keeps every parameter", 1);

    std::printf ("\n%s\n", fails ? "HOST TEST FAILED" : "HOST TEST PASSED");
    return fails ? 1 : 0;
}
