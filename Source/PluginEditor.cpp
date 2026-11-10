#include "PluginEditor.h"

namespace ui {

static const juce::Colour kBlack { 0xff000000 }, kWhite { 0xffffffff }, kGrey { 0xff8a8a8a }, kDim { 0xff3a3a3a };
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

juce::Font Look::font (float h, bool bold) const
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain));
}

// Big knurled knob with a numbered 0-10 scale around it.
void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&)
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float R = juce::jmin (area.getWidth(), area.getHeight()) / 2 - 1;   // scale radius
    const auto c = area.getCentre();
    const float rk = R * 0.70f;                                              // knob radius

    // scale: 11 ticks, numbers 0..10 on big knobs
    g.setColour (kWhite);
    const bool numbers = R > 30;
    for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (a1 - a0) * (float) i / 10.0f;
        const float r1 = rk + 3, r2 = rk + (i % 5 == 0 ? 8.0f : 5.0f);
        g.drawLine (c.x + std::sin (a) * r1, c.y - std::cos (a) * r1, c.x + std::sin (a) * r2, c.y - std::cos (a) * r2, 1.0f);
        if (numbers && i % 2 == 0)
        {
            const float rn = R - 4;
            g.setFont (font (8.0f));
            g.drawText (juce::String (i), juce::Rectangle<float> (c.x + std::sin (a) * rn - 7, c.y - std::cos (a) * rn - 5, 14, 10),
                        juce::Justification::centred);
        }
    }

    // knurled skirt
    juce::Path skirt;
    const int teeth = 28;
    for (int i = 0; i <= teeth * 2; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / (float) (teeth * 2);
        const float r = (i % 2 == 0) ? rk : rk - 2.0f;
        const juce::Point<float> p (c.x + std::sin (a) * r, c.y - std::cos (a) * r);
        if (i == 0) skirt.startNewSubPath (p); else skirt.lineTo (p);
    }
    skirt.closeSubPath();
    g.setColour (kBlack);
    g.fillPath (skirt);
    g.setColour (kWhite);
    g.strokePath (skirt, juce::PathStrokeType (1.0f));
    // cap
    const float rc = rk * 0.62f;
    g.drawEllipse (c.x - rc, c.y - rc, rc * 2, rc * 2, 1.0f);
    // pointer
    const float a = a0 + pos * (a1 - a0);
    g.drawLine (c.x + std::sin (a) * rc * 0.2f, c.y - std::cos (a) * rc * 0.2f, c.x + std::sin (a) * (rk - 1), c.y - std::cos (a) * (rk - 1), 3.0f);
}

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&)
{
    const float cx = (float) x + (float) w / 2;
    g.setColour (kWhite);
    g.fillRect (cx - 1.5f, (float) y, 3.0f, (float) h);
    g.setColour (kBlack);
    g.fillRect (cx - 0.5f, (float) y + 1, 1.0f, (float) h - 2);
    g.setColour (kWhite);
    for (int i = 0; i <= 10; ++i)   // scale ticks
    {
        const float ty = (float) y + (float) h * (float) i / 10.0f;
        const float len = i % 5 == 0 ? 12.0f : 7.0f;
        g.drawLine (cx - 6 - len, ty, cx - 6, ty, 1.0f);
        g.drawLine (cx + 6, ty, cx + 6 + len, ty, 1.0f);
    }
    // big fader cap with grip lines
    const juce::Rectangle<float> cap (cx - 20, pos - 11, 40, 22);
    g.setColour (kBlack); g.fillRect (cap);
    g.setColour (kWhite); g.drawRect (cap, 1.5f);
    g.fillRect (cap.getX() + 3, pos - 1.0f, cap.getWidth() - 6, 2.0f);
    g.drawLine (cap.getX() + 6, pos - 6, cap.getRight() - 6, pos - 6, 1.0f);
    g.drawLine (cap.getX() + 6, pos + 6, cap.getRight() - 6, pos + 6, 1.0f);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (b.getToggleState() ? kWhite : kBlack);
    g.fillRect (r);
    g.setColour (! b.isEnabled() ? kDim : over ? kGrey : kWhite);
    g.drawRect (r, 1.0f);
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (! b.isEnabled() ? kDim : b.getToggleState() ? kBlack : kWhite);
    g.setFont (font (10.0f, true));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3), juce::Justification::centred, 2);
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    g.setColour (kBlack); g.fillRect (0, 0, w, h);
    g.setColour (kWhite); g.drawRect (0, 0, w, h, 1);
    juce::Path tri; const float ax = (float) w - 9, ay = (float) h / 2;
    tri.addTriangle (ax - 3.5f, ay - 2, ax + 3.5f, ay - 2, ax, ay + 3);
    g.fillPath (tri);
}

void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (3, 1, box.getWidth() - 16, box.getHeight() - 2);
    label.setFont (font (11.0f));
    label.setMinimumHorizontalScale (0.6f);
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
    l.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold)));
}

