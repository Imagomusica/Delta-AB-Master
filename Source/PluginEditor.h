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

// Two-position switch for a two-choice parameter: shows both positions, the active one lit.
struct Switch : juce::Component
{
    Switch (APVTS&, const juce::String& id, const juce::String& a, const juce::String& b);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String la, lb;
    int index = 0;
    std::unique_ptr<juce::ParameterAttachment> att;
};

// Rotary selector with a pointer knob and engraved positions around it (oscilloscope style).
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
    void select (int i);
    float angleOf (int i) const;
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
    void selectorPair (APVTS&, const juce::String& idA, const juce::String& a, const juce::String& idB, const juce::String& b);
    void fader (APVTS&, const juce::String& id);
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::String title;
    juce::TextButton detail { "DETAIL" };

private:
    struct Item { juce::Component* c; int h; juce::Component* c2 = nullptr; };
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

    static constexpr int kW = 1560, kH = 900;

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

    std::unique_ptr<Detail> detail;
    int shownSpots = -1;
    int detailAer = -1;
    dab::AerEqParams shownEq;

    juce::Rectangle<int> rTop, rAb, rAer, rSpots, rConsole, rSign;
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
