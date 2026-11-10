#include "PluginProcessor.h"

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using Range = juce::NormalisableRange<float>;

static juce::String u (const char* s) { return juce::String::fromUTF8 (s); }

static const char* micId[3]   = { "m1", "m2", "m3" };
static const char* micName[3] = { "Micro 1 (G)", "Micro 2 (D)", "Micro 3 (C)" };
static const char* busId[3]   = { "L", "C", "R" };
static const char* busName[3] = { "Bus G", "Bus C", "Bus D" };
static const double xoverHz[4] = { 350, 700, 1500, 2200 };

static juce::String pid (int m, const char* p) { return juce::String (micId[m]) + "_" + p; }

juce::AudioProcessorValueTreeState::ParameterLayout DeltaABProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto db = [] (float lo, float hi, float step) { return Range (lo, hi, step); };
    auto attrDb = juce::AudioParameterFloatAttributes().withLabel ("dB");

    // Global
    l.add (std::make_unique<APC> (juce::ParameterID { "phase", 1 }, "Phase du crossover",
                                  juce::StringArray { u ("Linéaire"), "Minimale" }, 0));
    l.add (std::make_unique<APC> (juce::ParameterID { "quality", 1 }, u ("Qualité"),
                                  juce::StringArray { u ("Écoute"), "Export" }, 0));
    l.add (std::make_unique<APC> (juce::ParameterID { "slope", 1 }, "Pente",
                                  juce::StringArray { "6 dB/oct", "12 dB/oct", "24 dB/oct" }, 1));

    // Mics
    const float defPan[3] = { -1.0f, 1.0f, 0.0f };
    for (int m = 0; m < 3; ++m)
    {
        const juce::String n (micName[m]);
        l.add (std::make_unique<APB> (juce::ParameterID { pid (m, "swap"), 1 }, n + u (" · Inverser entrées O/R"), false));
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "match"), 1 }, n + u (" · Appariement ruban"), db (-24, 24, 0.1f), 0.0f, attrDb));
        l.add (std::make_unique<APB> (juce::ParameterID { pid (m, "analyse"), 1 }, n + u (" · Analyser O/R"), false));
        l.add (std::make_unique<APC> (juce::ParameterID { pid (m, "xover"), 1 }, n + u (" · Coupure"),
                                      juce::StringArray { "350 Hz", "700 Hz", "1500 Hz", "2200 Hz" }, 1));
        l.add (std::make_unique<APC> (juce::ParameterID { pid (m, "lows"), 1 }, n + u (" · Dans le grave"),
                                      juce::StringArray { "Omni", "Ruban" }, 0));
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "focus"), 1 }, n + u (" · Focus (aigu)"), db (-24, 12, 0.5f), 0.0f, attrDb));
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "bloom"), 1 }, n + u (" · Bloom (grave)"), db (-24, 12, 0.5f), 0.0f, attrDb));
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "pan"), 1 }, n + u (" · Pan G-C-D"), Range (-1, 1, 0.01f), defPan[m]));
    }
    l.add (std::make_unique<APF> (juce::ParameterID { "c_delay", 1 }, u ("Micro 3 (C) · Délai"), Range (-10, 10, 0.01f), 0.0f,
                                  juce::AudioParameterFloatAttributes().withLabel ("ms")));

    // Buses
    for (int b = 0; b < 3; ++b)
    {
        const juce::String n (busName[b]), id (busId[b]);
        l.add (std::make_unique<APF> (juce::ParameterID { "fader_" + id, 1 }, n + " · Fader", db (-60, 12, 0.1f), 0.0f, attrDb));
        l.add (std::make_unique<APB> (juce::ParameterID { "lc_on_" + id, 1 }, n + " · Low cut", false));
        l.add (std::make_unique<APF> (juce::ParameterID { "lc_hz_" + id, 1 }, n + u (" · Low cut fréquence"),
                                      Range (20, 200, 1, 0.5f), 40.0f, juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    }
    juce::StringArray sends;
    for (int i = 1; i <= 8; ++i) sends.add (juce::String (-1.5 * i, 1) + " dB");
    l.add (std::make_unique<APC> (juce::ParameterID { "c_send", 1 }, "Envoi du centre dans G et D", sends, 1));
    l.add (std::make_unique<APF> (juce::ParameterID { "out", 1 }, "Sortie", db (-24, 12, 0.1f), 0.0f, attrDb));
    return l;
}

DeltaABProcessor::DeltaABProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Entrées (3 micros x O/R)", juce::AudioChannelSet::create5point1(), true)
                          .withOutput ("Sortie", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DeltaAB", createLayout())
{
    startTimerHz (20);
}

DeltaABProcessor::~DeltaABProcessor()
{
    stopTimer();
    delete active;
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
}

bool DeltaABProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannels() == 6 && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

dab::CrossoverConfig DeltaABProcessor::currentConfig() const
{
    dab::CrossoverConfig c;
    c.fs = sr;
    c.linearPhase = (int) pv ("phase") == 0;
    c.exportQuality = (int) pv ("quality") == 1;
    const int s = (int) pv ("slope");
    c.slope = s == 0 ? dab::Slope::dB6 : s == 1 ? dab::Slope::dB12 : dab::Slope::dB24;
    for (int m = 0; m < 3; ++m) c.fc[(size_t) m] = xoverHz[juce::jlimit (0, 3, (int) pv (pid (m, "xover")))];
    return c;
}

void DeltaABProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.prepare (sampleRate, juce::jmax (512, samplesPerBlock));
    scratch.setSize (8, juce::jmax (512, samplesPerBlock));

    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
    delete active;
    builtCfg = currentConfig();
    active = new dab::CrossoverSet();
    active->build (builtCfg);
    setLatencySamples (engine.totalLatency (*active));
}

void DeltaABProcessor::timerCallback()
{
    delete retired.exchange (nullptr);

    const auto cfg = currentConfig();
    if (active != nullptr && cfg != builtCfg && pending.load() == nullptr && retired.load() == nullptr)
    {
        auto* s = new dab::CrossoverSet();
        s->build (cfg);
        builtCfg = cfg;
        const int lat = engine.totalLatency (*s);
        pending.store (s);
        if (lat != getLatencySamples()) setLatencySamples (lat);
    }

    // O/R analysis: when "Analyser" is switched off, the measured correction is written to "Appariement".
    for (int m = 0; m < 3; ++m)
    {
        const bool a = pv (pid (m, "analyse")) > 0.5f;
        if (a && ! wasAnalysing[(size_t) m]) engine.measureValid[(size_t) m].store (false);
        if (! a && wasAnalysing[(size_t) m] && engine.measureValid[(size_t) m].load())
        {
            const double v = std::round (engine.measuredMatchDb[(size_t) m].load() * 10.0) / 10.0;
            if (auto* p = apvts.getParameter (pid (m, "match")))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (-24.0, 24.0, v)));
        }
        wasAnalysing[(size_t) m] = a;
    }
}