Knob::Knob (APVTS& s, const juce::String& id, const juce::String& text, bool fader, bool delayMetres)
    : metres (delayMetres)
{
    slider.setSliderStyle (fader ? juce::Slider::LinearVertical : juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (metres ? juce::Slider::NoTextBox : juce::Slider::TextBoxBelow, false, 72, 14);
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
    if (caption.getText().isNotEmpty()) caption.setBounds (r.removeFromTop (13));
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

Switch::Switch (APVTS& s, const juce::String& id, const juce::StringArray& names)
{
    for (auto& n : names) labels.add (upper (n));
    if (auto* p = s.getParameter (id))
    {
        att = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { index = juce::jlimit (0, labels.size() - 1, juce::roundToInt (v)); repaint(); });
        att->sendInitialUpdate();
    }
}

Switch::Switch (APVTS& s, const juce::String& id, const juce::String& a, const juce::String& b)
    : Switch (s, id, juce::StringArray { a, b }) {}

static juce::Font monoFont (float h) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::bold)); }

// Slide switch in the style of the Telefunken W 659 (MS | XY): a slot with a light ridged block.
// Two positions: labels left and right of the slot. More: labels above each position.
juce::Rectangle<float> Switch::slot() const
{
    auto r = getLocalBounds().toFloat();
    if (labels.size() == 2) return r.reduced (r.getWidth() * 0.32f, 0).withSizeKeepingCentre (r.getWidth() * 0.36f, juce::jmin (16.0f, r.getHeight()));
    return r.withTrimmedTop (12).reduced (6, 1);
}

void Switch::paint (juce::Graphics& g)
{
    const int n = juce::jmax (2, labels.size());
    auto r = getLocalBounds().toFloat();
    const auto sl = slot();
    g.setFont (monoFont (9.5f));
    if (labels.size() == 2)
    {
        g.setColour (index == 0 ? kWhite : kGrey);
        g.drawFittedText (labels[0], juce::Rectangle<float> (r.getX(), r.getY(), sl.getX() - r.getX() - 3, r.getHeight()).toNearestInt(), juce::Justification::centredRight, 1, 0.6f);
        g.setColour (index == 1 ? kWhite : kGrey);
        g.drawFittedText (labels[1], juce::Rectangle<float> (sl.getRight() + 3, r.getY(), r.getRight() - sl.getRight() - 3, r.getHeight()).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
    }
    else
    {
        const float w = sl.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            g.setColour (i == index ? kWhite : kGrey);
            g.drawFittedText (labels[i], juce::Rectangle<float> (sl.getX() + w * (float) i - 4, r.getY(), w + 8, 11).toNearestInt(), juce::Justification::centred, 1, 0.5f);
        }
    }
    // slot: recessed, dark
    g.setColour (juce::Colour (0xff141414)); g.fillRoundedRectangle (sl, 2.0f);
    g.setColour (kGrey); g.drawRoundedRectangle (sl, 2.0f, 1.0f);
    // block
    const float w = sl.getWidth() / (float) n;
    auto blk = juce::Rectangle<float> (sl.getX() + w * (float) index, sl.getY(), w, sl.getHeight()).reduced (1.5f);
    g.setColour (juce::Colour (0xffe8e4da)); g.fillRoundedRectangle (blk, 1.5f);
    g.setColour (kBlack);
    for (int i = 1; i <= 3; ++i)
    {
        const float x = blk.getX() + blk.getWidth() * (float) i / 4.0f;
        g.drawLine (x, blk.getY() + 2.5f, x, blk.getBottom() - 2.5f, 1.0f);
    }
}

void Switch::mouseDown (const juce::MouseEvent& e)
{
    if (att == nullptr) return;
    const int n = juce::jmax (2, labels.size());
    int want;
    if (n == 2) want = index == 0 ? 1 : 0;   // a click flips it, like the real lever
    else
    {
        const auto sl = slot();
        want = juce::jlimit (0, n - 1, (int) ((e.position.x - sl.getX()) / (sl.getWidth() / (float) n)));
    }
    if (want != index) att->setValueAsCompleteGesture ((float) want);
}

Selector::Selector (APVTS& s, const juce::String& id, const juce::String& cap) : caption (upper (cap))
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
    {
        for (auto n : p->choices)
        {
            n = n.replace (" Hz", "").replace (u ("Focal"), "FOC");
            if (n.endsWith ("000")) n = n.dropLastCharacters (3) + "k";
            names.add (n);
        }
        att = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { index = juce::roundToInt (v); repaint(); });
        att->sendInitialUpdate();
    }
}

float Selector::angleOf (int i) const
{
    const int n = juce::jmax (1, names.size());
    const float span = juce::MathConstants<float>::pi * (n <= 4 ? 0.75f : 1.45f);   // 135 deg for 4 sources, ~260 deg for the crossover
    return n == 1 ? 0.0f : -span / 2 + span * (float) i / (float) (n - 1);
}

