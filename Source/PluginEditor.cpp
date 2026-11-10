#include "PluginEditor.h"

namespace ui {

static const juce::Colour kBlack { 0xff000000 }, kWhite { 0xffffffff }, kGrey { 0xff8a8a8a };
static juce::String u (const char* s) { return juce::String::fromUTF8 (s); }
// toUpperCase leaves accented letters alone in the C locale
static juce::String upper (const juce::String& t)
{
    return t.toUpperCase().replace (u ("é"), u ("É")).replace (u ("è"), u ("È")).replace (u ("à"), u ("À"))
            .replace (u ("ô"), u ("Ô")).replace (u ("ê"), u ("Ê"));
}

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

void Look::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool, bool)
{
    g.setColour (kBlack); g.fillRect (x, y, w, h);
    g.setColour (kWhite);
    if (vertical) g.drawRect (x + 2, thumbStart, w - 4, thumbSize, 1);
    else g.fillRect (thumbStart, y + h / 2 - 2, thumbSize, 4);
}

// ------------------------------------------------------------------ controls
static void styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (upper (text), juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setInterceptsMouseClicks (false, false);
    l.setMinimumHorizontalScale (0.6f);
}

Knob::Knob (APVTS& s, const juce::String& id, const juce::String& text, bool fader, bool delayMetres)
    : metres (delayMetres)
{
    slider.setSliderStyle (fader ? juce::Slider::LinearVertical : juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (metres ? juce::Slider::NoTextBox : juce::Slider::TextBoxBelow, false, 64, 15);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setVelocityBasedMode (false);
    slider.setMouseDragSensitivity (300);
    styleCaption (caption, text);
    addAndMakeVisible (slider);
    addAndMakeVisible (caption);
    att = std::make_unique<APVTS::SliderAttachment> (s, id, slider);
    if (metres)
    {
        value.setJustificationType (juce::Justification::centredTop);
        value.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (value);
        slider.onValueChange = [this] {
            const double v = slider.getValue();
            value.setText (juce::String (v, 2) + " ms\n" + juce::String (v * 0.343, 2) + " m", juce::dontSendNotification);
        };
        slider.onValueChange();
    }
}

void Knob::resized()
{
    auto r = getLocalBounds();
    caption.setBounds (r.removeFromTop (13));
    if (metres) value.setBounds (r.removeFromBottom (26));
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
    if (! caption.isVisible()) { box.setBounds (r); return; }
    caption.setBounds (r.removeFromTop (13));
    box.setBounds (r.removeFromTop (19));
}

Toggle::Toggle (APVTS& s, const juce::String& id, const juce::String& text)
{
    button.setButtonText (upper (text));
    button.setClickingTogglesState (true);
    addAndMakeVisible (button);
    att = std::make_unique<APVTS::ButtonAttachment> (s, id, button);
}

// ------------------------------------------------------------------ strip
Strip::Strip (const juce::String& t, std::function<void()> open) : title (t)
{
    if (open)
    {
        addAndMakeVisible (detail);
        detail.onClick = [open] { open(); };
    }
}

Knob& Strip::knob (APVTS& s, const juce::String& id, const juce::String& caption, bool delayMetres)
{
    auto* k = new Knob (s, id, caption, false, delayMetres); owned.add (k);
    addAndMakeVisible (k);
    items.push_back ({ k, nullptr, delayMetres ? 78 : 64 });
    return *k;
}

Choice& Strip::choice (APVTS& s, const juce::String& id, const juce::String& caption)
{
    auto* c = new Choice (s, id, caption); owned.add (c);
    addAndMakeVisible (c);
    items.push_back ({ c, nullptr, 34 });
    return *c;
}

Toggle& Strip::toggle (APVTS& s, const juce::String& id, const juce::String& text)
{
    auto* t = new Toggle (s, id, text); owned.add (t);
    addAndMakeVisible (t);
    items.push_back ({ t, nullptr, 20 });
    return *t;
}

void Strip::togglePair (APVTS& s, const juce::String& idA, const juce::String& a, const juce::String& idB, const juce::String& b)
{
    auto* t1 = new Toggle (s, idA, a); owned.add (t1);
    auto* t2 = new Toggle (s, idB, b); owned.add (t2);
    addAndMakeVisible (t1); addAndMakeVisible (t2);
    items.push_back ({ t1, t2, 20 });
}

void Strip::fader (APVTS& s, const juce::String& id)
{
    faderKnob = new Knob (s, id, "", true); owned.add (faderKnob);
    addAndMakeVisible (faderKnob);
}

void Strip::paint (juce::Graphics& g)
{
    g.setColour (kWhite);
    g.drawRect (getLocalBounds(), 1);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold)));
    g.drawFittedText (title, 3, 4, getWidth() - 6, 16, juce::Justification::centred, 1, 0.6f);
}

