#include "PluginProcessor.h"
#include "PluginEditor.h"

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using Range = juce::NormalisableRange<float>;

static juce::String u (const char* s) { return juce::String::fromUTF8 (s); }

static const char* micId[3]   = { "m1", "m2", "m3" };
static juce::String aid (int a, const char* p) { return "a" + juce::String (a + 1) + "_" + p; }
static juce::String sid (int k, const char* p) { return "s" + juce::String (k + 1) + "_" + p; }
static const char* micName[3] = { "AB.01", "AB.02", "AB.03" };
static const char* busId[3]   = { "L", "C", "R" };
static const char* busName[3] = { "Bus G", "Bus C", "Bus D" };
static constexpr int kNumX = 7;
static const double xoverHz[kNumX] = { 350, 700, 1250, 1600, 1950, 2200, 4400 };
static const juce::StringArray xoverNames { "350 Hz", "700 Hz", "1250 Hz", "1600 Hz", "1950 Hz", "2200 Hz", "4400 Hz" };

// Two of the four sources of a hybrid mic: X and Y. Sources: 0 = O1 MK221, 1 = O2 MK102, 2 = R1 (aigu), 3 = R2 (grave).
// Sources: 0 = O1 MK221, 1 = O2 MK102, 2 = R1 (tuned for highs), 3 = R2 (tuned for lows).
// Auto rule for the switch: the capsule more suited to the lows goes there (R2 > O2 > O1 > R1).
static const int lowRank[4] = { 1, 2, 0, 3 };
static const juce::StringArray sourceNames { "O1", "O2", "R1", "R2" };

static juce::String pid (int m, const char* p) { return juce::String (micId[m]) + "_" + p; }

