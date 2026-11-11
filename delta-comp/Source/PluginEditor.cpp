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

// Moog style: white tick ring, black knob with a grey rim, white pointer line.
void Look::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s)
{
    const auto r = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float rad = jmin (r.getWidth(), r.getHeight()) * 0.5f - 2.0f;
    const auto c = r.getCentre();
    g.setColour (kFg.withAlpha (0.7f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (a1 - a0) * (float) i / 10.0f;
        const float r0 = rad - (i % 5 == 0 ? 4.5f : 3.0f);
        g.drawLine ({ c.getPointOnCircumference (r0, a), c.getPointOnCircumference (rad, a) }, i % 5 == 0 ? 1.4f : 0.9f);
    }
    const float kr = rad - 7.0f;
    g.setColour (Colour (0xff4a4a4a)); g.fillEllipse (c.x - kr, c.y - kr, 2 * kr, 2 * kr);
    g.setColour (kBg); g.fillEllipse (c.x - kr + 2, c.y - kr + 2, 2 * kr - 4, 2 * kr - 4);
    const float a = a0 + pos * (a1 - a0);
    g.setColour (s.isMouseOverOrDragging() ? kFg : kFg.withAlpha (0.9f));
    g.drawLine ({ c.getPointOnCircumference (kr * 0.25f, a), c.getPointOnCircumference (kr - 1.5f, a) }, 2.0f);
}

void Look::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    if (b.getToggleState()) { g.setColour (kFg); g.fillRect (r); }
    else { g.setColour (over ? kFg : kGrey); g.drawRect (r, 1.0f); }
}

void Look::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
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
        button.setClickingTogglesState (true);
        addAndMakeVisible (button);
        bAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (s, id, button);
    }
    else
    {
        if (auto* c = dynamic_cast<AudioParameterChoice*> (param)) combo.addItemList (c->choices, 1);
        addAndMakeVisible (combo);
        cAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (s, id, combo);
    }
}

void Control::resized()
{
    auto r = getLocalBounds();
    if (kind == knob) { slider.setBounds (r.withTrimmedBottom (26).withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight() - 26), r.getHeight() - 26)); return; }
    auto box = r.withSizeKeepingCentre (r.getWidth() - 8, 20).translated (0, kind == choice ? 6 : 0);
    if (kind == toggle) button.setBounds (box); else combo.setBounds (box);
}

