#include "PluginEditor.h"

using namespace juce;
namespace ui
{
static const Colour kBg (0xff000000), kFg (0xffffffff), kGrey (0xff8c8c8c), kDim (0xff3a3a3a), kPanel (0xff0b0b0b);

Font mono (float h, bool bold)
{
    return Font (FontOptions (Font::getDefaultMonospacedFontName(), h, bold ? Font::bold : Font::plain));
}
String u8 (const char* s) { return String::fromUTF8 (s); }
void drawNagraCup (Graphics&, Point<float> c, float R, float a, bool hot);

// ------------------------------------------------------------------ look
Look::Look()
{
    setColour (ComboBox::textColourId, kFg);
    setColour (ComboBox::backgroundColourId, kBg);
    setColour (ComboBox::outlineColourId, kGrey);
    setColour (ComboBox::arrowColourId, kFg);
    setColour (PopupMenu::backgroundColourId, kBg);
    setColour (PopupMenu::textColourId, kFg);
    setColour (PopupMenu::highlightedBackgroundColourId, kFg);
    setColour (PopupMenu::highlightedTextColourId, kBg);
}

// Nagra style: flat bar knob with a pointed tip, sunk in a round cup, graduations engraved around it.
void Look::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s)
{
    const auto r = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const int npos = (int) s.getProperties().getWithDefault ("positions", 0);
    // rotary selector: fewer, bigger detents with their names engraved around, smaller cup
    float Rt = jmin (r.getWidth(), r.getHeight()) * 0.5f - (npos == 2 ? 9.0f : 2.0f);
    const auto c = r.getCentre().translated (0, npos == 2 ? 4.0f : 0.0f);
    const float R = Rt - 6.0f;
    if (npos > 0)
    {
        const auto names = StringArray::fromTokens (s.getProperties()["labels"].toString(), "|", "");
        const int cur = roundToInt (pos * (float) (npos - 1));
        g.setFont (mono (7.5f, true));
        for (int i = 0; i < npos; ++i)
        {
            const float a = npos > 1 ? a0 + (a1 - a0) * (float) i / (float) (npos - 1) : a0;
            g.setColour (i == cur ? kFg : kGrey);
            g.drawLine ({ c.getPointOnCircumference (R + 1.5f, a), c.getPointOnCircumference (Rt, a) }, i == cur ? 1.6f : 1.0f);
            // names left of the knob end at their detent, names right of it start there: nothing is clipped
            if (npos > 2) continue;   // 3 positions or more: the chosen name is written under the selector
            const auto tp = c.getPointOnCircumference (Rt + 9.0f, a);
            g.drawText (names[i], Rectangle<float> (60.0f, 10.0f).withCentre (tp.translated (0, -2.0f)), Justification::centred);
        }
    }
    else for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (a1 - a0) * (float) i / 10.0f;
        const bool major = i % 5 == 0;
        g.setColour (major ? kFg : kGrey);
        g.drawLine ({ c.getPointOnCircumference (R + 1.5f, a), c.getPointOnCircumference (major ? Rt : Rt - 2.5f, a) }, major ? 1.4f : 1.0f);
    }
    drawNagraCup (g, c, R, a0 + pos * (a1 - a0), s.isMouseOverOrDragging());
}

// grey level lit from the top left, for a face whose outward normal points at angle 'na' (screen radians, 0 = up)
static Colour lit (float na, float base, float amount)
{
    const float lx = -0.55f, ly = -0.83f;                          // light direction (towards top left)
    const float d = std::sin (na) * lx + (-std::cos (na)) * ly;     // normal (sin, -cos) . light
    return Colour::greyLevel (jlimit (0.0f, 1.0f, base + amount * d));
}

// Nagra recessed control: polished lip, deep well with a shaded wall, brushed floor, and a faceted chrome
// bar lever across the well with a wedge-pointed nose, casting its shadow on the floor.
void drawNagraCup (Graphics& g, Point<float> c, float R, float a, bool hot)
{
    auto disc = [&] (float rad, Point<float> at) { return Rectangle<float> (2 * rad, 2 * rad).withCentre (at); };
    // lip of the plate around the hole: bright towards the light, dark away from it
    g.setGradientFill (ColourGradient (Colour (0xffe6e6e6), c.x - R * 0.7f, c.y - R * 0.7f, Colour (0xff2c2c2c), c.x + R * 0.7f, c.y + R * 0.7f, false));
    g.fillEllipse (disc (R, c));
    // inner wall of the well: in shadow on the lit side, catching light opposite
    const float Rw = R * 0.92f;
    g.setGradientFill (ColourGradient (Colour (0xff141414), c.x - R * 0.5f, c.y - R * 0.6f, Colour (0xffb4b4b4), c.x + R * 0.5f, c.y + R * 0.7f, false));
    g.fillEllipse (disc (Rw, c));
    // floor, seen a little from above: shifted towards the light, brushed with fine concentric rings
    const float Rf = R * 0.80f; const auto cf = c.translated (R * 0.03f, R * 0.05f);
    g.setGradientFill (ColourGradient (Colour (0xffa8a8a8), cf.x - Rf * 0.3f, cf.y - Rf * 0.4f, Colour (0xff4a4a4a), cf.x + Rf, cf.y + Rf, true));
    g.fillEllipse (disc (Rf, cf));
    for (float k = 0.25f; k < 1.0f; k += 0.09f) { g.setColour (Colours::white.withAlpha (0.05f)); g.drawEllipse (disc (Rf * k, cf), 0.6f); }
    // lever, in its own frame: nose towards -y, rounded tail towards +y
    const float L = R * 0.86f, W = R * 0.36f, nose = W * 1.25f;
    auto xf = AffineTransform::rotation (a).translated (c);
    Path body;   // outline
    body.startNewSubPath (0, -L);
    body.lineTo (W, -L + nose); body.lineTo (W, L * 0.62f);
    body.quadraticTo (W, L * 0.86f, 0, L * 0.86f); body.quadraticTo (-W, L * 0.86f, -W, L * 0.62f);
    body.lineTo (-W, -L + nose); body.closeSubPath();
    // shadow on the floor, away from the light
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillPath (body, xf.translated (R * 0.07f, R * 0.11f));
    // two long facets meeting on a ridge, and two small facets forming the nose
    const float ridge = W * 0.18f;
    Path leftF, rightF, noseL, noseR;
    leftF.startNewSubPath (-ridge, -L + nose * 0.85f); leftF.lineTo (-ridge, L * 0.70f); leftF.quadraticTo (-W * 0.6f, L * 0.86f, -W, L * 0.62f); leftF.lineTo (-W, -L + nose); leftF.closeSubPath();
    rightF.startNewSubPath (ridge, -L + nose * 0.85f); rightF.lineTo (ridge, L * 0.70f); rightF.quadraticTo (W * 0.6f, L * 0.86f, W, L * 0.62f); rightF.lineTo (W, -L + nose); rightF.closeSubPath();
    Path top; top.addRectangle (-ridge, -L + nose * 0.85f, 2 * ridge, L * 0.70f + L - nose * 0.85f);
    noseL.startNewSubPath (0, -L); noseL.lineTo (-ridge, -L + nose * 0.85f); noseL.lineTo (-W, -L + nose); noseL.closeSubPath();
    noseR.startNewSubPath (0, -L); noseR.lineTo (W, -L + nose); noseR.lineTo (ridge, -L + nose * 0.85f); noseR.closeSubPath();
    const float hb = hot ? 0.08f : 0.0f;
    g.setColour (lit (a - MathConstants<float>::halfPi, 0.62f + hb, 0.30f)); g.fillPath (leftF, xf);
    g.setColour (lit (a + MathConstants<float>::halfPi, 0.62f + hb, 0.30f)); g.fillPath (rightF, xf);
    g.setColour (Colour::greyLevel (0.86f + hb)); g.fillPath (top, xf);
    g.setColour (lit (a - 0.6f, 0.70f + hb, 0.30f)); g.fillPath (noseL, xf);
    g.setColour (lit (a + 0.6f, 0.70f + hb, 0.30f)); g.fillPath (noseR, xf);
    // index groove along the nose, and a crisp edge
    Path groove; groove.startNewSubPath (0, -L + nose * 0.5f); groove.lineTo (0, -L * 0.15f);
    g.setColour (Colour (0xff111111)); g.strokePath (groove, PathStrokeType (jmax (1.0f, W * 0.16f)), xf);
    g.setColour (Colours::white.withAlpha (0.55f)); g.strokePath (body, PathStrokeType (0.7f), xf);
}

// Toggle switch: chrome nut with a bat lever, thrown towards ON or OFF (engraved beside it).
static void drawLever (Graphics& g, Rectangle<float> r, bool on, bool over, bool vertical)
{
    g.setFont (mono (8.5f, true));
    Point<float> c; Point<float> tip;
    if (vertical)
    {
        g.setColour (on ? kFg : kGrey);  g.drawText ("ON",  r.removeFromTop (10.0f), Justification::centred);
        g.setColour (on ? kGrey : kFg);  g.drawText ("OFF", r.removeFromBottom (10.0f), Justification::centred);
        c = r.getCentre(); const float L = r.getHeight() * 0.42f;
        tip = c.translated (0, on ? -L : L);
    }
    else
    {
        g.setColour (on ? kGrey : kFg);  g.drawText ("OFF", r.removeFromLeft (17.0f), Justification::centred);
        g.setColour (on ? kFg : kGrey);  g.drawText ("ON",  r.removeFromRight (13.0f), Justification::centred);
        c = r.getCentre(); const float L = r.getWidth() * 0.42f;
        tip = c.translated (on ? L : -L, 0);
    }
    const float nut = jmin (r.getWidth(), r.getHeight()) * 0.34f;
    auto disc = [&] (float rad, Point<float> at) { return Rectangle<float> (2 * rad, 2 * rad).withCentre (at); };
    // round bezel sunk in the plate (Nagra recorder style), dark hole with the bushing
    g.setGradientFill (ColourGradient (Colour (0xffe0e0e0), c.x - nut, c.y - nut, Colour (0xff303030), c.x + nut, c.y + nut, false));
    g.fillEllipse (disc (nut * 1.25f, c));
    g.setGradientFill (ColourGradient (Colour (0xff101010), c.x - nut, c.y - nut, Colour (0xff9a9a9a), c.x + nut, c.y + nut, false));
    g.fillEllipse (disc (nut * 1.05f, c));
    g.setColour (Colour (0xff1c1c1c)); g.fillEllipse (disc (nut * 0.62f, c));
    // bat lever: tapered chrome shaft with a ball end, shadow first
    const float w0 = jmax (2.0f, nut * 0.50f), w1 = jmax (3.0f, nut * 0.80f);
    const auto d = (tip - c) / jmax (1.0f, c.getDistanceFrom (tip)); const Point<float> n (-d.y, d.x);
    Path bat; bat.startNewSubPath (c + n * (w0 * 0.5f)); bat.lineTo (tip + n * (w1 * 0.5f)); bat.lineTo (tip - n * (w1 * 0.5f)); bat.lineTo (c - n * (w0 * 0.5f)); bat.closeSubPath();
    bat.addEllipse (disc (w1 * 0.68f, tip));
    g.setColour (Colours::black.withAlpha (0.5f)); g.fillPath (bat, AffineTransform::translation (1.2f, 1.8f));
    g.setGradientFill (ColourGradient (Colour (over ? 0xffffffff : 0xfff2f2f2), c.x - n.x * w1, c.y - n.y * w1, Colour (0xff6a6a6a), c.x + n.x * w1, c.y + n.y * w1, false));
    g.fillPath (bat);
    g.setColour (Colours::white); g.fillEllipse (disc (w1 * 0.18f, tip.translated (-w1 * 0.2f, -w1 * 0.2f)));
    g.setColour (Colour (0xff2a2a2a)); g.strokePath (bat, PathStrokeType (0.6f));
}