static const char* aerName[3] = { "AER.01", "AER.02", "AER.03" };
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
    const float defPan[3] = { -1.0f, 0.0f, 1.0f };
    for (int m = 0; m < 3; ++m)
    {
        const juce::String n (micName[m]);
        l.add (std::make_unique<APC> (juce::ParameterID { pid (m, "xsrc"), 1 }, n + " · Source X", sourceNames, 0));
        l.add (std::make_unique<APC> (juce::ParameterID { pid (m, "ysrc"), 1 }, n + " · Source Y", sourceNames, 2));
        l.add (std::make_unique<APB> (juce::ParameterID { pid (m, "swap"), 1 }, n + u (" · Inverser entrées X/Y (6 canaux)"), false));
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "gainx"), 1 }, n + u (" · Gain X"), db (-24, 24, 0.1f), 0.0f, attrDb));
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "match"), 1 }, n + u (" · Gain Y"), db (-24, 24, 0.1f), 0.0f, attrDb));
        l.add (std::make_unique<APB> (juce::ParameterID { pid (m, "analyse"), 1 }, n + u (" · Analyser X/Y"), false));
        l.add (std::make_unique<APC> (juce::ParameterID { pid (m, "xover"), 1 }, n + u (" · Coupure"),
                                      [] { juce::StringArray a { "Focal" }; a.addArray (xoverNames); return a; }(), 0));
        l.add (std::make_unique<APC> (juce::ParameterID { pid (m, "lows"), 1 }, n + u (" · Dans le grave"),
                                      juce::StringArray { u ("Auto (R2 > O2 > O1 > R1 vers le grave)"), u ("X grave, Y aigu"), u ("Y grave, X aigu") }, 0));
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "focus"), 1 }, n + u (" · Focus (aigu)"), db (-24, 12, 0.1f), 0.0f, attrDb));
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "bloom"), 1 }, n + u (" · Bloom (grave)"), db (-24, 12, 0.1f), 0.0f, attrDb));
        l.add (std::make_unique<APF> (juce::ParameterID { pid (m, "level"), 1 }, n + u (" · Niveau de sortie"), db (-60, 12, 0.1f), 0.0f, attrDb));
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

    // EN-MS spots: cardioid (Mid) + ribbon (Side)
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "spots_n", 1 }, "Spots EN-MS : nombre de tranches", 1, dab::kMaxSpots, 1));
    l.add (std::make_unique<APF> (juce::ParameterID { "spots_out", 1 }, "Spots EN-MS : sortie", db (-24, 12, 0.1f), 0.0f, attrDb));
    for (int k = 0; k < dab::kMaxSpots; ++k)
    {
        const juce::String n = "EN-MS." + juce::String (k + 1).paddedLeft ('0', 2);
        auto id = [k] (const char* x) { return sid (k, x); };
        l.add (std::make_unique<APC> (juce::ParameterID { id ("mode"), 1 }, n + " · Mode", juce::StringArray { "M/S", "Crossover" }, 0));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("gainc"), 1 }, n + " · Gain cardio (M)", db (-24, 24, 0.1f), 0.0f, attrDb));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("match"), 1 }, n + " · Gain ruban (S)", db (-24, 24, 0.1f), 0.0f, attrDb));
        l.add (std::make_unique<APB> (juce::ParameterID { id ("invert"), 1 }, n + u (" · M/S : ruban côté opposé"), false));
        l.add (std::make_unique<APC> (juce::ParameterID { id ("out"), 1 }, n + u (" · M/S : somme ou stéréo"), juce::StringArray { "Somme (mono)", u ("Stéréo") }, 0));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("width"), 1 }, n + u (" · Stéréo : largeur"), Range (0, 100, 1), 50.0f,
                                      juce::AudioParameterFloatAttributes().withLabel ("%")));
        l.add (std::make_unique<APC> (juce::ParameterID { id ("xover"), 1 }, n + " · Coupure",
                                      [] { juce::StringArray a { "Focal" }; a.addArray (xoverNames); return a; }(), 0));
        l.add (std::make_unique<APC> (juce::ParameterID { id ("lows"), 1 }, n + " · Dans le grave",
                                      juce::StringArray { "Cardio grave, ruban aigu", "Ruban grave, cardio aigu" }, 0));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("cardio"), 1 }, n + " · Niveau cardio (M)", db (-60, 12, 0.1f), 0.0f, attrDb));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("ribbon"), 1 }, n + " · Niveau ruban (S)", db (-60, 12, 0.1f), 0.0f, attrDb));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("delay"), 1 }, n + u (" · Délai"), Range (-10, 100, 0.01f), 0.0f,
                                      juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (
                                          [] (float v, int) { return juce::String (v, 2) + " ms (" + juce::String (v * 0.343f, 2) + " m)"; })));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("fader"), 1 }, n + " · Fader", db (-60, 12, 0.1f), 0.0f, attrDb));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("pan"), 1 }, n + u (" · Pan G-C-D"), Range (-1, 1, 0.01f),
                                      k < 8 ? -0.8f + 1.6f * (float) k / 7.0f : 0.0f));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("pan_lvl"), 1 }, n + u (" · Pan niveau"), Range (0, 100, 1), 100.0f, pct));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("pan_dly"), 1 }, n + u (" · Pan délai"), Range (0, 100, 1), 0.0f, pct));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("pan_ph"), 1 }, n + u (" · Pan phase"), Range (0, 100, 1), 0.0f, pct));
        l.add (std::make_unique<APF> (juce::ParameterID { id ("pan_hf"), 1 }, n + u (" · Pan HF (grave au centre)"), Range (0, 100, 1), 0.0f, pct));
    }
    return l;
}

