#include "PluginProcessor.h"
#include "PluginEditor.h"

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using Range = juce::NormalisableRange<float>;

static juce::String u (const char* s) { return juce::String::fromUTF8 (s); }

static const char* micId[3]   = { "m1", "m2", "m3" };
static juce::String aid (int a, const char* p) { return "a" + juce::String (a + 1) + "_" + p; }
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

static const char* aerName[3] = { "AER G", "AER C", "AER D" };
static const int kSlopes[3] = { 1, 2, 4 };

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

    // Delta AER: 3 omnis L, C, R
    auto hz = juce::AudioParameterFloatAttributes().withLabel ("Hz");
    auto pct = juce::AudioParameterFloatAttributes().withLabel ("%");
    const juce::StringArray slopes { "6 dB/oct", "12 dB/oct", "24 dB/oct" };
    l.add (std::make_unique<APC> (juce::ParameterID { "aer_eq_phase", 1 }, u ("AER · Phase des EQ"),
                                  juce::StringArray { "Minimale", u ("Linéaire") }, 0));
    l.add (std::make_unique<APB> (juce::ParameterID { "aer_link", 1 }, u ("AER · Faders liés"), false));
    l.add (std::make_unique<APF> (juce::ParameterID { "aer_out", 1 }, u ("AER · Sortie"), db (-24, 12, 0.1f), 0.0f, attrDb));
    const float aerPan[3] = { -1.0f, 0.0f, 1.0f };
    const float bellF[3] = { 250.0f, 1000.0f, 4000.0f };
    for (int a = 0; a < 3; ++a)
    {
        const juce::String n (aerName[a]);
        auto id = [a] (const char* x) { return aid (a, x); };
        l.add (std::make_unique<APB> (juce::ParameterID { id ("lc_on"), 1 }, n + " · Low cut", false));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("lc_hz"), 1 }, n + u (" · Low cut fréquence"), Range (10, 1000, 1, 0.3f), 40.0f, hz));
        l.add (std::make_unique<APC> (juce::ParameterID { id ("lc_slope"), 1 }, n + " · Low cut pente", slopes, 1));
        l.add (std::make_unique<APB> (juce::ParameterID { id ("hc_on"), 1 }, n + " · High cut", false));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("hc_hz"), 1 }, n + u (" · High cut fréquence"), Range (1000, 22000, 10, 0.4f), 18000.0f, hz));
        l.add (std::make_unique<APC> (juce::ParameterID { id ("hc_slope"), 1 }, n + " · High cut pente", slopes, 1));
        for (int b = 0; b < 3; ++b)
        {
            const juce::String bn = n + " · Cloche " + juce::String (b + 1);
            const juce::String bp = "b" + juce::String (b + 1);
            l.add (std::make_unique<APF> (juce::ParameterID { aid (a, (bp + "_f").toRawUTF8()), 1 }, bn + u (" fréquence"), Range (20, 20000, 1, 0.25f), bellF[b], hz));
            l.add (std::make_unique<APF> (juce::ParameterID { aid (a, (bp + "_g").toRawUTF8()), 1 }, bn + " gain", db (-18, 18, 0.1f), 0.0f, attrDb));
            l.add (std::make_unique<APF> (juce::ParameterID { aid (a, (bp + "_q").toRawUTF8()), 1 }, bn + " Q", Range (0.1f, 10.0f, 0.01f, 0.4f), 1.0f));
        }
        const char* shN[2] = { " · Shelf grave", " · Shelf aigu" };
        const char* shP[2] = { "ls", "hs" };
        const float shF[2] = { 120.0f, 8000.0f };
        for (int k = 0; k < 2; ++k)
        {
            const juce::String sp (shP[k]);
            l.add (std::make_unique<APF> (juce::ParameterID { aid (a, (sp + "_f").toRawUTF8()), 1 }, n + u (shN[k]) + u (" fréquence"), Range (20, 20000, 1, 0.25f), shF[k], hz));
            l.add (std::make_unique<APF> (juce::ParameterID { aid (a, (sp + "_g").toRawUTF8()), 1 }, n + u (shN[k]) + " gain", db (-18, 18, 0.1f), 0.0f, attrDb));
            l.add (std::make_unique<APF> (juce::ParameterID { aid (a, (sp + "_q").toRawUTF8()), 1 }, n + u (shN[k]) + " Q", Range (0.3f, 2.0f, 0.01f), 0.707f));
        }
        // delay in ms, also shown in metres (sound at 343 m/s)
        l.add (std::make_unique<APF> (juce::ParameterID { id ("delay"), 1 }, n + u (" · Délai"), Range (-10, 100, 0.01f), 0.0f,
                                      juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (
                                          [] (float v, int) { return juce::String (v, 2) + " ms (" + juce::String (v * 0.343f, 2) + " m)"; })));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("fader"), 1 }, n + " · Fader", db (-60, 12, 0.1f), 0.0f, attrDb));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("pan"), 1 }, n + u (" · Pan G-C-D"), Range (-1, 1, 0.01f), aerPan[a]));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("pan_lvl"), 1 }, n + u (" · Pan niveau"), Range (0, 100, 1), 100.0f, pct));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("pan_dly"), 1 }, n + u (" · Pan délai"), Range (0, 100, 1), 0.0f, pct));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("pan_ph"), 1 }, n + u (" · Pan phase"), Range (0, 100, 1), 0.0f, pct));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("pan_hf"), 1 }, n + u (" · Pan HF (grave au centre)"), Range (0, 100, 1), 0.0f, pct));
    }
    return l;
}

