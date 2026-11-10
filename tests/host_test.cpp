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

    check (p->getTotalNumInputChannels() == 6, "6 input channels", p->getTotalNumInputChannels());
    check (p->getTotalNumOutputChannels() == 2, "stereo output", p->getTotalNumOutputChannels());
    check (p->supportsDoublePrecisionProcessing(), "64-bit processing supported", 1);
    check (p->getParameters().size() == 40, "parameter count", p->getParameters().size());

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
                juce::AudioBuffer<double> b (6, 512); b.clear();
                if (blk == 0) for (int c = 0; c < 6; ++c) b.setSample (c, 0, 1.0);
                p->processBlock (b, midi);
                for (int i = 0; i < 512; ++i) if (std::abs (b.getSample (0, i)) > peak) { peak = std::abs (b.getSample (0, i)); peakPos = pos + i; }
            }
            else
            {
                juce::AudioBuffer<float> b (6, 512); b.clear();
                if (blk == 0) for (int c = 0; c < 6; ++c) b.setSample (c, 0, 1.0f);
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