// AB (12) and AER (3) inputs, then one stereo input per EN-MS spot (left = cardio, right = ribbon).
// Fixed VST3 speaker layouts only: hosts flatten them to inputs 1-12, 13-15, then 16-47.
// Inputs: ΔAB X/Y (6, or 12 = all four capsules), AER G C D (3), then one stereo input per EN-MS spot (cardio, ruban).
// Outputs, for summing in the DAW bus (Pyramix): one stereo pair per AB mic, the AER, one per spot, then an optional mix.
// Only spot 1 is enabled by default (first test: 11 inputs); the host can enable more spot buses.
juce::AudioProcessor::BusesProperties DeltaABProcessor::makeBuses()
{
    BusesProperties b;
    b = b.withInput (juce::String::fromUTF8 ("ΔAB X/Y (AB.01-03)"), juce::AudioChannelSet::create5point1(), true)
         .withInput ("AER.01-03", juce::AudioChannelSet::createLCR(), true);
    for (int k = 0; k < dab::kMaxSpots; ++k)
        b = b.withInput ("EN-MS." + juce::String (k + 1).paddedLeft ('0', 2) + " (cardio, ruban)", juce::AudioChannelSet::stereo(), k == 0);
    const char* micOut[3] = { "AB.01", "AB.02", "AB.03" };
    for (int m = 0; m < 3; ++m) b = b.withOutput (micOut[m], juce::AudioChannelSet::stereo(), true);
    b = b.withOutput ("AER", juce::AudioChannelSet::stereo(), true);
    for (int k = 0; k < dab::kMaxSpots; ++k) b = b.withOutput ("EN-MS." + juce::String (k + 1).paddedLeft ('0', 2), juce::AudioChannelSet::stereo(), k == 0);
    b = b.withOutput ("Mix", juce::AudioChannelSet::stereo(), false);
    return b;
}

DeltaABProcessor::DeltaABProcessor()
    : AudioProcessor (makeBuses()),
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
    for (int b = 2; b < layouts.inputBuses.size(); ++b)
        if (! (layouts.inputBuses[b].isDisabled() || layouts.inputBuses[b] == juce::AudioChannelSet::stereo())) return false;
    for (int b = 1; b < layouts.outputBuses.size(); ++b)
        if (! (layouts.outputBuses[b].isDisabled() || layouts.outputBuses[b] == juce::AudioChannelSet::stereo())) return false;
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
    for (int k = 0; k < dab::kMaxSpots; ++k)
    {
        const int x = juce::jlimit (0, kNumX, (int) pv (sid (k, "xover")));
        c.spotFc[(size_t) k] = x == 0 ? focal : xoverHz[x - 1];
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
        mp.gainXDb = pv (pid (m, "gainx"));
        mp.analyse = pv (pid (m, "analyse")) > 0.5f;
        const int xs = juce::jlimit (0, 3, (int) pv (pid (m, "xsrc"))), ys = juce::jlimit (0, 3, (int) pv (pid (m, "ysrc")));
        const int lows = (int) pv (pid (m, "lows"));
        const bool inLows = lows == 0 ? lowRank[ys] > lowRank[xs] : lows == 2;   // true: Y in the lows
        mp.ribbonInLows = inLows != (pv ("ambient") > 0.5f);
        const double mf = pv ("m_focus"), mb = pv ("m_bloom");
        mp.focusDb = pv (pid (m, "focus")) + mf - ((int) pv ("bloom_mode") == 1 ? mb : 0.0);
        mp.bloomDb = pv (pid (m, "bloom")) + mb;
        mp.levelDb = pv (pid (m, "level"));
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

    p.numSpots = juce::jlimit (1, dab::kMaxSpots, (int) pv ("spots_n"));
    p.spotsOutDb = pv ("spots_out");
    for (int k = 0; k < p.numSpots; ++k)
    {
        auto& sp = p.spot[(size_t) k];
        sp.crossover = (int) pv (sid (k, "mode")) == 1;
        sp.matchDb = pv (sid (k, "match"));
        sp.gainCDb = pv (sid (k, "gainc"));
        sp.ribbonInvert = pv (sid (k, "invert")) > 0.5f;
        sp.stereo = (int) pv (sid (k, "out")) == 1;
        sp.width = pv (sid (k, "width")) / 100.0;
        sp.ribbonInLows = (int) pv (sid (k, "lows")) == 1;
        const float c = pv (sid (k, "cardio")), r = pv (sid (k, "ribbon"));
        sp.cardioDb = c <= -60.0f ? -400.0 : c;    // bottom of the range = off
        sp.ribbonDb = r <= -60.0f ? -400.0 : r;
        sp.delayMs = pv (sid (k, "delay"));
        sp.faderDb = pv (sid (k, "fader")) <= -60.0f ? -400.0 : pv (sid (k, "fader"));
        sp.pan = pv (sid (k, "pan"));
        sp.panLevel = pv (sid (k, "pan_lvl")) / 100.0;
        sp.panDelay = pv (sid (k, "pan_dly")) / 100.0;
        sp.panPhase = pv (sid (k, "pan_ph")) / 100.0;
        sp.panHF = pv (sid (k, "pan_hf")) / 100.0;
    }
}

