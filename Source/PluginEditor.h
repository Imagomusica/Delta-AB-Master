#pragma once
#include "PluginProcessor.h"

// Minimal black-and-white console, in the spirit of an 80s synth front panel.
// One window: Delta AB strips, Delta AER strips, EN-MS spot strips, then the buses.
// Every strip opens a detail panel with all its settings.
namespace ui {

struct Look : juce::LookAndFeel_V4
{
    Look();
    juce::Font font (float h) const;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (11.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (10.0f); }
    juce::Font getLabelFont (juce::Label&) override { return font (10.0f); }
    juce::Font getPopupMenuFont() override { return font (13.0f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool, bool) override;
};

using APVTS = juce::AudioProcessorValueTreeState;

// A labelled control bound to one parameter.
struct Knob : juce::Component
{
    // fader = vertical fader; twoLine = value shown under the knob on two lines ("ms" / "m")
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

// One console strip: title, DETAIL button, a column of controls, then the fader at the bottom.
struct Strip : juce::Component
{
    Strip (const juce::String& title, std::function<void()> openDetail);
    Knob& knob (APVTS&, const juce::String& id, const juce::String& caption, bool delayMetres = false);
    Choice& choice (APVTS&, const juce::String& id, const juce::String& caption);
    Toggle& toggle (APVTS&, const juce::String& id, const juce::String& text);
    void togglePair (APVTS&, const juce::String& idA, const juce::String& a, const juce::String& idB, const juce::String& b);
    void fader (APVTS&, const juce::String& id);
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::String title;
    juce::TextButton detail { "DETAIL" };

private:
    struct Item { juce::Component* c; juce::Component* c2; int h; };
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

} // namespace ui

class DeltaABEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DeltaABEditor (DeltaABProcessor&);
    ~DeltaABEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showDetail (std::unique_ptr<ui::Detail>);
    void openMic (int mic);
    void openAer (int ch);
    void openSpot (int spot);
    void openBus();
    void setSpotCount (int n);
    void layoutSpots();

    DeltaABProcessor& proc;
    ui::Look look;

    // global
    ui::Choice phase, quality, slope, focal, panLaw, panMax, bloomMode;
    ui::Toggle ambiance;
    ui::Knob focus, bloom;

    // groups
    std::unique_ptr<ui::Strip> ab[3], aer[3], spot[dab::kMaxSpots], bus[3];
    ui::Choice aerPhase;
    ui::Toggle aerLink;
    juce::Viewport spotView;
    juce::Component spotHolder;
    juce::TextButton addSpot { "+ TRANCHE" }, removeSpot { "- TRANCHE" };
    ui::Choice centreSend;
    ui::Knob aerOut, spotsOut, master;

    std::unique_ptr<ui::Detail> detail;
    int shownSpots = -1;
    int detailAer = -1;
    dab::AerEqParams shownEq;

    juce::Rectangle<int> rTop, rAb, rAer, rSpots, rBus, rConsole;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeltaABEditor)
};