void Selector::select (int i)
{
    i = juce::jlimit (0, names.size() - 1, i);
    if (att != nullptr && i != index) att->setValueAsCompleteGesture ((float) i);
}

Selector::Geo Selector::geo() const
{
    auto r = getLocalBounds().toFloat();
    const bool wide = names.size() > 4;
    if (! wide)   // source selector: caption on the left, cup low, labels in an arc above it
    {
        const float R = (r.getHeight() - 17) / 2;
        return { { r.getCentreX() + 8, r.getBottom() - R - 1 }, R };
    }
    if (caption.isNotEmpty()) r.removeFromBottom (12);
    // label ring sits outside the cup: leave room for the text all around (wide) or above (4 sources)
    const float R = wide ? juce::jmin (r.getWidth() / 2 - 14, r.getHeight() / 2 - 8) : juce::jmin (r.getWidth() / 2 - 12, r.getHeight() - 26);
    const juce::Point<float> c (r.getCentreX(), wide ? r.getCentreY() + 3 : r.getBottom() - R - 2);
    return { c, R };
}

// Nagra HD style: a flat bar knob with a pointed tip, sunk in a round cup, positions engraved around it.
void Selector::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (kWhite);
    g.setFont (monoFont (10.0f));
    const auto [c, R] = geo();
    if (names.size() <= 4)
    {
        g.setFont (monoFont (16.0f));
        g.drawText (caption, juce::Rectangle<float> (r.getX() + 2, c.y - 10, c.x - R - 24 - r.getX(), 20), juce::Justification::centredLeft);
    }
    else if (caption.isNotEmpty()) g.drawText (caption, r.removeFromBottom (12), juce::Justification::centred);
    // engraved positions: tick on the rim, label outside
    g.setFont (monoFont (names.size() > 4 ? 8.0f : 8.5f));
    for (int i = 0; i < names.size(); ++i)
    {
        const float a = angleOf (i);
        g.setColour (i == index ? kWhite : kGrey);
        g.drawLine (c.x + std::sin (a) * (R + 1), c.y - std::cos (a) * (R + 1), c.x + std::sin (a) * (R + 4), c.y - std::cos (a) * (R + 4), 1.5f);
        const float rn = R + 11;
        g.drawText (names[i], juce::Rectangle<float> (c.x + std::sin (a) * rn - 11, c.y - std::cos (a) * rn - 5, 22, 10), juce::Justification::centred);
    }
    // cup: bevelled rim, dark well
    g.setColour (kGrey);  g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);
    g.setColour (kDim);   g.fillEllipse (c.x - R + 1.5f, c.y - R + 1.5f, 2 * R - 3, 2 * R - 3);
    g.setColour (juce::Colour (0xff101010)); g.fillEllipse (c.x - R * 0.86f, c.y - R * 0.86f, R * 1.72f, R * 1.72f);
    // bar knob across the cup, pointed at the selected position
    const float a = angleOf (index), L = R * 0.84f, Wd = R * 0.30f;
    juce::Path bar;
    bar.startNewSubPath (0, -L);                       // tip
    bar.lineTo (Wd, -L + Wd * 1.3f);
    bar.lineTo (Wd, L - Wd * 0.4f);
    bar.quadraticTo (Wd, L, 0, L);
    bar.quadraticTo (-Wd, L, -Wd, L - Wd * 0.4f);
    bar.lineTo (-Wd, -L + Wd * 1.3f);
    bar.closeSubPath();
    bar.applyTransform (juce::AffineTransform::rotation (a).translated (c));
    g.setColour (juce::Colour (0xffd9d9d9)); g.fillPath (bar);
    g.setColour (kWhite); g.strokePath (bar, juce::PathStrokeType (1.0f));
    // ridge along the bar and the index line on the tip
    g.setColour (kGrey);
    g.drawLine (c.x - std::sin (a) * L * 0.8f, c.y + std::cos (a) * L * 0.8f, c.x + std::sin (a) * L * 0.5f, c.y - std::cos (a) * L * 0.5f, 1.0f);
    g.setColour (kBlack);
    g.drawLine (c.x + std::sin (a) * L * 0.55f, c.y - std::cos (a) * L * 0.55f, c.x + std::sin (a) * (L - 2), c.y - std::cos (a) * (L - 2), 2.0f);
}

void Selector::mouseDown (const juce::MouseEvent& e)
{
    dragStart = index;
    // a click on (or near) a position label selects it
    const auto [c, R] = geo();
    const auto d = e.position - c;
    if (d.getDistanceFromOrigin() > R * 0.9f)
    {
        const float a = std::atan2 (d.x, -d.y);
        int best = 0; float bd = 1e9f;
        for (int i = 0; i < names.size(); ++i) { const float dd = std::abs (angleOf (i) - a); if (dd < bd) { bd = dd; best = i; } }
        select (best);
    }
}