void Strip::resized()
{
    auto r = getLocalBounds().reduced (3);
    r.removeFromTop (20);
    if (detail.isVisible()) { detail.setBounds (r.removeFromTop (18)); }
    r.removeFromTop (4);
    for (auto& it : items)
    {
        auto b = r.removeFromTop (it.h);
        if (it.c2 != nullptr)
        {
            it.c->setBounds (b.removeFromLeft (b.getWidth() / 2 - 1));
            b.removeFromLeft (2);
            it.c2->setBounds (b);
        }
        else it.c->setBounds (b);
        r.removeFromTop (4);
    }
    if (faderKnob != nullptr) faderKnob->setBounds (r.withTrimmedTop (2));
}

// ------------------------------------------------------------------ detail
Detail::Detail (const juce::String& t, std::function<void()> close) : title (t)
{
    addAndMakeVisible (closeButton);
    closeButton.onClick = [close] { close(); };
}

void Detail::row (const juce::String& name) { rows.push_back ({ upper (name), {}, 0 }); }

Knob& Detail::knob (APVTS& s, const juce::String& id, const juce::String& caption, bool delayMetres)
{
    auto* k = new Knob (s, id, caption, false, delayMetres); owned.add (k);
    addAndMakeVisible (k);
    rows.back().items.push_back (k);
    return *k;
}

Choice& Detail::choice (APVTS& s, const juce::String& id, const juce::String& caption)
{
    auto* c = new Choice (s, id, caption); owned.add (c);
    addAndMakeVisible (c);
    rows.back().items.push_back (c);
    return *c;
}

Toggle& Detail::toggle (APVTS& s, const juce::String& id, const juce::String& text)
{
    auto* t = new Toggle (s, id, text); owned.add (t);
    addAndMakeVisible (t);
    rows.back().items.push_back (t);
    return *t;
}

void Detail::paint (juce::Graphics& g)
{
    g.fillAll (kBlack);
    g.setColour (kWhite);
    g.drawRect (getLocalBounds(), 2);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 16.0f, juce::Font::bold)));
    g.drawText (title, 16, 10, getWidth() - 140, 24, juce::Justification::centredLeft);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold)));
    for (auto& r : rows)
    {
        g.drawText (r.name, 16, r.y, 120, 20, juce::Justification::centredLeft);
        g.drawLine (16.0f, (float) r.y + 20, 130.0f, (float) r.y + 20, 1.0f);
    }
    if (drawExtra)
    {
        g.drawRect (extraArea, 1);
        drawExtra (g, extraArea.reduced (1));
    }
}

