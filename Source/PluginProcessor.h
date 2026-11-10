#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Engine.h"
#include "dsp/Meter.h"
#include "Inserts.h"

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
    dab::AerEqParams aerEq (int a) const;   // current EQ settings of AER channel a (for the curve display)
    // Gain-match meters (after the input gains): mic m X = 2m, Y = 2m+1; spot k cardio = 6+2k, ribbon = 7+2k.
    float takeMeterPeak (int ch) { return engine.meterPeak[(size_t) ch].exchange (0.0f); }
    float meterRmsDb (int ch) const { return engine.meterRmsDb[(size_t) ch].load(); }
    // Master meters: the sum of every flow (what the Pyramix bus will add up), for scope, peak, VU, true peak.
    dab::MasterMeter master;
    InsertRack rack;   // insert slots: hosted VST3 or DA/AD loop
    double sampleRate() const { return sr; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static BusesProperties makeBuses();
    void timerCallback() override;
    dab::CrossoverConfig currentConfig() const;
    void readParams (dab::EngineParams&) const;
    dab::AerEqFilter* makeEq (int a) const;
    void processDouble (const double* const* in, int numMain, double* const* out,
                        double* const* aerOut, double* const* spotsOut, int n);
    template <typename T> void processAny (juce::AudioBuffer<T>&);
    struct Io { int numMain = 0, aerStart = 0, numAer = 0, aerOutStart = 2, mixStart = 0; bool mixOn = false;
                std::array<int, dab::kMaxSpots> spotStart {}, spotIn {};
                bool aerOutOn = false;
                std::array<int, 3 + dab::kMaxSpots> flowStart {};
                std::array<bool, 3 + dab::kMaxSpots> flowOn {};
                int hwRetStart = 0, hwSendStart = 0; bool hwRetOn = false, hwSendOn = false; };
    static constexpr int kNumFlowOut = 3 + dab::kMaxSpots;   // stereo outputs: AB mics, spots
    double* const* flowPtrs = nullptr;
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
    // scratch: main inputs, AER inputs, spot inputs, then 6 outputs (main, AER, spots)
    static constexpr int kInMain = 0, kInAer = 12, kInSpots = 15, kOut = kInSpots + 2 * dab::kMaxSpots,
                         kFlowOut = kOut + 6, kHwRet = kFlowOut + 2 * (3 + dab::kMaxSpots),
                         kHwSend = kHwRet + 2, kScratchCh = kHwSend + 2;
    juce::AudioBuffer<double> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeltaABProcessor)
};
