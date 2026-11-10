#pragma once
#include "PluginProcessor.h"

// Minimal black-and-white console, in the spirit of an 80s synth front panel.
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
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (11.0f); }
    juce::Font getLabelFont (juce::Label&) override { return font (11.0f); }
    juce::Font getPopupMenuFont() override { return font (13.0f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
};

using APVTS = juce::AudioProcessorValueTreeState;

// A labelled control bound to one parameter.
struct Knob : juce::Component
{
    Knob (APVTS&, const juce::String& id, const juce::String& caption, bool vertical = false);
    void resized() override;
    juce::Slider slider;
    juce::Label caption;
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

// One hybrid mic: the strip on the console, plus its detail panel.
struct Strip : juce::Component
{
    Strip (APVTS&, int mic, const juce::String& title, std::function<void (int)> openDetail);
    void paint (juce::Graphics&) override;
    void resized() override;

    int mic;
    juce::String title;
    juce::TextButton detail { "DETAIL" };
    Choice combo, position, xover;
    Knob focus, bloom, delay, pan;
};

struct Detail : juce::Component
{
    Detail (APVTS&, int mic, const juce::String& title, std::function<void()> close);
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::String title;
    juce::TextButton closeButton { "FERMER" };
    Choice combo, position, xover;
    Toggle swap, analyse;
    Knob match, focus, bloom, delay, pan, panLevel, panDelay, panPhase, panHF;
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
    void openDetail (int mic);

    DeltaABProcessor& proc;
    ui::Look look;

    // global
    ui::Choice phase, quality, slope, focal, panLaw, panMax, bloomMode;
    ui::Toggle ambiance;
    ui::Knob focus, bloom;

    // mics, shown left to right as G, C, D
    std::unique_ptr<ui::Strip> strips[3];
    std::unique_ptr<ui::Detail> detail;

    // buses
    ui::Knob faderL, faderC, faderR, lcL, lcC, lcR, out;
    ui::Toggle lcOnL, lcOnC, lcOnR;
    ui::Choice centreSend;

    juce::Rectangle<int> rGlobal, rStrips, rAer, rBus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeltaABEditor)
};
