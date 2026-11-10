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
static constexpr int kNumX = 7;
static const double xoverHz[kNumX] = { 350, 700, 1250, 1600, 1950, 2200, 4400 };
static const juce::StringArray xoverNames { "350 Hz", "700 Hz", "1250 Hz", "1600 Hz", "1950 Hz", "2200 Hz", "4400 Hz" };

// Two of the four sources of a hybrid mic: X and Y. Sources: 0 = O1 MK221, 1 = O2 MK102, 2 = R1 (aigu), 3 = R2 (grave).
struct Combo { int x, y; bool autoYInLows; };
static const Combo combos[6] = { { 0, 2, false }, { 0, 3, true }, { 1, 2, false }, { 1, 3, true }, { 0, 1, true }, { 2, 3, true } };

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
    l.add (std::make_unique<APC> (juce::ParameterID { "focal", 1 }, "Focal (point de crossover)", xoverNames, 1));
    l.add (std::make_unique<APB> (juce::ParameterID { "ambient", 1 }, u ("Ambiance : inverser grave / aigu"), false));
    l.add (std::make_unique<APF> (juce::ParameterID { "m_focus", 1 }, u ("Focus (macro) : plus ou moins d’aigu"), db (-12, 12, 0.1f), 0.0f, attrDb));
    l.add (std::make_unique<APF> (juce::ParameterID { "m_bloom", 1 }, u ("Bloom (macro) : plus ou moins d’ambiance (grave)"), db (-12, 12, 0.1f), 0.0f, attrDb));
    l.add (std::make_unique<APC> (juce::ParameterID { "bloom_mode", 1 }, u ("Bloom : action"),
                                  juce::StringArray { u ("Grave ↑"), u ("Grave ↑ et aigu ↓") }, 0));

    // Mics
    const float defPan[3] = { -1.0f, 1.0f, 0.0f };
    for (int m = 0; m < 3; ++m)
    {
        const juce::String n (micName[m]);
        l.add (std::make_unique<APC> (juce::ParameterID { pid (m, "combo"), 1 }, n + u (" · Combinaison"),
                                      juce::StringArray { u ("O1 MK221 + R1 aigu"), u ("O1 MK221 + R2 grave"),
                                                          u ("O2 MK102 + R1 aigu"), u ("O2 MK102 + R2 grave"),
                                                          u ("O1 MK221 + O2 MK102"), u ("R1 aigu + R2 grave") }, 0));
        l.add (std::make_unique<APB> (juce::ParameterID { pid (m, "swap"), 1 }, n + u (" · Inverser entrées X/Y (6 canaux)"), false));
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "match"), 1 }, n + u (" · Appariement Y"), db (-24, 24, 0.1f), 0.0f, attrDb));
        l.add (std::make_unique<APB> (juce::ParameterID { pid (m, "analyse"), 1 }, n + u (" · Analyser X/Y"), false));
        l.add (std::make_unique<APC> (juce::ParameterID { pid (m, "xover"), 1 }, n + u (" · Coupure"),
                                      [] { juce::StringArray a { "Focal" }; a.addArray (xoverNames); return a; }(), 0));
        l.add (std::make_unique<APC> (juce::ParameterID { pid (m, "lows"), 1 }, n + u (" · Dans le grave"),
                                      juce::StringArray { u ("Auto (R1 aigu, R2 grave, O2 grave)"), u ("X grave, Y aigu"), u ("Y grave, X aigu") }, 0));
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "focus"), 1 }, n + u (" · Focus (aigu)"), db (-24, 12, 0.1f), 0.0f, attrDb));
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "bloom"), 1 }, n + u (" · Bloom (grave)"), db (-24, 12, 0.1f), 0.0f, attrDb));
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "pan"), 1 }, n + u (" · Pan G-C-D"), Range (-1, 1, 0.01f), defPan[m]));
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "delay"), 1 }, n + u (" · Délai"), Range (-10, 10, 0.5f), 0.0f,
                                      juce::AudioParameterFloatAttributes().withLabel ("ms")));
        auto pct = juce::AudioParameterFloatAttributes().withLabel ("%");
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "pan_lvl"), 1 }, n + u (" · Pan niveau"), Range (0, 100, 1), 100.0f, pct));
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "pan_dly"), 1 }, n + u (" · Pan délai"), Range (0, 100, 1), 0.0f, pct));
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "pan_ph"), 1 }, n + u (" · Pan phase"), Range (0, 100, 1), 0.0f, pct));
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "pan_hf"), 1 }, n + u (" · Pan HF (grave au centre)"), Range (0, 100, 1), 0.0f, pct));
    }
    l.add (std::make_unique<APC> (juce::ParameterID { "pan_law", 1 }, "Loi de pan (milieu)",
                                  juce::StringArray { "-3 dB", "-4.5 dB", "-6 dB" }, 0));
    l.add (std::make_unique<APC> (juce::ParameterID { "pan_mult", 1 }, u ("Pan délai : écart max"),
                                  juce::StringArray { "0.7 ms", "1.4 ms", "2.8 ms", "5.6 ms", "11.2 ms", "30 ms (Haas)" }, 0));

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
                          .withInput ("Entrées (3 micros x O1 O2 R1 R2)", juce::AudioChannelSet::create7point1point4(), true)
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
    const int in = layouts.getMainInputChannels();
    return (in == 6 || in == 12) && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