void Look::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool)
{
    if ((bool) b.getProperties().getWithDefault ("lever", false))
    {
        drawLever (g, b.getLocalBounds().toFloat().reduced (1.0f), b.getToggleState(), over, b.getHeight() > b.getWidth());
        return;
    }
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    if (b.getToggleState()) { g.setColour (kFg); g.fillRect (r); }
    else { g.setColour (over ? kFg : kGrey); g.drawRect (r, 1.0f); }
}

void Look::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    if ((bool) b.getProperties().getWithDefault ("lever", false)) return;
    g.setFont (mono (10.5f, true));
    g.setColour (b.getToggleState() ? kBg : kFg);
    g.drawText (b.getButtonText(), b.getLocalBounds(), Justification::centred);
}

void Look::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& cb)
{
    g.setColour (kBg); g.fillRect (0, 0, w, h);
    g.setColour (cb.isMouseOver() ? kFg : kGrey); g.drawRect (0, 0, w, h, 1);
    Path tri; tri.addTriangle ((float) w - 11, (float) h * 0.4f, (float) w - 5, (float) h * 0.4f, (float) w - 8, (float) h * 0.65f);
    g.setColour (kFg); g.fillPath (tri);
}
Font Look::getComboBoxFont (ComboBox&) { return mono (10.5f, true); }
void Look::positionComboBoxText (ComboBox& cb, Label& l)
{
    l.setBounds (2, 0, cb.getWidth() - 14, cb.getHeight());
    l.setFont (getComboBoxFont (cb)); l.setJustificationType (Justification::centred);
}
void Look::drawPopupMenuBackground (Graphics& g, int w, int h) { g.fillAll (kBg); g.setColour (kGrey); g.drawRect (0, 0, w, h); }

// ------------------------------------------------------------------ controls
Control::Control (AudioProcessorValueTreeState& s, Kind k, const char* id, const char* lbl)
    : kind (k), label (u8 (lbl)), param (s.getParameter (id))
{
    jassert (param != nullptr);
    if (kind == knob)
    {
        slider.setSliderStyle (Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
        slider.setMouseDragSensitivity (260);
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        addAndMakeVisible (slider);
        sAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
        slider.onValueChange = [this] { repaint(); };
        slider.onDragStart = slider.onDragEnd = [this] { repaint(); };
    }
    else if (kind == toggle)
    {
        button.setButtonText (label);
        button.getProperties().set ("lever", true);
        button.setClickingTogglesState (true);
        addAndMakeVisible (button);
        bAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (s, id, button);
    }
    else
    {
        // rotary selector (choices and switches that are not a bypass)
        StringArray names;
        if (auto* c = dynamic_cast<AudioParameterChoice*> (param)) names = c->choices; else names = { "OFF", "ON" };
        const int n = names.size();
        const float span = n > 2 ? jmin (MathConstants<float>::pi * 1.2f, (float) (n - 1) * MathConstants<float>::pi * 0.3f) : MathConstants<float>::pi * 0.44f;
        slider.getProperties().set ("positions", n);
        slider.getProperties().set ("labels", names.joinIntoString ("|"));
        slider.setSliderStyle (Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (MathConstants<float>::twoPi - span * 0.5f, MathConstants<float>::twoPi + span * 0.5f, true);
        slider.setMouseDragSensitivity (40 * n);
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        addAndMakeVisible (slider);
        sAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
        slider.onValueChange = [this] { repaint(); if (auto* sec = getParentComponent()) sec->repaint(); };
        slider.onDragStart = slider.onDragEnd = [this] { repaint(); };
    }
}

void Control::resized()
{
    auto r = getLocalBounds();
    if (kind == choice) { slider.setBounds (r.withTrimmedBottom ((int) slider.getProperties()["positions"] > 2 ? 26 : 14)); return; }
    if (kind == toggle && label != "ON") { button.setBounds (r.withTrimmedBottom (14).withSizeKeepingCentre (26, jmin (54, r.getHeight() - 18))); return; }
    if (kind == toggle) { button.setBounds (r); return; }
    if (kind == knob) { slider.setBounds (r.withTrimmedBottom (26).withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight() - 26), r.getHeight() - 26)); return; }
    auto box = r.withSizeKeepingCentre (r.getWidth() - 8, 20).translated (0, kind == choice ? 6 : 0);
    if (kind == toggle) button.setBounds (box); else combo.setBounds (box);
}

void Control::paint (Graphics& g)
{
    if (kind == choice && (int) slider.getProperties()["positions"] > 2)
    {
        auto r = getLocalBounds().removeFromBottom (26);
        g.setColour (kFg); g.setFont (mono (9.5f, true));
        g.drawText (label, r.removeFromTop (12), Justification::centred);
        g.setColour (slider.isMouseOverOrDragging() ? kFg : kGrey); g.setFont (mono (9.5f));
        g.drawText (param->getCurrentValueAsText(), r, Justification::centred);
        return;
    }
    if (kind == choice || (kind == toggle && label != "ON"))
    {
        g.setColour (kFg); g.setFont (mono (9.5f, true));
        g.drawText (label, getLocalBounds().removeFromBottom (13), Justification::centred);
        return;
    }
    if (kind != knob) return;
    auto r = getLocalBounds().removeFromBottom (26);
    g.setColour (kFg); g.setFont (mono (9.5f, true));
    g.drawText (label, r.removeFromTop (12), Justification::centred);
    const bool live = slider.isMouseOverOrDragging();
    g.setColour (live ? kFg : kGrey); g.setFont (mono (9.5f));
    const auto txt = param->getCurrentValueAsText();
    g.drawText (txt == "OFF" ? txt : txt + " " + param->getLabel(), r, Justification::centred);
}

// ------------------------------------------------------------------ sections
Section::Section (AudioProcessorValueTreeState& s, const char* t, const char* onId, int c,
                  std::vector<std::tuple<Control::Kind, const char*, const char*>> items)
    : title (u8 (t)), cols (c)
{
    if (onId != nullptr) { on = std::make_unique<Control> (s, Control::toggle, onId, "ON"); addAndMakeVisible (*on); }
    for (auto& [k, id, lbl] : items) addAndMakeVisible (controls.add (new Control (s, k, id, lbl)));
}

void Section::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (kPanel); g.fillRect (r);
    g.setColour (kDim); g.drawRect (r, 1.0f);
    g.setColour (kFg); g.setFont (mono (11.0f, true));
    float tx = 8.0f;
    if (marking.isNotEmpty())
    {   // score dynamics: bold italic serif, as printed under the staff
        const Font dyn (FontOptions (Font::getDefaultSerifFontName(), 19.0f, Font::bold | Font::italic));
        g.setFont (dyn);
        const float mw = GlyphArrangement::getStringWidth (dyn, marking);
        g.drawText (marking, Rectangle<float> (8.0f, 1.0f, mw + 4.0f, 22.0f), Justification::centredLeft);
        tx += mw + 10.0f;
        g.setFont (mono (11.0f, true));
    }
    g.drawText (title, (int) tx, 5, getWidth() - 62 - (int) tx, 14, Justification::centredLeft);
    g.drawHorizontalLine (24, 6.0f, (float) getWidth() - 6.0f);
    if (gain)
    {   // one-line meter: track with dB ticks, bar, peak hold, value
        const float x0 = 30.0f, x1 = (float) getWidth() - 62.0f, y = 27.0f, h = 9.0f, w = x1 - x0;
        g.setColour (kGrey); g.setFont (mono (8.5f, true));
        g.drawText (expand ? "GE" : bipolar ? "G" : "GR", 6, (int) y - 1, 22, 11, Justification::centredLeft);
        g.setColour (Colour (0xff050505)); g.fillRect (x0, y, w, h);
        g.setColour (kDim); g.drawRect (x0, y, w, h, 1.0f);
        auto xOf = [&] (float db) {
            return bipolar ? x0 + w * 0.5f + jlimit (-1.0f, 1.0f, db / range) * w * 0.5f
                 : expand  ? x0 + jlimit (0.0f, 1.0f, db / range) * w      // gain expansion: same meter, grows from the left
                           : x1 - jlimit (0.0f, 1.0f, -db / range) * w;
        };
        const float step = range > 12 ? 5.0f : 3.0f;
        for (float d = expand ? 0.0f : -range; d <= (bipolar || expand ? range : 0.0f) + 0.01f; d += step)
        { g.setColour (std::abs (d) < 0.01f ? kGrey : kDim); g.drawVerticalLine ((int) xOf (d), y + (std::abs (d) < 0.01f ? 0.0f : h - 3.0f), y + h); }
        const float z = xOf (0.0f), v = xOf (shownGain);
        g.setColour (shownGain >= 0 ? Colour (0xffbdbdbd) : kFg);
        g.fillRect (Rectangle<float>::leftTopRightBottom (jmin (z, v), y + 2.0f, jmax (z, v), y + h - 2.0f));
        g.setColour (kFg); g.fillRect (xOf (holdGain) - 0.75f, y + 1.0f, 1.5f, h - 2.0f);
        g.setFont (mono (9.5f, true));
        g.drawText ((shownGain > 0.05f ? "+" : "") + String (shownGain, 1), (int) x1 + 4, (int) y - 2, 52, 12, Justification::centredRight);
        if (shown.contains ("@")) { g.setColour (kGrey); g.setFont (mono (8.5f)); g.drawText (shown.fromFirstOccurrenceOf ("@", true, false), (int) x1 - 70, (int) y - 13, 70, 11, Justification::centredRight); }
    }
    else if (shown.isNotEmpty()) { g.setColour (kGrey); g.setFont (mono (9.5f)); g.drawText (shown, 8, 26, getWidth() - 16, 12, Justification::centredLeft); }
    if (hairpin)
    {   // hairpin as printed in a score: opens to the right (crescendo) when louder, closes (diminuendo) when quieter
        const float x0 = getWidth() * 0.38f, x1 = getWidth() - 60.0f, yc = 32.0f;
        const float open = jlimit (0.0f, 1.0f, std::abs (hairShown) / 12.0f) * 6.0f;
        g.setColour (std::abs (hairShown) < 0.2f ? kDim : kFg);
        if (hairShown >= 0) { g.drawLine (x0, yc, x1, yc - open, 1.6f); g.drawLine (x0, yc, x1, yc + open, 1.6f); }
        else { g.drawLine (x0, yc - open, x1, yc, 1.6f); g.drawLine (x0, yc + open, x1, yc, 1.6f); }
        g.setFont (Font (FontOptions (Font::getDefaultSerifFontName(), 12.0f, Font::bold | Font::italic)));
        g.drawText (hairShown >= 0 ? "cresc." : "dim.", (int) x0 - 46, 25, 44, 14, Justification::centredRight);
        g.setFont (mono (9.5f, true));
        g.drawText ((hairShown > 0.05f ? "+" : "") + String (hairShown, 1), (int) x1 + 4, 26, 52, 12, Justification::centredRight);
    }
    const bool off = on != nullptr && ! on->button.getToggleState();
    if (off) { g.setColour (kBg.withAlpha (0.55f)); g.fillRect (r.withTrimmedTop (25).reduced (1)); }
}

