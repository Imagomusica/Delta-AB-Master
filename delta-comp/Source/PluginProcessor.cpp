#include "PluginProcessor.h"
#include "PluginEditor.h"

DeltaCompProcessor::DeltaCompProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DeltaComp", createLayout())
{
   #if JUCE_PLUGINHOST_VST3
    formats.addFormat (new juce::VST3PluginFormat());
   #endif
   #if JUCE_PLUGINHOST_AU && JUCE_MAC
    formats.addFormat (new juce::AudioUnitPluginFormat());
   #endif
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
    fl ("target",  "Cible",            -30.f,  -5.f, -16.f, 0.1f, 0,     "LUFS", 1);
    bo ("autoGain","Gain auto",        true);
    fl ("window",  "Fenêtre RMS",        5.f, 3000.f, 300.f, 1.f, 300.f, "ms", 0);
    fl ("link",    "Lien stéréo",        0.f, 100.f, 100.f, 1.f, 0,      "%", 0);
    fl ("knee",    "Genou",              0.f,  18.f,   6.f, 0.1f, 0,     "dB", 1);
    fl ("density", "Density",            0.f, 100.f,   0.f, 1.f, 0,      "%", 0);
    fl ("mix",     "Mix",                0.f, 100.f, 100.f, 1.f, 0,      "%", 0);
    fl ("in",      "Entrée",           -18.f,  18.f,   0.f, 0.1f, 0,     "dB", 1);
    ch ("meterLook","Avance modulomètres", { "OFF", "25 ms", "50 ms", "75 ms", "100 ms", "150 ms", "200 ms" }, 0);
    fl ("out",     "Sortie",           -18.f,  18.f,   0.f, 0.1f, 0,     "dB", 1);
    // T transients
    bo ("active",  "Actif (bypass général)", true);
    bo ("trOn",    "Transitoires actif", true);
    fl ("transient","Transitoires",   -100.f, 100.f,  0.f, 1.f, 0,      "%", 0);
    ch ("trTarget","Transitoires sur", { "ST", "MID", "SIDE" }, 0);
    fl ("trDur",   "Durée",              5.f, 200.f,  30.f, 1.f, 30.f,   "ms", 0);
    // A upward low level
    bo ("upOn",    "Upward actif",     true);
    ch ("upMode",  "Upward mode",      { "SPECTRAL", "LARGE" }, 0);
    fl ("upThr",   "Upward seuil",     -74.f,   0.f, -22.f, 0.1f, 0,     "LU", 1);
    fl ("upRatio", "Upward ratio",       1.f,   4.f,  2.0f, 0.01f, 1.8f, ":1", 2);
    fl ("upRange", "Upward plage",       0.f,  18.f,   6.f, 0.1f, 0,     "dB", 1);
    fl ("upAtt",   "Upward attaque",   0.5f, 500.f,  50.f, 0.1f, 40.f,   "ms", 1);
    fl ("upRel",   "Upward release",   10.f, 5000.f, 600.f, 1.f, 500.f,  "ms", 0);
    // B downward low level
    bo ("llOn",    "Bas niveau actif", true);
    fl ("llThr",   "Bas niveau seuil", -74.f,   0.f, -20.f, 0.1f, 0,     "LU", 1);
    fl ("llRatio", "Bas niveau ratio",   1.f,   4.f,  1.5f, 0.01f, 1.8f, ":1", 2);
    fl ("llRange", "Bas niveau plage",   0.f,  12.f,   3.f, 0.1f, 0,     "dB", 1);
    fl ("llAtt",   "Bas niveau attaque",0.5f, 500.f, 30.f, 0.1f, 40.f,   "ms", 1);
    fl ("llRel",   "Bas niveau release",10.f, 5000.f, 400.f, 1.f, 500.f, "ms", 0);
    // C double threshold
    bo ("dnOn",    "Double seuil actif", true);
    ch ("opto",    "Détection",        { "RMS", "OPTO", "VARI-MU" }, 0);
    fl ("thr1",    "Seuil 1",          -24.f,  12.f,  -9.f, 0.1f, 0,     "LU", 1);
    fl ("ratio1",  "Ratio 1",            1.f,  10.f,  2.0f, 0.01f, 2.5f, ":1", 2);
    fl ("att1",    "Attaque 1",        0.1f, 500.f,  30.f, 0.1f, 30.f,   "ms", 1);
    fl ("rel1",    "Release 1",         10.f, 5000.f, 300.f, 1.f, 300.f, "ms", 0);
    fl ("thr2",    "Seuil 2",          -18.f,  18.f,  -3.f, 0.1f, 0,     "LU", 1);
    fl ("ratio2",  "Ratio 2",            1.f,  20.f,   4.f, 0.01f, 4.f,  ":1", 2);
    fl ("att2",    "Attaque 2",        0.1f, 500.f,  10.f, 0.1f, 30.f,   "ms", 1);
    fl ("rel2",    "Release 2",         10.f, 5000.f, 800.f, 1.f, 300.f, "ms", 0);
    fl ("hyst",    "Hystérésis",         0.f,   6.f,   0.f, 0.1f, 0,     "dB", 1);
    // D peak
    bo ("pkOn",    "Peak actif",       true);
    fl ("crest",   "Crest",              0.f,  24.f,  12.f, 0.1f, 0,     "dB", 1);
    fl ("pkThr",   "Peak seuil",       -24.f,   0.f,  -6.f, 0.1f, 0,     "dBFS", 1);
    ch ("pkMode",  "Peak seuil mode",  { "SEUIL", "CREST" }, 1);
    fl ("pkRatio", "Peak ratio",         1.f,  20.f,   4.f, 0.01f, 4.f,  ":1", 2);
    fl ("pkAtt",   "Peak attaque",    0.05f,  20.f,  0.5f, 0.01f, 1.f,   "ms", 2);
    fl ("pkRel",   "Peak release",       5.f, 1000.f, 80.f, 1.f, 80.f,   "ms", 0);
    // Ambiance
    bo ("ambOn",   "Ambiance active",  true);
    bo ("kstOn",   "K-Stereo actif",   true);
    fl ("kStereo", "K-Stereo",           0.f, 100.f,  30.f, 1.f, 0,      "%", 0);
    fl ("ambiance","Ambiance",           0.f, 100.f,  50.f, 1.f, 0,      "%", 0);
    fl ("ambThr",  "Ambiance seuil",   -40.f,   0.f, -30.f, 0.1f, 0,     "LU", 1);
    fl ("ambRatio","Ambiance ratio",     1.f,   4.f,   3.f, 0.01f, 1.8f, ":1", 2);
    fl ("ambAtt",  "Ambiance attaque",   1.f, 200.f,  20.f, 0.1f, 20.f,  "ms", 1);
    fl ("ambRel",  "Ambiance release",  20.f, 3000.f, 400.f, 1.f, 400.f, "ms", 0);
    bo ("ambAuto", "Ambiance auto",    true);
    fl ("ambPre",  "Ambiance pré-délai", 2.f,  80.f,  20.f, 0.1f, 20.f,  "ms", 1);
    ch ("ambStage","Ambiance après",   { "UPWARD", "BAS NIV.", "DOUBLE", "PEAK" }, 2);
    fl ("ambTr",   "Ambiance transitoires", 0.f, 100.f, 50.f, 1.f, 0,   "%", 0);
    // M/S
    bo ("msOn",    "M/S actif",        true);
    fl ("width",   "Largeur",           -6.f,   6.f,   0.f, 0.1f, 0,     "dB", 1);
    bo ("tiltOn",  "Tilt actif",       true);
    fl ("tilt",    "Tilt",              -6.f,   6.f,   0.f, 0.1f, 0,     "dB", 1);   // + = brighter: +dB at 20 kHz end, -dB at the 20 Hz end, straight line
    fl ("tiltPivot","Tilt pivot",       100.f, 10000.f, 1000.f, 1.f, 1000.f, "Hz", 0);
    ch ("tiltPhase","Tilt phase",      { "LINÉAIRE", "MIN" }, 0);
    ch ("tiltPos", "Tilt position",    { "APRÈS COMP", "APRÈS INSERT", "APRÈS LIMITEUR" }, 1);
    ch ("tiltTarget","Tilt sur",       { "ST", "MID", "SIDE" }, 0);
    fl ("centre",  "Centre",            -6.f,   6.f,   0.f, 0.1f, 0,     "dB", 1);
    ch ("centreFilter", "Filtre centre", { "LARGE", "GRAVE", "AIGU", "CLOCHE" }, 0);
    fl ("centreFreq", "Fréquence centre", 20.f, 20000.f, 1000.f, 1.f, 1000.f, "Hz", 0);
    fl ("sideLowCut", "Low cut side",    0.f, 300.f,   0.f, 1.f, 0,      "Hz", -1);   // 0 = OFF
    ch ("sideSlope", "Pente",          { "6", "12", "24" }, 1);
    bo ("revOn",   "Insert actif",            false);
    ch ("revTarget","Insert sur",      { "MID", "SIDE", "L+R" }, 1);
    // E limiter
    bo ("limOn",   "Limiteur actif",   true);
    fl ("ceiling", "Plafond",           -6.f,   0.f,  -1.f, 0.1f, 0,     "dBTP", 1);
    fl ("limRel",  "Limiteur release",   1.f, 1000.f, 60.f, 1.f, 60.f,   "ms", 0);
    fl ("look",    "Lookahead",        0.1f,   5.f,   2.f, 0.01f, 0,     "ms", 2);
    ch ("os",      "Oversampling",     { "OFF", "2X", "4X", "8X", "16X", "32X", "40X" }, 0);
    fl ("osCut",   "OS coupure",        16.f,  96.f,  96.f, 0.01f, 30.f,  "kHz", 2);   // above fs/2 = fs/2
    fl ("osSteep", "OS pente",           0.f, 100.f,  85.f, 1.f, 0,       "%", 0);
    fl ("osPre",   "OS pre-ringing",     0.f, 100.f, 100.f, 1.f, 0,       "%", 0);
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
    p.trOn = pb ("trOn"); p.trTarget = pi ("trTarget"); p.transient = pv ("transient") * 0.01; p.trDurMs = pv ("trDur");
    p.upOn = pb ("upOn"); p.upSpectral = pi ("upMode") == 0; p.upThr = pv ("upThr"); p.upRatio = pv ("upRatio"); p.upRange = pv ("upRange"); p.upAtt = pv ("upAtt"); p.upRel = pv ("upRel");
    p.llOn = pb ("llOn"); p.llThr = pv ("llThr"); p.llRatio = pv ("llRatio"); p.llRange = pv ("llRange"); p.llAtt = pv ("llAtt"); p.llRel = pv ("llRel");
    p.dnOn = pb ("dnOn"); p.opto = pi ("opto") == 1; p.variMu = pi ("opto") == 2;
    p.thr1 = pv ("thr1"); p.ratio1 = pv ("ratio1"); p.att1 = pv ("att1"); p.rel1 = pv ("rel1");
    p.thr2 = pv ("thr2"); p.ratio2 = pv ("ratio2"); p.att2 = pv ("att2"); p.rel2 = pv ("rel2"); p.hyst = pv ("hyst");
    p.pkOn = pb ("pkOn"); p.crest = pv ("crest"); p.pkThr = pv ("pkThr"); p.pkCrest = pi ("pkMode") == 1;
    // K-Stereo: the knob is the room lift in dB on the side (100 % = +9 dB of decorrelated, pre-delayed ambience):
    // power 1 + k^2 = 10^(dB/10), so 30 % already gives +2.7 dB of room
    { const double kdb = pv ("kStereo") * 0.09; p.kStereo = std::sqrt (std::pow (10.0, kdb / 10.0) - 1.0); }
    p.kstOn = pb ("kstOn"); p.msOn = pb ("msOn"); p.tiltOn = pb ("tiltOn"); p.tilt = pv ("tilt") / (0.5 * std::log2 (1000.0));
    // tilt: dB at 100 Hz / 10 kHz (each end of 1000x around the pivot) -> dB/oct
    p.tiltPivot = pv ("tiltPivot"); p.tiltPos = pi ("tiltPos"); p.tiltTarget = pi ("tiltTarget"); p.tiltMin = pi ("tiltPhase") == 1; p.pkRatio = pv ("pkRatio"); p.pkAtt = pv ("pkAtt"); p.pkRel = pv ("pkRel");
    p.ambOn = pb ("ambOn"); p.ambiance = pv ("ambiance") * 0.01; p.ambThr = pv ("ambThr"); p.ambRatio = pv ("ambRatio");
    p.ambAtt = pv ("ambAtt"); p.ambRel = pv ("ambRel"); p.ambAuto = pb ("ambAuto"); p.ambPreMs = pv ("ambPre"); p.ambTr = pv ("ambTr") * 0.01; p.ambStage = pi ("ambStage") + 1;
    p.widthDb = pv ("width"); p.centreDb = pv ("centre"); p.centreFilter = pi ("centreFilter"); p.centreFreq = pv ("centreFreq");
    p.sideLowCut = pv ("sideLowCut"); p.sideSlope = pi ("sideSlope") == 0 ? 6 : pi ("sideSlope") == 1 ? 12 : 24;
    p.revOn = pb ("revOn"); p.revTarget = pi ("revTarget");
    p.limOn = pb ("limOn"); p.ceiling = pv ("ceiling"); p.limRel = pv ("limRel"); p.lookMs = pv ("look");
    p.os = dcomp::Oversampler::kFactors[std::clamp (pi ("os"), 0, dcomp::Oversampler::kNumF - 1)]; p.truePeak = pb ("truePeak"); p.osCut = pv ("osCut") * 1000.0; p.osSteep = pv ("osSteep") * 0.01; p.osPre = pv ("osPre") * 0.01; p.linkT = pv ("linkT") * 0.01; p.linkR = pv ("linkR") * 0.01;
    return p;
}

void DeltaCompProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate; blockSize = juce::jmax (samplesPerBlock, dcomp::Engine::maxInsertBlock());
    for (auto& r : dryRing) r.assign ((size_t) (6 * sampleRate) + (size_t) samplesPerBlock + 16, 0.0);
    for (auto& r : lookRing) r.assign ((size_t) (0.25 * sampleRate) + 16, 0.0);
    lookW = 0; meterLook = wantedMeterLook(); lookFadeState = 0; lookFade = 1.0;
    dryW = 0; bypassMix = pb ("active") ? 0.0 : 1.0; bypassK = std::exp (-1.0 / (0.02 * sampleRate));
    engine.prepare (sampleRate, juce::jmax (1, getTotalNumInputChannels()));
    engine.setParams (currentParams());
    engine.reset();
    engine.setInsert (&DeltaCompProcessor::insertCallback, this);
    {
        const juce::SpinLock::ScopedLockType l (insertLock);
        if (insert != nullptr) insert->prepareToPlay (sr, blockSize);
    }
    updateLatency();
}

void DeltaCompProcessor::updateLatency()
{
    const int il = insert != nullptr ? insert->getLatencySamples() : 0;
    insertLatency = il;
    engine.setInsertLatency (il);
    setLatencySamples (engine.latency() + il + meterLook);
}

