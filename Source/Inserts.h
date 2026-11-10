#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Engine.h"

// Insert slots of the console: a hosted VST3 plugin, or an analog loop through the interface
// (the plugin sends on its "Insert DA" output bus, Pyramix routes it to a DA; the AD comes back on
// the "Insert AD" input bus). A PING measures the round trip; every insert's latency is compensated.
//
// Threads: load / unload / scan / state on the message thread; insert() on the audio thread.
// A slot's plugin is swapped under a spin lock that the audio thread only try-locks (busy = bypass).
class InsertRack : public dab::InsertHook
{
public:
    static constexpr int kSlots = dab::kInsertSlots, kLoops = 2;
    enum Kind { kEmpty = 0, kPlugin = 1, kHardware = 2 };

    InsertRack();
    ~InsertRack() override;

    void prepare (double sampleRate, int maxBlock);
    void release();

    // audio thread
    bool active (int slot) const override { return slots[(size_t) slot].kind.load (std::memory_order_acquire) != kEmpty; }
    void insert (int slot, double* l, double* r, int n, int offset) override;
    // hardware loop buffers of this host block (nullptr when the bus is off); call before the engine, then endBlock()
    void beginBlock (double* const* send, const double* const* ret, int n);
    void endBlock();

    // latency in samples of each slot, and the largest per-channel sum (what the plugin adds to its latency)
    int slotLatency (int slot) const { return slots[(size_t) slot].latency.load(); }
    int maxLatency() const;
    void computeCompensation (std::array<int, dab::kPanCh>& comp) const;

    // message thread
    juce::KnownPluginList& knownPlugins() { return known; }
    void scan (std::function<void (const juce::String&)> progress = {});
    void loadPlugin (int slot, const juce::PluginDescription&, std::function<void (const juce::String& error)> done = {});
    void setHardware (int slot, int loop);
    void clear (int slot);
    void ping (int loop);
    bool pinging (int loop) const { return loops[(size_t) loop].state.load() != 0; }
    int loopLatency (int loop) const { return loops[(size_t) loop].latency.load(); }
    int kind (int slot) const { return slots[(size_t) slot].kind.load(); }
    int hardwareLoop (int slot) const { return slots[(size_t) slot].loop; }
    juce::String name (int slot) const;
    juce::AudioPluginInstance* plugin (int slot) const { return slots[(size_t) slot].plugin.get(); }
    std::function<void()> onChange;   // a slot changed (UI refresh, latency)

    juce::ValueTree saveState() const;
    void restoreState (const juce::ValueTree&);

private:
    struct Slot
    {
        std::atomic<int> kind { kEmpty }, latency { 0 };
        int loop = 0;
        juce::SpinLock lock;
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        bool pluginDouble = false;
        int pluginChannels = 1;
        juce::AudioBuffer<float> fbuf;
        juce::AudioBuffer<double> dbuf;
        juce::MidiBuffer midi;
    };
    struct Loop
    {
        std::atomic<int> state { 0 };     // 0 idle, 1 send the click, 2 wait for it
        std::atomic<int> latency { 0 };
        int elapsed = 0;
    };
    void installPlugin (int slot, std::unique_ptr<juce::AudioPluginInstance>, const juce::String& stateB64 = {});
    void updateLatency (int slot);
    juce::File listFile() const;

    std::array<Slot, kSlots> slots;
    std::array<Loop, kLoops> loops;
    double fs = 48000;
    int maxN = 512;
    double* const* hwSend = nullptr;
    const double* const* hwRet = nullptr;
    int hwN = 0;
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    JUCE_DECLARE_NON_COPYABLE (InsertRack)
};