void Section::tickGain (float dt)
{
    if (hairpin)
    {
        const float v = hairpin();
        if (std::isfinite (v)) hairShown += (1.0f - std::exp (-dt / 0.5f)) * (v - hairShown);
        if (! gain) repaint (0, 24, getWidth(), 16);
    }
    if (! gain) return;
    const float v = gain();
    // meter ballistics: instant attack, 300 ms return; peak hold 1.5 s
    const bool more = std::abs (v) > std::abs (shownGain);
    shownGain = more ? v : v + (shownGain - v) * std::exp (-dt / 0.3f);
    if (std::abs (v) >= std::abs (holdGain)) { holdGain = v; holdTime = 0; }
    else if ((holdTime += dt) > 1.5f) holdGain = v;
    repaint (0, 24, getWidth(), 16);
}

Rectangle<int> Section::cell (int i) const
{
    auto r = getLocalBounds().reduced (4, 0).withTrimmedTop (38).withTrimmedBottom (4);
    const int rows = jmax ((int) controls.size(), extra != nullptr ? extraCell + extraSpan : 0);
    const int nr = (rows + cols - 1) / cols;
    const int cw = r.getWidth() / cols, rh = jmin (84, r.getHeight() / jmax (1, nr));
    return { r.getX() + (i % cols) * cw, r.getY() + (i / cols) * rh, cw, rh };
}

void Section::resized()
{
    if (extra != nullptr) extra->setBounds (cell (extraCell).getUnion (cell (extraCell + extraSpan - 1)).reduced (4, 6));
    if (on) on->setBounds (getWidth() - 54, 2, 50, 20);
    auto r = getLocalBounds().reduced (4, 0).withTrimmedTop (38).withTrimmedBottom (4);
    const int rows = ((int) controls.size() + cols - 1) / cols;
    const int cw = r.getWidth() / cols, rh = jmin (84, r.getHeight() / jmax (1, rows));
    for (int i = 0; i < controls.size(); ++i)
        controls[i]->setBounds (r.getX() + (i % cols) * cw, r.getY() + (i / cols) * rh, cw, rh);
}

// ------------------------------------------------------------------ sand
static constexpr float kTop = 0.f, kBottom = -60.f;

float Sand::yOf (float lufs) const
{
    const float h = (float) getHeight() - 84.0f;
    return 26.0f + jlimit (0.0f, 1.0f, (kTop - lufs) / (kTop - kBottom)) * (h - 26.0f);
}

Sand::Sand (DeltaCompProcessor& p) : proc (p)
{
    std::uniform_real_distribution<float> U (0, 1);
    gx.resize (kGrains); gy.resize (kGrains); gz.assign (kGrains, 0.0f); gv.assign (kGrains, 0.0f);
    for (int i = 0; i < kGrains; ++i) { gx[(size_t) i] = U (rng); gy[(size_t) i] = U (rng); }
    spec.resize (kFFT);
    setOpaque (true);
}

void Sand::resized()
{
    const float W = (float) getWidth(), H = (float) getHeight();
    plateArea = { 8.0f, 24.0f, W - 150.0f - 8.0f, H - 24.0f - (float) kBand - 4.0f };
    plate = Image (Image::ARGB, jmax (1, (int) plateArea.getWidth()), jmax (1, (int) plateArea.getHeight()), true);
}

// plate coordinates: x 0..1 across, y 0 (far) .. 1 (near), z height above the plate (plate widths)
Point<float> Sand::project (float x, float y, float z) const
{
    const float W = plateArea.getWidth(), H = plateArea.getHeight();
    const float Z = 1.55f + (1.0f - y) * 2.1f;              // depth from the eye
    const float camH = 1.0f, near = 1.55f;
    const float fx = 0.47f * W * near, horizon = -0.30f * H, fy = (H - 6.0f - horizon) * near / camH;
    return { W * 0.5f + fx * (x - 0.5f) * 2.0f / Z, horizon + fy * (camH - z) / Z };
}