void Control::paint (Graphics& g)
{
    if (kind == choice)
    {
        g.setColour (kGrey); g.setFont (mono (9.0f));
        g.drawText (label, getLocalBounds().withHeight (getHeight() / 2 - 6), Justification::centredBottom);
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
    g.drawText (title, 8, 5, getWidth() - 60, 14, Justification::centredLeft);
    g.drawHorizontalLine (24, 6.0f, (float) getWidth() - 6.0f);
    if (shown.isNotEmpty()) { g.setColour (kGrey); g.setFont (mono (9.5f)); g.drawText (shown, 8, 26, getWidth() - 16, 12, Justification::centredLeft); }
    const bool off = on != nullptr && ! on->button.getToggleState();
    if (off) { g.setColour (kBg.withAlpha (0.55f)); g.fillRect (r.withTrimmedTop (25).reduced (1)); }
}

void Section::resized()
{
    if (on) on->setBounds (getWidth() - 46, 3, 40, 18);
    auto r = getLocalBounds().reduced (4, 0).withTrimmedTop (38).withTrimmedBottom (4);
    const int rows = ((int) controls.size() + cols - 1) / cols;
    const int cw = r.getWidth() / cols, rh = jmin (84, r.getHeight() / jmax (1, rows));
    for (int i = 0; i < controls.size(); ++i)
        controls[i]->setBounds (r.getX() + (i % cols) * cw, r.getY() + (i / cols) * rh, cw, rh);
}

// ------------------------------------------------------------------ sand
static constexpr float kTop = 0.f, kBottom = -54.f;

float Sand::yOf (float lufs) const
{
    const float h = (float) getHeight() - 70.0f;
    return 24.0f + jlimit (0.0f, 1.0f, (kTop - lufs) / (kTop - kBottom)) * (h - 24.0f);
}

void Sand::push()
{
    auto& e = proc.engine;
    level[(size_t) head] = e.meterLevel.load();
    down[(size_t) head] = e.meterB.load() + e.meterC.load() + e.meterD.load();
    up[(size_t) head] = e.meterA.load();
    lim[(size_t) head] = e.meterLim.load();
    amb[(size_t) head] = e.meterAmb.load();
    head = (head + 1) % kCols; ++frame;
}

void Sand::paint (Graphics& g)
{
    g.fillAll (kBg);
    const auto p = proc.currentParams();
    const float W = (float) getWidth(), H = (float) getHeight();
    g.setColour (kDim); g.drawRect (getLocalBounds().toFloat(), 1.0f);

    // scale
    g.setFont (mono (9.0f));
    for (int db = 0; db >= (int) kBottom; db -= 6)
    {
        const float y = yOf ((float) db);
        g.setColour (kFg.withAlpha (0.07f)); g.drawHorizontalLine ((int) y, 34.0f, W);
        g.setColour (kFg.withAlpha (0.35f)); g.drawText (String (db), 2, (int) y - 6, 28, 12, Justification::centredRight);
    }
    // thresholds
    struct Th { float lufs; const char* name; bool on; };
    const float dash[] = { 3.f, 4.f };
    const Th th[] = { { (float) (p.target + p.upThr), "UPWARD", p.upOn }, { (float) (p.target + p.llThr), "BAS NIV.", p.llOn },
                      { (float) (p.target + p.thr1), "SEUIL 1", p.dnOn }, { (float) std::max (p.target + p.thr1, p.target + p.thr2), "SEUIL 2", p.dnOn },
                      { (float) (p.target + p.ambThr), "AMBIANCE", p.ambOn } };
    for (auto& t : th)
    {
        if (! t.on) continue;
        const float y = yOf (t.lufs);
        g.setColour (kFg.withAlpha (0.4f)); g.drawDashedLine ({ 34.0f, y, W - 4, y }, dash, 2, 1.0f);
        g.setColour (kFg.withAlpha (0.6f)); g.drawText (u8 (t.name), (int) W - 80, (int) y - 12, 74, 11, Justification::centredRight);
    }
    const float yt = yOf ((float) p.target);
    g.setColour (kFg); g.drawHorizontalLine ((int) yt, 34.0f, W - 4);
    g.drawText ("CIBLE " + String (p.target, 1), 38, (int) yt - 12, 100, 11, Justification::centredLeft);

    // grains: each column one frame of the detector, they shiver every frame; more grains where it
    // compresses, and they spread upward where the ambiance lifts the tails
    const float x0 = 34.0f, colW = (W - x0 - 4) / kCols;
    Random rnd ((int64) frame * 2654435761u);
    for (int i = 0; i < kCols; ++i)
    {
        const int idx = (head + i) % kCols;
        const float lv = level[(size_t) idx];
        if (lv < kBottom - 6) continue;
        const float age = (float) i / kCols, y = yOf (lv);
        const float gr = -down[(size_t) idx], lift = up[(size_t) idx] + amb[(size_t) idx];
        const int n = 6 + (int) (jmin (12.0f, gr) * 1.6f) + (int) (jmin (8.0f, lift) * 1.2f);
        for (int k = 0; k < n; ++k)
        {
            float jy = (rnd.nextFloat() + rnd.nextFloat() - 1.0f) * (3.0f + 6.0f * (float) k / (float) n);
            if (k % 3 == 0 && lift > 0.2f) jy -= rnd.nextFloat() * lift * 3.0f;
            g.setColour (kFg.withAlpha (0.12f + 0.8f * age * rnd.nextFloat()));
            g.fillRect (x0 + i * colW + rnd.nextFloat() * colW, y + jy, 1.4f, 1.4f);
        }
    }

    // gain strip: downward (B + C + D) under the line, upward (A) above, limiter as a bottom band
    const float mid = H - 36.0f;
    g.setColour (kFg.withAlpha (0.25f)); g.drawHorizontalLine ((int) mid, x0, W - 4);
    for (int i = 0; i < kCols; ++i)
    {
        const int idx = (head + i) % kCols;
        const float dn = jmin (12.0f, -down[(size_t) idx]) * 2.0f, u = jmin (12.0f, up[(size_t) idx]) * 2.0f;
        const float x = x0 + i * colW, w = jmax (1.0f, colW - 0.5f);
        if (dn > 0.05f) { g.setColour (kFg); g.fillRect (x, mid, w, dn); }
        if (u > 0.05f) { g.setColour (kGrey); g.fillRect (x, mid - u, w, u); }
        const float l = jmin (6.0f, -lim[(size_t) idx]) * 1.5f;
        if (l > 0.05f) { g.setColour (kFg); g.fillRect (x, H - 3.0f - l, w, l); }
    }
    g.setColour (kFg); g.setFont (mono (11.0f, true));
    g.drawText ("DELTA COMP", 38, 4, 200, 16, Justification::centredLeft);
    g.setColour (kGrey); g.setFont (mono (9.0f));
    g.drawText ("GR", 4, (int) mid - 6, 26, 12, Justification::centredRight);
    g.drawText ("LIM", 4, (int) H - 14, 26, 12, Justification::centredRight);
}

// ------------------------------------------------------------------ meter
static float ampDb (double a) { return a > 1e-12 ? (float) (20.0 * std::log10 (a)) : -200.0f; }
static float powDb (double p) { return p > 1e-24 ? (float) (10.0 * std::log10 (p)) : -200.0f; }

void Meter::tick (float dt)
{
    const auto m = proc.engine.takeMeter();
    const float pk = ampDb (std::max (m.peak[0], m.peak[1]));
    ppm = jmax (pk, ppm - 20.0f * dt / 1.7f);                         // Nagra peak: instant rise, 20 dB in 1.7 s
    if (m.n > 0)
    {
        const double ms = 0.5 * (m.sumSq[0] + m.sumSq[1]) / m.n;
        const float aV = std::exp (-dt / 0.3f), a3 = std::exp (-dt / 3.0f);
        vuPow = aV * vuPow + (1 - aV) * (float) ms;                    // VU: 300 ms integration
        rms3Pow = a3 * rms3Pow + (1 - a3) * (float) ms;
        if (ms > 1e-10) { longSum += ms * m.n; longN += m.n; }
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
    repaint();
}

void Meter::mouseDown (const MouseEvent&)
{
    for (int c = 0; c < 2; ++c) peakMax[c] = tpMax[c] = -200;
    longSum = 0; longN = 0;
    proc.engine.resetIntegrated.store (true);
}

// Nagra style dial: chrome ring, white face, scale bunched at the top, plate over the pivot,
// black needle = peak (PPM), grey needle = VU (sine calibrated, so a steady tone reads the same on both).
static void drawDial (Graphics& g, Rectangle<float> b, float peakDb, float vuDb)
{
    const float R = jmin (b.getWidth(), b.getHeight()) / 2 - 2;
    const auto c = b.getCentre();
    g.setColour (Colour (0xff9a9a9a)); g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);
    g.setColour (Colour (0xff5a5a5a)); g.fillEllipse (c.x - R * 0.93f, c.y - R * 0.93f, R * 1.86f, R * 1.86f);
    const float Rf = R * 0.9f;
    g.setColour (Colour (0xfff4f4f0)); g.fillEllipse (c.x - Rf, c.y - Rf, 2 * Rf, 2 * Rf);
    const Point<float> pv (c.x, c.y + Rf * 0.55f);
    auto ang = [] (float db) {
        if (db <= -60) return -52.0f;
        const float t = jlimit (0.0f, 1.0f, std::pow (10.0f, (db - 2.0f) / 20.0f));
        return -52.0f + 104.0f * std::pow (t, 0.55f);
    };
    auto pt = [&] (float aDeg, float r) { const float a = degreesToRadians (aDeg); return Point<float> (pv.x + r * std::sin (a), pv.y - r * std::cos (a)); };
    const float Rs = Rf * 1.02f;
    Path arc; arc.addCentredArc (pv.x, pv.y, Rs, Rs, 0, degreesToRadians (-52.0f), degreesToRadians (52.0f), true);
    g.setColour (kBg); g.strokePath (arc, PathStrokeType (1.2f));
    Path over; over.addCentredArc (pv.x, pv.y, Rs + 2.5f, Rs + 2.5f, 0, degreesToRadians (ang (0.0f)), degreesToRadians (52.0f), true);
    g.strokePath (over, PathStrokeType (4.0f));
    const std::pair<float, const char*> marks[] = { { -60.0f, "-\xe2\x88\x9e" }, { -30.0f, "-30" }, { -20.0f, "-20" }, { -12.0f, "-12" },
                                                    { -9.0f, "-9" }, { -6.0f, "-6" }, { -3.0f, "-3" }, { 0.0f, "0" }, { 2.0f, "+2" } };
    for (auto& [db, txt] : marks)
    {
        const float a = ang (db);
        g.drawLine (Line<float> (pt (a, Rs), pt (a, Rs + 7)), 1.4f);
        g.setFont (mono (jmax (8.0f, R * 0.085f), true));
        const auto q = pt (a, Rs + 16);
        g.drawText (u8 (txt), Rectangle<float> (q.x - 15, q.y - 6, 30, 12), Justification::centred);
    }
    g.setFont (mono (jmax (8.0f, R * 0.075f), true));
    g.drawText ("dBFS", Rectangle<float> (c.x - R, pv.y - Rf * 0.62f, 2 * R, 12), Justification::centred);
    g.setColour (Colour (0xff808080)); g.drawLine (Line<float> (pv, pt (ang (vuDb), Rs + 6)), 1.6f);
    g.setColour (kBg); g.drawLine (Line<float> (pv, pt (ang (peakDb), Rs + 6)), 1.6f);
    const float chY = pv.y - 6;
    {
        Graphics::ScopedSaveState ss (g);
        Path face; face.addEllipse (c.x - Rf, c.y - Rf, 2 * Rf, 2 * Rf);
        g.reduceClipRegion (face);
        g.setColour (Colour (0xffb5b5b5)); g.fillRect (c.x - Rf, chY, 2 * Rf, Rf * 2);
        g.setColour (Colour (0xff333333)); g.drawLine (c.x - Rf, chY, c.x + Rf, chY, 1.0f);
    }
    g.setFont (mono (9.0f, true));
    g.setColour (kBg); g.drawText ("PEAK", Rectangle<float> (c.x - 46, chY + 5, 40, 11), Justification::centredRight);
    g.setColour (Colour (0xff606060)); g.drawText ("VU", Rectangle<float> (c.x + 6, chY + 5, 40, 11), Justification::centredLeft);
}

void Meter::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (kPanel); g.fillRect (r);
    g.setColour (kDim); g.drawRect (r, 1.0f);
    r.reduce (8, 8);
    auto dial = r.removeFromLeft (jmin (r.getHeight(), r.getWidth() * 0.55f));
    drawDial (g, dial, ppm, powDb (vuPow) + 3.01f);
    r.removeFromLeft (10);

    auto f = [] (float db) { return db < -150 ? String (u8 ("-\xe2\x88\x9e")) : String (db, 1); };
    auto& e = proc.engine;
    struct RowT { const char* name; String a, b; bool strong; };
    const RowT rows[] = {
        { "", "L", "R", false },
        { "PEAK", f (peakNow[0]), f (peakNow[1]), true },
        { "  MAX", f (peakMax[0]), f (peakMax[1]), false },
        { "TRUE PEAK", f (tpNow[0]), f (tpNow[1]), true },
        { "  MAX", f (tpMax[0]), f (tpMax[1]), false },
        { "LUFS M", f (e.meterM.load()), "", true },
        { "LUFS S", f (e.meterS.load()), "", true },
        { "LUFS I", f (e.meterI.load()), "", true },
        { "RMS 3 s", f (powDb (rms3Pow) + 3.01f), "", false },
        { "RMS LONG", f (longN > 0 ? powDb (longSum / (double) longN) + 3.01f : -200.0f), "", false },
        { "GAIN AUTO", String (e.meterMakeup.load(), 1), "", false },
        { "RÉFLEXIONS", String (roundToInt (e.meterEr.load())) + " ms", "", false },
    };
    const float rh = jmin (17.0f, r.getHeight() / (float) std::size (rows));
    for (auto& row : rows)
    {
        auto line = r.removeFromTop (rh);
        g.setFont (mono (10.5f, row.strong));
        g.setColour (row.strong ? kFg : kGrey);
        g.drawText (u8 (row.name), line.removeFromLeft (line.getWidth() * 0.44f), Justification::centredLeft);
        g.setColour (kFg);
        g.drawText (row.a, line.removeFromLeft (line.getWidth() * 0.5f), Justification::centredRight);
        g.drawText (row.b, line, Justification::centredRight);
    }
    g.setColour (kDim); g.setFont (mono (8.5f));
    g.drawText ("CLIC = REMISE A ZERO", r, Justification::bottomLeft);
}
} // namespace ui

