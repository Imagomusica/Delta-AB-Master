#include "PluginProcessor.h"
#include "PluginEditor.h"

DeltaCompProcessor::DeltaCompProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DeltaComp", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DeltaCompProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    auto fl = [&] (const char* id, const char* name, float lo, float hi, float def, float step, float skewMid, const char* unit, int dec)
    {
        NormalisableRange<float> r (lo, hi, step);
        if (skewMid > 0) r.setSkewForCentre (skewMid);
        v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, String::fromUTF8 (name), r, def,
            AudioParameterFloatAttributes().withLabel (String::fromUTF8 (unit))
                .withStringFromValueFunction ([dec, lo] (float x, int) { return dec < 0 ? (x <= lo ? String ("OFF") : String (roundToInt (x))) : String (x, dec); })));
    };
    auto bo = [&] (const char* id, const char* name, bool def)
    { v.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, String::fromUTF8 (name), def)); };
    auto ch = [&] (const char* id, const char* name, std::initializer_list<const char*> items, int def)
    {
        StringArray a; for (auto* s : items) a.add (String::fromUTF8 (s));
        v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, String::fromUTF8 (name), a, def));
    };

    // global
    fl ("target",  "Cible",            -30.f,  -5.f, -14.f, 0.1f, 0,     "LUFS", 1);
    bo ("autoGain","Gain auto",        false);
    fl ("window",  "Fenêtre RMS",        5.f, 3000.f, 300.f, 1.f, 300.f, "ms", 0);
    fl ("link",    "Lien stéréo",        0.f, 100.f, 100.f, 1.f, 0,      "%", 0);
    fl ("knee",    "Genou",              0.f,  18.f,   6.f, 0.1f, 0,     "dB", 1);
    fl ("density", "Density",            0.f, 100.f,   0.f, 1.f, 0,      "%", 0);
    fl ("mix",     "Mix",                0.f, 100.f, 100.f, 1.f, 0,      "%", 0);
    fl ("out",     "Sortie",           -18.f,  18.f,   0.f, 0.1f, 0,     "dB", 1);
    // T transients
    fl ("transient","Transitoires",   -100.f, 100.f,  0.f, 1.f, 0,      "%", 0);
    fl ("trDur",   "Durée",              5.f, 200.f,  30.f, 1.f, 30.f,   "ms", 0);
    // A upward low level
    bo ("upOn",    "Upward actif",     true);
    fl ("upThr",   "Upward seuil",     -30.f,   0.f, -14.f, 0.1f, 0,     "LU", 1);
    fl ("upRatio", "Upward ratio",       1.f,   4.f,  1.5f, 0.01f, 1.8f, ":1", 2);
    fl ("upRange", "Upward plage",       0.f,  18.f,   4.f, 0.1f, 0,     "dB", 1);
    fl ("upAtt",   "Upward attaque",   0.5f, 500.f,  50.f, 0.1f, 40.f,   "ms", 1);
    fl ("upRel",   "Upward release",   10.f, 5000.f, 600.f, 1.f, 500.f,  "ms", 0);
    // B downward low level
    bo ("llOn",    "Bas niveau actif", false);
    fl ("llThr",   "Bas niveau seuil", -40.f,   0.f, -24.f, 0.1f, 0,     "LU", 1);
    fl ("llRatio", "Bas niveau ratio",   1.f,   4.f,  1.5f, 0.01f, 1.8f, ":1", 2);
    fl ("llRange", "Bas niveau plage",   0.f,  12.f,   3.f, 0.1f, 0,     "dB", 1);
    fl ("llAtt",   "Bas niveau attaque",0.5f, 500.f, 30.f, 0.1f, 40.f,   "ms", 1);
    fl ("llRel",   "Bas niveau release",10.f, 5000.f, 400.f, 1.f, 500.f, "ms", 0);
    // C double threshold
    bo ("dnOn",    "Double seuil actif", true);
    ch ("opto",    "Détection",        { "RMS", "OPTO" }, 0);
    fl ("thr1",    "Seuil 1",          -12.f,  12.f,   0.f, 0.1f, 0,     "LU", 1);
    fl ("ratio1",  "Ratio 1",            1.f,  10.f,  1.5f, 0.01f, 2.5f, ":1", 2);
    fl ("att1",    "Attaque 1",        0.1f, 500.f,  30.f, 0.1f, 30.f,   "ms", 1);
    fl ("rel1",    "Release 1",         10.f, 5000.f, 300.f, 1.f, 300.f, "ms", 0);
    fl ("thr2",    "Seuil 2",          -12.f,  18.f,   4.f, 0.1f, 0,     "LU", 1);
    fl ("ratio2",  "Ratio 2",            1.f,  20.f,   4.f, 0.01f, 4.f,  ":1", 2);
    fl ("att2",    "Attaque 2",        0.1f, 500.f,  10.f, 0.1f, 30.f,   "ms", 1);
    fl ("rel2",    "Release 2",         10.f, 5000.f, 800.f, 1.f, 300.f, "ms", 0);
    fl ("hyst",    "Hystérésis",         0.f,   6.f,   0.f, 0.1f, 0,     "dB", 1);
    // D peak
    bo ("pkOn",    "Peak actif",       true);
    fl ("crest",   "Crest",              0.f,  24.f,  12.f, 0.1f, 0,     "dB", 1);
    fl ("pkRatio", "Peak ratio",         1.f,  20.f,   4.f, 0.01f, 4.f,  ":1", 2);
    fl ("pkAtt",   "Peak attaque",    0.05f,  20.f,  0.5f, 0.01f, 1.f,   "ms", 2);
    fl ("pkRel",   "Peak release",       5.f, 1000.f, 80.f, 1.f, 80.f,   "ms", 0);
    // Ambiance
    bo ("ambOn",   "Ambiance active",  false);
    fl ("ambiance","Ambiance",           0.f, 100.f,  50.f, 1.f, 0,      "%", 0);
    fl ("ambThr",  "Ambiance seuil",   -30.f,   0.f, -20.f, 0.1f, 0,     "LU", 1);
    fl ("ambRatio","Ambiance ratio",     1.f,   4.f,   2.f, 0.01f, 1.8f, ":1", 2);
    fl ("ambAtt",  "Ambiance attaque",   1.f, 200.f,  20.f, 0.1f, 20.f,  "ms", 1);
    fl ("ambRel",  "Ambiance release",  20.f, 3000.f, 400.f, 1.f, 400.f, "ms", 0);
    bo ("ambAuto", "Ambiance auto",    true);
    fl ("ambPre",  "Ambiance pré-délai", 2.f,  80.f,  20.f, 0.1f, 20.f,  "ms", 1);
    ch ("ambStage","Ambiance après",   { "UPWARD", "BAS NIV.", "DOUBLE", "PEAK" }, 2);
    // M/S
    fl ("width",   "Largeur",           -6.f,   6.f,   0.f, 0.1f, 0,     "dB", 1);
    fl ("centre",  "Centre",            -6.f,   6.f,   0.f, 0.1f, 0,     "dB", 1);
    ch ("centreFilter", "Filtre centre", { "LARGE", "GRAVE", "AIGU", "CLOCHE" }, 0);
    fl ("centreFreq", "Fréquence centre", 20.f, 20000.f, 1000.f, 1.f, 1000.f, "Hz", 0);
    fl ("sideLowCut", "Low cut side",    0.f, 300.f,   0.f, 1.f, 0,      "Hz", -1);   // 0 = OFF
    ch ("sideSlope", "Pente",          { "6", "12", "24" }, 1);
    bo ("revOn",   "Réverbe active",   false);
    ch ("revTarget","Réverbe sur",     { "MID", "SIDE", "M+S" }, 1);
    fl ("revSend", "Réverbe envoi",    -48.f,   0.f, -24.f, 0.1f, 0,     "dB", 1);
    fl ("revDecay","Réverbe durée",    0.3f,   6.f,  1.8f, 0.01f, 1.5f,  "s", 2);
    fl ("revPre",  "Réverbe pré-délai",  0.f,  80.f,  20.f, 0.1f, 20.f,  "ms", 1);
    bo ("revPreAuto","Réverbe pré auto", true);
    fl ("revDyn",  "Réverbe dynamique",  0.f, 100.f, 100.f, 1.f, 0,      "%", 0);
    // E limiter
    bo ("limOn",   "Limiteur actif",   true);
    fl ("ceiling", "Plafond",           -6.f,   0.f,  -1.f, 0.1f, 0,     "dBTP", 1);
    fl ("limRel",  "Limiteur release",   1.f, 1000.f, 60.f, 1.f, 60.f,   "ms", 0);
    fl ("look",    "Lookahead",        0.1f,   5.f,   2.f, 0.01f, 0,     "ms", 2);
    ch ("os",      "Oversampling",     { "OFF", "2X", "4X", "8X" }, 0);
    bo ("truePeak","True peak",        true);
    fl ("linkT",   "Lien transitoires",  0.f, 100.f, 100.f, 1.f, 0,      "%", 0);
    fl ("linkR",   "Lien release",       0.f, 100.f, 100.f, 1.f, 0,      "%", 0);
    return { v.begin(), v.end() };
}