void Sand::analyse()
{
    const int w = proc.scopeWrite.load (std::memory_order_acquire);
    double rms = 0;
    for (int i = 0; i < kFFT; ++i)
    {
        const float v = proc.scope[(size_t) ((w - kFFT + i) & (DeltaCompProcessor::kScopeSize - 1))];
        rms += (double) v * v;
        spec[(size_t) i] = v * (0.5 - 0.5 * std::cos (2 * MathConstants<double>::pi * i / kFFT));
    }
    // radix-2 FFT
    const size_t n = (size_t) kFFT;
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (spec[i], spec[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const std::complex<double> wl (std::cos (-2 * MathConstants<double>::pi / (double) len), std::sin (-2 * MathConstants<double>::pi / (double) len));
        for (size_t i = 0; i < n; i += len) { std::complex<double> wv (1); for (size_t j = 0; j < len / 2; ++j) { auto u = spec[i + j], v = spec[i + j + len / 2] * wv; spec[i + j] = u + v; spec[i + j + len / 2] = u - v; wv *= wl; } }
    }
    rms = std::sqrt (rms / kFFT);
    const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    double tot = 1e-20; std::array<double, kModes> e {};
    for (int k = 1; k < kFFT / 2; ++k)
    {
        const double f = k * fs / kFFT;
        if (f < 30) continue;
        const int band = jlimit (0, kModes - 1, (int) (std::log2 (f / 40.0) * kModes / 9.0));
        const double m = std::norm (spec[(size_t) k]);
        e[(size_t) band] += m; tot += m;
    }
    // the strongest registers dominate the figure, so the pattern changes with what is playing
    double mx = 1e-30; for (auto v : e) mx = std::max (mx, v);
    for (int bnd = 0; bnd < kModes; ++bnd) { const double r = e[(size_t) bnd] / mx; target[(size_t) bnd] = (float) (r * r); }
    (void) tot;
    // the shake follows the level of the last 85 ms against the level of the last seconds (the dynamics),
    // and an attack (level jumping up) throws the sand up at once
    const int nShort = (int) std::min<double> (kFFT, 0.085 * fs);
    double rs = 0;
    for (int i = kFFT - nShort; i < kFFT; ++i)
    {
        const float v = proc.scope[(size_t) ((w - kFFT + i) & (DeltaCompProcessor::kScopeSize - 1))];
        rs += (double) v * v;
    }
    const float db = (float) (10 * std::log10 (rs / nShort + 1e-24));
    if (db > lastDb + 3.0f && db > -60.0f) kick = jmin (1.0f, kick + (db - lastDb) / 12.0f);
    lastDb = db;
    avgDb += 0.02f * (db - avgDb);
    shakeTarget = jlimit (0.0f, 1.0f, (db - (avgDb - 18.0f)) / 30.0f) * jlimit (0.0f, 1.0f, (db + 70.0f) / 20.0f);
    (void) rms;
}

// Four terraces, one per dynamic zone (pp at the back and highest, then p, mp mf, f ff in front and lowest).
// Each terrace is a Chladni plate: the output spectrum picks its modes (a different set per terrace), the music
// shakes it, and its own stage's gain makes it work harder. The sand creeps forward and pours over each front
// edge onto the next terrace, like the signal going through the stages; from the last edge it goes back to pp.
float Sand::tierBase (float y) const { return (float) (kTiers - 1 - tierOf (y)) * kStep; }

void Sand::stepPlate()
{
    analyse();
    for (int k = 0; k < kModes; ++k) energy[(size_t) k] += 0.15f * (target[(size_t) k] - energy[(size_t) k]);
    shakeLevel += (shakeTarget > shakeLevel ? 0.6f : 0.12f) * (shakeTarget - shakeLevel);
    const float kNow = kick; kick *= 0.5f;
    for (int t = 0; t < kTiers; ++t)
    {
        act[(size_t) t] += (actTarget[(size_t) t] > act[(size_t) t] ? 0.5f : 0.08f) * (actTarget[(size_t) t] - act[(size_t) t]);
        ceilDb[(size_t) t] += 0.35f * (ceilTarget[(size_t) t] - ceilDb[(size_t) t]);
    }
    static const int modes[kModes][2] = { { 1, 2 }, { 1, 3 }, { 2, 3 }, { 1, 4 }, { 3, 4 }, { 2, 5 }, { 3, 5 }, { 4, 7 } };
    static float cx[8][kGW], cy[8][kTH]; static bool init = false;
    if (! init)
    {
        for (int m = 1; m < 8; ++m)
        {
            for (int i = 0; i < kGW; ++i) cx[m][i] = std::cos (m * MathConstants<float>::pi * (i + 0.5f) / kGW);
            for (int j = 0; j < kTH; ++j) cy[m][j] = std::cos (m * MathConstants<float>::pi * (j + 0.5f) / kTH);
        }
        init = true;
    }
    for (int t = 0; t < kTiers; ++t)
        for (int j = 0; j < kTH; ++j)
            for (int i = 0; i < kGW; ++i)
            {
                float a = 0;
                for (int k = 0; k < kModes; ++k)
                {
                    const int mk = (k + 2 * t) % kModes, m1 = modes[mk][0], m2 = modes[mk][1];
                    a += energy[(size_t) k] * (cx[m1][i] * cy[m2][j] - cx[m2][i] * cy[m1][j]);
                }
                fields[(size_t) t][(size_t) (j * kGW + i)] = std::abs (a);
            }
    std::normal_distribution<float> N (0, 1); std::uniform_real_distribution<float> U (0, 1);
    const float tierD = 1.0f / kTiers;
    for (int i = 0; i < kGrains; ++i)
    {
        float& x = gx[(size_t) i]; float& y = gy[(size_t) i]; float& z = gz[(size_t) i]; float& v = gv[(size_t) i];
        const int t = tierOf (y);
        const float base = tierBase (y), ly = (y - t * tierD) / tierD;
        const int ci = jlimit (0, kGW - 1, (int) (x * kGW)), cj = jlimit (0, kTH - 1, (int) (ly * kTH));
        const float f = fields[(size_t) t][(size_t) (cj * kGW + ci)];
        const float drive = shakeLevel * (0.35f + 0.65f * act[(size_t) t]);
        const float shake = 0.0006f + 0.02f * drive * drive;
        const float amp = f * shake + 0.00015f;
        if (z <= base)
        {   // on its terrace: kicked up where it vibrates
            z = base;
            if (U (rng) < f * (drive * 0.9f + kNow)) v = 0.004f + 0.05f * f * (drive + 1.5f * kNow) * U (rng);
        }
        if (z > base || v > 0.0f)
        {
            z += v; v -= 0.0045f;
            if (z < base) { z = base; v = 0.0f; }
            if (y < (t + 1) * tierD && z > base + ceilH (t)) { z = base + ceilH (t); v = -0.5f * std::abs (v); }   // hits the ceiling
        }
        x += amp * N (rng);
        // creep towards the front edge, faster when the music is loud; over the edge it falls to the next terrace
        y += amp * N (rng) * 1.2f + 0.0002f + 0.0012f * drive * (z <= base + 1e-6f ? 1.0f : 0.3f);
        if (x < 0) x = -x;
        if (x > 1) x = 2 - x;
        x = jlimit (0.0f, 1.0f, x);
        if (y < t * tierD) y = t * tierD + (t * tierD - y);   // a terrace's back wall
        if (y >= 1.0f) { y = U (rng) * 0.05f; z = tierBase (y) + 0.25f + 0.1f * U (rng); v = 0.0f; }   // back to pp, raining in
    }
}

void Sand::renderPlate()
{
    const int W = plate.getWidth(), H = plate.getHeight();
    Image::BitmapData bd (plate, Image::BitmapData::readWrite);
    for (int y = 0; y < H; ++y) { auto* row = bd.getLinePointer (y); for (int x = 0; x < W * bd.pixelStride; ++x) row[x] = (uint8) ((row[x] * 140) >> 8); }
    auto put = [&] (int px, int py, uint8 v) {
        if (px < 0 || py < 0 || px >= W || py >= H) return;
        auto* q = bd.getPixelPointer (px, py); for (int c = 0; c < bd.pixelStride; ++c) q[c] = jmax (q[c], v);
    };
    for (int i = 0; i < kGrains; ++i)
    {
        const float x = gx[(size_t) i], y = gy[(size_t) i], z = gz[(size_t) i];
        const float depth = 0.35f + 0.65f * y;                       // far grains dimmer and smaller
        const auto p = project (x, y, z);
        const int px = (int) p.x, py = (int) p.y;
        const float base = tierBase (y);
        if (z > base) { const auto s = project (x, y, base); put ((int) s.x, (int) s.y, (uint8) (40 * depth)); }   // shadow on its terrace
        const uint8 v = (uint8) jlimit (0.0f, 255.0f, (150.0f + (float) (i % 90)) * depth + 60.0f * jmin (1.0f, (z - base) * 12.0f));
        put (px, py, v);
        if (y > 0.55f) { put (px + 1, py, v); if (y > 0.8f) { put (px, py + 1, v); put (px + 1, py + 1, v); } }
    }
}

void Sand::push()
{
    auto& e = proc.engine;
    level[(size_t) head] = e.meterLevel.load();
    in[(size_t) head] = e.meterIn.load();
    out[(size_t) head] = e.meterOut.load();
    const float v[kLanes] = { e.meterAmb.load(), e.meterA.load(), e.meterB.load(), e.meterC.load(), e.meterD.load(), e.meterLim.load() };
    for (int k = 0; k < kLanes; ++k) lane[(size_t) k][(size_t) head] = v[k];
    grNow = -(v[2] + v[3] + v[4] + v[5]);
    for (int t = 0; t < kTiers; ++t) { actTarget[(size_t) t] = jlimit (0.0f, 1.0f, std::abs (v[t + 1]) / 6.0f); ceilTarget[(size_t) t] = v[t + 1]; }   // pp, p, mp mf, f ff
    // phase: correlation of L and R over the last 2048 samples, and a slow auto scale for the scope
    {
        const int w = proc.scopeWrite.load (std::memory_order_acquire);
        double lr = 0, ll = 0, rr = 0, pk = 1e-9;
        for (int i = 0; i < 2048; ++i)
        {
            const size_t q = (size_t) ((w - 2048 + i) & (DeltaCompProcessor::kScopeSize - 1));
            const double m = proc.scope[q], sd = proc.scopeS[q], L = m + sd, R = m - sd;
            lr += L * R; ll += L * L; rr += R * R; pk = std::max ({ pk, std::abs (m), std::abs (sd) });
        }
        corr = ll * rr > 1e-18 ? (float) (lr / std::sqrt (ll * rr)) : 0.0f;
        corrShown += 0.25f * (corr - corrShown);
        const float want = (float) jlimit (1.0, 1000.0, 0.8 / pk);
        scopeGain += (want < scopeGain ? 0.5f : 0.05f) * (want - scopeGain);
    }
    head = (head + 1) % kCols; ++frame;
    stepPlate(); renderPlate();
}

void Sand::paint (Graphics& g)
{
    g.fillAll (kBg);
    const float W = (float) getWidth(), H = (float) getHeight();
    g.setColour (kDim); g.drawRect (getLocalBounds().toFloat(), 1.0f);
    const float x0 = 38.0f, xr = W - 150.0f;

    // the plate in perspective: edge, a faint grid, then the sand
    {
        Graphics::ScopedSaveState ss (g);
        g.addTransform (AffineTransform::translation (plateArea.getX(), plateArea.getY()));
        static const char* names[kTiers] = { "pp", "p", "mp mf", "f ff" };
        for (int t = 0; t < kTiers; ++t)
        {
            const float y0 = (float) t / kTiers, y1 = (float) (t + 1) / kTiers, zb = (float) (kTiers - 1 - t) * kStep;
            Path top; top.startNewSubPath (project (0, y0, zb)); top.lineTo (project (1, y0, zb)); top.lineTo (project (1, y1, zb)); top.lineTo (project (0, y1, zb)); top.closeSubPath();
            g.setColour (Colour (0xff070707)); g.fillPath (top);
            g.setColour (kFg.withAlpha (0.05f));
            for (int k = 1; k < 8; ++k) { const float u = (float) k / 8.0f; g.drawLine (Line<float> (project (u, y0, zb), project (u, y1, zb)), 0.6f); }
            g.setColour (kFg.withAlpha (0.25f)); g.strokePath (top, PathStrokeType (1.0f));
            // riser: the front face down to the next terrace (or a slab under the last one)
            const float zn = t < kTiers - 1 ? zb - kStep : -0.02f;
            Path face; face.startNewSubPath (project (0, y1, zb)); face.lineTo (project (1, y1, zb)); face.lineTo (project (1, y1, zn)); face.lineTo (project (0, y1, zn)); face.closeSubPath();
            g.setColour (Colour (0xff1e1e1e)); g.fillPath (face);
            g.setColour (kFg.withAlpha (0.18f)); g.strokePath (face, PathStrokeType (0.8f));
            g.setColour (kFg.withAlpha (0.8f));
            g.setFont (Font (FontOptions (Font::getDefaultSerifFontName(), 16.0f, Font::bold | Font::italic)));
            const auto lp = project (0, 0.5f * (y0 + y1), zb);
            g.drawText (names[t], Rectangle<float> (lp.x - 64.0f, lp.y - 9.0f, 58.0f, 18.0f), Justification::centredRight);
        }
        // the ceilings, mirror of each terrace: pressed down by gain reduction, lifted by expansion
        for (int t = 0; t < kTiers; ++t)
        {
            const float y0 = (float) t / kTiers, y1 = (float) (t + 1) / kTiers, zc = (float) (kTiers - 1 - t) * kStep + ceilH (t);
            const float gdb = ceilDb[(size_t) t];
            Path c; c.startNewSubPath (project (0, y0, zc)); c.lineTo (project (1, y0, zc)); c.lineTo (project (1, y1, zc)); c.lineTo (project (0, y1, zc)); c.closeSubPath();
            g.setColour (kFg.withAlpha (0.035f + 0.05f * jmin (1.0f, std::abs (gdb) / 6.0f))); g.fillPath (c);
            g.setColour (kFg.withAlpha (std::abs (gdb) > 0.2f ? 0.75f : 0.3f));
            g.drawLine (Line<float> (project (0, y1, zc), project (1, y1, zc)), std::abs (gdb) > 0.2f ? 1.4f : 0.8f);
            const auto rp = project (1, y1, zc);
            g.setFont (mono (9.5f, true));
            g.drawText ((gdb > 0.05f ? "GE +" : gdb < -0.05f ? "GR " : "") + String (std::abs (gdb) < 0.05f ? 0.0f : gdb, 1), Rectangle<float> (rp.x + 6.0f, rp.y - 7.0f, 64.0f, 14.0f), Justification::centredLeft);
        }
        g.setColour (kFg);
        g.drawImageAt (plate, 0, 0, true);
    }
    g.setColour (kFg); g.setFont (mono (12.0f, true));
    g.drawText ("DELTA COMP", 12, 5, 200, 16, Justification::centredLeft);

    // bottom band: the phase scope under the sand, the five gain lanes beside it
    {
        const float by = H - (float) kBand, side = (float) kBand - 8.0f;
        paintPhase (g, { 8.0f, by, side, side });
        paintLanes (g, { 8.0f + side + 10.0f, by, xr - 8.0f - side - 10.0f, (float) kBand - 6.0f });
    }
    (void) x0;

    // dynamic range spread: two brackets, input and output, from the 10th to the 95th percentile
    g.setColour (kDim); g.drawVerticalLine ((int) xr + 6, 4.0f, H - 4.0f);
    g.setFont (mono (8.5f)); g.setColour (kFg.withAlpha (0.35f));
    for (int db = 0; db >= (int) kBottom; db -= 12) g.drawText (String (db), (int) xr + 8, (int) yOf ((float) db) - 6, 22, 12, Justification::centredLeft);
    auto bracket = [&] (float cx, float lo, float hi, const String& name, bool bright)
    {
        g.setColour (bright ? kFg : kGrey); g.setFont (mono (9.5f, true));
        g.drawText (name, (int) cx - 30, 6, 60, 12, Justification::centred);
        if (lo < -150 || hi < -150) return;
        const float y0 = yOf (hi), y1 = yOf (lo);
        g.fillRect (cx - 1.0f, y0, 2.0f, y1 - y0);
        g.fillRect (cx - 12.0f, y0 - 1.0f, 24.0f, 2.0f);
        g.fillRect (cx - 12.0f, y1 - 1.0f, 24.0f, 2.0f);
        g.setFont (mono (11.0f, true));
        g.drawText (String (hi - lo, 1), (int) cx - 30, (int) y1 + 4, 60, 14, Justification::centred);
        g.setFont (mono (8.5f)); g.drawText ("LU", (int) cx - 30, (int) y1 + 17, 60, 11, Justification::centred);
    };
    bracket (xr + 46, inLo, inHi, u8 ("ENTRÉE"), false);
    bracket (xr + 108, outLo, outHi, "SORTIE", true);
    g.setColour (kGrey); g.setFont (mono (8.5f));
    g.drawText ("DYNAMIC RANGE", (int) xr + 10, (int) H - 16, 140, 12, Justification::centred);
}

// Phase scope (goniometer): mid up, side across, L and R on the diagonals; correlation bar underneath.
void Sand::paintPhase (Graphics& g, Rectangle<float> r)
{
    const float barH = 14.0f;
    auto sq = r.withTrimmedBottom (barH + 4.0f);
    sq = sq.withSizeKeepingCentre (sq.getHeight(), sq.getHeight());
    g.setColour (Colour (0xff070707)); g.fillRect (sq);
    g.setColour (kFg.withAlpha (0.12f));
    g.drawLine (sq.getCentreX(), sq.getY(), sq.getCentreX(), sq.getBottom(), 0.6f);
    g.drawLine (sq.getX(), sq.getCentreY(), sq.getRight(), sq.getCentreY(), 0.6f);
    g.drawLine (sq.getX(), sq.getY(), sq.getRight(), sq.getBottom(), 0.6f);
    g.drawLine (sq.getRight(), sq.getY(), sq.getX(), sq.getBottom(), 0.6f);
    g.setColour (kDim); g.drawRect (sq, 1.0f);
    g.setFont (mono (8.0f)); g.setColour (kGrey);
    g.drawText ("L", sq.withSize (12, 12).translated (2, 1), Justification::centred);
    g.drawText ("R", sq.withSize (12, 12).translated (sq.getWidth() - 14, 1), Justification::centred);
    g.drawText ("M", sq.withSize (12, 12).translated (sq.getWidth() * 0.5f + 2, 1), Justification::centred);
    const int w = proc.scopeWrite.load (std::memory_order_acquire);
    const float cx = sq.getCentreX(), cy = sq.getCentreY(), half = sq.getWidth() * 0.5f - 2.0f;
    for (int i = 0; i < 2048; ++i)
    {
        const size_t q = (size_t) ((w - 2048 + i) & (DeltaCompProcessor::kScopeSize - 1));
        const float m = jlimit (-1.0f, 1.0f, proc.scope[q] * scopeGain), sd = jlimit (-1.0f, 1.0f, proc.scopeS[q] * scopeGain);
        g.setColour (kFg.withAlpha (0.12f + 0.5f * (float) i / 2048.0f));
        g.fillRect (cx - sd * half, cy - m * half, 1.2f, 1.2f);
    }
    // correlation: -1 (out of phase) .. +1 (mono)
    auto bar = Rectangle<float> (sq.getX(), r.getBottom() - barH, sq.getWidth(), barH);
    g.setColour (kDim); g.drawRect (bar, 1.0f);
    const float zx = bar.getCentreX(), px = zx + corrShown * (bar.getWidth() * 0.5f - 2.0f);
    g.setColour (corrShown < 0 ? kFg : kGrey);
    g.fillRect (Rectangle<float>::leftTopRightBottom (jmin (zx, px), bar.getY() + 3, jmax (zx, px), bar.getBottom() - 3));
    g.setColour (kFg.withAlpha (0.4f)); g.drawVerticalLine ((int) zx, bar.getY(), bar.getBottom());
    g.setFont (mono (8.0f)); g.setColour (kGrey);
    g.drawText ("-1", bar.translated (-18, 0).withWidth (16), Justification::centredRight);
    g.drawText ("+1", bar.translated (bar.getWidth() + 2, 0).withWidth (16), Justification::centredLeft);
    g.setColour (kFg); g.setFont (mono (9.0f, true));
    g.drawText ("PHASE " + String (corrShown, 2), sq.reduced (4, 3).toNearestInt(), Justification::bottomLeft);
}

// Five lanes scrolling in time: the classical zones from the bottom of the dynamics to the top.
void Sand::paintLanes (Graphics& g, Rectangle<float> r)
{
    static const char* names[kLanes] = { "AMBIANCE", "pp", "p", "mp  mf", "f  ff", "LIMITEUR" };
    static const float range[kLanes] = { 12, 12, 6, 12, 12, 6 };
    const float lh = r.getHeight() / kLanes, labelW = 64.0f, valW = 48.0f;
    const float x0 = r.getX() + labelW, x1 = r.getRight() - valW, colW = (x1 - x0) / kCols;
    for (int k = 0; k < kLanes; ++k)
    {
        const float top = r.getY() + k * lh, h = lh - 4.0f;
        const bool upLane = k <= 1;
        const float base = upLane ? top + h : top;     // upward grows up from the bottom, reductions hang from the top
        g.setColour (Colour (0xff070707)); g.fillRect (x0, top, x1 - x0, h);
        g.setColour (kFg.withAlpha (0.2f)); g.drawHorizontalLine ((int) base, x0, x1);
        float now = 0;
        for (int i = 0; i < kCols; ++i)
        {
            const int idx = (head + i) % kCols;
            const float v = lane[(size_t) k][(size_t) idx];
            const float a = jlimit (0.0f, 1.0f, (upLane ? v : -v) / range[k]) * h;
            if (a > 0.3f)
            {
                g.setColour (upLane ? kGrey : kFg);
                g.fillRect (x0 + i * colW, upLane ? base - a : base, jmax (1.0f, colW), a);
            }
            now = v;
        }
        g.setColour (kFg);
        if (k >= 1 && k <= 4) g.setFont (Font (FontOptions (Font::getDefaultSerifFontName(), 15.0f, Font::bold | Font::italic)));
        else g.setFont (mono (9.0f, true));
        g.drawText (names[k], (int) r.getX(), (int) top, (int) labelW - 4, (int) h, Justification::centredRight);
        g.setFont (mono (10.0f, true));
        g.drawText ((now > 0.05f ? "+" : "") + String (now, 1), (int) x1 + 4, (int) top, (int) valW - 4, (int) h, Justification::centredLeft);
        g.setFont (mono (7.5f)); g.setColour (kGrey);
        g.drawText (String ((int) range[k]), (int) x1 - 22, (int) (upLane ? top : top + h - 10), 20, 10, Justification::centredRight);
    }
}

// ------------------------------------------------------------------ meter
static float ampDb (double a) { return a > 1e-12 ? (float) (20.0 * std::log10 (a)) : -200.0f; }
static float powDb (double p) { return p > 1e-24 ? (float) (10.0 * std::log10 (p)) : -200.0f; }

void Meter::tick (float dt)
{
    const auto m = proc.engine.takeMeter();
    if (m.n > 0)
    {
        const float aV = std::exp (-dt / 0.3f), a3 = std::exp (-dt / 3.0f);
        for (int c = 0; c < 2; ++c) vuPow[c] = aV * vuPow[c] + (1 - aV) * (float) (m.sumSq[c] / m.n);   // VU: 300 ms
        const double ms = 0.5 * (m.sumSq[0] + m.sumSq[1]) / m.n;
        rms3Pow = a3 * rms3Pow + (1 - a3) * (float) ms;
        if (ms > 1e-10) { longSum += ms * m.n; longN += m.n; }
        // crest factor over 1 s windows
        cwInPk = std::max ({ cwInPk, m.inPeak[0], m.inPeak[1] }); cwOutPk = std::max ({ cwOutPk, m.peak[0], m.peak[1] });
        cwInSq += 0.5 * (m.inSumSq[0] + m.inSumSq[1]); cwOutSq += 0.5 * (m.sumSq[0] + m.sumSq[1]); cwN += m.n;
    }
    cwT += dt;
    if (cwT >= 1.0f && cwN > 0)
    {
        if (cwInSq / cwN > 1e-9) crestIn = ampDb (cwInPk) - powDb (cwInSq / cwN);
        if (cwOutSq / cwN > 1e-9) crestOut = ampDb (cwOutPk) - powDb (cwOutSq / cwN);
        cwInPk = cwOutPk = cwInSq = cwOutSq = 0; cwN = 0; cwT = 0;
    }
    for (int c = 0; c < 2; ++c) ppm[c] = jmax (ampDb (m.peak[c]), ppm[c] - 20.0f * dt / 1.7f);   // Nagra peak: 20 dB in 1.7 s
    {   // global gain reduction (p + mp mf + f ff + limiter): slow needle 300 ms, fast needle instant with the Nagra return
        auto& eg = proc.engine;
        const float grNow = jmin (0.0f, eg.meterB.load() + eg.meterC.load() + eg.meterD.load() + eg.meterLim.load());
        grSlow += (1.0f - std::exp (-dt / 0.3f)) * (grNow - grSlow);
        grFast = grNow < grFast ? grNow : jmin (0.0f, grFast + 20.0f * dt / 1.7f);
    }
    holdT += dt;
    for (int c = 0; c < 2; ++c)
    {
        const float p = ampDb (m.peak[c]), t = ampDb (m.tp[c]);
        if (holdT > 1.0f) { peakNow[c] = -200; tpNow[c] = -200; }
        peakNow[c] = jmax (peakNow[c], p); tpNow[c] = jmax (tpNow[c], t);
        peakMax[c] = jmax (peakMax[c], p); tpMax[c] = jmax (tpMax[c], t);
    }
    if (holdT > 1.0f) holdT = 0;

    // dynamic range: 400 ms loudness of input and output, 10th..95th percentile over the last 30 s
    const float a4 = std::exp (-dt / 0.4f);
    auto& e = proc.engine;
    mIn = a4 * mIn + (1 - a4) * std::pow (10.0f, e.meterIn.load() / 10.0f);
    mOut = a4 * mOut + (1 - a4) * std::pow (10.0f, e.meterOut.load() / 10.0f);
    histIn[(size_t) histPos] = powDb (mIn); histOut[(size_t) histPos] = powDb (mOut);
    histPos = (histPos + 1) % kHist; histN = jmin (histN + 1, kHist);
    auto spread = [&] (const std::array<float, kHist>& h, float& lo, float& hi) {
        std::vector<float> v; v.reserve ((size_t) histN);
        for (int i = 0; i < histN; ++i) if (h[(size_t) i] > -70.0f) v.push_back (h[(size_t) i]);
        if (v.size() < 30) { lo = hi = -200; return; }
        std::sort (v.begin(), v.end());
        lo = v[(size_t) (0.10 * (double) (v.size() - 1))]; hi = v[(size_t) (0.95 * (double) (v.size() - 1))];
    };
    if (sand != nullptr)
    {
        spread (histIn, sand->inLo, sand->inHi); spread (histOut, sand->outLo, sand->outHi);
        drIn = sand->inLo > -150 ? sand->inHi - sand->inLo : 0; drOut = sand->outLo > -150 ? sand->outHi - sand->outLo : 0;
    }
    repaint();
}

void Meter::mouseDown (const MouseEvent&)
{
    for (int c = 0; c < 2; ++c) peakMax[c] = tpMax[c] = -200;
    longSum = 0; longN = 0; histN = 0;
    proc.engine.resetIntegrated.store (true);
}

// Modulometer in the spirit of the Nagra: chrome bezel with screws, white face, dB scale graduated every dB
// at the top, % modulation scale inside, thick band above 0, plate over the pivot, glass reflection.
// Black needle = peak (PPM), grey needle = VU (sine calibrated: a steady tone reads the same on both).
// gr = true: the global gain reduction modulometer, 0 dB at the right, -20 at the left, burgundy needles
static void drawDial (Graphics& g, Rectangle<float> b, float peakDb, float vuDb, const String& name, bool gr = false)
{
    const float R = jmin (b.getWidth(), b.getHeight()) / 2 - 2;
    const auto c = b.getCentre();
    g.setGradientFill (ColourGradient (Colour (0xffd0d0d0), c.x - R, c.y - R, Colour (0xff606060), c.x + R, c.y + R, false));
    g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);
    g.setColour (Colour (0xff3c3c3c)); g.fillEllipse (c.x - R * 0.93f, c.y - R * 0.93f, R * 1.86f, R * 1.86f);
    for (int k = 0; k < 4; ++k)   // bezel screws
    {
        const float a = MathConstants<float>::pi * (0.25f + 0.5f * k), sr = R * 0.045f;
        const auto sp = c.getPointOnCircumference (R * 0.965f, a);
        g.setColour (Colour (0xff2a2a2a)); g.fillEllipse (sp.x - sr, sp.y - sr, 2 * sr, 2 * sr);
        g.setColour (Colour (0xffa0a0a0)); g.drawLine (Line<float> (sp.getPointOnCircumference (sr * 0.8f, a + 0.6f), sp.getPointOnCircumference (sr * 0.8f, a + 0.6f + MathConstants<float>::pi)), 0.8f);
    }
    const float Rf = R * 0.89f;
    g.setColour (Colour (0xfff4f4f0)); g.fillEllipse (c.x - Rf, c.y - Rf, 2 * Rf, 2 * Rf);
    const Point<float> pv (c.x, c.y + Rf * 0.55f);
    auto ang = [gr] (float db) {
        if (gr) return 50.0f - 100.0f * jlimit (0.0f, 1.0f, -db / 20.0f);
        if (db <= -60) return -50.0f;
        const float t = jlimit (0.0f, 1.0f, std::pow (10.0f, (db - 3.0f) / 20.0f));
        return -50.0f + 100.0f * std::pow (t, 0.5f);
    };
    auto pt = [&] (float aDeg, float r) { const float a = degreesToRadians (aDeg); return Point<float> (pv.x + r * std::sin (a), pv.y - r * std::cos (a)); };
    const float Rs = Rf * 1.0f;
    g.setColour (kBg);
    Path arc; arc.addCentredArc (pv.x, pv.y, Rs, Rs, 0, degreesToRadians (-50.0f), degreesToRadians (50.0f), true);
    g.strokePath (arc, PathStrokeType (1.1f));
    if (! gr)
    {
        Path over; over.addCentredArc (pv.x, pv.y, Rs + 3.0f, Rs + 3.0f, 0, degreesToRadians (ang (0.0f)), degreesToRadians (50.0f), true);
        g.strokePath (over, PathStrokeType (4.5f));
    }
    if (gr)
    {   // GR scale: every dB to -10, every 2 dB to -20
        for (int db = 0; db >= -20; --db)
        {
            if (db < -10 && db % 2 != 0) continue;
            const bool major = db % 5 == 0 || db == -2;
            const float a = ang ((float) db);
            g.drawLine (Line<float> (pt (a, Rs), pt (a, Rs + (major ? 7.5f : 4.0f))), major ? 1.3f : 0.7f);
        }
        g.setFont (mono (jmax (7.5f, R * 0.08f), true));
        for (int db : { 0, -2, -5, -10, -15, -20 })
        {
            const auto q = pt (ang ((float) db), Rs + 15.5f);
            g.drawText (String (-db), Rectangle<float> (q.x - 14, q.y - 6, 28, 12), Justification::centred);
        }
    }
    else
    {
    // fine graduations: every dB from -20 to +3, every 5 dB below
    for (int db = -40; db <= 3; ++db)
    {
        if (db < -20 && db % 5 != 0) continue;
        const bool major = db % 5 == 0 || db == -3 || db == -1;
        const float a = ang ((float) db);
        g.drawLine (Line<float> (pt (a, Rs), pt (a, Rs + (major ? 7.5f : 4.0f))), major ? 1.3f : 0.7f);
    }
    const std::pair<float, const char*> marks[] = { { -60.0f, "-\xe2\x88\x9e" }, { -30.0f, "30" }, { -20.0f, "20" }, { -15.0f, "15" }, { -10.0f, "10" },
                                                    { -6.0f, "6" }, { -3.0f, "3" }, { 0.0f, "0" }, { 3.0f, "+3" } };
    g.setFont (mono (jmax (7.5f, R * 0.08f), true));
    for (auto& [db, txt] : marks)
    {
        const float a = ang (db);
        if (db <= -60) g.drawLine (Line<float> (pt (a, Rs), pt (a, Rs + 7.5f)), 1.3f);
        const auto q = pt (a, Rs + 15.5f);
        g.drawText (u8 (txt), Rectangle<float> (q.x - 14, q.y - 6, 28, 12), Justification::centred);
    }
    // inner % modulation scale (linear amplitude, 100 % = 0 dB)
    const float Ri = Rs * 0.8f;
    Path inner; inner.addCentredArc (pv.x, pv.y, Ri, Ri, 0, degreesToRadians (ang (-20.0f)), degreesToRadians (ang (0.0f)), true);
    g.setColour (Colour (0xff555555)); g.strokePath (inner, PathStrokeType (0.8f));
    g.setFont (mono (jmax (6.5f, R * 0.06f)));
    for (int pc : { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 })
    {
        const float a = ang (20.0f * std::log10 (pc / 100.0f));
        g.drawLine (Line<float> (pt (a, Ri), pt (a, Ri - 4.0f)), 0.8f);
        if (pc == 10 || pc == 30 || pc == 50 || pc == 70 || pc == 100)
        {
            const auto q = pt (a, Ri - 11.0f);
            g.drawText (String (pc), Rectangle<float> (q.x - 12, q.y - 5, 24, 10), Justification::centred);
        }
    }
    g.drawText ("%", Rectangle<float> (pv.x - 10, pv.y - Ri * 0.62f, 20, 10), Justification::centred);
    }
    g.setColour (kBg); g.setFont (mono (jmax (7.5f, R * 0.075f), true));
    g.drawText (gr ? "GR dB" : "dB", Rectangle<float> (pt (-43.0f, Rs * 0.86f).x - 16, pt (-43.0f, Rs * 0.86f).y - 6, 32, 12), Justification::centred);
    g.setFont (mono (jmax (7.0f, R * 0.07f), true));
    g.drawText (u8 ("MODULOMÈTRE"), Rectangle<float> (c.x - R, pv.y - Rs * 0.42f, 2 * R, 11), Justification::centred);
    // needles: VU (grey) under peak (black)
    g.setColour (gr ? Colour (0xffb25563) : Colour (0xff8a8a8a)); g.drawLine (Line<float> (pv, pt (ang (vuDb), Rs + 6)), 1.5f);
    g.setColour (gr ? Colour (0xff6e1423) : kBg); g.drawLine (Line<float> (pv, pt (ang (peakDb), Rs + 7)), 1.6f);
    const float chY = pv.y - 5;
    {
        Graphics::ScopedSaveState ss (g);
        Path face; face.addEllipse (c.x - Rf, c.y - Rf, 2 * Rf, 2 * Rf);
        g.reduceClipRegion (face);
        g.setGradientFill (ColourGradient (Colour (0xffc4c4c4), c.x, chY, Colour (0xff8e8e8e), c.x, c.y + Rf, false));
        g.fillRect (c.x - Rf, chY, 2 * Rf, Rf * 2);
        g.setColour (Colour (0xff333333)); g.drawLine (c.x - Rf, chY, c.x + Rf, chY, 1.0f);
        // glass reflection
        Path glare; glare.addCentredArc (c.x, c.y, Rf * 0.92f, Rf * 0.92f, 0, degreesToRadians (-75.0f), degreesToRadians (-15.0f), true);
        g.setColour (Colours::white.withAlpha (0.55f)); g.strokePath (glare, PathStrokeType (Rf * 0.06f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    g.setColour (Colour (0xff4a4a4a)); g.fillEllipse (pv.x - 3.5f, pv.y - 3.5f, 7.0f, 7.0f);
    g.setFont (mono (jmax (8.0f, R * 0.08f), true));
    g.setColour (gr ? Colour (0xff6e1423) : kBg); g.drawText (gr ? "RAPIDE" : "PEAK", Rectangle<float> (c.x - 50, chY + 6, 44, 11), Justification::centredRight);
    g.setColour (gr ? Colour (0xffb25563) : Colour (0xff5a5a5a)); g.drawText (gr ? "LENT" : "VU", Rectangle<float> (c.x + 6, chY + 6, 44, 11), Justification::centredLeft);
    g.setColour (kBg); g.setFont (mono (jmax (10.0f, R * 0.12f), true));
    g.drawText (name, Rectangle<float> (c.x - 20, chY + Rf * 0.18f, 40, 16), Justification::centred);
}

void Meter::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (kPanel); g.fillRect (r);
    g.setColour (kDim); g.drawRect (r, 1.0f);
    r.reduce (8, 8);
    auto dials = r.removeFromTop (r.getWidth() * 0.5f);
    drawDial (g, dials.removeFromLeft (dials.getWidth() * 0.5f).reduced (2), ppm[0], powDb (vuPow[0]) + 3.01f, "L");
    drawDial (g, dials.reduced (2), ppm[1], powDb (vuPow[1]) + 3.01f, "R");
    r.removeFromTop (6);
    auto& e = proc.engine;
    auto f = [] (float db) { return db < -150 ? String (u8 ("-\xe2\x88\x9e")) : String (db, 1); };
    {   // second row: the global GR modulometer, and the loudness figures beside it
        auto row = r.removeFromTop (r.getWidth() * 0.5f);
        drawDial (g, row.removeFromLeft (row.getWidth() * 0.5f).reduced (2), grFast, grSlow, "GR", true);
        row.removeFromLeft (10);
        const std::pair<const char*, String> big[] = {
            { "LUFS M", f (e.meterM.load()) }, { "LUFS S", f (e.meterS.load()) }, { "LUFS I", f (e.meterI.load()) },
            { "CREST", String (crestOut, 1) }, { "DYN. RANGE", String (drOut, 1) }, { "GAIN AUTO", String (e.meterMakeup.load(), 1) } };
        const float bh = row.getHeight() / (float) std::size (big);
        for (auto& [name, val] : big)
        {
            auto line = row.removeFromTop (bh);
            g.setColour (kGrey); g.setFont (mono (9.5f, true));
            g.drawText (u8 (name), line.removeFromLeft (line.getWidth() * 0.5f), Justification::centredLeft);
            g.setColour (kFg); g.setFont (mono (15.0f, true));
            g.drawText (val, line, Justification::centredRight);
        }
    }
    r.removeFromTop (6);
    struct RowT { const char* name; String a, b; bool strong; };
    const RowT rows[] = {
        { "", "L", "R", false },
        { "PEAK", f (peakNow[0]), f (peakNow[1]), true },
        { "  MAX", f (peakMax[0]), f (peakMax[1]), false },
        { "TRUE PEAK", f (tpNow[0]), f (tpNow[1]), true },
        { "  MAX", f (tpMax[0]), f (tpMax[1]), false },
        { "CREST ENTR\xc3\x89" "E", "", String (crestIn, 1), false },
        { "RMS 3 s / LONG", f (powDb (rms3Pow) + 3.01f), f (longN > 0 ? powDb (longSum / (double) longN) + 3.01f : -200.0f), false },
    };
    const float rh = jmin (16.0f, (r.getHeight() - 10) / (float) std::size (rows));
    for (auto& row : rows)
    {
        auto line = r.removeFromTop (rh);
        g.setFont (mono (10.0f, row.strong));
        g.setColour (row.strong ? kFg : kGrey);
        g.drawText (u8 (row.name), line.removeFromLeft (line.getWidth() * 0.42f), Justification::centredLeft);
        g.setColour (row.name[0] == 0 ? kGrey : kFg);
        g.drawText (row.a, line.removeFromLeft (line.getWidth() * 0.5f), Justification::centredRight);
        g.drawText (row.b, line, Justification::centredRight);
    }
    g.setColour (kDim); g.setFont (mono (8.5f));
    g.drawText ("CLIC = REMISE A ZERO", r, Justification::bottomLeft);
}
} // namespace ui

// ------------------------------------------------------------------ editor
using K = ui::Control::Kind;

// ------------------------------------------------------------------ insert slot
namespace
{
struct InsertWindow : DocumentWindow
{
    InsertWindow (const String& name, AudioProcessorEditor* ed, std::function<void()> onClose)
        : DocumentWindow (name, Colours::black, DocumentWindow::closeButton), closed (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (ed, true);
        setResizable (ed->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }
    void closeButtonPressed() override { if (closed) MessageManager::callAsync (closed); }
    std::function<void()> closed;
};

Array<File> installedPlugins()
{
    Array<File> dirs, out;
   #if JUCE_MAC
    dirs.add (File ("/Library/Audio/Plug-Ins/VST3")); dirs.add (File ("~/Library/Audio/Plug-Ins/VST3"));
    dirs.add (File ("/Library/Audio/Plug-Ins/Components")); dirs.add (File ("~/Library/Audio/Plug-Ins/Components"));
   #elif JUCE_WINDOWS
    dirs.add (File::getSpecialLocation (File::globalApplicationsDirectory).getChildFile ("Common Files/VST3"));
   #else
    dirs.add (File ("~/.vst3")); dirs.add (File ("/usr/lib/vst3")); dirs.add (File ("/usr/local/lib/vst3"));
   #endif
    for (auto& d : dirs)
        if (d.isDirectory())
            for (auto& f : d.findChildFiles (File::findFilesAndDirectories, true, "*.vst3;*.component"))
                if (! f.getFileNameWithoutExtension().containsIgnoreCase ("Delta Comp") && ! f.getParentDirectory().hasFileExtension ("vst3"))
                    out.addIfNotAlreadyThere (f);
    struct ByName { static int compareElements (const File& a, const File& b) { return a.getFileName().compareIgnoreCase (b.getFileName()); } } cmp;
    out.sort (cmp);
    return out;
}
}

namespace ui
{
InsertSlot::InsertSlot (DeltaCompProcessor& p) : proc (p)
{
    addAndMakeVisible (pick); addAndMakeVisible (edit);
    edit.setButtonText (u8 ("ÉDITEUR"));
    pick.onClick = [this] { showMenu(); };
    edit.onClick = [this] { openEditor(); };
    proc.onInsertAboutToChange = [this] { closeWindow(); };
    proc.onInsertChanged = [this] { refresh(); };
    refresh();
}

InsertSlot::~InsertSlot()
{
    closeWindow();
    proc.onInsertAboutToChange = nullptr; proc.onInsertChanged = nullptr;
}

void InsertSlot::refresh()
{
    const auto n = proc.insertName();
    pick.setButtonText (n.isEmpty() ? u8 ("CHOISIR UN PLUGIN…") : n.toUpperCase());
    edit.setEnabled (proc.getInsert() != nullptr && proc.getInsert()->hasEditor());
    repaint();
}

void InsertSlot::closeWindow() { window.reset(); }

void InsertSlot::openEditor()
{
    auto* ins = proc.getInsert();
    if (ins == nullptr) return;
    if (window != nullptr) { window->toFront (true); return; }
    if (auto* ed = ins->createEditorIfNeeded())
        window = std::make_unique<InsertWindow> (ins->getName(), ed, [this] { closeWindow(); });
}

void InsertSlot::showMenu()
{
    found = installedPlugins();
    PopupMenu m;
    for (int i = 0; i < found.size(); ++i)
        m.addItem (i + 1, found[i].getFileNameWithoutExtension() + (found[i].hasFileExtension ("component") ? "  (AU)" : ""));
    if (found.isEmpty()) m.addItem (-100, u8 ("aucun plugin trouvé"), false);
    m.addSeparator();
    m.addItem (1000, u8 ("Autre fichier…"));
    m.addItem (1001, "Retirer", proc.getInsert() != nullptr);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&pick), [this] (int r)
    {
        String err;
        if (r >= 1 && r <= found.size()) { if (! proc.loadInsertFile (found[r - 1], err)) error = err; else error.clear(); }
        else if (r == 1001) { proc.clearInsert(); error.clear(); }
        else if (r == 1000)
        {
            chooser = std::make_unique<FileChooser> ("Plugin (VST3 / AU)", File(), "*.vst3;*.component");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectDirectories,
                                  [this] (const FileChooser& fc)
                                  {
                                      const auto f = fc.getResult();
                                      if (f == File()) return;
                                      String e2; if (! proc.loadInsertFile (f, e2)) error = e2; else error.clear();
                                      refresh();
                                  });
        }
        refresh();
    });
}

