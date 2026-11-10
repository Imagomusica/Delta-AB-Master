#include "PluginEditor.h"

namespace ui {

static const juce::Colour kBlack { 0xff000000 }, kWhite { 0xffffffff }, kGrey { 0xff8a8a8a };
static juce::String u (const char* s) { return juce::String::fromUTF8 (s); }

// ------------------------------------------------------------------ look
Look::Look()
{
    setColour (juce::ResizableWindow::backgroundColourId, kBlack);
    setColour (juce::Label::textColourId, kWhite);
    setColour (juce::Slider::textBoxTextColourId, kWhite);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, kBlack);
    setColour (juce::ComboBox::textColourId, kWhite);
    setColour (juce::ComboBox::backgroundColourId, kBlack);
    setColour (juce::ComboBox::outlineColourId, kWhite);
    setColour (juce::ComboBox::arrowColourId, kWhite);
    setColour (juce::PopupMenu::backgroundColourId, kBlack);
    setColour (juce::PopupMenu::textColourId, kWhite);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kWhite);
    setColour (juce::PopupMenu::highlightedTextColourId, kBlack);
    setColour (juce::TextButton::textColourOffId, kWhite);
    setColour (juce::TextButton::textColourOnId, kBlack);
    setColour (juce::TextEditor::backgroundColourId, kBlack);
    setColour (juce::TextEditor::textColourId, kWhite);
    setColour (juce::TextEditor::highlightColourId, kGrey);
    setColour (juce::CaretComponent::caretColourId, kWhite);
}

juce::Font Look::font (float h) const
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain));
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&)
{
    const float d = (float) juce::jmin (w, h) - 6.0f;
    const auto c = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).getCentre();
    g.setColour (kWhite);
    g.drawEllipse (c.x - d / 2, c.y - d / 2, d, d, 1.0f);
    const float a = a0 + pos * (a1 - a0);
    const juce::Point<float> p (c.x + std::sin (a) * d / 2, c.y - std::cos (a) * d / 2);
    g.drawLine (c.x, c.y, p.x, p.y, 2.0f);
}

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&)
{
    const float cx = (float) x + (float) w / 2;
    g.setColour (kWhite);
    g.drawLine (cx, (float) y, cx, (float) (y + h), 1.0f);
    for (int i = 0; i <= 10; ++i)   // scale ticks
    {
        const float ty = (float) y + (float) h * (float) i / 10.0f;
        g.drawLine (cx - 10, ty, cx - 5, ty, 1.0f);
        g.drawLine (cx + 5, ty, cx + 10, ty, 1.0f);
    }
    const juce::Rectangle<float> cap (cx - 16, pos - 6, 32, 12);
    g.setColour (kBlack); g.fillRect (cap);
    g.setColour (kWhite); g.drawRect (cap, 1.0f);
    g.drawLine (cap.getX() + 3, pos, cap.getRight() - 3, pos, 1.0f);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (b.getToggleState() ? kWhite : kBlack);
    g.fillRect (r);
    g.setColour (over ? kGrey : kWhite);
    g.drawRect (r, 1.0f);
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? kBlack : kWhite);
    g.setFont (font (11.0f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3), juce::Justification::centred, 2);
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    g.setColour (kBlack); g.fillRect (0, 0, w, h);
    g.setColour (kWhite); g.drawRect (0, 0, w, h, 1);
    juce::Path tri; const float ax = (float) w - 12, ay = (float) h / 2;
    tri.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.fillPath (tri);
}

void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 1, box.getWidth() - 22, box.getHeight() - 2);
    label.setFont (font (12.0f));
}

// ------------------------------------------------------------------ controls
static void styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text.toUpperCase(), juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setInterceptsMouseClicks (false, false);
}