void Selector::mouseDrag (const juce::MouseEvent& e)
{
    select (dragStart + (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / 20);
}

void Selector::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    select (index + (w.deltaY > 0 ? 1 : -1));
}

void MatchMeter::tick()
{
    for (int i = 0; i < 2; ++i)
    {
        const float pk = proc.takeMeterPeak (ch[i]);
        const float db = pk > 1e-6f ? 20.0f * std::log10 (pk) : -100.0f;
        peakDb[i] = juce::jmax (db, peakDb[i] - 1.5f);   // falls about 20 dB/s at 15 Hz
        rmsDb[i] = proc.meterRmsDb (ch[i]);
    }
    repaint();
}

void MatchMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float lo = -48.0f;
    auto xOf = [&] (juce::Rectangle<float> b, float db) { return b.getX() + b.getWidth() * juce::jlimit (0.0f, 1.0f, (db - lo) / -lo); };
    g.setFont (monoFont (9.0f));
    for (int i = 0; i < 2; ++i)
    {
        auto row = r.removeFromTop (10);
        g.setColour (kWhite);
        g.drawText (lab[i], row.removeFromLeft (10), juce::Justification::centredLeft);
        auto bar = row.reduced (0, 1.5f);
        g.setColour (kDim); g.fillRect (bar);
        g.setColour (kWhite); g.fillRect (bar.withRight (xOf (bar, peakDb[i])));
        if (rmsDb[i] > lo) { g.setColour (kBlack); g.fillRect (xOf (bar, rmsDb[i]) - 1.0f, bar.getY(), 2.0f, bar.getHeight()); }
        r.removeFromTop (2);
    }
    // the difference, from the 300 ms RMS: what to add to the second input to match the first
    juce::String t = u ("Δ  —");
    if (rmsDb[0] > -90.0f && rmsDb[1] > -90.0f)
    {
        const float d = rmsDb[0] - rmsDb[1];
        t = u ("Δ ") + (d >= 0 ? "+" : "") + juce::String (d, 1) + " dB";
    }
    g.setColour (kWhite);
    g.setFont (monoFont (13.0f));
    g.drawText (t, r, juce::Justification::centred);
}

void InsertSlot::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (kDim);
    const float dash[] = { 3.0f, 3.0f };
    juce::Path p; p.addRectangle (r);
    juce::Path d; juce::PathStrokeType (1.0f).createDashedStroke (d, p, dash, 2);
    g.fillPath (d);
    g.setColour (kGrey);
    g.setFont (monoFont (9.0f));
    g.drawFittedText (upper (text), getLocalBounds().reduced (2), juce::Justification::centred, 1, 0.6f);
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
    items.push_back ({ { k }, delayMetres ? 92 : 80 });
    return *k;
}

Choice& Strip::choice (APVTS& s, const juce::String& id, const juce::String& caption)
{
    auto* c = new Choice (s, id, caption); owned.add (c);
    addAndMakeVisible (c);
    items.push_back ({ { c }, 33 });
    return *c;
}

Toggle& Strip::toggle (APVTS& s, const juce::String& id, const juce::String& text)
{
    auto* t = new Toggle (s, id, text); owned.add (t);
    addAndMakeVisible (t);
    items.push_back ({ { t }, 20 });
    return *t;
}

Switch& Strip::switch2 (APVTS& s, const juce::String& id, const juce::String& a, const juce::String& b)
{
    auto* t = new Switch (s, id, a, b); owned.add (t);
    addAndMakeVisible (t);
    items.push_back ({ { t }, 20 });
    return *t;
}

void Strip::insert (const juce::String& text)
{
    auto* t = new InsertSlot (text); owned.add (t);
    addAndMakeVisible (t);
    items.push_back ({ { t }, 18 });
}

void Strip::selectorPair (APVTS& s, const juce::String& idA, const juce::String& a, const juce::String& idB, const juce::String& b)
{
    auto* x = new Selector (s, idA, a); owned.add (x);
    auto* y = new Selector (s, idB, b); owned.add (y);
    addAndMakeVisible (x); addAndMakeVisible (y);
    items.push_back ({ { x }, 60 });
    items.push_back ({ { y }, 60 });
}

void Strip::selector (APVTS& s, const juce::String& id, const juce::String& caption, int h)
{
    auto* x = new Selector (s, id, caption); owned.add (x);
    addAndMakeVisible (x);
    items.push_back ({ { x }, h });
}

Switch& Strip::switchN (APVTS& s, const juce::String& id, const juce::StringArray& names)
{
    auto* t = new Switch (s, id, names); owned.add (t);
    addAndMakeVisible (t);
    items.push_back ({ { t }, 26 });
    return *t;
}