dab::CrossoverConfig DeltaABProcessor::currentConfig() const
{
    dab::CrossoverConfig c;
    c.fs = sr;
    c.linearPhase = (int) pv ("phase") == 0;
    c.exportQuality = (int) pv ("quality") == 1;
    const int s = (int) pv ("slope");
    c.slope = s == 0 ? dab::Slope::dB6 : s == 1 ? dab::Slope::dB12 : dab::Slope::dB24;
    const double focal = xoverHz[juce::jlimit (0, kNumX - 1, (int) pv ("focal"))];
    for (int m = 0; m < 3; ++m)
    {
        const int x = juce::jlimit (0, kNumX, (int) pv (pid (m, "xover")));
        c.fc[(size_t) m] = x == 0 ? focal : xoverHz[x - 1];
    }
    return c;
}

void DeltaABProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.prepare (sampleRate, juce::jmax (512, samplesPerBlock));
    scratch.setSize (kScratchCh, juce::jmax (512, samplesPerBlock));

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
        mp.swapInputs = false;   // routing is resolved in processDouble
        mp.matchDb = pv (pid (m, "match"));
        mp.analyse = pv (pid (m, "analyse")) > 0.5f;
        const auto& cb = combos[juce::jlimit (0, 5, (int) pv (pid (m, "combo")))];
        const int lows = (int) pv (pid (m, "lows"));
        const bool inLows = lows == 0 ? cb.autoYInLows : lows == 2;   // true: Y in the lows
        mp.ribbonInLows = inLows != (pv ("ambient") > 0.5f);
        const double mf = pv ("m_focus"), mb = pv ("m_bloom");
        mp.focusDb = pv (pid (m, "focus")) + mf - ((int) pv ("bloom_mode") == 1 ? mb : 0.0);
        mp.bloomDb = pv (pid (m, "bloom")) + mb;
        mp.pan = pv (pid (m, "pan"));
        mp.panLevel = pv (pid (m, "pan_lvl")) / 100.0;
        mp.panDelay = pv (pid (m, "pan_dly")) / 100.0;
        mp.panPhase = pv (pid (m, "pan_ph")) / 100.0;
        mp.panHF = pv (pid (m, "pan_hf")) / 100.0;
    }
    for (int m = 0; m < 3; ++m) p.delayMs[(size_t) m] = pv (pid (m, "delay"));
    p.panLawDb = std::array<double, 3> { -3.0, -4.5, -6.0 }[(size_t) juce::jlimit (0, 2, (int) pv ("pan_law"))];
    p.panDelayMaxMs = std::array<double, 6> { 0.7, 1.4, 2.8, 5.6, 11.2, 30.0 }[(size_t) juce::jlimit (0, 5, (int) pv ("pan_mult"))];
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

    // Source selection. 12 inputs: per mic O1, O2, R1, R2. 6 inputs: per mic O, R.
    scratch.clear (kSilence, 0, n);
    const double* silence = scratch.getReadPointer (kSilence);
    auto ch = [&] (int c) { return c < numIn ? in[c] : silence; };
    const double* ip[6];
    for (int m = 0; m < 3; ++m)
    {
        const auto& cb = combos[juce::jlimit (0, 5, (int) pv (pid (m, "combo")))];
        if (numIn >= 12)
        {
            ip[m * 2]     = ch (m * 4 + cb.x);   // X
            ip[m * 2 + 1] = ch (m * 4 + cb.y);   // Y
        }
        else
        {
            const bool sw = pv (pid (m, "swap")) > 0.5f;
            ip[m * 2]     = ch (m * 2 + (sw ? 1 : 0));
            ip[m * 2 + 1] = ch (m * 2 + (sw ? 0 : 1));
        }
    }
    dab::EngineParams p;
    readParams (p);
    engine.process (*active, p, ip, out, n);
}

void DeltaABProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), numIn = juce::jmin (12, getTotalNumInputChannels(), buffer.getNumChannels());
    if (buffer.getNumChannels() < 2) return;
    if (scratch.getNumSamples() < n) scratch.setSize (kScratchCh, n, false, false, true);
    double* out[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
    processDouble (buffer.getArrayOfReadPointers(), numIn, out, n);
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
}

void DeltaABProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), numIn = juce::jmin (12, getTotalNumInputChannels(), buffer.getNumChannels());
    if (buffer.getNumChannels() < 2) return;
    if (scratch.getNumSamples() < n) scratch.setSize (kScratchCh, n, false, false, true);

    for (int c = 0; c < numIn; ++c)
    {
        const float* src = buffer.getReadPointer (c);
        double* dst = scratch.getWritePointer (c);
        for (int i = 0; i < n; ++i) dst[i] = src[i];
    }
    const double* in[12];
    for (int c = 0; c < 12; ++c) in[c] = scratch.getReadPointer (c);
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