// in: scratch slots (kInMain main inputs, kInAer AER, kInSpots spots); numMain: 12 or 6 channels on the main bus.
void DeltaABProcessor::processDouble (const double* const* in, int numMain, double* const* out,
                                      double* const* aerOut, double* const* spotsOut, int n)
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
            for (auto* o : { out, aerOut, spotsOut })
                if (o != nullptr) juce::FloatVectorOperations::clear (o[c], n);
        return;
    }

    // Source selection. 12 inputs: per mic O1, O2, R1, R2 (routed by the X/Y selectors). 6 inputs: per mic X, Y.
    const double* ip[dab::kNumIn];
    for (int m = 0; m < 3; ++m)
    {
        if (numMain >= 12)   // all four capsules come in: the selectors route them
        {
            ip[m * 2]     = in[m * 4 + juce::jlimit (0, 3, (int) pv (pid (m, "xsrc")))];   // X
            ip[m * 2 + 1] = in[m * 4 + juce::jlimit (0, 3, (int) pv (pid (m, "ysrc")))];   // Y
        }
        else
        {
            const bool sw = pv (pid (m, "swap")) > 0.5f;
            ip[m * 2]     = in[m * 2 + (sw ? 1 : 0)];
            ip[m * 2 + 1] = in[m * 2 + (sw ? 0 : 1)];
        }
    }
    for (int a = 0; a < 3; ++a) ip[6 + a] = in[kInAer + a];
    for (int c = 0; c < 2 * dab::kMaxSpots; ++c) ip[9 + c] = in[kInSpots + c];

    dab::EngineParams p;
    readParams (p);
    dab::AerEqFilter* eq[3] = { eqActive[0], eqActive[1], eqActive[2] };
    engine.process (*active, eq, p, ip, out, aerOut, spotsOut, n, flowPtrs);
}

// Where each bus sits in the process buffer (inputs and outputs both start at channel 0).
DeltaABProcessor::Io DeltaABProcessor::io (int bufferChannels) const
{
    Io r;
    auto busCh = [&] (bool input, int b) { return b < getBusCount (input) && getBus (input, b)->isEnabled() ? getChannelCountOfBus (input, b) : 0; };
    auto start = [&] (bool input, int b) { int st = 0; for (int k = 0; k < b; ++k) st += busCh (input, k); return st; };
    const int clampN = bufferChannels;
    auto fit = [&] (int st, int cnt) { return juce::jlimit (0, juce::jmax (0, clampN - st), cnt); };
    r.numMain = fit (0, juce::jmin (12, busCh (true, 0)));
    r.aerStart = start (true, 1);   r.numAer = fit (r.aerStart, juce::jmin (3, busCh (true, 1)));
    for (int k = 0; k < dab::kMaxSpots; ++k)
    {
        r.spotStart[(size_t) k] = start (true, 2 + k);
        r.spotIn[(size_t) k] = fit (r.spotStart[(size_t) k], juce::jmin (2, busCh (true, 2 + k)));
    }
    // output buses: 0..2 AB mics, 3 AER, 4.. spots, last = mix
    auto flowBus = [] (int f) { return f < 3 ? f : f + 1; };
    for (int f = 0; f < kNumFlowOut; ++f)
    {
        r.flowStart[(size_t) f] = start (false, flowBus (f));
        r.flowOn[(size_t) f] = busCh (false, flowBus (f)) >= 2 && r.flowStart[(size_t) f] + 2 <= clampN;
    }
    r.aerOutStart = start (false, 3);   r.aerOutOn = busCh (false, 3) >= 2 && r.aerOutStart + 2 <= clampN;
    const int mixBus = 4 + dab::kMaxSpots;
    r.mixStart = start (false, mixBus); r.mixOn = busCh (false, mixBus) >= 2 && r.mixStart + 2 <= clampN;
    return r;
}

