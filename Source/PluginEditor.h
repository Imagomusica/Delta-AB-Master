#pragma once
#include "PluginProcessor.h"

// Black and white console in the style of an old modular synth front panel: big knurled knobs with
// numbered scales, engraved labels. One window, three sections: Delta AB | Delta AER | EN-MS.
// Each strip opens a detail panel with all its settings. The window scales as a whole.
namespace ui {

struct Look : juce::LookAndFeel_V4
{
    Look();
    juce::Font font (float h, bool bold = false) const;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (11.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (10.0f, true); }
    juce::Font getLabelFont (juce::Label&) override { return font (10.0f); }
    juce::Font getPopupMenuFont() override { return font (13.0f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool, bool) override;
};

using APVTS = juce::AudioProcessorValueTreeState;

// A labelled control bound to one parameter.
struct Knob : juce::Component
{
    // fader = vertical fader; delayMetres = value shown as "ms" and "m" under the knob
    Knob (APVTS&, const juce::String& id, const juce::String& caption, bool fader = false, bool delayMetres = false);
    void resized() override;
    juce::Slider slider;
    juce::Label caption, value;
    bool metres = false;
    std::unique_ptr<APVTS::SliderAttachment> att;
};

struct Choice : juce::Component
{
    Choice (APVTS&, const juce::String& id, const juce::String& caption);
    void resized() override;
    juce::ComboBox box;
    juce::Label caption;
    std::unique_ptr<APVTS::ComboBoxAttachment> att;
};

struct Toggle : juce::Component
{
    Toggle (APVTS&, const juce::String& id, const juce::String& text);
    void resized() override { button.setBounds (getLocalBounds()); }
    juce::TextButton button;
    std::unique_ptr<APVTS::ButtonAttachment> att;
};

// Slide switch (Telefunken W 659 style) for a bool or choice parameter.
struct Switch : juce::Component
{
    Switch (APVTS&, const juce::String& id, const juce::StringArray& names);
    Switch (APVTS&, const juce::String& id, const juce::String& a, const juce::String& b);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::StringArray labels;
    int index = 0;
    juce::Rectangle<float> slot() const;
    std::unique_ptr<juce::ParameterAttachment> att;
};

// Rotary selector, Nagra style: flat pointed bar knob sunk in a round cup, positions engraved around it.
struct Selector : juce::Component
{
    Selector (APVTS&, const juce::String& id, const juce::String& caption);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String caption;
    juce::StringArray names;
    int index = 0, dragStart = 0;
    std::unique_ptr<juce::ParameterAttachment> att;
private:
    struct Geo { juce::Point<float> c; float R; };
    void select (int i);
    float angleOf (int i) const;
    Geo geo() const;
};

// Gain-match meters: two peak bars (with the 300 ms RMS as a marker) and the live difference in dB.
struct MatchMeter : juce::Component
{
    MatchMeter (DeltaABProcessor& p, int a, int b, const juce::String& la, const juce::String& lb) : proc (p), ch { a, b }, lab { la, lb } {}
    void tick();
    void paint (juce::Graphics&) override;
    DeltaABProcessor& proc;
    int ch[2];
    juce::String lab[2];
    float peakDb[2] { -100.0f, -100.0f }, rmsDb[2] { -300.0f, -300.0f };
};

// Master section (right side): scope, Nagra-style peak and VU modulometers, dynamic range readings.
// Fed by the processor's master meter (sum of every flow). A click resets the holds.
struct MasterPanel : juce::Component
{
    explicit MasterPanel (DeltaABProcessor& p) : proc (p) { hist.assign (1800, -300.0f); }
    void tick();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    DeltaABProcessor& proc;
    double lastMs = 0;
    float ppm[2] { -100, -100 }, vuPow[2] {}, rmsPow[2] {}, stPow = 0;
    float peakHold[2] { -100, -100 }, tpHold[2] { -100, -100 };
    float corr = 0, scopeGain = 4.0f;
    std::vector<float> hist;   // short-term (400 ms) level, 60 s at 30 Hz, for low level and dynamic range
    int histPos = 0, histN = 0;
    float lowDb = -300, highDb = -300;
    static constexpr int kPts = 2048;
    std::array<float, kPts> px {}, py {};
    int ptsPos = 0;
};

// Placeholder for an insert slot (VST or hardware), not active yet.
struct InsertSlot : juce::Component
{
    explicit InsertSlot (const juce::String& t) : text (t) {}
    void paint (juce::Graphics&) override;
    juce::String text;
};

// One console strip: title, DETAIL button, a column of controls, then the fader at the bottom.
struct Strip : juce::Component
{
    Strip (const juce::String& title, std::function<void()> openDetail);
    Knob& knob (APVTS&, const juce::String& id, const juce::String& caption, bool delayMetres = false);
    Choice& choice (APVTS&, const juce::String& id, const juce::String& caption);
    Toggle& toggle (APVTS&, const juce::String& id, const juce::String& text);
    Switch& switch2 (APVTS&, const juce::String& id, const juce::String& a, const juce::String& b);
    void insert (const juce::String& text);
    void selector (APVTS&, const juce::String& id, const juce::String& caption, int h);
    Switch& switchN (APVTS&, const juce::String& id, const juce::StringArray& names);
    void knobRow (APVTS&, std::initializer_list<std::pair<juce::String, juce::String>>, int h, bool values);
    void selectorPair (APVTS&, const juce::String& idA, const juce::String& a, const juce::String& idB, const juce::String& b);
    void fader (APVTS&, const juce::String& id);
    void matchMeter (DeltaABProcessor&, int a, int b, const juce::String& la, const juce::String& lb);
    void tick() { for (auto* m : meters) m->tick(); }
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::String title;
    juce::TextButton detail { "DETAIL" };
    std::vector<MatchMeter*> meters;

private:
    struct Item { std::vector<juce::Component*> cs; int h; };
    std::vector<Item> items;
    juce::OwnedArray<juce::Component> owned;
    Knob* faderKnob = nullptr;
};

// Full settings of one channel, shown over the console. Controls are laid out in titled rows.
struct Detail : juce::Component
{
    Detail (const juce::String& title, std::function<void()> close);
    void row (const juce::String& name);
    Knob& knob (APVTS&, const juce::String& id, const juce::String& caption, bool delayMetres = false);
    Choice& choice (APVTS&, const juce::String& id, const juce::String& caption);
    Toggle& toggle (APVTS&, const juce::String& id, const juce::String& text);
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::String title;
    juce::TextButton closeButton { "FERMER" };
    std::function<void (juce::Graphics&, juce::Rectangle<int>)> drawExtra;   // e.g. EQ curve
    juce::Rectangle<int> extraArea;

private:
    struct Row { juce::String name; std::vector<juce::Component*> items; int y = 0; };
    std::vector<Row> rows;
    juce::OwnedArray<juce::Component> owned;
};

// The console itself, laid out at a fixed size and scaled by the editor.
struct Console : juce::Component, private juce::Timer
{
    explicit Console (DeltaABProcessor&);
    ~Console() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kW = 1560, kH = 960;

private:
    void timerCallback() override;
    void showDetail (std::unique_ptr<Detail>);
    void openMic (int mic);
    void openAer (int ch);
    void openSpot (int spot);
    void setSpotCount (int n);
    void layoutSpots();

    DeltaABProcessor& proc;

    // global
    Choice phase, quality, slope, focal, panLaw, panMax, bloomMode, centreSend;
    Toggle ambiance;
    Knob focus, bloom;

    std::unique_ptr<Strip> ab[3], aer[3], spot[dab::kMaxSpots];
    Switch aerPhase;
    Toggle aerLink;
    juce::Viewport spotView;
    juce::Component spotHolder;
    juce::TextButton add4 { "+4" }, add8 { "+8" }, remove1 { "-1" };

    MasterPanel masterPanel;
    std::unique_ptr<Detail> detail;
    int shownSpots = -1;
    int detailAer = -1;
    dab::AerEqParams shownEq;

    juce::Rectangle<int> rTop, rAb, rAer, rSpots, rMaster, rConsole, rSign;
};

} // namespace ui

class DeltaABEditor : public juce::AudioProcessorEditor
{
public:
    explicit DeltaABEditor (DeltaABProcessor&);
    ~DeltaABEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ui::Look look;
    ui::Console console;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeltaABEditor)
};
