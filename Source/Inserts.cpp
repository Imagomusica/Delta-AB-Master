#include "Inserts.h"

InsertRack::InsertRack()
{
    formats.addFormat (new juce::VST3PluginFormat());
    if (auto xml = juce::XmlDocument::parse (listFile())) known.recreateFromXml (*xml);
}

InsertRack::~InsertRack()
{
    for (auto& s : slots) { const juce::SpinLock::ScopedLockType sl (s.lock); s.plugin.reset(); }
}

juce::File InsertRack::listFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("ImagoMusica").getChildFile ("Delta AB").getChildFile ("plugins.xml");
}

void InsertRack::prepare (double sampleRate, int maxBlock)
{
    fs = sampleRate; maxN = juce::jmax (512, maxBlock);
    for (auto& s : slots)
    {
        const juce::SpinLock::ScopedLockType sl (s.lock);
        s.fbuf.setSize (2, maxN); s.dbuf.setSize (2, maxN);
        s.midi.ensureSize (16);
        if (s.plugin != nullptr) { s.plugin->releaseResources(); s.plugin->prepareToPlay (fs, maxN); }
    }
}

void InsertRack::release()
{
    for (auto& s : slots) { const juce::SpinLock::ScopedLockType sl (s.lock); if (s.plugin != nullptr) s.plugin->releaseResources(); }
}

void InsertRack::beginBlock (double* const* send, const double* const* ret, int n)
{
    hwSend = send; hwRet = ret; hwN = n;
}

void InsertRack::endBlock()
{
    for (int j = 0; j < kLoops; ++j)
    {
        auto& lp = loops[(size_t) j];
        const int st = lp.state.load();
        if (st == 0) continue;
        if (hwSend == nullptr || hwRet == nullptr || hwSend[j] == nullptr || hwRet[j] == nullptr) { lp.state = 0; continue; }
        int from = 0;
        if (st == 1)
        {
            hwSend[j][0] = 0.5;   // the click (-6 dBFS)
            lp.elapsed = 0; lp.state = 2;
            from = 1;
        }
        for (int i = from; i < hwN; ++i)
            if (std::abs (hwRet[j][i]) > 0.05)
            {
                lp.latency = lp.elapsed + i;
                lp.state = 0;
                break;
            }
        lp.elapsed += hwN;
        if (lp.state.load() == 2 && lp.elapsed > (int) (2.0 * fs)) lp.state = 0;   // nothing came back
    }
    for (int s = 0; s < kSlots; ++s)
        if (slots[(size_t) s].kind.load() == kHardware)
        {
            const int want = loops[(size_t) slots[(size_t) s].loop].latency.load();
            if (slots[(size_t) s].latency.load() != want) slots[(size_t) s].latency = want;
        }
    hwSend = nullptr; hwRet = nullptr;
}

void InsertRack::insert (int slot, double* l, double* r, int n, int offset)
{
    auto& s = slots[(size_t) slot];
    const int k = s.kind.load (std::memory_order_acquire);
    if (k == kHardware)
    {
        // mono: loop s.loop; stereo (EN-MS M/S stereo): left on loop 1, right on loop 2
        double* ch[2] = { l, r };
        for (int c = 0; c < (r != nullptr ? 2 : 1); ++c)
        {
            const int j = r != nullptr ? c : s.loop;
            const bool ok = hwSend != nullptr && hwRet != nullptr && hwSend[j] != nullptr && hwRet[j] != nullptr && offset + n <= hwN;
            if (! ok || loops[(size_t) j].state.load() != 0) { std::fill (ch[c], ch[c] + n, 0.0); continue; }   // muted while pinging
            std::copy (ch[c], ch[c] + n, hwSend[j] + offset);
            std::copy (hwRet[j] + offset, hwRet[j] + offset + n, ch[c]);
        }
        return;
    }
    if (k != kPlugin) return;
    const juce::SpinLock::ScopedTryLockType tl (s.lock);
    if (! tl.isLocked() || s.plugin == nullptr || n > maxN) return;   // being swapped: pass through
    const int pc = s.pluginChannels;
    s.midi.clear();
    auto run = [&] (auto& buf)
    {
        using T = std::remove_reference_t<decltype (*buf.getWritePointer (0))>;
        juce::AudioBuffer<T> b (buf.getArrayOfWritePointers(), juce::jmax (pc, 1), n);
        for (int i = 0; i < n; ++i)
        {
            const double L = l[i], R = r != nullptr ? r[i] : l[i];
            b.setSample (0, i, (T) L);
            if (pc > 1) b.setSample (1, i, (T) R);
        }
        s.plugin->processBlock (b, s.midi);
        for (int i = 0; i < n; ++i)
        {
            const double L = (double) b.getSample (0, i), R = pc > 1 ? (double) b.getSample (1, i) : L;
            if (r != nullptr) { l[i] = L; r[i] = R; }
            else l[i] = pc > 1 ? 0.5 * (L + R) : L;
        }
    };
    if (s.pluginDouble) run (s.dbuf); else run (s.fbuf);
}