template <typename T>
void DeltaABProcessor::processAny (juce::AudioBuffer<T>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), nch = buffer.getNumChannels();
    if (nch < 2) return;
    if (scratch.getNumSamples() < n) scratch.setSize (kScratchCh, n, false, false, true);
    const auto r = io (nch);

    // Inputs and outputs share the buffer: copy every input to its scratch slot first (missing ones silent).
    auto take = [&] (int src, int count, int slot, int slots)
    {
        for (int c = 0; c < slots; ++c)
        {
            double* d = scratch.getWritePointer (slot + c);
            if (c < count) { const T* s = buffer.getReadPointer (src + c); for (int i = 0; i < n; ++i) d[i] = (double) s[i]; }
            else juce::FloatVectorOperations::clear (d, n);
        }
    };
    take (0, r.numMain, kInMain, 12);
    take (r.aerStart, r.numAer, kInAer, 3);
    for (int k = 0; k < dab::kMaxSpots; ++k) take (r.spotStart[(size_t) k], r.spotIn[(size_t) k], kInSpots + 2 * k, 2);

    const double* in[kOut];
    for (int c = 0; c < kOut; ++c) in[c] = scratch.getReadPointer (c);
    double* o[6];
    for (int c = 0; c < 6; ++c) o[c] = scratch.getWritePointer (kOut + c);
    // engine flow order: AB mics 0..2, AER 3..5 (not sent out one by one), spots 6..
    double* flows[2 * dab::kPanCh] = {};
    for (int f = 0; f < kNumFlowOut; ++f)
        if (r.flowOn[(size_t) f])
        {
            const int e = f < 3 ? f : f + 3;
            flows[2 * e] = scratch.getWritePointer (kFlowOut + 2 * f);
            flows[2 * e + 1] = scratch.getWritePointer (kFlowOut + 2 * f + 1);
            if (f >= 3)   // a spot's right side (stereo M/S) goes to the same pair
            {
                const int r = 6 + dab::kMaxSpots + (f - 3);
                flows[2 * r] = flows[2 * e]; flows[2 * r + 1] = flows[2 * e + 1];
            }
        }
    flowPtrs = flows;
    processDouble (in, r.numMain, o, r.aerOutOn ? o + 2 : nullptr, nullptr, n);
    flowPtrs = nullptr;

    buffer.clear();
    auto give = [&] (int slot, int dst) { const double* s = scratch.getReadPointer (slot); T* d = buffer.getWritePointer (dst); for (int i = 0; i < n; ++i) d[i] = (T) s[i]; };
    if (r.mixOn) { give (kOut, r.mixStart); give (kOut + 1, r.mixStart + 1); }
    if (r.aerOutOn) { give (kOut + 2, r.aerOutStart); give (kOut + 3, r.aerOutStart + 1); }
    for (int f = 0; f < kNumFlowOut; ++f)
        if (r.flowOn[(size_t) f]) { give (kFlowOut + 2 * f, r.flowStart[(size_t) f]); give (kFlowOut + 2 * f + 1, r.flowStart[(size_t) f] + 1); }
}

void DeltaABProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) { processAny (buffer); }
void DeltaABProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { processAny (buffer); }

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