void Strip::knobRow (APVTS& s, std::initializer_list<std::pair<juce::String, juce::String>> ks, int h, bool values)
{
    Item it { {}, h };
    for (auto& [id, cap] : ks)
    {
        auto* k = new Knob (s, id, cap); owned.add (k);
        if (! values) k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (k);
        it.cs.push_back (k);
    }
    items.push_back (it);
}

void Strip::matchMeter (DeltaABProcessor& p, int a, int b, const juce::String& la, const juce::String& lb)
{
    auto* m = new MatchMeter (p, a, b, la, lb); owned.add (m);
    addAndMakeVisible (m);
    meters.push_back (m);
    items.push_back ({ { m }, 40 });
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
    g.fillRect (0, 0, getWidth(), 20);
    g.setColour (kBlack);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold)));
    g.drawFittedText (title, 3, 2, getWidth() - 6, 16, juce::Justification::centred, 1, 0.6f);
}

void Strip::resized()
{
    auto r = getLocalBounds().reduced (4);
    r.removeFromTop (19);
    if (detail.isVisible()) { detail.setBounds (r.removeFromTop (18)); }
    r.removeFromTop (5);
    for (auto& it : items)
    {
        auto b = r.removeFromTop (it.h);
        const int n = (int) it.cs.size(), w = b.getWidth() / juce::jmax (1, n);
        for (int i = 0; i < n; ++i) it.cs[(size_t) i]->setBounds (i == n - 1 ? b : b.removeFromLeft (w));
        r.removeFromTop (5);
    }
    if (faderKnob != nullptr) faderKnob->setBounds (r.withTrimmedTop (4));
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
    g.fillRect (2, 2, getWidth() - 4, 36);
    g.setColour (kBlack);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 17.0f, juce::Font::bold)));
    g.drawText (title, 16, 8, getWidth() - 160, 24, juce::Justification::centredLeft);
    g.setColour (kWhite);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold)));
    for (auto& r : rows)
    {
        g.drawText (r.name, 16, r.y, 130, 20, juce::Justification::centredLeft);
        g.drawLine (16.0f, (float) r.y + 20, 140.0f, (float) r.y + 20, 1.0f);
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
    closeButton.setBounds (r.removeFromTop (22).removeFromRight (100).translated (0, -6));
    r.removeFromTop (16);
    if (drawExtra) { extraArea = r.removeFromRight (r.getWidth() * 2 / 5).withTrimmedLeft (16).withHeight (juce::jmin (r.getHeight(), 300)); }
    const int rowH = rows.empty() ? 0 : juce::jmin (96, r.getHeight() / (int) rows.size());
    for (auto& row : rows)
    {
        auto b = r.removeFromTop (rowH);
        row.y = b.getY();
        b.removeFromLeft (150);
        for (auto* c : row.items)
        {
            const bool isChoice = dynamic_cast<Choice*> (c) != nullptr, isToggle = dynamic_cast<Toggle*> (c) != nullptr;
            const int w = isChoice ? 160 : isToggle ? 130 : 92;
            auto cell = b.removeFromLeft (w);
            b.removeFromLeft (10);
            if (isToggle) c->setBounds (cell.withTrimmedTop (14).withHeight (22));
            else if (isChoice) c->setBounds (cell.withHeight (34));
            else c->setBounds (cell.withHeight (juce::jmin (rowH - 4, 92)));
        }
    }
}

// ------------------------------------------------------------------ console
static juce::String mid (int m, const char* p) { return "m" + juce::String (m + 1) + "_" + p; }
static juce::String aid (int a, const char* p) { return "a" + juce::String (a + 1) + "_" + p; }
static juce::String sid (int k, const char* p) { return "s" + juce::String (k + 1) + "_" + p; }

static const int abMic[3] = { 0, 1, 2 };   // left to right: AB.01, AB.02, AB.03
static const char* abTitle[3] = { "AB.01", "AB.02", "AB.03" };
static const char* aerTitle[3] = { "AER.01", "AER.02", "AER.03" };
static constexpr int kStripW = 92, kAbW = 140, kGap = 6, kHead = 48, kPad = 8;

