#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <complex>
#include <random>

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
    juce::String title, marking;   // marking: the classical dynamic (pp, p, mp mf, f ff) in score style
    int cols;
    std::unique_ptr<Control> on;
    juce::OwnedArray<Control> controls;
    std::function<juce::String()> readout;
    juce::String shown;
    juce::Component* extra = nullptr; int extraCell = 0, extraSpan = 1, extraTop = 0;
    // one-line gain meter in the header: GR (0 at the right, reduction grows left) or bipolar expansion (0 centred)
    std::function<float()> hairpin; float hairShown = 0;   // crescendo / diminuendo: output louder or quieter than input (dB)
    std::function<float()> gain; bool bipolar = false, expand = false; float range = 20.0f, shownGain = 0, holdGain = 0, holdTime = 0;
    void tickGain (float dt);   // a free component placed on grid cells
    juce::Rectangle<int> cell (int i) const;
};

// Oversampling filter view: magnitude response (0 .. fs, image region shaded) and impulse (pre-ringing left of
// the centre line), recomputed when the engine designs a new filter.
struct OsView : juce::Component
{
    explicit OsView (DeltaCompProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void poll();
    DeltaCompProcessor& proc;
    std::vector<double> taps; int F = 1, P = 16, version = -1; double fs = 48000;
    std::vector<float> magDb; juce::String info;
};

// Insert slot: pick a VST3 / AU (e.g. Altiverb) from the installed ones, open its own editor, remove it.
struct InsertSlot : juce::Component
{
    explicit InsertSlot (DeltaCompProcessor&);
    ~InsertSlot() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();
    void closeWindow();
    void showMenu();
    void openEditor();
    DeltaCompProcessor& proc;
    juce::TextButton pick, edit;
    juce::String error;
    std::unique_ptr<juce::DocumentWindow> window;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Array<juce::File> found;
};

// Big moving sand: detector level as grains scrolling in time, the thresholds, gain traces underneath.
struct Sand : juce::Component
{
    explicit Sand (DeltaCompProcessor& p);
    void resized() override;
    void paint (juce::Graphics&) override;
    void push();
    float yOf (float lufs) const;
    DeltaCompProcessor& proc;
    static constexpr int kCols = 420;
    std::array<float, kCols> level {}, in {}, out {};
    // the six gain lanes: ambiance, upward, mezzo downward, double threshold, peak, limiter
    static constexpr int kLanes = 6;
    std::array<std::array<float, kCols>, kLanes> lane {};
    static constexpr int kBand = 162;      // height of the bottom band (phase scope + gain lanes)
    float corr = 0, corrShown = 0, scopeGain = 1;
    float lastDb = -100, avgDb = -60, kick = 0;
    void paintPhase (juce::Graphics&, juce::Rectangle<float>);
    void paintLanes (juce::Graphics&, juce::Rectangle<float>);
    int head = 0; unsigned frame = 0;
    // 3D plate (Dune-like Chladni sand): the output spectrum picks the plate modes, its level shakes the grains,
    // which jump off the plate, fall back and gather on the nodal lines; seen in perspective
    static constexpr int kGrains = 14000, kGW = 120, kGH = 80, kModes = 8, kFFT = 4096;
    std::vector<float> gx, gy, gz, gv;
    static constexpr int kTiers = 4, kTH = 30;
    static constexpr float kStep = 0.09f;      // height of each terrace step (plate widths)
    std::array<std::array<float, kGW * kTH>, kTiers> fields {};
    std::array<float, kTiers> act {}, actTarget {}, ceilDb {}, ceilTarget {};   // ceiling over each terrace: down with GR, up with expansion
    float ceilH (int t) const { return juce::jlimit (0.012f, 0.16f, 0.07f * (1.0f + ceilDb[(size_t) t] / 12.0f)); }
    int tierOf (float y) const { return juce::jlimit (0, kTiers - 1, (int) (y * kTiers)); }
    float tierBase (float y) const;
    std::array<float, kModes> energy {}, target {};
    float shakeLevel = 0, shakeTarget = 0, grNow = 0;
    std::vector<std::complex<double>> spec;
    juce::Image plate;
    std::mt19937 rng { 7 };
    void analyse();
    void stepPlate();
    void renderPlate();
    juce::Point<float> project (float x, float y, float z) const;
    juce::Rectangle<float> plateArea;
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
    float grSlow = 0, grFast = 0;
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
    ui::InsertSlot insertSlot;
    ui::OsView osView;
private:
    DeltaCompProcessor& proc;
    ui::Look look;
    double lastMs = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeltaCompEditor)
};