void InsertSlot::resized()
{
    auto r = getLocalBounds();
    pick.setBounds (r.removeFromTop (22));
    r.removeFromTop (3);
    edit.setBounds (r.removeFromTop (20).withSizeKeepingCentre (jmin (110, r.getWidth()), 20));
}

void InsertSlot::paint (Graphics& g)
{
    g.setFont (mono (8.5f));
    g.setColour (error.isNotEmpty() ? kFg : kGrey);
    const auto lat = proc.getInsert() != nullptr ? proc.getInsert()->getLatencySamples() : 0;
    const String t = error.isNotEmpty() ? error : proc.getInsert() != nullptr ? u8 ("latence compensée : ") + String (lat) + u8 (" éch.") : u8 ("VST3 ou AU, ex. Altiverb");
    g.drawFittedText (t, getLocalBounds().removeFromBottom (14), Justification::centred, 1);
}
}

DeltaCompEditor::DeltaCompEditor (DeltaCompProcessor& p) : AudioProcessorEditor (p), sand (p), meter (p), insertSlot (p), proc (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (content);
    content.addAndMakeVisible (sand);
    content.addAndMakeVisible (meter);
    meter.sand = &sand;
    auto& s = p.apvts;
    auto add = [&] (OwnedArray<ui::Section>& row, ui::Section* sec, std::function<String()> ro) { sec->readout = std::move (ro); content.addAndMakeVisible (row.add (sec)); };
    auto db = [] (float v) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; };
    auto& e = p.engine;

    add (row1, new ui::Section (s, "TRANSITOIRES", "trOn", 2, { { K::knob, "transient", "ROND / ATT." }, { K::knob, "trDur", "DURÉE" }, { K::choice, "trTarget", "SUR" } }),
         [&e, db] { return db (e.meterT.load()); });
    add (row1, new ui::Section (s, "UPWARD", "upOn", 2, { { K::knob, "upThr", "SEUIL" }, { K::knob, "upRatio", "RATIO" }, { K::knob, "upRange", "PLAGE" },
                                                                  { K::knob, "upAtt", "ATTAQUE" }, { K::knob, "upRel", "RELEASE" }, { K::choice, "upMode", "MODE" } }),
         [&e, db] { return db (e.meterA.load()); });
    row1.getLast()->gain = [&e] { return e.meterA.load(); }; row1.getLast()->expand = true; row1.getLast()->range = 12;
    add (row1, new ui::Section (s, "DOWNWARD", "llOn", 2, { { K::knob, "llThr", "SEUIL" }, { K::knob, "llRatio", "RATIO" }, { K::knob, "llRange", "PLAGE" },
                                                                    { K::knob, "llAtt", "ATTAQUE" }, { K::knob, "llRel", "RELEASE" } }),
         [&e, db] { return db (e.meterB.load()); });
    row1.getLast()->gain = [&e] { return e.meterB.load(); }; row1.getLast()->range = 12;
    add (row1, new ui::Section (s, "DOUBLE SEUIL", "dnOn", 4,
                                { { K::knob, "thr1", "SEUIL mp" }, { K::knob, "ratio1", "RATIO 1" }, { K::knob, "att1", "ATTAQUE 1" }, { K::knob, "rel1", "RELEASE 1" },
                                  { K::knob, "thr2", "SEUIL mf" }, { K::knob, "ratio2", "RATIO 2" }, { K::knob, "att2", "ATTAQUE 2" }, { K::knob, "rel2", "RELEASE 2" },
                                  { K::knob, "hyst", "HYSTÉRÉSIS" }, { K::knob, "knee", "GENOU" }, { K::knob, "window", "FENÊTRE" }, { K::choice, "opto", "DÉTECTION" } }),
         [&e, db] { return db (e.meterC.load()); });
    row1.getLast()->gain = [&e] { return e.meterC.load(); }; row1.getLast()->range = 20;
    add (row1, new ui::Section (s, "PEAK", "pkOn", 2, { { K::knob, "pkThr", "SEUIL" }, { K::knob, "pkRatio", "RATIO" },
                                                       { K::knob, "pkAtt", "ATTAQUE" }, { K::knob, "pkRel", "RELEASE" },
                                                       { K::choice, "pkMode", "MODE" }, { K::knob, "crest", "CREST" } }),
         [&e, &p, db] { return db (e.meterD.load()) + "  @ " + String (p.engine.peakThr(), 1); });
    row1.getLast()->gain = [&e] { return e.meterD.load(); }; row1.getLast()->range = 12;

    add (row2, new ui::Section (s, "AMBIANCE · K-STEREO", "ambOn", 4, { { K::knob, "kStereo", "K-STEREO" }, { K::knob, "ambiance", "QUEUES" }, { K::knob, "ambThr", "SEUIL" }, { K::knob, "ambRatio", "RATIO" },
                                                            { K::knob, "ambAtt", "ATTAQUE" }, { K::knob, "ambRel", "RELEASE" }, { K::knob, "ambPre", "PRÉ-DÉLAI" },
                                                            { K::knob, "ambTr", "TRANSITOIRES" }, { K::choice, "ambAuto", "AUTO RÉFL." }, { K::choice, "ambStage", "APRÈS" },
                                                            { K::toggle, "kstOn", "K-STEREO" } }),
         [&e, db] { return db (e.meterAmb.load()) + ui::u8 ("  réfl. ") + String (roundToInt (e.meterEr.load())) + " ms"; });
    // INSERT: a plain VST3/AU insert (Altiverb or anything), independent of the ambiance, on Mid, Side or L/R
    add (row2, new ui::Section (s, "INSERT", "revOn", 2, { { K::choice, "revTarget", "SUR" } }),
         [this] { auto n = proc.insertName(); return n.isEmpty() ? String ("VIDE") : n.toUpperCase(); });
    {
        auto* ins = row2.getLast();
        ins->extra = &insertSlot; ins->extraCell = 2; ins->extraSpan = 2;
        ins->addAndMakeVisible (insertSlot);
    }
    add (row2, new ui::Section (s, "M/S", "msOn", 3, { { K::knob, "centre", "CENTRE" }, { K::knob, "centreFreq", "FRÉQ." }, { K::choice, "centreFilter", "FILTRE" },
                                                       { K::knob, "width", "LARGEUR" }, { K::knob, "sideLowCut", "LOW CUT S" },
                                                       { K::choice, "sideSlope", "PENTE dB/oct" } }),
         [] { return String ("CENTRE / LARGEUR"); });
    add (row2, new ui::Section (s, "LIMITEUR", "limOn", 3, { { K::knob, "ceiling", "PLAFOND" }, { K::knob, "limRel", "RELEASE" }, { K::knob, "look", "LOOKAHEAD" },
                                                            { K::knob, "linkT", "LIEN TRANS." }, { K::knob, "linkR", "LIEN REL." }, { K::choice, "os", "OVERSAMPLING" },
                                                            { K::choice, "truePeak", "TRUE PEAK" } }),
         [&e, db] { return db (e.meterLim.load()); });
    row2.getLast()->hairpin = [&e] { const float i = e.meterIn.load(), o = e.meterOut.load(); return i > -70.0f && o > -70.0f ? o - i : 0.0f; };
    add (row1, new ui::Section (s, "TILT", "tiltOn", 2, { { K::knob, "tilt", "GRAVE / AIGU" }, { K::knob, "tiltPivot", "PIVOT" },
                                                         { K::choice, "tiltPhase", "PHASE" }, { K::choice, "tiltTarget", "SUR" }, { K::choice, "tiltPos", "POSITION" } }),
         [this] { return ui::u8 (proc.apvts.getRawParameterValue ("tiltPhase")->load() > 0.5f ? "MIN PHASE" : "LINÉAIRE"); });
    add (row2, new ui::Section (s, "GLOBAL", "active", 4, { { K::knob, "in", "ENTRÉE" }, { K::knob, "target", "CIBLE" }, { K::knob, "density", "DENSITY" }, { K::knob, "link", "LIEN STÉRÉO" },
                                                          { K::knob, "mix", "MIX" }, { K::knob, "out", "SORTIE" }, { K::choice, "autoGain", "GAIN AUTO" }, { K::choice, "meterLook", "AVANCE NAGRA" } }),
         [&e, db] { return "GAIN AUTO " + db (e.meterMakeup.load()) + "  DENSITY " + db (e.meterDensity.load()); });

    row1[1]->marking = "pp"; row1[2]->marking = "p"; row1[3]->marking = "mp mf"; row1[4]->marking = "f ff";
    setResizable (true, true);
    setResizeLimits (kW / 2, kH / 2, kW * 3 / 2, kH * 3 / 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kW / kH);
    setSize (kW * 8 / 10, kH * 8 / 10);
    startTimerHz (30);
}

