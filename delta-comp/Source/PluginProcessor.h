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
    dcomp::Params currentParams() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    template <typename T> void processAny (juce::AudioBuffer<T>&);
    float pv (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    bool pb (const char* id) const { return pv (id) > 0.5f; }
    int pi (const char* id) const { return (int) std::lround (pv (id)); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeltaCompProcessor)
};
