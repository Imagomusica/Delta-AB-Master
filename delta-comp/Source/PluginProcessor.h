#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Compressor.h"

class DeltaCompProcessor : public juce::AudioProcessor
{
public:
    DeltaCompProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    bool supportsDoublePrecisionProcessing() const override { return true; }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override  { processAny (b); }
    void processBlock (juce::AudioBuffer<double>& b, juce::MidiBuffer&) override { processAny (b); }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Delta Comp"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    dcomp::Engine engine;
    // output (mid) for the sand view's spectrum, written by the audio thread
    static constexpr int kScopeSize = 8192;
    std::array<float, kScopeSize> scope {}, scopeS {};   // output mid and side, for the sand and the phase scope
    std::atomic<int> scopeWrite { 0 };
    dcomp::Params currentParams() const;

    // External insert (VST3, e.g. a reverb) on Mid / Side / L-R, before the limiter.
    // Loaded and released on the message thread; the audio thread only try-locks to use it.
    bool loadInsert (const juce::PluginDescription&, juce::String& error, const juce::MemoryBlock* state = nullptr);
    bool loadInsertFile (const juce::File& vst3, juce::String& error);
    void clearInsert();
    juce::AudioPluginInstance* getInsert() const { return insert.get(); }
    juce::String insertName() const { return insert != nullptr ? insert->getName() : juce::String(); }
    std::function<void()> onInsertChanged;        // editor refresh (message thread)
    std::function<void()> onInsertAboutToChange;  // editor closes the plugin's window before it is released

private:
    static void insertCallback (void* ctx, double* a, double* b, int n);
    void updateLatency();
    juce::AudioPluginFormatManager formats;
    std::unique_ptr<juce::AudioPluginInstance> insert;
    juce::PluginDescription insertDesc;
    juce::SpinLock insertLock;
    juce::AudioBuffer<float> insertBuf;
    juce::MidiBuffer insertMidi;
    double sr = 48000; int blockSize = 512; int insertLatency = 0;
    std::array<std::vector<double>, 2> dryRing; int dryW = 0; double bypassMix = 0, bypassK = 0.999;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    template <typename T> void processAny (juce::AudioBuffer<T>&);
    float pv (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    bool pb (const char* id) const { return pv (id) > 0.5f; }
    int pi (const char* id) const { return (int) std::lround (pv (id)); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeltaCompProcessor)
};