void DeltaCompProcessor::insertCallback (void* ctx, double* a, double* b, int n)
{
    auto& self = *static_cast<DeltaCompProcessor*> (ctx);
    const juce::SpinLock::ScopedTryLockType l (self.insertLock);
    if (! l.isLocked() || self.insert == nullptr) return;
    auto& buf = self.insertBuf;
    const int nc = juce::jmax (2, juce::jmax (self.insert->getTotalNumInputChannels(), self.insert->getTotalNumOutputChannels()));
    if (buf.getNumChannels() < nc || buf.getNumSamples() < n) return;   // sized when loading: never allocate here
    buf.clear();
    for (int i = 0; i < n; ++i) { buf.setSample (0, i, (float) a[i]); buf.setSample (1, i, (float) b[i]); }
    juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), nc, n);
    self.insertMidi.clear();
    self.insert->processBlock (view, self.insertMidi);
    const bool stereoOut = self.insert->getTotalNumOutputChannels() >= 2;
    for (int i = 0; i < n; ++i) { a[i] = (double) view.getSample (0, i); b[i] = (double) view.getSample (stereoOut ? 1 : 0, i); }
}

bool DeltaCompProcessor::loadInsert (const juce::PluginDescription& d, juce::String& error, const juce::MemoryBlock* state)
{
    auto inst = formats.createPluginInstance (d, sr, blockSize, error);
    if (inst == nullptr) return false;
    // stereo in / out when the plugin allows it
    auto layout = inst->getBusesLayout();
    if (layout.inputBuses.size() > 0) layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (layout.outputBuses.size() > 0) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    inst->setBusesLayout (layout);
    if (state != nullptr && state->getSize() > 0) inst->setStateInformation (state->getData(), (int) state->getSize());
    inst->prepareToPlay (sr, blockSize);
    const int nc = juce::jmax (2, juce::jmax (inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels()));
    if (onInsertAboutToChange) onInsertAboutToChange();
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::SpinLock::ScopedLockType l (insertLock);
        insertBuf.setSize (nc, blockSize, false, true, false);
        old = std::move (insert); insert = std::move (inst); insertDesc = d;
    }
    old.reset();
    updateLatency();
    if (onInsertChanged) onInsertChanged();
    return true;
}

