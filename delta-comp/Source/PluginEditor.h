#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace ui
{
// Black and white, Moog panel style.
struct Look : juce::LookAndFeel_V4
{
    Look();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
};

juce::Font mono (float h, bool bold = false);

// One control of a panel: knob, toggle or choice.
struct Control : juce::Component
{
    enum Kind { knob, toggle, choice };
    Control (juce::AudioProcessorValueTreeState&, Kind, const char* id, const char* label);
    void resized() override;
    void paint (juce::Graphics&) override;
    Kind kind;
    juce::String label;
    juce::RangedAudioParameter* param;
    juce::Slider slider;
    juce::TextButton button;
    juce::ComboBox combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAtt;
};

// A module of the panel: title, ON switch, live gain readout, controls in a grid.
struct Section : juce::Component
{
    Section (juce::AudioProcessorValueTreeState&, const char* title, const char* onId, int cols,
             std::vector<std::tuple<Control::Kind, const char*, const char*>> items);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::String title;
    int cols;
    std::unique_ptr<Control> on;
    juce::OwnedArray<Control> controls;
    std::function<juce::String()> readout;
    juce::String shown;
};

// Big moving sand: detector level as grains scrolling in time, the thresholds, gain traces underneath.
struct Sand : juce::Component
{
    explicit Sand (DeltaCompProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void push();
    float yOf (float lufs) const;
    DeltaCompProcessor& proc;
    static constexpr int kCols = 420;
    std::array<float, kCols> level {}, in {}, out {}, down {}, up {}, lim {}, amb {};
    int head = 0; unsigned frame = 0;
    // dynamic range spread (10th..95th percentile of the 400 ms loudness over the last 30 s), set by the meter
    float inLo = -200, inHi = -200, outLo = -200, outHi = -200;
};

// Nagra style modulometer (VU needle + peak needle) and the numeric readouts.
struct Meter : juce::Component
{
    explicit Meter (DeltaCompProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick (float dt);
    DeltaCompProcessor& proc;
    Sand* sand = nullptr;
    float ppm[2] { -100, -100 }, vuPow[2] {}, rms3Pow = 0;
    double longSum = 0; long long longN = 0;
    float peakNow[2] { -100, -100 }, tpNow[2] { -100, -100 }, peakMax[2] { -100, -100 }, tpMax[2] { -100, -100 };
    float holdT = 0;
    // crest factor (1 s windows) and dynamic range history
    double cwInPk = 0, cwInSq = 0, cwOutPk = 0, cwOutSq = 0; long long cwN = 0; float cwT = 0;
    float crestIn = 0, crestOut = 0;
    static constexpr int kHist = 900;
    std::array<float, kHist> histIn {}, histOut {}; int histPos = 0, histN = 0;
    float mIn = 0, mOut = 0, drIn = 0, drOut = 0;
};
} // namespace ui

class DeltaCompEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DeltaCompEditor (DeltaCompProcessor&);
    ~DeltaCompEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    static constexpr int kW = 1440, kH = 1000;
    juce::Component content;
    ui::Sand sand;
    ui::Meter meter;
    juce::OwnedArray<ui::Section> row1, row2;
private:
    DeltaCompProcessor& proc;
    ui::Look look;
    double lastMs = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeltaCompEditor)
};