DeltaABProcessor::DeltaABProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Entrées (3 micros x O1 O2 R1 R2)", juce::AudioChannelSet::create7point1point4(), true)
                          .withInput ("AER (G C D)", juce::AudioChannelSet::createLCR(), true)
                          .withOutput ("Sortie", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("AER séparé", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "DeltaAB", createLayout())
{
    startTimerHz (20);
}

DeltaABProcessor::~DeltaABProcessor()
{
    stopTimer();
    for (int a = 0; a < 3; ++a)
    {
        delete eqActive[(size_t) a];
        delete eqPending[(size_t) a].exchange (nullptr);
        delete eqRetired[(size_t) a].exchange (nullptr);
    }
    delete active;
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
}

bool DeltaABProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const int in = layouts.getMainInputChannels();
    if (! (in == 6 || in == 12) || layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (layouts.inputBuses.size() > 1 && ! (layouts.inputBuses[1].isDisabled() || layouts.inputBuses[1].size() == 3)) return false;
    if (layouts.outputBuses.size() > 1 && ! (layouts.outputBuses[1].isDisabled() || layouts.outputBuses[1] == juce::AudioChannelSet::stereo())) return false;
    return true;
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
    c.aerEqLinear = (int) pv ("aer_eq_phase") == 1;
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
    for (int a = 0; a < 3; ++a)
    {
        delete eqPending[(size_t) a].exchange (nullptr);
        delete eqRetired[(size_t) a].exchange (nullptr);
        delete eqActive[(size_t) a];
        eqActive[(size_t) a] = nullptr;
        if (builtCfg.aerEqLinear) eqActive[(size_t) a] = makeEq (a);
        eqBuilt[(size_t) a] = eqActive[(size_t) a];
    }
    setLatencySamples (engine.totalLatency (*active));
}

dab::AerEqFilter* DeltaABProcessor::makeEq (int a) const
{
    auto* f = new dab::AerEqFilter();
    f->build (aerEq (a), sr, dab::firLength (sr, (int) pv ("quality") == 1));
    return f;
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

    // Linear-phase AER EQ: one filter per channel, rebuilt when its EQ, the rate or the length changes.
    if (builtCfg.aerEqLinear && active != nullptr)
    {
        const int N = dab::firLength (sr, builtCfg.exportQuality);
        for (int a = 0; a < 3; ++a)
        {
            delete eqRetired[(size_t) a].exchange (nullptr);
            const auto e = aerEq (a);
            const auto* b = eqBuilt[(size_t) a];
            const bool stale = b == nullptr || b->params != e || b->fs != sr || b->N != N;
            if (stale && eqPending[(size_t) a].load() == nullptr && eqRetired[(size_t) a].load() == nullptr)
            {
                auto* f = new dab::AerEqFilter();
                f->build (e, sr, N);
                eqBuilt[(size_t) a] = f;
                eqPending[(size_t) a].store (f);
            }
        }
    }
    for (int a = 0; a < 3; ++a) delete eqRetired[(size_t) a].exchange (nullptr);

    // AER faders linked: a move of one fader moves the other two by the same number of dB.
    {
        std::array<float, 3> f;
        int moved = -1, nMoved = 0;
        for (int a = 0; a < 3; ++a)
        {
            f[(size_t) a] = pv (aid (a, "fader"));
            if (f[(size_t) a] != lastAerFader[(size_t) a]) { moved = a; ++nMoved; }
        }
        if (pv ("aer_link") > 0.5f && nMoved == 1 && linkPrimed)
        {
            const float d = f[(size_t) moved] - lastAerFader[(size_t) moved];
            for (int a = 0; a < 3; ++a)
                if (a != moved)
                    if (auto* prm = apvts.getParameter (aid (a, "fader")))
                    {
                        const float nv = juce::jlimit (-60.0f, 12.0f, f[(size_t) a] + d);
                        prm->beginChangeGesture();
                        prm->setValueNotifyingHost (prm->convertTo0to1 (nv));
                        prm->endChangeGesture();
                        f[(size_t) a] = pv (aid (a, "fader"));
                    }
        }
        lastAerFader = f;
        linkPrimed = true;
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

dab::AerEqParams DeltaABProcessor::aerEq (int a) const
{
    dab::AerEqParams e;
    auto v = [&] (const char* x) { return (double) pv (aid (a, x)); };
    e.lcOn = v ("lc_on") > 0.5; e.lcHz = v ("lc_hz"); e.lcSlope = kSlopes[juce::jlimit (0, 2, (int) v ("lc_slope"))];
    e.hcOn = v ("hc_on") > 0.5; e.hcHz = v ("hc_hz"); e.hcSlope = kSlopes[juce::jlimit (0, 2, (int) v ("hc_slope"))];
    const char* bf[3][3] = { { "b1_f", "b1_g", "b1_q" }, { "b2_f", "b2_g", "b2_q" }, { "b3_f", "b3_g", "b3_q" } };
    for (int b = 0; b < 3; ++b) e.bell[(size_t) b] = { v (bf[b][0]), v (bf[b][1]), v (bf[b][2]) };
    e.lowShelf = { v ("ls_f"), v ("ls_g"), v ("ls_q") };
    e.highShelf = { v ("hs_f"), v ("hs_g"), v ("hs_q") };
    return e;
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

    for (int a = 0; a < 3; ++a)
    {
        auto& ap = p.aer[(size_t) a];
        ap.eq = aerEq (a);
        ap.delayMs = pv (aid (a, "delay"));
        ap.faderDb = pv (aid (a, "fader"));
        ap.pan = pv (aid (a, "pan"));
        ap.panLevel = pv (aid (a, "pan_lvl")) / 100.0;
        ap.panDelay = pv (aid (a, "pan_dly")) / 100.0;
        ap.panPhase = pv (aid (a, "pan_ph")) / 100.0;
        ap.panHF = pv (aid (a, "pan_hf")) / 100.0;
    }
    p.aerOutDb = pv ("aer_out");
}

void DeltaABProcessor::processDouble (const double* const* in, int numIn, const double* const* aerIn, int numAerIn,
                                      double* const* out, double* const* aerOut, int n)
{
    if (auto* s = pending.exchange (nullptr))
    {
        retired.store (active);
        active = s;
    }
    for (int a = 0; a < 3; ++a)
        if (auto* f = eqPending[(size_t) a].exchange (nullptr))
        {
            eqRetired[(size_t) a].store (eqActive[(size_t) a]);
            eqActive[(size_t) a] = f;
        }
    if (active == nullptr)
    {
        for (int c = 0; c < 2; ++c)
        {
            juce::FloatVectorOperations::clear (out[c], n);
            if (aerOut != nullptr) juce::FloatVectorOperations::clear (aerOut[c], n);
        }
        return;
    }

    // Source selection. 12 inputs: per mic O1, O2, R1, R2. 6 inputs: per mic O, R. Then AER L, C, R.
    scratch.clear (kSilence, 0, n);
    const double* silence = scratch.getReadPointer (kSilence);
    auto ch = [&] (int c) { return c < numIn ? in[c] : silence; };
    const double* ip[9];
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
    for (int a = 0; a < 3; ++a) ip[6 + a] = a < numAerIn ? aerIn[a] : silence;

    dab::EngineParams p;
    readParams (p);
    dab::AerEqFilter* eq[3] = { eqActive[0], eqActive[1], eqActive[2] };
    engine.process (*active, eq, p, ip, out, aerOut, n);
}

// Channel layout of the process buffer: main in (12 or 6), AER in (3), and outputs main (2), AER (2) from channel 0.
DeltaABProcessor::Io DeltaABProcessor::io (int bufferChannels) const
{
    Io r;
    r.numIn = juce::jmin (12, getChannelCountOfBus (true, 0));
    r.aerInStart = getChannelCountOfBus (true, 0);
    r.numAerIn = getBusCount (true) > 1 && getBus (true, 1)->isEnabled() ? juce::jmin (3, getChannelCountOfBus (true, 1)) : 0;
    r.aerOutStart = getChannelCountOfBus (false, 0);
    r.aerOutOn = getBusCount (false) > 1 && getBus (false, 1)->isEnabled() && getChannelCountOfBus (false, 1) >= 2
                 && r.aerOutStart + 2 <= bufferChannels;
    r.numIn = juce::jmin (r.numIn, bufferChannels);
    r.numAerIn = juce::jlimit (0, juce::jmax (0, bufferChannels - r.aerInStart), r.numAerIn);
    return r;
}

void DeltaABProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), nch = buffer.getNumChannels();
    if (nch < 2) return;
    if (scratch.getNumSamples() < n) scratch.setSize (kScratchCh, n, false, false, true);
    const auto r = io (nch);
    // Inputs and outputs share channels: copy the inputs out first.
    for (int c = 0; c < r.numIn; ++c) scratch.copyFrom (c, 0, buffer, c, 0, n);
    for (int c = 0; c < r.numAerIn; ++c) scratch.copyFrom (12 + c, 0, buffer, r.aerInStart + c, 0, n);
    const double* in[12];
    const double* aerIn[3];
    for (int c = 0; c < 12; ++c) in[c] = scratch.getReadPointer (c);
    for (int c = 0; c < 3; ++c) aerIn[c] = scratch.getReadPointer (12 + c);
    double* out[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
    double* aerOut[2] = { nullptr, nullptr };
    if (r.aerOutOn) { aerOut[0] = buffer.getWritePointer (r.aerOutStart); aerOut[1] = buffer.getWritePointer (r.aerOutStart + 1); }
    processDouble (in, r.numIn, aerIn, r.numAerIn, out, r.aerOutOn ? aerOut : nullptr, n);
    for (int c = r.aerOutOn ? r.aerOutStart + 2 : 2; c < nch; ++c) buffer.clear (c, 0, n);
    if (r.aerOutOn) for (int c = 2; c < r.aerOutStart; ++c) buffer.clear (c, 0, n);
}

void DeltaABProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), nch = buffer.getNumChannels();
    if (nch < 2) return;
    if (scratch.getNumSamples() < n) scratch.setSize (kScratchCh, n, false, false, true);
    const auto r = io (nch);
    auto toD = [&] (int src, int dst) { const float* s = buffer.getReadPointer (src); double* d = scratch.getWritePointer (dst); for (int i = 0; i < n; ++i) d[i] = s[i]; };
    for (int c = 0; c < r.numIn; ++c) toD (c, c);
    for (int c = 0; c < r.numAerIn; ++c) toD (r.aerInStart + c, 12 + c);
    const double* in[12];
    const double* aerIn[3];
    for (int c = 0; c < 12; ++c) in[c] = scratch.getReadPointer (c);
    for (int c = 0; c < 3; ++c) aerIn[c] = scratch.getReadPointer (12 + c);
    double* out[2] = { scratch.getWritePointer (kOut), scratch.getWritePointer (kOut + 1) };
    double* aerOut[2] = { scratch.getWritePointer (kOut + 2), scratch.getWritePointer (kOut + 3) };
    processDouble (in, r.numIn, aerIn, r.numAerIn, out, r.aerOutOn ? aerOut : nullptr, n);
    buffer.clear();
    auto toF = [&] (int src, int dst) { const double* s = scratch.getReadPointer (src); float* d = buffer.getWritePointer (dst); for (int i = 0; i < n; ++i) d[i] = (float) s[i]; };
    toF (kOut, 0); toF (kOut + 1, 1);
    if (r.aerOutOn) { toF (kOut + 2, r.aerOutStart); toF (kOut + 3, r.aerOutStart + 1); }
}

juce::AudioProcessorEditor* DeltaABProcessor::createEditor()
{
    return new DeltaABEditor (*this);
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