void Detail::resized()
{
    auto r = getLocalBounds().reduced (16);
    closeButton.setBounds (r.removeFromTop (26).removeFromRight (100));
    r.removeFromTop (12);
    if (drawExtra) { extraArea = r.removeFromRight (r.getWidth() * 2 / 5).withTrimmedLeft (16).withHeight (juce::jmin (r.getHeight(), 260)); }
    const int rowH = rows.empty() ? 0 : juce::jmin (84, r.getHeight() / (int) rows.size());
    for (auto& row : rows)
    {
        auto b = r.removeFromTop (rowH);
        row.y = b.getY();
        b.removeFromLeft (130);
        for (auto* c : row.items)
        {
            const bool isChoice = dynamic_cast<Choice*> (c) != nullptr, isToggle = dynamic_cast<Toggle*> (c) != nullptr;
            const int w = isChoice ? 160 : isToggle ? 130 : 84;
            auto cell = b.removeFromLeft (w);
            b.removeFromLeft (8);
            if (isToggle) c->setBounds (cell.withTrimmedTop (14).withHeight (22));
            else if (isChoice) c->setBounds (cell.withHeight (34));
            else c->setBounds (cell.withHeight (juce::jmin (rowH - 4, 80)));
        }
    }
}

} // namespace ui

// ------------------------------------------------------------------ editor
using namespace ui;

static juce::String mid (int m, const char* p) { return "m" + juce::String (m + 1) + "_" + p; }
static juce::String aid (int a, const char* p) { return "a" + juce::String (a + 1) + "_" + p; }
static juce::String sid (int k, const char* p) { return "s" + juce::String (k + 1) + "_" + p; }

static const int abMic[3] = { 0, 2, 1 };   // left to right: G, C, D
static const char* abTitle[3] = { "MIC 1 G", "MIC 3 C", "MIC 2 D" };
static const char* aerTitle[3] = { "AER G", "AER C", "AER D" };
static const char* busIdS[3] = { "L", "C", "R" };
static const char* busTitle[3] = { "BUS G", "BUS C", "BUS D" };
static constexpr int kStripW = 68, kGap = 4, kHead = 44, kPad = 6;