Knob::Knob (APVTS& s, const juce::String& id, const juce::String& text, bool vertical)
{
    slider.setSliderStyle (vertical ? juce::Slider::LinearVertical : juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setVelocityBasedMode (false);
    slider.setMouseDragSensitivity (300);
    styleCaption (caption, text);
    addAndMakeVisible (slider);
    addAndMakeVisible (caption);
    att = std::make_unique<APVTS::SliderAttachment> (s, id, slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    caption.setBounds (r.removeFromTop (14));
    slider.setBounds (r);
}

Choice::Choice (APVTS& s, const juce::String& id, const juce::String& text)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
        box.addItemList (p->choices, 1);
    styleCaption (caption, text);
    addAndMakeVisible (box);
    addAndMakeVisible (caption);
    att = std::make_unique<APVTS::ComboBoxAttachment> (s, id, box);
}

void Choice::resized()
{
    auto r = getLocalBounds();
    caption.setBounds (r.removeFromTop (14));
    box.setBounds (r.removeFromTop (22));
}

Toggle::Toggle (APVTS& s, const juce::String& id, const juce::String& text)
{
    button.setButtonText (text.toUpperCase());
    button.setClickingTogglesState (true);
    addAndMakeVisible (button);
    att = std::make_unique<APVTS::ButtonAttachment> (s, id, button);
}

// ------------------------------------------------------------------ strip
static juce::String mp (int m, const char* p) { return "m" + juce::String (m + 1) + "_" + p; }

Strip::Strip (APVTS& s, int m, const juce::String& t, std::function<void (int)> open)
    : mic (m), title (t),
      combo (s, mp (m, "combo"), "sources X + Y"),
      position (s, mp (m, "lows"), "position"),
      xover (s, mp (m, "xover"), "coupure"),
      focus (s, mp (m, "focus"), "aigu"),
      bloom (s, mp (m, "bloom"), "grave"),
      delay (s, mp (m, "delay"), u ("délai")),
      pan (s, mp (m, "pan"), "pan")
{
    for (juce::Component* c : { (juce::Component*) &combo, (juce::Component*) &position, (juce::Component*) &xover,
                                (juce::Component*) &focus, (juce::Component*) &bloom, (juce::Component*) &delay, (juce::Component*) &pan,
                                (juce::Component*) &detail })
        addAndMakeVisible (c);
    detail.onClick = [open, m] { open (m); };
}

void Strip::paint (juce::Graphics& g)
{
    g.setColour (kWhite);
    g.drawRect (getLocalBounds(), 1);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::bold)));
    g.drawText (title, 12, 6, getWidth() - 110, 22, juce::Justification::centredLeft);
}

void Strip::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto head = r.removeFromTop (24);
    detail.setBounds (head.removeFromRight (80));
    r.removeFromTop (8);
    combo.setBounds (r.removeFromTop (38));
    r.removeFromTop (6);
    auto row = r.removeFromTop (38);
    position.setBounds (row.removeFromLeft (row.getWidth() / 2 - 4));
    row.removeFromLeft (8);
    xover.setBounds (row);
    r.removeFromTop (10);
    const int kw = r.getWidth() / 4;
    focus.setBounds (r.removeFromLeft (kw));
    bloom.setBounds (r.removeFromLeft (kw));
    delay.setBounds (r.removeFromLeft (kw));
    pan.setBounds (r);
}

Detail::Detail (APVTS& s, int m, const juce::String& t, std::function<void()> close)
    : title (t),
      combo (s, mp (m, "combo"), "sources X + Y"),
      position (s, mp (m, "lows"), "position X / Y"),
      xover (s, mp (m, "xover"), "coupure"),
      swap (s, mp (m, "swap"), u ("inverser X/Y (6 canaux)")),
      analyse (s, mp (m, "analyse"), "analyser X/Y"),
      match (s, mp (m, "match"), "gain match Y"),
      focus (s, mp (m, "focus"), "aigu"),
      bloom (s, mp (m, "bloom"), "grave"),
      delay (s, mp (m, "delay"), u ("délai")),
      pan (s, mp (m, "pan"), "pan"),
      panLevel (s, mp (m, "pan_lvl"), "pan niveau"),
      panDelay (s, mp (m, "pan_dly"), u ("pan délai")),
      panPhase (s, mp (m, "pan_ph"), "pan phase"),
      panHF (s, mp (m, "pan_hf"), "pan HF")
{
    for (juce::Component* c : { (juce::Component*) &combo, (juce::Component*) &position, (juce::Component*) &xover,
                                (juce::Component*) &swap, (juce::Component*) &analyse, (juce::Component*) &match,
                                (juce::Component*) &focus, (juce::Component*) &bloom, (juce::Component*) &delay,
                                (juce::Component*) &pan, (juce::Component*) &panLevel, (juce::Component*) &panDelay,
                                (juce::Component*) &panPhase, (juce::Component*) &panHF, (juce::Component*) &closeButton })
        addAndMakeVisible (c);
    closeButton.onClick = [close] { close(); };
}