int InsertRack::maxLatency() const
{
    int mx = 0;
    for (int a = 0; a < 3; ++a) mx = juce::jmax (mx, slotLatency (3 * a) + slotLatency (3 * a + 1) + slotLatency (3 * a + 2));
    for (int k = 0; k < dab::kMaxSpots; ++k) mx = juce::jmax (mx, slotLatency (9 + k));
    return juce::jmin (mx, dab::Engine::kMaxComp - 1);
}

void InsertRack::computeCompensation (std::array<int, dab::kPanCh>& comp) const
{
    const int mx = maxLatency();
    for (int m = 0; m < 3; ++m) comp[(size_t) m] = mx;
    for (int a = 0; a < 3; ++a) comp[(size_t) (3 + a)] = juce::jmax (0, mx - (slotLatency (3 * a) + slotLatency (3 * a + 1) + slotLatency (3 * a + 2)));
    for (int k = 0; k < dab::kMaxSpots; ++k)
    {
        const int c = juce::jmax (0, mx - slotLatency (9 + k));
        comp[(size_t) (6 + k)] = c;
        comp[(size_t) (6 + dab::kMaxSpots + k)] = c;
    }
}

void InsertRack::scan (std::function<void (const juce::String&)> progress)
{
    juce::VST3PluginFormat vst3;
    const auto dead = listFile().getSiblingFile ("scan-crash.txt");
    listFile().getParentDirectory().createDirectory();
    juce::PluginDirectoryScanner scanner (known, vst3, vst3.getDefaultLocationsToSearch(), true, dead, true);
    juce::String name;
    while (true)
    {
        if (progress) progress (scanner.getNextPluginFileThatWillBeScanned());
        if (! scanner.scanNextFile (true, name)) break;
    }
    // never offer this plugin inside itself
    for (auto& d : known.getTypes())
        if (d.name == "Delta AB") known.removeType (d);
    if (auto xml = known.createXml()) xml->writeTo (listFile());
}

void InsertRack::installPlugin (int slot, std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& stateB64)
{
    auto& s = slots[(size_t) slot];
    // mono in / out for AER, stereo for EN-MS spots (their stereo M/S mode); fall back to whatever the plugin takes
    const bool wantStereo = slot >= 9;
    auto tryLayout = [&] (const juce::AudioChannelSet& set) {
        juce::AudioProcessor::BusesLayout lay;
        lay.inputBuses.add (set); lay.outputBuses.add (set);
        return inst->checkBusesLayoutSupported (lay) && inst->setBusesLayout (lay);
    };
    if (! (wantStereo ? tryLayout (juce::AudioChannelSet::stereo()) || tryLayout (juce::AudioChannelSet::mono())
                      : tryLayout (juce::AudioChannelSet::mono()) || tryLayout (juce::AudioChannelSet::stereo())))
        inst->enableAllBuses();
    const int ch = juce::jlimit (1, 2, juce::jmax (inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels()));
    const bool dbl = inst->supportsDoublePrecisionProcessing();
    inst->setProcessingPrecision (dbl ? juce::AudioProcessor::doublePrecision : juce::AudioProcessor::singlePrecision);
    if (stateB64.isNotEmpty())
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (stateB64)) inst->setStateInformation (mb.getData(), (int) mb.getSize());
    }
    inst->prepareToPlay (fs, maxN);
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::SpinLock::ScopedLockType sl (s.lock);
        old = std::move (s.plugin);
        s.plugin = std::move (inst);
        s.pluginDouble = dbl;
        s.pluginChannels = ch;
        s.fbuf.setSize (2, maxN); s.dbuf.setSize (2, maxN);
        s.kind = kPlugin;
    }
    old.reset();
    updateLatency (slot);
}