// ------------------------------------------------------------------ editor
using K = ui::Control::Kind;

DeltaCompEditor::DeltaCompEditor (DeltaCompProcessor& p) : AudioProcessorEditor (p), sand (p), meter (p), proc (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (content);
    content.addAndMakeVisible (sand);
    content.addAndMakeVisible (meter);
    auto& s = p.apvts;
    auto add = [&] (OwnedArray<ui::Section>& row, ui::Section* sec, std::function<String()> ro) { sec->readout = std::move (ro); content.addAndMakeVisible (row.add (sec)); };
    auto db = [] (float v) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; };
    auto& e = p.engine;

    add (row1, new ui::Section (s, "TRANSIT.", nullptr, 1, { { K::knob, "transient", "ROND / ATT." }, { K::knob, "trDur", "DURÉE" } }),
         [&e, db] { return db (e.meterT.load()); });
    add (row1, new ui::Section (s, "UPWARD BAS NIV.", "upOn", 2, { { K::knob, "upThr", "SEUIL" }, { K::knob, "upRatio", "RATIO" }, { K::knob, "upRange", "PLAGE" },
                                                                  { K::knob, "upAtt", "ATTAQUE" }, { K::knob, "upRel", "RELEASE" } }),
         [&e, db] { return db (e.meterA.load()); });
    add (row1, new ui::Section (s, "DOWNWARD BAS NIV.", "llOn", 2, { { K::knob, "llThr", "SEUIL" }, { K::knob, "llRatio", "RATIO" }, { K::knob, "llRange", "PLAGE" },
                                                                    { K::knob, "llAtt", "ATTAQUE" }, { K::knob, "llRel", "RELEASE" } }),
         [&e, db] { return db (e.meterB.load()); });
    add (row1, new ui::Section (s, "DOWNWARD DOUBLE SEUIL", "dnOn", 4,
                                { { K::knob, "thr1", "SEUIL 1" }, { K::knob, "ratio1", "RATIO 1" }, { K::knob, "att1", "ATTAQUE 1" }, { K::knob, "rel1", "RELEASE 1" },
                                  { K::knob, "thr2", "SEUIL 2" }, { K::knob, "ratio2", "RATIO 2" }, { K::knob, "att2", "ATTAQUE 2" }, { K::knob, "rel2", "RELEASE 2" },
                                  { K::knob, "hyst", "HYSTÉRÉSIS" }, { K::knob, "knee", "GENOU" }, { K::knob, "window", "FENÊTRE" }, { K::choice, "opto", "DÉTECTION" } }),
         [&e, db] { return db (e.meterC.load()); });
    add (row1, new ui::Section (s, "PEAK", "pkOn", 2, { { K::knob, "crest", "CREST" }, { K::knob, "pkRatio", "RATIO" },
                                                       { K::knob, "pkAtt", "ATTAQUE" }, { K::knob, "pkRel", "RELEASE" } }),
         [&e, &p, db] { return db (e.meterD.load()) + "  @ " + String (p.engine.peakThr(), 1); });

    add (row2, new ui::Section (s, "AMBIANCE", "ambOn", 3, { { K::knob, "ambiance", "QUANTITÉ" }, { K::knob, "ambThr", "SEUIL" }, { K::knob, "ambRatio", "RATIO" },
                                                            { K::knob, "ambAtt", "ATTAQUE" }, { K::knob, "ambRel", "RELEASE" }, { K::knob, "ambPre", "PRÉ-DÉLAI" },
                                                            { K::toggle, "ambAuto", "AUTO RÉFL." }, { K::choice, "ambStage", "APRÈS" } }),
         [&e, db] { return db (e.meterAmb.load()) + ui::u8 ("  réfl. ") + String (roundToInt (e.meterEr.load())) + " ms"; });
    add (row2, new ui::Section (s, "M/S", nullptr, 5, { { K::knob, "centre", "CENTRE" }, { K::knob, "centreFreq", "FRÉQ." }, { K::choice, "centreFilter", "FILTRE" },
                                                       { K::knob, "width", "LARGEUR" }, { K::knob, "sideLowCut", "LOW CUT S" },
                                                       { K::choice, "sideSlope", "PENTE dB/oct" }, { K::toggle, "revOn", "REVERBE" }, { K::choice, "revTarget", "SUR" },
                                                       { K::knob, "revSend", "ENVOI" }, { K::knob, "revDecay", "DURÉE" },
                                                       { K::knob, "revPre", "PRÉ-DÉLAI" }, { K::toggle, "revPreAuto", "PRE AUTO" }, { K::knob, "revDyn", "DYNAMIQUE" } }),
         [&e] { return String ("REVERBE ") + (e.meterRev.load() < -90 ? String ("--") : String (e.meterRev.load(), 1) + " dB"); });
    add (row2, new ui::Section (s, "LIMITEUR", "limOn", 3, { { K::knob, "ceiling", "PLAFOND" }, { K::knob, "limRel", "RELEASE" }, { K::knob, "look", "LOOKAHEAD" },
                                                            { K::knob, "linkT", "LIEN TRANS." }, { K::knob, "linkR", "LIEN REL." }, { K::choice, "os", "OVERSAMPLING" },
                                                            { K::toggle, "truePeak", "TRUE PEAK" } }),
         [&e, db] { return db (e.meterLim.load()); });
    add (row2, new ui::Section (s, "GLOBAL", nullptr, 4, { { K::knob, "target", "CIBLE" }, { K::knob, "density", "DENSITY" }, { K::knob, "link", "LIEN STÉRÉO" },
                                                          { K::knob, "mix", "MIX" }, { K::knob, "out", "SORTIE" }, { K::toggle, "autoGain", "GAIN AUTO" } }),
         [&e, db] { return "GAIN AUTO " + db (e.meterMakeup.load()) + "  DENSITY " + db (e.meterDensity.load()); });

    setResizable (true, true);
    setResizeLimits (kW / 2, kH / 2, kW * 3 / 2, kH * 3 / 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kW / kH);
    setSize (kW * 9 / 10, kH * 9 / 10);
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
    auto top = r.removeFromTop (300);
    meter.setBounds (top.removeFromRight (420));
    top.removeFromRight (10);
    sand.setBounds (top);
    r.removeFromTop (10);
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
            if (s->readout) { auto t = s->readout(); if (t != s->shown) { s->shown = t; s->repaint(); } }
}