DeltaABEditor::DeltaABEditor (DeltaABProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      phase (p.apvts, "phase", "phase xover"),
      quality (p.apvts, "quality", u ("qualité")),
      slope (p.apvts, "slope", "pente"),
      focal (p.apvts, "focal", "focal"),
      panLaw (p.apvts, "pan_law", "loi de pan"),
      panMax (p.apvts, "pan_mult", u ("pan délai max")),
      bloomMode (p.apvts, "bloom_mode", "bloom mode"),
      ambiance (p.apvts, "ambient", "ambiance"),
      focus (p.apvts, "m_focus", "focus"),
      bloom (p.apvts, "m_bloom", "bloom"),
      aerPhase (p.apvts, "aer_eq_phase", "phase EQ"),
      aerLink (p.apvts, "aer_link", u ("faders liés")),
      centreSend (p.apvts, "c_send", "centre > G/D"),
      aerOut (p.apvts, "aer_out", "AER", true),
      spotsOut (p.apvts, "spots_out", "SPOTS", true),
      master (p.apvts, "out", "MASTER", true)
{
    setLookAndFeel (&look);
    auto& s = p.apvts;
    aerPhase.caption.setVisible (false);
    if (aerPhase.box.getNumItems() == 2) { aerPhase.box.changeItemText (1, u ("EQ phase min.")); aerPhase.box.changeItemText (2, u ("EQ phase lin."));
                                         aerPhase.box.setText (aerPhase.box.getItemText (aerPhase.box.getSelectedItemIndex()), juce::dontSendNotification); }
    for (juce::Component* c : { (juce::Component*) &phase, (juce::Component*) &quality, (juce::Component*) &slope,
                                (juce::Component*) &focal, (juce::Component*) &panLaw, (juce::Component*) &panMax,
                                (juce::Component*) &bloomMode, (juce::Component*) &ambiance, (juce::Component*) &focus,
                                (juce::Component*) &bloom, (juce::Component*) &aerPhase, (juce::Component*) &aerLink,
                                (juce::Component*) &centreSend, (juce::Component*) &aerOut, (juce::Component*) &spotsOut,
                                (juce::Component*) &master, (juce::Component*) &spotView, (juce::Component*) &addSpot,
                                (juce::Component*) &removeSpot })
        addAndMakeVisible (c);

    for (int i = 0; i < 3; ++i)
    {
        const int m = abMic[i];
        auto& st = *(ab[i] = std::make_unique<Strip> (abTitle[i], [this, m] { openMic (m); }));
        st.choice (s, mid (m, "combo"), "X + Y");
        st.choice (s, mid (m, "lows"), "grave");
        st.choice (s, mid (m, "xover"), "coupure");
        st.knob (s, mid (m, "focus"), "aigu");
        st.knob (s, mid (m, "bloom"), "grave");
        st.knob (s, mid (m, "delay"), u ("délai"));
        st.knob (s, mid (m, "pan"), "pan");
        st.fader (s, mid (m, "level"));
        addAndMakeVisible (st);
    }
    for (int a = 0; a < 3; ++a)
    {
        auto& st = *(aer[a] = std::make_unique<Strip> (aerTitle[a], [this, a] { openAer (a); }));
        st.togglePair (s, aid (a, "lc_on"), "LC", aid (a, "hc_on"), "HC");
        st.knob (s, aid (a, "lc_hz"), "low cut");
        st.knob (s, aid (a, "delay"), u ("délai"), true);
        st.knob (s, aid (a, "pan"), "pan");
        st.fader (s, aid (a, "fader"));
        addAndMakeVisible (st);
    }
    for (int k = 0; k < dab::kMaxSpots; ++k)
    {
        auto& st = *(spot[k] = std::make_unique<Strip> ("SPOT " + juce::String (k + 1), [this, k] { openSpot (k); }));
        st.choice (s, sid (k, "mode"), "mode");
        st.knob (s, sid (k, "cardio"), "cardio M");
        st.knob (s, sid (k, "ribbon"), "ruban S");
        st.knob (s, sid (k, "delay"), u ("délai"), true);
        st.knob (s, sid (k, "pan"), "pan");
        st.fader (s, sid (k, "fader"));
        spotHolder.addChildComponent (st);
    }
    spotView.setViewedComponent (&spotHolder, false);
    spotView.setScrollBarsShown (false, true);
    spotView.setScrollBarThickness (10);
    for (int b = 0; b < 3; ++b)
    {
        const juce::String id (busIdS[b]);
        auto& st = *(bus[b] = std::make_unique<Strip> (busTitle[b], nullptr));
        st.toggle (s, "lc_on_" + id, "low cut");
        st.knob (s, "lc_hz_" + id, "LC Hz");
        st.fader (s, "fader_" + id);
        addAndMakeVisible (st);
    }
    addSpot.onClick = [this] { setSpotCount ((int) proc.apvts.getRawParameterValue ("spots_n")->load() + 1); };
    removeSpot.onClick = [this] { setSpotCount ((int) proc.apvts.getRawParameterValue ("spots_n")->load() - 1); };

    setSize (1460, 860);
    startTimerHz (15);
}

DeltaABEditor::~DeltaABEditor()
{
    stopTimer();
    detail.reset();
    setLookAndFeel (nullptr);
}

void DeltaABEditor::setSpotCount (int n)
{
    if (auto* prm = proc.apvts.getParameter ("spots_n"))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) juce::jlimit (1, dab::kMaxSpots, n)));
        prm->endChangeGesture();
    }
}

void DeltaABEditor::layoutSpots()
{
    const int n = juce::jlimit (1, dab::kMaxSpots, (int) proc.apvts.getRawParameterValue ("spots_n")->load());
    const int h = spotView.getHeight() - spotView.getScrollBarThickness() - 2;
    for (int k = 0; k < dab::kMaxSpots; ++k)
    {
        spot[k]->setVisible (k < n);
        spot[k]->setBounds (k * (kStripW + kGap), 0, kStripW, h);
    }
    spotHolder.setSize (n * (kStripW + kGap) - kGap, h);
    addSpot.setEnabled (n < dab::kMaxSpots);
    removeSpot.setEnabled (n > 1);
    shownSpots = n;
}