bool DeltaCompProcessor::loadInsertFile (const juce::File& f, juce::String& error)
{
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* fmt : formats.getFormats()) if (fmt->fileMightContainThisPluginType (f.getFullPathName())) fmt->findAllTypesForFile (types, f.getFullPathName());
    if (types.isEmpty()) { error = "Aucun plugin dans " + f.getFileName(); return false; }
    for (auto* t : types) if (t->isInstrument == false) return loadInsert (*t, error);
    return loadInsert (*types[0], error);
}

void DeltaCompProcessor::clearInsert()
{
    if (insert == nullptr) return;
    if (onInsertAboutToChange) onInsertAboutToChange();
    std::unique_ptr<juce::AudioPluginInstance> old;
    { const juce::SpinLock::ScopedLockType l (insertLock); old = std::move (insert); }
    old.reset();
    updateLatency();
    if (onInsertChanged) onInsertChanged();
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
    engine.setRealtime (! isNonRealtime());
    engine.setParams (currentParams());
    {   // the hosted plugin may change its latency (e.g. a convolution reverb's settings)
        const juce::SpinLock::ScopedTryLockType l (insertLock);
        if (l.isLocked() && insert != nullptr && insert->getLatencySamples() != insertLatency) updateLatency();
    }
    const int ch = juce::jmin (b.getNumChannels(), getTotalNumInputChannels());
    // dry copy for the general bypass, delayed by the full latency so switching is seamless
    // the meter lookahead moves the read point of the delay ring: fade out (10 ms), move it, fade back in, so the
    // jump in time is never heard as a crack (the ring always holds the recent output, so no refill is needed)
    if (wantedMeterLook() != meterLook && lookFadeState == 0) lookFadeState = 1;
    if (lookFadeState == 1 && lookFade <= 0.0)
    {
        meterLook = wantedMeterLook();
        updateLatency();
        lookFadeState = 2;
    }
    const int total = getLatencySamples() - meterLook, R = (int) dryRing[0].size();
    for (int c = 0; c < juce::jmin (ch, 2); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) dryRing[(size_t) c][(size_t) ((dryW + i) % R)] = (double) b.getSample (c, i);
    {   // input gain, after the dry copy (the bypass compares with the untouched input), 20 ms glide
        const double gt = juce::Decibels::decibelsToGain ((double) pv ("in"));
        if (std::abs (gt - inGain) > 1e-12 || gt != 1.0)
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                inGain = gt + bypassK * (inGain - gt);
                for (int c = 0; c < ch; ++c) b.setSample (c, i, (T) ((double) b.getSample (c, i) * inGain));
            }
    }
    engine.process (b.getArrayOfWritePointers(), ch, b.getNumSamples());
    const double want = pb ("active") ? 0.0 : 1.0;
    if (want != 0.0 || bypassMix > 1e-9)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            bypassMix = want + bypassK * (bypassMix - want);
            const int rd = ((dryW + i - juce::jmin (total, R - 1)) % R + R) % R;
            for (int c = 0; c < juce::jmin (ch, 2); ++c)
                b.setSample (c, i, (T) ((1.0 - bypassMix) * (double) b.getSample (c, i) + bypassMix * dryRing[(size_t) c][(size_t) rd]));
        }
    dryW = (dryW + b.getNumSamples()) % R;
    int w = scopeWrite.load (std::memory_order_relaxed);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double m = ch > 1 ? 0.5 * ((double) b.getSample (0, i) + (double) b.getSample (1, i)) : (double) b.getSample (0, i);
        const double sd = ch > 1 ? 0.5 * ((double) b.getSample (0, i) - (double) b.getSample (1, i)) : 0.0;
        scope[(size_t) (w & (kScopeSize - 1))] = (float) m; scopeS[(size_t) (w & (kScopeSize - 1))] = (float) sd; ++w;
    }
    scopeWrite.store (w, std::memory_order_release);
    {
        const int LR = (int) lookRing[0].size();
        const double step = 1.0 / (0.01 * sr);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const int wr = (lookW + i) % LR, rd = ((lookW + i - juce::jmin (meterLook, LR - 1)) % LR + LR) % LR;
            if (lookFadeState == 1) lookFade = juce::jmax (0.0, lookFade - step);
            else if (lookFadeState == 2) { lookFade = juce::jmin (1.0, lookFade + step); if (lookFade >= 1.0) lookFadeState = 0; }
            const double g = lookFade >= 1.0 ? 1.0 : 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * lookFade);
            for (int c = 0; c < juce::jmin (ch, 2); ++c)
            {
                lookRing[(size_t) c][(size_t) wr] = (double) b.getSample (c, i);
                if (meterLook > 0 || g < 1.0) b.setSample (c, i, (T) (lookRing[(size_t) c][(size_t) rd] * g));
            }
        }
        lookW = (lookW + b.getNumSamples()) % LR;
    }
}

void DeltaCompProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml())
    {
        if (insert != nullptr)
        {
            auto* ins = xml->createNewChildElement ("INSERT");
            ins->addChildElement (insertDesc.createXml().release());
            juce::MemoryBlock st; insert->getStateInformation (st);
            ins->setAttribute ("state", st.toBase64Encoding());
        }
        copyXmlToBinary (*xml, d);
    }
}

void DeltaCompProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    std::unique_ptr<juce::XmlElement> ins;
    if (auto* e = xml->getChildByName ("INSERT")) { ins = std::make_unique<juce::XmlElement> (*e); xml->removeChildElement (e, true); }
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    auto restore = [this, ins = std::shared_ptr<juce::XmlElement> (std::move (ins))]
    {
        if (ins == nullptr) { clearInsert(); return; }
        juce::PluginDescription d;
        if (auto* de = ins->getChildByName ("PLUGIN")) d.loadFromXml (*de);
        juce::MemoryBlock st; st.fromBase64Encoding (ins->getStringAttribute ("state"));
        juce::String err; loadInsert (d, err, &st);
    };
    // plugins are created on the message thread
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) restore();
    else juce::MessageManager::callAsync (restore);
}

juce::AudioProcessorEditor* DeltaCompProcessor::createEditor() { return new DeltaCompEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DeltaCompProcessor(); }