dcomp::Params DeltaCompProcessor::currentParams() const
{
    dcomp::Params p;
    p.target = pv ("target"); p.autoGain = pb ("autoGain"); p.windowMs = pv ("window"); p.link = pv ("link") * 0.01;
    p.knee = pv ("knee"); p.density = pv ("density") * 0.01; p.mix = pv ("mix") * 0.01; p.outGainDb = pv ("out");
    p.transient = pv ("transient") * 0.01; p.trDurMs = pv ("trDur");
    p.upOn = pb ("upOn"); p.upThr = pv ("upThr"); p.upRatio = pv ("upRatio"); p.upRange = pv ("upRange"); p.upAtt = pv ("upAtt"); p.upRel = pv ("upRel");
    p.llOn = pb ("llOn"); p.llThr = pv ("llThr"); p.llRatio = pv ("llRatio"); p.llRange = pv ("llRange"); p.llAtt = pv ("llAtt"); p.llRel = pv ("llRel");
    p.dnOn = pb ("dnOn"); p.opto = pi ("opto") == 1;
    p.thr1 = pv ("thr1"); p.ratio1 = pv ("ratio1"); p.att1 = pv ("att1"); p.rel1 = pv ("rel1");
    p.thr2 = pv ("thr2"); p.ratio2 = pv ("ratio2"); p.att2 = pv ("att2"); p.rel2 = pv ("rel2"); p.hyst = pv ("hyst");
    p.pkOn = pb ("pkOn"); p.crest = pv ("crest"); p.pkRatio = pv ("pkRatio"); p.pkAtt = pv ("pkAtt"); p.pkRel = pv ("pkRel");
    p.ambOn = pb ("ambOn"); p.ambiance = pv ("ambiance") * 0.01; p.ambThr = pv ("ambThr"); p.ambRatio = pv ("ambRatio");
    p.ambAtt = pv ("ambAtt"); p.ambRel = pv ("ambRel"); p.ambAuto = pb ("ambAuto"); p.ambPreMs = pv ("ambPre"); p.ambStage = pi ("ambStage") + 1;
    p.widthDb = pv ("width"); p.centreDb = pv ("centre"); p.centreFilter = pi ("centreFilter"); p.centreFreq = pv ("centreFreq");
    p.sideLowCut = pv ("sideLowCut"); p.sideSlope = pi ("sideSlope") == 0 ? 6 : pi ("sideSlope") == 1 ? 12 : 24;
    p.revOn = pb ("revOn"); p.revTarget = pi ("revTarget"); p.revSendDb = pv ("revSend"); p.revDecay = pv ("revDecay");
    p.revPreMs = pv ("revPre"); p.revPreAuto = pb ("revPreAuto"); p.revDyn = pv ("revDyn") * 0.01;
    p.limOn = pb ("limOn"); p.ceiling = pv ("ceiling"); p.limRel = pv ("limRel"); p.lookMs = pv ("look");
    p.os = 1 << pi ("os"); p.truePeak = pb ("truePeak"); p.linkT = pv ("linkT") * 0.01; p.linkR = pv ("linkR") * 0.01;
    return p;
}

void DeltaCompProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, juce::jmax (1, getTotalNumInputChannels()));
    engine.setParams (currentParams());
    engine.reset();
    setLatencySamples (engine.latency());
}

bool DeltaCompProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

template <typename T>
void DeltaCompProcessor::processAny (juce::AudioBuffer<T>& b)
{
    juce::ScopedNoDenormals nd;
    engine.setParams (currentParams());
    const int ch = juce::jmin (b.getNumChannels(), getTotalNumInputChannels());
    engine.process (b.getArrayOfWritePointers(), ch, b.getNumSamples());
}

void DeltaCompProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void DeltaCompProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* DeltaCompProcessor::createEditor() { return new DeltaCompEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DeltaCompProcessor(); }