Console::Console (DeltaABProcessor& p)
    : proc (p),
      phase (p.apvts, "phase", "phase xover"),
      quality (p.apvts, "quality", u ("qualité")),
      slope (p.apvts, "slope", "pente"),
      focal (p.apvts, "focal", "focal"),
      panLaw (p.apvts, "pan_law", "loi de pan"),
      panMax (p.apvts, "pan_mult", u ("pan délai max")),
      bloomMode (p.apvts, "bloom_mode", "bloom mode"),
      centreSend (p.apvts, "c_send", "centre > G/D"),
      ambiance (p.apvts, "ambient", "ambiance"),
      focus (p.apvts, "m_focus", "focus"),
      bloom (p.apvts, "m_bloom", "bloom"),
      aerPhase (p.apvts, "aer_eq_phase", "EQ min", "EQ lin"),
      aerLink (p.apvts, "aer_link", u ("faders liés"))
{
    auto& s = p.apvts;
    for (juce::Component* c : { (juce::Component*) &phase, (juce::Component*) &quality, (juce::Component*) &slope,
                                (juce::Component*) &focal, (juce::Component*) &panLaw, (juce::Component*) &panMax,
                                (juce::Component*) &bloomMode, (juce::Component*) &centreSend, (juce::Component*) &ambiance,
                                (juce::Component*) &focus, (juce::Component*) &bloom, (juce::Component*) &aerPhase,
                                (juce::Component*) &aerLink, (juce::Component*) &spotView, (juce::Component*) &add4,
                                (juce::Component*) &add8, (juce::Component*) &remove1 })
        addAndMakeVisible (c);

    for (int i = 0; i < 3; ++i)
    {
        const int m = abMic[i];
        auto& st = *(ab[i] = std::make_unique<Strip> (abTitle[i], [this, m] { openMic (m); }));
        st.selectorPair (s, mid (m, "xsrc"), "X", mid (m, "ysrc"), "Y");
        st.knobRow (s, { { mid (m, "gainx"), "gain X" }, { mid (m, "match"), "gain Y" } }, 64, true);
        st.matchMeter (p, 2 * m, 2 * m + 1, "X", "Y");
        st.selector (s, mid (m, "xover"), "xover Hz", 100);
        st.switchN (s, mid (m, "lows"), { "auto", "X LF", "Y LF" });
        st.knobRow (s, { { mid (m, "focus"), "focus" }, { mid (m, "bloom"), "bloom" }, { "focal", "focale" } }, 64, true);
        st.knob (s, mid (m, "pan"), "pan");
        st.knobRow (s, { { mid (m, "pan_lvl"), "niv" }, { mid (m, "pan_dly"), u ("dél") }, { mid (m, "pan_ph"), "phase" }, { mid (m, "pan_hf"), "HF" } }, 44, false);
        st.fader (s, mid (m, "level"));
        addAndMakeVisible (st);
    }
    for (int a = 0; a < 3; ++a)
    {
        auto& st = *(aer[a] = std::make_unique<Strip> (aerTitle[a], [this, a] { openAer (a); }));
        st.insert ("insert vst");
        st.toggle (s, aid (a, "lc_on"), "low cut");
        st.toggle (s, aid (a, "hc_on"), "high cut");
        st.knob (s, aid (a, "delay"), u ("délai"), true);
        st.insert ("insert rev / DA-AD");
        st.insert ("insert comp");
        st.knob (s, aid (a, "pan"), "pan");
        st.fader (s, aid (a, "fader"));
        addAndMakeVisible (st);
    }
    for (int k = 0; k < dab::kMaxSpots; ++k)
    {
        auto& st = *(spot[k] = std::make_unique<Strip> ("EN-MS." + juce::String (k + 1).paddedLeft ('0', 2), [this, k] { openSpot (k); }));
        st.knobRow (s, { { sid (k, "gainc"), "gain M" }, { sid (k, "match"), "gain S" } }, 64, true);
        st.matchMeter (p, 6 + 2 * k, 7 + 2 * k, "M", "S");
        st.switch2 (s, sid (k, "mode"), "M/S", "xover");
        st.knob (s, sid (k, "cardio"), "level M");
        st.knob (s, sid (k, "ribbon"), "level S");
        st.insert ("insert");
        st.switch2 (s, sid (k, "out"), "somme", u ("stéréo"));
        st.knob (s, sid (k, "pan"), "pan");
        st.fader (s, sid (k, "fader"));
        spotHolder.addChildComponent (st);
    }
    spotView.setViewedComponent (&spotHolder, false);
    spotView.setScrollBarsShown (false, true);
    spotView.setScrollBarThickness (10);
    auto count = [this] { return (int) proc.apvts.getRawParameterValue ("spots_n")->load(); };
    add4.onClick = [this, count] { setSpotCount (count() + 4); };
    add8.onClick = [this, count] { setSpotCount (count() + 8); };
    remove1.onClick = [this, count] { setSpotCount (count() - 1); };

    setSize (kW, kH);
    startTimerHz (15);
}

Console::~Console()
{
    stopTimer();
    detail.reset();
}

void Console::setSpotCount (int n)
{
    if (auto* prm = proc.apvts.getParameter ("spots_n"))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) juce::jlimit (1, dab::kMaxSpots, n)));
        prm->endChangeGesture();
    }
}