void InsertRack::updateLatency (int slot)
{
    auto& s = slots[(size_t) slot];
    const int k = s.kind.load();
    s.latency = k == kPlugin && s.plugin != nullptr ? juce::jmax (0, s.plugin->getLatencySamples())
              : k == kHardware ? loops[(size_t) s.loop].latency.load() : 0;
    if (onChange) onChange();
}

void InsertRack::loadPlugin (int slot, const juce::PluginDescription& d, std::function<void (const juce::String&)> done)
{
    formats.createPluginInstanceAsync (d, fs, maxN,
        [this, slot, done] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err) {
            if (inst != nullptr) installPlugin (slot, std::move (inst));
            if (done) done (inst == nullptr ? err : juce::String());
        });
}

void InsertRack::setHardware (int slot, int loop)
{
    auto& s = slots[(size_t) slot];
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::SpinLock::ScopedLockType sl (s.lock);
        old = std::move (s.plugin);
        s.loop = juce::jlimit (0, kLoops - 1, loop);
        s.kind = kHardware;
    }
    old.reset();
    updateLatency (slot);
}

void InsertRack::clear (int slot)
{
    auto& s = slots[(size_t) slot];
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::SpinLock::ScopedLockType sl (s.lock);
        s.kind = kEmpty;
        old = std::move (s.plugin);
    }
    old.reset();
    updateLatency (slot);
}

void InsertRack::ping (int loop)
{
    loops[(size_t) loop].state = 1;
}

juce::String InsertRack::name (int slot) const
{
    const auto& s = slots[(size_t) slot];
    switch (s.kind.load())
    {
        case kPlugin:   return s.plugin != nullptr ? s.plugin->getName() : juce::String();
        case kHardware: return "DA/AD " + juce::String (s.loop + 1);
        default:        return {};
    }
}

juce::ValueTree InsertRack::saveState() const
{
    juce::ValueTree t ("INSERTS");
    for (int i = 0; i < kSlots; ++i)
    {
        const auto& s = slots[(size_t) i];
        const int k = s.kind.load();
        if (k == kEmpty) continue;
        juce::ValueTree c ("SLOT");
        c.setProperty ("index", i, nullptr);
        c.setProperty ("kind", k, nullptr);
        c.setProperty ("loop", s.loop, nullptr);
        if (k == kPlugin && s.plugin != nullptr)
        {
            if (auto xml = s.plugin->getPluginDescription().createXml()) c.setProperty ("desc", xml->toString(), nullptr);
            juce::MemoryBlock mb; s.plugin->getStateInformation (mb);
            c.setProperty ("state", mb.toBase64Encoding(), nullptr);
        }
        t.appendChild (c, nullptr);
    }
    for (int j = 0; j < kLoops; ++j) t.setProperty ("loop" + juce::String (j), loops[(size_t) j].latency.load(), nullptr);
    return t;
}

void InsertRack::restoreState (const juce::ValueTree& t)
{
    for (int i = 0; i < kSlots; ++i) clear (i);
    if (! t.isValid()) return;
    for (int j = 0; j < kLoops; ++j) loops[(size_t) j].latency = (int) t.getProperty ("loop" + juce::String (j), 0);
    for (const auto& c : t)
    {
        const int i = c.getProperty ("index", -1), k = c.getProperty ("kind", 0);
        if (i < 0 || i >= kSlots) continue;
        if (k == kHardware) setHardware (i, c.getProperty ("loop", 0));
        else if (k == kPlugin)
        {
            juce::PluginDescription d;
            if (auto xml = juce::parseXML (c.getProperty ("desc").toString()); xml != nullptr && d.loadFromXml (*xml))
            {
                const juce::String st = c.getProperty ("state").toString();
                formats.createPluginInstanceAsync (d, fs, maxN,
                    [this, i, st] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String&) {
                        if (inst != nullptr) installPlugin (i, std::move (inst), st);
                    });
            }
        }
    }
}