void DeltaABProcessor::readParams (dab::EngineParams& p) const
{
    for (int m = 0; m < 3; ++m)
    {
        auto& mp = p.mic[(size_t) m];
        mp.swapInputs = pv (pid (m, "swap")) > 0.5f;
        mp.matchDb = pv (pid (m, "match"));
        mp.analyse = pv (pid (m, "analyse")) > 0.5f;
        mp.ribbonInLows = (int) pv (pid (m, "lows")) == 1;
        mp.focusDb = pv (pid (m, "focus"));
        mp.bloomDb = pv (pid (m, "bloom"));
        mp.pan = pv (pid (m, "pan"));
    }
    p.centreDelayMs = pv ("c_delay");
    for (int b = 0; b < 3; ++b)
    {
        const juce::String id (busId[b]);
        p.faderDb[(size_t) b] = pv ("fader_" + id);
        p.lowCutOn[(size_t) b] = pv ("lc_on_" + id) > 0.5f;
        p.lowCutHz[(size_t) b] = pv ("lc_hz_" + id);
    }
    p.centreSendDb = -1.5 * (1 + (int) pv ("c_send"));
    p.outputDb = pv ("out");
}

void DeltaABProcessor::processDouble (const double* const* in, int numIn, double* const* out, int n)
{
    if (auto* s = pending.exchange (nullptr))
    {
        retired.store (active);
        active = s;
    }
    if (active == nullptr) { juce::FloatVectorOperations::clear (out[0], n); juce::FloatVectorOperations::clear (out[1], n); return; }

    // missing inputs read as silence
    const double* ip[6];
    for (int c = 0; c < 6; ++c)
    {
        if (c < numIn) ip[c] = in[c];
        else { scratch.clear (6, 0, n); ip[c] = scratch.getReadPointer (6); }
    }
    dab::EngineParams p;
    readParams (p);
    engine.process (*active, p, ip, out, n);
}

void DeltaABProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), numIn = juce::jmin (6, getTotalNumInputChannels());
    if (buffer.getNumChannels() < 2) return;
    if (scratch.getNumSamples() < n) scratch.setSize (8, n, false, false, true);
    double* out[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
    processDouble (buffer.getArrayOfReadPointers(), numIn, out, n);
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
}

void DeltaABProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), numIn = juce::jmin (6, getTotalNumInputChannels());
    if (buffer.getNumChannels() < 2) return;
    if (scratch.getNumSamples() < n) scratch.setSize (8, n, false, false, true);

    for (int c = 0; c < numIn; ++c)
    {
        const float* src = buffer.getReadPointer (c);
        double* dst = scratch.getWritePointer (c);
        for (int i = 0; i < n; ++i) dst[i] = src[i];
    }
    const double* in[6];
    for (int c = 0; c < 6; ++c) in[c] = scratch.getReadPointer (c);
    // outputs go to channels 0 and 1 of scratch after inputs were consumed (engine reads all inputs first per chunk)
    double* out[2] = { scratch.getWritePointer (0), scratch.getWritePointer (1) };
    processDouble (in, numIn, out, n);
    for (int c = 0; c < 2; ++c)
    {
        float* dst = buffer.getWritePointer (c);
        const double* src = scratch.getReadPointer (c);
        for (int i = 0; i < n; ++i) dst[i] = (float) src[i];
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
}

juce::AudioProcessorEditor* DeltaABProcessor::createEditor()
{
    // Generic editor for now; the analog-console interface comes next.
    return new juce::GenericAudioProcessorEditor (*this);
}

void DeltaABProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void DeltaABProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DeltaABProcessor(); }