void Console::layoutSpots()
{
    const int n = juce::jlimit (1, dab::kMaxSpots, (int) proc.apvts.getRawParameterValue ("spots_n")->load());
    const int h = spotView.getHeight() - spotView.getScrollBarThickness() - 2;
    for (int k = 0; k < dab::kMaxSpots; ++k)
    {
        spot[k]->setVisible (k < n);
        spot[k]->setBounds (k * (kStripW + kGap), 0, kStripW, h);
    }
    spotHolder.setSize (n * (kStripW + kGap) - kGap, h);
    add4.setEnabled (n < dab::kMaxSpots);
    add8.setEnabled (n < dab::kMaxSpots);
    remove1.setEnabled (n > 1);
    shownSpots = n;
}

void Console::timerCallback()
{
    for (auto& st : ab) st->tick();
    for (int k = 0; k < juce::jmax (0, shownSpots); ++k) spot[k]->tick();
    if ((int) proc.apvts.getRawParameterValue ("spots_n")->load() != shownSpots) { layoutSpots(); repaint(); }
    if (detail != nullptr && detailAer >= 0)
    {
        const auto e = proc.aerEq (detailAer);
        if (e != shownEq) { shownEq = e; detail->repaint(); }
    }
}

void Console::showDetail (std::unique_ptr<Detail> d)
{
    detail = std::move (d);
    addAndMakeVisible (*detail);
    detail->setBounds (rConsole);
}

static std::function<void()> closer (Console* c, std::unique_ptr<Detail>& slot, int& aerIdx)
{
    return [c, &slot, &aerIdx] {
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<Console> (c), &slot, &aerIdx] {
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

void Console::openMic (int m)
{
    const char* names[3] = { "MICRO 1 G", "MICRO 2 D", "MICRO 3 C" };
    auto d = std::make_unique<Detail> (u ("ΔAB · ") + names[m], closer (this, detail, detailAer));
    auto& s = proc.apvts;
    d->row ("source");
    d->choice (s, mid (m, "xsrc"), "source X");
    d->choice (s, mid (m, "ysrc"), "source Y");
    d->toggle (s, mid (m, "swap"), u ("inverser X/Y (6 can.)"));
    d->row ("gain match");
    d->knob (s, mid (m, "match"), "gain Y");
    d->toggle (s, mid (m, "analyse"), "analyser X/Y");
    d->row ("xover / switch");
    d->choice (s, mid (m, "xover"), "coupure");
    d->choice (s, mid (m, "lows"), "dans le grave");
    d->row ("focus / bloom");
    d->knob (s, mid (m, "focus"), "focus (aigu)");
    d->knob (s, mid (m, "bloom"), "bloom (grave)");
    d->knob (s, mid (m, "delay"), u ("délai"));
    d->knob (s, mid (m, "level"), "niveau sortie");
    panRow (*d, s, mid (m, ""));
    showDetail (std::move (d));
}

void Console::openAer (int a)
{
    auto d = std::make_unique<Detail> (u ("ΔAER · ") + aerTitle[a], closer (this, detail, detailAer));
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
        g.setColour (kDim);
        for (double f : { 100.0, 1000.0, 10000.0 }) g.drawVerticalLine ((int) xOf (f), (float) r.getY(), (float) r.getBottom());
        for (double dB : { -12.0, 0.0, 12.0 }) g.drawHorizontalLine ((int) yOf (dB), (float) r.getX(), (float) r.getRight());
        g.setColour (kGrey);
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
        g.strokePath (path, juce::PathStrokeType (2.0f));
    };
    detailAer = a;
    shownEq = proc.aerEq (a);
    showDetail (std::move (d));
}

void Console::openSpot (int k)
{
    auto d = std::make_unique<Detail> ("EN-MS." + juce::String (k + 1).paddedLeft ('0', 2) + u (" · CARDIO (M) + RUBAN (S)"), closer (this, detail, detailAer));
    auto& s = proc.apvts;
    d->row ("gain match");
    d->knob (s, sid (k, "match"), "gain ruban");
    d->row ("M/S ou xover");
    d->choice (s, sid (k, "mode"), "mode");
    d->toggle (s, sid (k, "invert"), u ("ruban côté opposé"));
    d->choice (s, sid (k, "xover"), "coupure (xover)");
    d->choice (s, sid (k, "lows"), "dans le grave");
    d->row ("level M / S");
    d->knob (s, sid (k, "cardio"), "level M");
    d->knob (s, sid (k, "ribbon"), "level S");
    d->row (u ("somme / stéréo"));
    d->choice (s, sid (k, "out"), "sortie M/S");
    d->knob (s, sid (k, "width"), "largeur");
    d->row (u ("délai / fader"));
    d->knob (s, sid (k, "delay"), u ("délai"), true);
    d->knob (s, sid (k, "fader"), "fader");
    panRow (*d, s, sid (k, ""));
    showDetail (std::move (d));
}

void Console::paint (juce::Graphics& g)
{
    g.fillAll (kBlack);
    g.setColour (kWhite);
    auto mono = [] (float h, int style) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, style)); };
    g.setFont (mono (28.0f, juce::Font::bold));
    g.drawText (u ("ΔAB"), 16, 8, 200, 34, juce::Justification::centredLeft);
    g.setFont (mono (10.0f, juce::Font::bold));
    g.drawText (u ("ΔAB · ΔAER · EN-MS"), 16, 44, 260, 14, juce::Justification::centredLeft);
    g.drawLine ((float) rTop.getX(), (float) rTop.getBottom() + 4, (float) rTop.getRight(), (float) rTop.getBottom() + 4, 2.0f);

    auto group = [&] (juce::Rectangle<int> r, const juce::String& name)
    {
        g.setColour (kWhite);
        g.drawRect (r, 2);
        g.setFont (mono (20.0f, juce::Font::bold));
        g.drawText (name, r.getX() + 10, r.getY() + 6, r.getWidth() - 20, 24, juce::Justification::centredLeft);
    };
    group (rAb, u ("ΔAB"));
    group (rAer, u ("ΔAER"));
    group (rSpots, "EN-MS");
    g.setFont (mono (11.0f, juce::Font::bold));
    g.setFont (mono (13.0f, juce::Font::bold));
    g.drawText ("ImagoMusica", rSign, juce::Justification::centredRight);
    g.setFont (mono (11.0f, juce::Font::bold));
    g.drawText (juce::String (shownSpots) + " TRANCHES", rSpots.getX() + 90, rSpots.getY() + 12, 120, 16, juce::Justification::centredLeft);
}

