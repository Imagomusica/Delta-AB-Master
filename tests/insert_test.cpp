// Insert rack test, without a DAW: DA/AD loop simulated by feeding the "Insert DA" output back into the
// "Insert AD" input one block later. Checks the PING, the reported latency and the alignment of all flows.
#include "PluginProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
static int fails = 0;
static void check (bool ok, const char* what, double v) { std::printf ("[%s] %-60s %g\n", ok ? " OK " : "FAIL", what, v); if (! ok) ++fails; }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<DeltaABProcessor> p (static_cast<DeltaABProcessor*> (createPluginFilter()));
    auto layout = p->getBusesLayout();
    layout.inputBuses.getReference (layout.inputBuses.size() - 1) = juce::AudioChannelSet::stereo();     // Insert AD
    layout.outputBuses.getReference (layout.outputBuses.size() - 1) = juce::AudioChannelSet::stereo();   // Insert DA
    check (p->setBusesLayout (layout), "insert buses can be enabled", 1);
    const int nIn = p->getTotalNumInputChannels(), nOut = p->getTotalNumOutputChannels(), N = std::max (nIn, nOut);
    const int retCh = nIn - 2, sendCh = nOut - 2, B = 512;
    p->setProcessingPrecision (juce::AudioProcessor::doublePrecision);
    p->prepareToPlay (48000, B);
    const int base = p->getLatencySamples();
    p->rack.setHardware (1, 0);   // AER.01, slot after delay/fader, loop 1
    juce::AudioBuffer<double> buf (N, B), prevSend (2, B);
    prevSend.clear();
    juce::MidiBuffer midi;
    auto block = [&] (std::function<void (juce::AudioBuffer<double>&)> fill) {
        buf.clear();
        if (fill) fill (buf);
        for (int c = 0; c < 2; ++c) buf.copyFrom (retCh + c, 0, prevSend, c, 0, B);   // interface: one block round trip
        p->processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) prevSend.copyFrom (c, 0, buf, sendCh + c, 0, B);
    };
    p->rack.ping (0);
    for (int i = 0; i < 6; ++i) block ({});
    check (p->rack.loopLatency (0) == B, "PING measures the round trip (samples)", p->rack.loopLatency (0));
    check (p->rack.maxLatency() == B, "insert latency in the rack", p->rack.maxLatency());
    // the processor reports it on its timer: call it the way the host would see it
    p->prepareToPlay (48000, B);
    check (p->getLatencySamples() == base + B, "plugin latency = base + loop", p->getLatencySamples());

    // alignment: impulse on AB.01 X+Y and AER.01 at the same time: both arrive at the reported latency
    const int lat = p->getLatencySamples();
    int posAb = -1, posAer = -1, t = 0;
    for (int blk = 0; blk < lat / B + 4; ++blk)
    {
        block ([&] (juce::AudioBuffer<double>& b) { if (blk == 0) { b.setSample (0, 0, 1.0); b.setSample (1, 0, 1.0); b.setSample (6, 0, 1.0); } });
        for (int i = 0; i < B; ++i)
        {
            if (posAb < 0 && std::abs (buf.getSample (0, i)) > 0.5) posAb = t + i;   // AB.01 out L
            if (posAer < 0 && std::abs (buf.getSample (6, i)) > 0.5) posAer = t + i; // AER out L
        }
        t += B;
    }
    check (posAb == lat, "AB.01 impulse at the reported latency", posAb);
    check (posAer == lat, "AER.01 through the DA/AD loop, same latency", posAer);
    p->rack.clear (1);
    std::printf ("\n%s\n", fails ? "INSERT TEST FAILED" : "INSERT TEST PASSED");
    return fails ? 1 : 0;
}