void Detail::paint (juce::Graphics& g)
{
    g.fillAll (kBlack);
    g.setColour (kWhite);
    g.drawRect (getLocalBounds(), 2);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 16.0f, juce::Font::bold)));
    g.drawText (title, 16, 10, getWidth() - 140, 24, juce::Justification::centredLeft);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
    g.drawText ("1 SOURCES   2 GAIN MATCH   3 CROSSOVER   4 SWAP   5 GAINS / DELAI   6 SOMME   7 PAN   8 SORTIE",
                16, 34, getWidth() - 32, 16, juce::Justification::centredLeft);
}

void Detail::resized()
{
    auto r = getLocalBounds().reduced (16);
    closeButton.setBounds (r.removeFromTop (26).removeFromRight (100));
    r.removeFromTop (26);
    auto row = r.removeFromTop (38);
    const int cw = (row.getWidth() - 16) / 3;
    combo.setBounds (row.removeFromLeft (cw)); row.removeFromLeft (8);
    position.setBounds (row.removeFromLeft (cw)); row.removeFromLeft (8);
    xover.setBounds (row);
    r.removeFromTop (12);
    row = r.removeFromTop (26);
    swap.setBounds (row.removeFromLeft (220)); row.removeFromLeft (12);
    analyse.setBounds (row.removeFromLeft (160));
    r.removeFromTop (12);
    const int rowH = (r.getHeight() - 10) / 2;
    auto row1 = r.removeFromTop (rowH);
    r.removeFromTop (10);
    auto row2 = r;
    const int kw = row1.getWidth() / 5;
    match.setBounds (row1.removeFromLeft (kw));
    focus.setBounds (row1.removeFromLeft (kw));
    bloom.setBounds (row1.removeFromLeft (kw));
    delay.setBounds (row1.removeFromLeft (kw));
    pan.setBounds (row1);
    panLevel.setBounds (row2.removeFromLeft (kw));
    panDelay.setBounds (row2.removeFromLeft (kw));
    panPhase.setBounds (row2.removeFromLeft (kw));
    panHF.setBounds (row2.removeFromLeft (kw));
}

} // namespace ui

// ------------------------------------------------------------------ editor
using namespace ui;

static const int stripMic[3] = { 0, 2, 1 };   // left to right: G, C, D
static const char* stripTitle[3] = { "MICRO 1 · G", "MICRO 3 · C", "MICRO 2 · D" };

DeltaABEditor::DeltaABEditor (DeltaABProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      phase (p.apvts, "phase", "phase"),
      quality (p.apvts, "quality", u ("qualité")),
      slope (p.apvts, "slope", "pente"),
      focal (p.apvts, "focal", "focal"),
      panLaw (p.apvts, "pan_law", "loi de pan"),
      panMax (p.apvts, "pan_mult", u ("pan délai max")),
      bloomMode (p.apvts, "bloom_mode", "bloom mode"),
      ambiance (p.apvts, "ambient", "ambiance"),
      focus (p.apvts, "m_focus", "focus"),
      bloom (p.apvts, "m_bloom", "bloom"),
      faderL (p.apvts, "fader_L", "G", true),
      faderC (p.apvts, "fader_C", "C", true),
      faderR (p.apvts, "fader_R", "D", true),
      lcL (p.apvts, "lc_hz_L", "low cut"),
      lcC (p.apvts, "lc_hz_C", "low cut"),
      lcR (p.apvts, "lc_hz_R", "low cut"),
      out (p.apvts, "out", "sortie", true),
      lcOnL (p.apvts, "lc_on_L", "LC"),
      lcOnC (p.apvts, "lc_on_C", "LC"),
      lcOnR (p.apvts, "lc_on_R", "LC"),
      centreSend (p.apvts, "c_send", "centre > G/D")
{
    setLookAndFeel (&look);
    for (juce::Component* c : { (juce::Component*) &phase, (juce::Component*) &quality, (juce::Component*) &slope,
                                (juce::Component*) &focal, (juce::Component*) &panLaw, (juce::Component*) &panMax,
                                (juce::Component*) &bloomMode, (juce::Component*) &ambiance, (juce::Component*) &focus,
                                (juce::Component*) &bloom, (juce::Component*) &faderL, (juce::Component*) &faderC,
                                (juce::Component*) &faderR, (juce::Component*) &lcL, (juce::Component*) &lcC,
                                (juce::Component*) &lcR, (juce::Component*) &out, (juce::Component*) &lcOnL,
                                (juce::Component*) &lcOnC, (juce::Component*) &lcOnR, (juce::Component*) &centreSend })
        addAndMakeVisible (c);
    for (int i = 0; i < 3; ++i)
    {
        strips[i] = std::make_unique<Strip> (p.apvts, stripMic[i], stripTitle[i], [this] (int m) { openDetail (m); });
        addAndMakeVisible (*strips[i]);
    }
    setSize (1120, 780);
}

DeltaABEditor::~DeltaABEditor()
{
    detail.reset();
    setLookAndFeel (nullptr);
}

void DeltaABEditor::openDetail (int mic)
{
    const char* names[3] = { "MICRO 1 · G", "MICRO 2 · D", "MICRO 3 · C" };
    detail = std::make_unique<Detail> (proc.apvts, mic, juce::String (names[mic]) + u (" · DÉTAIL"),
                                       [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<DeltaABEditor> (this)] { if (sp) sp->detail.reset(); }); });
    addAndMakeVisible (*detail);
    detail->setBounds (rStrips.getUnion (rAer).reduced (4));
}

void DeltaABEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBlack);
    g.setColour (kWhite);
    auto mono = [] (float h, int style) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, style)); };
    g.setFont (mono (24.0f, juce::Font::bold));
    g.drawText ("DELTA AB", 16, 10, 200, 30, juce::Justification::centredLeft);
    g.setFont (mono (11.0f, juce::Font::plain));
    g.drawText ("3 MICROS HYBRIDES  O1 O2 R1 R2", 16, 40, 260, 16, juce::Justification::centredLeft);

    g.drawLine ((float) rGlobal.getX(), (float) rGlobal.getBottom(), (float) rGlobal.getRight(), (float) rGlobal.getBottom(), 1.0f);

    // Delta AER placeholder
    g.drawRect (rAer, 1);
    g.setFont (mono (15.0f, juce::Font::bold));
    g.drawText ("DELTA AER", rAer.getX() + 12, rAer.getY() + 6, 200, 22, juce::Justification::centredLeft);
    g.setFont (mono (11.0f, juce::Font::plain));
    g.setColour (kGrey);
    g.drawText ("AMBIANCE 3 CANAUX - A VENIR", rAer.reduced (12), juce::Justification::centred);

    g.setColour (kWhite);
    g.drawRect (rBus, 1);
    g.setFont (mono (15.0f, juce::Font::bold));
    g.drawText ("BUS", rBus.getX() + 12, rBus.getY() + 6, 200, 22, juce::Justification::centredLeft);
}

void DeltaABEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    rGlobal = r.removeFromTop (70);
    {
        auto g = rGlobal.withTrimmedLeft (280).withTrimmedTop (6);
        const int cw = 104;
        for (auto* c : { &phase, &quality, &slope, &focal, &panLaw, &panMax, &bloomMode })
        {
            c->setBounds (g.removeFromLeft (cw).withHeight (38));
            g.removeFromLeft (6);
        }
        ambiance.setBounds (rGlobal.getX() + 280, rGlobal.getY() + 50, 104, 18);
        focus.setBounds (g.removeFromLeft (64).withHeight (64));
        bloom.setBounds (g.removeFromLeft (64).withHeight (64));
    }
    r.removeFromTop (10);
    rStrips = r.removeFromTop (290);
    {
        auto s = rStrips;
        const int w = (s.getWidth() - 20) / 3;
        for (int i = 0; i < 3; ++i)
        {
            strips[i]->setBounds (s.removeFromLeft (w));
            s.removeFromLeft (10);
        }
    }
    r.removeFromTop (10);
    rAer = r.removeFromTop (110);
    r.removeFromTop (10);
    rBus = r;
    {
        auto b = rBus.reduced (12).withTrimmedTop (26);
        auto col = [&] (Knob& fader, Toggle& on, Knob& lc)
        {
            auto c = b.removeFromLeft (150);
            fader.setBounds (c.removeFromLeft (70));
            c.removeFromLeft (6);
            on.setBounds (c.removeFromTop (22));
            c.removeFromTop (6);
            lc.setBounds (c.removeFromTop (74));
            b.removeFromLeft (20);
        };
        col (faderL, lcOnL, lcL);
        col (faderC, lcOnC, lcC);
        col (faderR, lcOnR, lcR);
        centreSend.setBounds (b.removeFromLeft (140).withHeight (38));
        b.removeFromLeft (20);
        out.setBounds (b.removeFromLeft (70));
    }
    if (detail) detail->setBounds (rStrips.getUnion (rAer).reduced (4));
}
