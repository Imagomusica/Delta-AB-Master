#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Engine.h"

class DeltaABProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    DeltaABProcessor();
    ~DeltaABProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    bool supportsDoublePrecisionProcessing() const override { return true; }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Delta AB"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    dab::CrossoverConfig currentConfig() const;
    void readParams (dab::EngineParams&) const;
    void processDouble (const double* const* in, int numIn, double* const* out, int n);

    float pv (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    dab::Engine engine;
    double sr = 48000;

    // Filter sets are built on the message thread and handed to the audio thread lock-free.
    dab::CrossoverSet* active = nullptr;
    std::atomic<dab::CrossoverSet*> pending { nullptr };
    std::atomic<dab::CrossoverSet*> retired { nullptr };
    dab::CrossoverConfig builtCfg;

    std::array<bool, 3> wasAnalysing { false, false, false };
    static constexpr int kSilence = 12, kScratchCh = 14;
    juce::AudioBuffer<double> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeltaABProcessor)
};