void DeltaABEditor::timerCallback()
{
    if ((int) proc.apvts.getRawParameterValue ("spots_n")->load() != shownSpots) { layoutSpots(); repaint(); }
    if (detail != nullptr && detailAer >= 0)
    {
        const auto e = proc.aerEq (detailAer);
        if (e != shownEq) { shownEq = e; detail->repaint(); }
    }
}

void DeltaABEditor::showDetail (std::unique_ptr<Detail> d)
{
    detail = std::move (d);
    addAndMakeVisible (*detail);
    detail->setBounds (rConsole);
}

static std::function<void()> closer (DeltaABEditor* ed, std::unique_ptr<Detail>& slot, int& aerIdx)
{
    return [ed, &slot, &aerIdx] {
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<DeltaABEditor> (ed), &slot, &aerIdx] {
            if (sp) { slot.reset(); aerIdx = -1; }
        });
    };
}

static void panRow (Detail& d, APVTS& s, const juce::String& prefix)
{
    d.row ("pan total");
    d.knob (s, prefix + "pan", "pan G-C-D");
    d.knob (s, prefix + "pan_lvl", "niveau");
    d.knob (s, prefix + "pan_dly", u ("délai"));
    d.knob (s, prefix + "pan_ph", "phase");
    d.knob (s, prefix + "pan_hf", "pan HF");
}

void DeltaABEditor::openMic (int m)
{
    const char* names[3] = { "MICRO 1 G", "MICRO 2 D", "MICRO 3 C" };
    auto d = std::make_unique<Detail> (u ("DELTA AB · ") + names[m], closer (this, detail, detailAer));
    auto& s = proc.apvts;
    d->row ("sources");
    d->choice (s, mid (m, "combo"), "sources X + Y");
    d->toggle (s, mid (m, "swap"), u ("inverser X/Y (6 can.)"));
    d->row ("gain match");
    d->knob (s, mid (m, "match"), "gain Y");
    d->toggle (s, mid (m, "analyse"), "analyser X/Y");
    d->row ("crossover");
    d->choice (s, mid (m, "xover"), "coupure");
    d->choice (s, mid (m, "lows"), "dans le grave");
    d->row ("gains / delai");
    d->knob (s, mid (m, "focus"), "aigu (focus)");
    d->knob (s, mid (m, "bloom"), "grave (bloom)");
    d->knob (s, mid (m, "delay"), u ("délai"));
    d->knob (s, mid (m, "level"), "niveau sortie");
    panRow (*d, s, mid (m, ""));
    showDetail (std::move (d));
}