DeltaCompEditor::~DeltaCompEditor() { setLookAndFeel (nullptr); }

void DeltaCompEditor::paint (Graphics& g) { g.fillAll (Colours::black); }

void DeltaCompEditor::resized()
{
    const float k = (float) getWidth() / kW;
    content.setBounds (0, 0, kW, kH);
    content.setTransform (AffineTransform::scale (k));
    auto r = Rectangle<int> (0, 0, kW, kH).reduced (14);
    sand.setBounds (r.removeFromTop (440));
    r.removeFromTop (8);
    meter.setBounds (r.removeFromRight (390));
    r.removeFromRight (8);
    auto layRow = [] (OwnedArray<ui::Section>& row, Rectangle<int> area) {
        int cols = 0; for (auto* s : row) cols += s->cols;
        const int gap = 8, avail = area.getWidth() - gap * (row.size() - 1);
        for (auto* s : row) { const int w = avail * s->cols / cols; s->setBounds (area.removeFromLeft (w)); area.removeFromLeft (gap); }
    };
    const int rowH = (r.getHeight() - 8) / 2;
    layRow (row1, r.removeFromTop (rowH));
    r.removeFromTop (8);
    layRow (row2, r);
}

void DeltaCompEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes();
    const float dt = lastMs > 0 ? (float) jlimit (0.005, 0.5, (now - lastMs) / 1000.0) : 1.0f / 30.0f;
    lastMs = now;
    sand.push(); sand.repaint();
    meter.tick (dt);
    for (auto* row : { &row1, &row2 })
        for (auto* s : *row)
        {
            if (s->readout) { auto t = s->readout(); if (t != s->shown) { s->shown = t; if (! s->gain) s->repaint(); } }
            s->tickGain (dt);
        }
}