void Console::resized()
{
    auto r = getLocalBounds().reduced (12);
    rTop = r.removeFromTop (66);
    {
        auto g = rTop.withTrimmedLeft (230).withTrimmedTop (2);
        for (auto* c : { &phase, &quality, &slope, &focal, &panLaw, &panMax, &centreSend, &bloomMode })
        {
            c->setBounds (g.removeFromLeft (112).withHeight (34));
            g.removeFromLeft (8);
        }
        ambiance.setBounds (g.removeFromLeft (100).withHeight (22).translated (0, 12));
        g.removeFromLeft (12);
        focus.setBounds (g.removeFromLeft (76).withHeight (66));
        bloom.setBounds (g.removeFromLeft (76).withHeight (66));
    }
    r.removeFromTop (14);
    rConsole = r;

    r.removeFromBottom (20);   // signature line
    rSign = r.withY (r.getBottom()).withHeight (20);
    auto groupW = [] (int strips, int w = kStripW) { return strips * w + (strips - 1) * kGap + 2 * kPad; };
    rAb = r.removeFromLeft (groupW (3, kAbW));   r.removeFromLeft (12);
    rAer = r.removeFromLeft (groupW (3));  r.removeFromLeft (12);
    rSpots = r;

    auto placeStrips = [] (juce::Rectangle<int> g, std::unique_ptr<Strip>* st, int n, int w = kStripW)
    {
        auto b = g.reduced (kPad).withTrimmedTop (kHead - kPad);
        for (int i = 0; i < n; ++i) { st[i]->setBounds (b.removeFromLeft (w)); b.removeFromLeft (kGap); }
    };
    placeStrips (rAb, ab, 3, kAbW);
    placeStrips (rAer, aer, 3);
    aerPhase.setBounds (rAer.getX() + 90, rAer.getY() + 10, 104, 20);
    aerLink.setBounds (rAer.getRight() - kPad - 96, rAer.getY() + 10, 96, 20);

    spotView.setBounds (rSpots.reduced (kPad).withTrimmedTop (kHead - kPad));
    remove1.setBounds (rSpots.getRight() - kPad - 3 * 44 - 8, rSpots.getY() + 10, 40, 22);
    add4.setBounds (rSpots.getRight() - kPad - 2 * 44, rSpots.getY() + 10, 40, 22);
    add8.setBounds (rSpots.getRight() - kPad - 44, rSpots.getY() + 10, 40, 22);
    layoutSpots();
    if (detail) detail->setBounds (rConsole);
}

} // namespace ui

// ------------------------------------------------------------------ editor
DeltaABEditor::DeltaABEditor (DeltaABProcessor& p)
    : AudioProcessorEditor (p), console (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (console);
    // the whole console scales with the window, aspect ratio kept
    setResizable (true, true);
    setResizeLimits (ui::Console::kW / 2, ui::Console::kH / 2, ui::Console::kW * 2, ui::Console::kH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) ui::Console::kW / ui::Console::kH);
    setSize (ui::Console::kW * 85 / 100, ui::Console::kH * 85 / 100);
}

DeltaABEditor::~DeltaABEditor()
{
    setLookAndFeel (nullptr);
}

void DeltaABEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colours::black); }

void DeltaABEditor::resized()
{
    const float sc = (float) getWidth() / (float) ui::Console::kW;
    console.setBounds (0, 0, ui::Console::kW, ui::Console::kH);
    console.setTransform (juce::AffineTransform::scale (sc));
}