void DeltaABEditor::openAer (int a)
{
    auto d = std::make_unique<Detail> (u ("DELTA AER · ") + aerTitle[a], closer (this, detail, detailAer));
    auto& s = proc.apvts;
    d->row ("low cut");
    d->toggle (s, aid (a, "lc_on"), "low cut");
    d->knob (s, aid (a, "lc_hz"), "LC Hz");
    d->choice (s, aid (a, "lc_slope"), "pente LC");
    d->row ("high cut");
    d->toggle (s, aid (a, "hc_on"), "high cut");
    d->knob (s, aid (a, "hc_hz"), "HC Hz");
    d->choice (s, aid (a, "hc_slope"), "pente HC");
    d->row ("shelf grave");
    d->knob (s, aid (a, "ls_f"), "Hz"); d->knob (s, aid (a, "ls_g"), "gain"); d->knob (s, aid (a, "ls_q"), "Q");
    for (int b = 1; b <= 3; ++b)
    {
        const juce::String bp = "b" + juce::String (b);
        d->row ("cloche " + juce::String (b));
        d->knob (s, aid (a, (bp + "_f").toRawUTF8()), "Hz");
        d->knob (s, aid (a, (bp + "_g").toRawUTF8()), "gain");
        d->knob (s, aid (a, (bp + "_q").toRawUTF8()), "Q");
    }
    d->row ("shelf aigu");
    d->knob (s, aid (a, "hs_f"), "Hz"); d->knob (s, aid (a, "hs_g"), "gain"); d->knob (s, aid (a, "hs_q"), "Q");
    d->row (u ("délai / fader"));
    d->knob (s, aid (a, "delay"), u ("délai"), true);
    d->knob (s, aid (a, "fader"), "fader");
    d->choice (s, "aer_eq_phase", "phase EQ (3 canaux)");
    panRow (*d, s, aid (a, ""));

    // EQ curve, -24 .. +24 dB, 20 Hz .. 20 kHz (same curve in linear and minimum phase)
    d->drawExtra = [this, a] (juce::Graphics& g, juce::Rectangle<int> r)
    {
        const auto sec = dab::eqSections (proc.aerEq (a), 48000.0);
        auto xOf = [&] (double f) { return (float) r.getX() + (float) r.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); };
        auto yOf = [&] (double dB) { return (float) r.getCentreY() - (float) dB / 24.0f * (float) r.getHeight() / 2; };
        g.setColour (kGrey);
        for (double f : { 100.0, 1000.0, 10000.0 }) g.drawVerticalLine ((int) xOf (f), (float) r.getY(), (float) r.getBottom());
        for (double dB : { -12.0, 0.0, 12.0 }) g.drawHorizontalLine ((int) yOf (dB), (float) r.getX(), (float) r.getRight());
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain)));
        g.drawText ("100", (int) xOf (100) + 2, r.getBottom() - 14, 40, 12, juce::Justification::left);
        g.drawText ("1k", (int) xOf (1000) + 2, r.getBottom() - 14, 40, 12, juce::Justification::left);
        g.drawText ("10k", (int) xOf (10000) + 2, r.getBottom() - 14, 40, 12, juce::Justification::left);
        g.drawText ("+12", r.getX() + 2, (int) yOf (12) - 12, 40, 12, juce::Justification::left);
        g.drawText ("-12", r.getX() + 2, (int) yOf (-12) - 12, 40, 12, juce::Justification::left);
        juce::Path path;
        for (int i = 0; i <= r.getWidth(); ++i)
        {
            const double f = 20.0 * std::pow (1000.0, (double) i / r.getWidth());
            const double dB = juce::jlimit (-30.0, 30.0, 20.0 * std::log10 (std::max (1e-9, dab::sectionsMagnitude (sec, f, 48000.0))));
            const float x = (float) (r.getX() + i), y = juce::jlimit ((float) r.getY(), (float) r.getBottom(), yOf (dB));
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        g.setColour (kWhite);
        g.strokePath (path, juce::PathStrokeType (1.5f));
    };
    detailAer = a;
    shownEq = proc.aerEq (a);
    showDetail (std::move (d));
}

void DeltaABEditor::openSpot (int k)
{
    auto d = std::make_unique<Detail> ("SPOT EN-MS " + juce::String (k + 1) + u (" · CARDIO (M) + RUBAN (S)"), closer (this, detail, detailAer));
    auto& s = proc.apvts;
    d->row ("gain match");
    d->knob (s, sid (k, "match"), "gain ruban");
    d->row ("mid/side ou xover");
    d->choice (s, sid (k, "mode"), "mode");
    d->toggle (s, sid (k, "invert"), u ("M/S : ruban côté opposé"));
    d->choice (s, sid (k, "xover"), "coupure (crossover)");
    d->choice (s, sid (k, "lows"), "dans le grave");
    d->row ("niveaux");
    d->knob (s, sid (k, "cardio"), "cardio (M)");
    d->knob (s, sid (k, "ribbon"), "ruban (S)");
    d->row (u ("délai / fader"));
    d->knob (s, sid (k, "delay"), u ("délai"), true);
    d->knob (s, sid (k, "fader"), "fader");
    panRow (*d, s, sid (k, ""));
    showDetail (std::move (d));
}

void DeltaABEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBlack);
    g.setColour (kWhite);
    auto mono = [] (float h, int style) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, style)); };
    g.setFont (mono (24.0f, juce::Font::bold));
    g.drawText ("DELTA AB", 16, 10, 200, 30, juce::Justification::centredLeft);
    g.setFont (mono (10.0f, juce::Font::plain));
    g.drawText (u ("DELTA AB · DELTA AER · SPOTS EN-MS"), 16, 40, 260, 14, juce::Justification::centredLeft);
    g.drawLine ((float) rTop.getX(), (float) rTop.getBottom(), (float) rTop.getRight(), (float) rTop.getBottom(), 1.0f);

    auto group = [&] (juce::Rectangle<int> r, const juce::String& name)
    {
        g.setColour (kWhite);
        g.drawRect (r, 1);
        g.setFont (mono (13.0f, juce::Font::bold));
        g.drawText (name, r.getX() + 8, r.getY() + 4, r.getWidth() - 16, 16, juce::Justification::centredLeft);
    };
    group (rAb, "DELTA AB");
    group (rAer, "DELTA AER");
    group (rSpots, "SPOTS EN-MS  " + juce::String (shownSpots));
    group (rBus, "BUS");
}

void DeltaABEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    rTop = r.removeFromTop (66);
    {
        auto g = rTop.withTrimmedLeft (280).withTrimmedTop (4);
        for (auto* c : { &phase, &quality, &slope, &focal, &panLaw, &panMax, &bloomMode })
        {
            c->setBounds (g.removeFromLeft (110).withHeight (34));
            g.removeFromLeft (6);
        }
        ambiance.setBounds (g.removeFromLeft (100).withHeight (22).translated (0, 12));
        g.removeFromLeft (10);
        focus.setBounds (g.removeFromLeft (70).withHeight (62));
        bloom.setBounds (g.removeFromLeft (70).withHeight (62));
    }
    r.removeFromTop (10);
    rConsole = r;

    auto groupW = [] (int strips) { return strips * kStripW + (strips - 1) * kGap + 2 * kPad; };
    rAb = r.removeFromLeft (groupW (3));   r.removeFromLeft (10);
    rAer = r.removeFromLeft (groupW (3));  r.removeFromLeft (10);
    rBus = r.removeFromRight (groupW (6)); r.removeFromRight (10);
    rSpots = r;

    auto placeStrips = [] (juce::Rectangle<int> g, std::unique_ptr<Strip>* st, int n)
    {
        auto b = g.reduced (kPad).withTrimmedTop (kHead - kPad);
        for (int i = 0; i < n; ++i) { st[i]->setBounds (b.removeFromLeft (kStripW)); b.removeFromLeft (kGap); }
        return b;
    };
    placeStrips (rAb, ab, 3);
    placeStrips (rAer, aer, 3);
    aerPhase.setBounds (rAer.getX() + kPad, rAer.getY() + 22, 120, 18);
    aerLink.setBounds (rAer.getRight() - kPad - 88, rAer.getY() + 22, 88, 18);

    auto sb = rSpots.reduced (kPad).withTrimmedTop (kHead - kPad);
    spotView.setBounds (sb);
    addSpot.setBounds (rSpots.getRight() - 6 - 80, rSpots.getY() + 6, 80, 20);
    removeSpot.setBounds (rSpots.getRight() - 6 - 166, rSpots.getY() + 6, 80, 20);
    layoutSpots();

    auto b = placeStrips (rBus, bus, 3);
    for (auto* f : { &aerOut, &spotsOut, &master })
    {
        f->setBounds (b.removeFromLeft (kStripW).withTrimmedTop (20));
        b.removeFromLeft (kGap);
    }
    centreSend.setBounds (rBus.getX() + 60, rBus.getY() + 3, 120, 34);
    if (detail) detail->setBounds (rConsole);
}
