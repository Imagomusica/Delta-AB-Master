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
    dab::AerEqParams aerEq (int a) const;
    dab::AerEqFilter* makeEq (int a) const;
    void processDouble (const double* const* in, int numIn, const double* const* aerIn, int numAerIn,
                        double* const* out, double* const* aerOut, int n);
    struct Io { int numIn = 0, aerInStart = 0, numAerIn = 0, aerOutStart = 2; bool aerOutOn = false; };
    Io io (int bufferChannels) const;

    float pv (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    dab::Engine engine;
    double sr = 48000;

    // Filter sets are built on the message thread and handed to the audio thread lock-free.
    dab::CrossoverSet* active = nullptr;
    std::atomic<dab::CrossoverSet*> pending { nullptr };
    std::atomic<dab::CrossoverSet*> retired { nullptr };
    dab::CrossoverConfig builtCfg;

    // Linear-phase AER EQ filters, one handoff per channel.
    std::array<dab::AerEqFilter*, 3> eqActive { nullptr, nullptr, nullptr };
    std::array<std::atomic<dab::AerEqFilter*>, 3> eqPending {}, eqRetired {};
    std::array<const dab::AerEqFilter*, 3> eqBuilt { nullptr, nullptr, nullptr };   // last built (message thread)

    std::array<float, 3> lastAerFader { 0, 0, 0 };
    bool linkPrimed = false;

    std::array<bool, 3> wasAnalysing { false, false, false };
    // scratch: 0..11 main inputs, 12..14 AER inputs, 15 silence, 16..19 outputs (float path)
    static constexpr int kSilence = 15, kOut = 16, kScratchCh = 20;
    juce::AudioBuffer<double> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeltaABProcessor)
};
