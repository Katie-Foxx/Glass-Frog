#include "PluginEditor.h"
#if AHR1_HAS_FONT || AHR1_HAS_FROG
 #include <BinaryData.h>
#endif

using namespace juce;

namespace
{
const Colour chassis { 0xff15131b }, panel { 0xff1f1c29 }, panelHi { 0xff2a2638 }, panelEdge { 0xff353048 }, textC { 0xffe9e5f3 },
             textDim { 0xff8f89a8 }, lilac { 0xffc6a9ff }, lilacDeep { 0xff7f62c9 }, pink { 0xffff94c9 }, mint { 0xffa8e6c1 };

Font makeWordmark (float h)
{
   #if AHR1_HAS_FONT
    if (auto tf = Typeface::createSystemTypefaceFor (BinaryData::Wordmark_ttf, BinaryData::Wordmark_ttfSize))
        return Font (FontOptions (tf).withHeight (h));
   #endif
    return Font (FontOptions (Font::getDefaultSerifFontName(), h, Font::bold | Font::italic));
}
float textW (const Font& f, const String& t)
{
    GlyphArrangement ga;
    ga.addLineOfText (f, t, 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

// The soft glow that every Lismok plugin puts around lit text and lines.
void glowText (Graphics& g, const String& t, Rectangle<float> area, Justification j, Colour c, float strength = 1.0f)
{
    static const float dx[8] = { 1, -1, 0, 0, 0.7f, -0.7f, 0.7f, -0.7f }, dy[8] = { 0, 0, 1, -1, 0.7f, 0.7f, -0.7f, -0.7f };
    g.setColour (c.withAlpha (0.055f * strength));
    for (int k = 0; k < 8; ++k) g.drawText (t, area.translated (dx[k] * 1.4f, dy[k] * 1.4f), j);
}

// Pop-up lists as in Rain Frog: panel colour, purple outline, pink selection.
struct LismokLook : public LookAndFeel_V4
{
    LismokLook()
    {
        setColour (PopupMenu::backgroundColourId, panel);
        setColour (PopupMenu::textColourId, textC);
        setColour (PopupMenu::highlightedBackgroundColourId, pink.withAlpha (0.28f));
        setColour (PopupMenu::highlightedTextColourId, Colours::white);
        setColour (ScrollBar::thumbColourId, pink.withAlpha (0.8f));
    }
    void drawPopupMenuBackground (Graphics& g, int w, int h) override { g.fillAll (panel); g.setColour (lilac); g.drawRect (0, 0, w, h, 1); }
    Font getPopupMenuFont() override { return FontOptions (13.0f); }
    void drawScrollbar (Graphics& g, ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool, bool) override
    {
        g.setColour (pink.withAlpha (0.85f));
        if (vertical) g.fillRoundedRectangle ((float) x + 1.0f, (float) thumbStart, (float) w - 2.0f, (float) thumbSize, 2.0f);
        else g.fillRoundedRectangle ((float) thumbStart, (float) y + 1.0f, (float) thumbSize, (float) h - 2.0f, 2.0f);
    }
};
LismokLook& lismokLook() { static LismokLook l; return l; }

}

// ---- pill ------------------------------------------------------------------------------------------------------
class GlassFrogEditor::Pill : public Component
{
public:
    std::function<void()> onClick;
    String label;
    bool lit = false;
    void setLit (bool l) { if (l != lit) { lit = l; repaint(); } }
    void setLabel (const String& s) { if (s != label) { label = s; repaint(); } }
    void paint (Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto c = lit ? pink : lilac;
        g.setColour (c.withAlpha (over ? 0.28f : (lit ? 0.24f : 0.12f)));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (c.withAlpha (over || lit ? 1.0f : 0.75f));
        g.drawRoundedRectangle (r, 8.0f, 1.2f);
        g.setFont (FontOptions (13.0f, Font::bold));
        if (lit) glowText (g, label, getLocalBounds().reduced (4, 0).toFloat(), Justification::centred, pink);
        g.setColour (lit ? pink : textC);
        g.drawFittedText (label, getLocalBounds().reduced (4, 0), Justification::centred, 1);
    }
    void mouseEnter (const MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const MouseEvent&) override { over = false; repaint(); }
    void mouseUp (const MouseEvent& e) override { if (getLocalBounds().contains (e.getPosition()) && onClick) onClick(); }
private:
    bool over = false;
};

// ---- knob ------------------------------------------------------------------------------------------------------
class GlassFrogEditor::Knob : public Component
{
public:
    Knob (AudioProcessorValueTreeState& s, const char* id, const String& name, const String& suffix = {}, int decimals = 2) : title (name)
    {
        slider.setSliderStyle (Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
        slider.setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
        slider.setLookAndFeel (&look);
        slider.setDoubleClickReturnValue (true, s.getParameter (id)->convertFrom0to1 (s.getParameter (id)->getDefaultValue()));
        slider.setMouseDragSensitivity (180);
        slider.setScrollWheelEnabled (false);
        slider.textFromValueFunction = [suffix, decimals] (double v) { return String (v, decimals) + suffix; };
        addAndMakeVisible (slider);
        att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
        slider.onValueChange = [this] { repaint(); };
    }
    ~Knob() override { slider.setLookAndFeel (nullptr); }
    void resized() override { slider.setBounds (getLocalBounds().withTrimmedBottom (34).reduced (6)); }
    void paint (Graphics& g) override
    {
        g.setColour (textC);
        g.setFont (FontOptions (13.0f, Font::bold));
        g.drawText (title, 0, getHeight() - 32, getWidth(), 16, Justification::centred);
        g.setColour (slider.isMouseOverOrDragging() ? pink : textDim);
        g.setFont (FontOptions (11.5f));
        g.drawText (slider.getTextFromValue (slider.getValue()), 0, getHeight() - 16, getWidth(), 14, Justification::centred);
    }
private:
    struct Look : LookAndFeel_V4
    {
        void drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s) override
        {
            const float d = (float) std::min (w, h), r = d * 0.5f - 3.0f;
            const Point<float> c ((float) x + w * 0.5f, (float) y + h * 0.5f);
            const float ang = a0 + pos * (a1 - a0);
            // value arc around the knob
            Path track, val;
            track.addCentredArc (c.x, c.y, r, r, 0.0f, a0, a1, true);
            val.addCentredArc (c.x, c.y, r, r, 0.0f, a0, ang, true);
            g.setColour (panelEdge);
            g.strokePath (track, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (s.isMouseOverOrDragging() ? pink : lilac);
            g.strokePath (val, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
            // body
            const float rb = r - 7.0f;
            g.setColour (Colours::black.withAlpha (0.5f));
            g.fillEllipse (c.x - rb, c.y - rb + 2.0f, rb * 2.0f, rb * 2.0f);
            g.setGradientFill (ColourGradient (Colour (0xff4a4560), c.x - rb * 0.6f, c.y - rb * 0.8f, Colour (0xff1c1a27), c.x + rb * 0.7f, c.y + rb * 0.9f, false));
            g.fillEllipse (c.x - rb, c.y - rb, rb * 2.0f, rb * 2.0f);
            g.setColour (panelEdge.brighter (0.2f));
            g.drawEllipse (c.x - rb, c.y - rb, rb * 2.0f, rb * 2.0f, 1.0f);
            const float rc = rb * 0.72f;
            g.setGradientFill (ColourGradient (Colour (0xff8d86a6), c.x - rc * 0.6f, c.y - rc * 0.8f, Colour (0xff35314a), c.x + rc * 0.6f, c.y + rc * 0.8f, false));
            g.fillEllipse (c.x - rc, c.y - rc, rc * 2.0f, rc * 2.0f);
            // pointer
            const Point<float> u (std::cos (ang - MathConstants<float>::halfPi), std::sin (ang - MathConstants<float>::halfPi));
            g.setColour (pink);
            g.drawLine ({ c + u * (rc * 0.25f), c + u * (rb * 0.97f) }, 2.4f);
        }
    } look;
    Slider slider;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> att;
    String title;
};

// ---- editor -------------------------------------------------------------------------------------------------------
GlassFrogEditor::GlassFrogEditor (GlassFrogProcessor& p)
    : AudioProcessorEditor (&p), proc (p), wordmark (makeWordmark (46.0f))
{
    setLookAndFeel (&lismokLook());
   #if AHR1_HAS_FROG
    frog = ImageFileFormat::loadFrom (BinaryData::RainFrog_png, BinaryData::RainFrog_pngSize);
   #endif
    auto pill = [this] (std::unique_ptr<Pill>& b, const char* label, std::function<void()> f)
    {
        b = std::make_unique<Pill>(); b->label = label; b->onClick = std::move (f); addAndMakeVisible (*b);
    };
    pill (learnBtn, "Learn Noise", [this] { proc.requestLearn (! proc.isLearning()); });
    pill (clearBtn, "Clear Profile", [this] { proc.requestClear(); });
    pill (adaptBtn, "Adaptive", [this] { toggleParam (gfid::adaptive); });
    pill (deltaBtn, "Listen to Removed", [this] { toggleParam (gfid::delta); });
    pill (powerBtn, "Power", [this] { toggleParam (gfid::power); });

    auto& a = proc.apvts;
    reduction = std::make_unique<Knob> (a, gfid::reduction, "Reduction", " dB", 1);
    sensitivity = std::make_unique<Knob> (a, gfid::sensitivity, "Sensitivity", "", 2);
    air = std::make_unique<Knob> (a, gfid::air, "Air", "", 2);
    body = std::make_unique<Knob> (a, gfid::body, "Body", "", 2);
    lowcut = std::make_unique<Knob> (a, gfid::lowcut, "Low Cut", " Hz", 0);
    room = std::make_unique<Knob> (a, gfid::room, "Room", "", 2);
    smooth = std::make_unique<Knob> (a, gfid::smooth, "Smooth", "", 2);
    for (auto* k : { reduction.get(), sensitivity.get(), air.get(), body.get(), lowcut.get(), room.get(), smooth.get() }) addAndMakeVisible (*k);

    setSize (820, 600);
    startTimerHz (25);
    timerCallback();
}
GlassFrogEditor::~GlassFrogEditor() { stopTimer(); setLookAndFeel (nullptr); }

void GlassFrogEditor::toggleParam (const char* id)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
}

void GlassFrogEditor::timerCallback()
{
    const bool learning = proc.isLearning();
    learnBtn->setLabel (learning ? "Learning... press to finish" : "Learn Noise"); learnBtn->setLit (learning);
    clearBtn->setLit (false);
    adaptBtn->setLit (getBool (gfid::adaptive));
    deltaBtn->setLit (getBool (gfid::delta));
    powerBtn->setLit (getBool (gfid::power));
    const auto& d = proc.dn;
    for (int i = 0; i < gf::Denoiser::kDisplay; ++i)
    {
        smoothIn[i] += 0.5f * (d.inDb[i] - smoothIn[i]);
        smoothNz[i] += 0.3f * (d.noiseDb[i] - smoothNz[i]);
        smoothG[i] += 0.5f * (d.gainDb[i] - smoothG[i]);
    }
    repaint (modView);
    repaint (modCtl.getX(), modCtl.getY(), modCtl.getWidth(), 34);
}

void GlassFrogEditor::resized()
{
    modView = { 24, 96, 772, 300 };
    modCtl = { 24, 408, 772, 176 };
    const int kw = (modCtl.getWidth() - 32 - 170) / 7;
    Knob* ks[7] = { reduction.get(), sensitivity.get(), air.get(), body.get(), lowcut.get(), room.get(), smooth.get() };
    for (int i = 0; i < 7; ++i) ks[i]->setBounds (modCtl.getX() + 16 + i * kw, modCtl.getY() + 34, kw, modCtl.getHeight() - 40);
    const int bx = modCtl.getRight() - 16 - 150;
    learnBtn->setBounds (bx, modCtl.getY() + 38, 150, 28);
    clearBtn->setBounds (bx, modCtl.getY() + 72, 150, 26);
    adaptBtn->setBounds (bx, modCtl.getY() + 104, 72, 26);
    powerBtn->setBounds (bx + 78, modCtl.getY() + 104, 72, 26);
    deltaBtn->setBounds (bx, modCtl.getY() + 136, 150, 26);
}

void GlassFrogEditor::paint (Graphics& g)
{
    g.fillAll (chassis);
    g.setGradientFill (ColourGradient (lilacDeep.withAlpha (0.20f), 0.0f, 0.0f, Colours::transparentBlack, 0.0f, 170.0f, false));
    g.fillRect (0, 0, getWidth(), getHeight());
    g.setFont (makeWordmark (46.0f));
    glowText (g, "Lismok", Rectangle<float> (28, 14, 170, 56), Justification::centredLeft, lilac, 1.4f);
    g.setColour (lilac);
    g.drawText ("Lismok", 28, 14, 170, 56, Justification::centredLeft);
    g.setColour (panelEdge);
    g.fillRect (198, 24, 2, 38);
    glowText (g, "Glass Frog B", Rectangle<float> (214, 14, 300, 56), Justification::centredLeft, textC, 1.0f);
    g.setColour (textC);
    g.drawText ("Glass Frog B", 214, 14, 300, 56, Justification::centredLeft);
    if (frog.isValid())
        g.drawImage (frog, Rectangle<float> ((float) getWidth() - 120.0f, 10.0f, 92.0f, 69.0f), RectanglePlacement::centred);

    auto module = [&] (Rectangle<int> r, const String& title)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 14.0f);
        g.setColour (panelEdge);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 14.0f, 1.0f);
        g.setColour (lilacDeep);
        g.fillRoundedRectangle ((float) r.getX() + 14.0f, (float) r.getY() + 14.0f, 3.0f, 14.0f, 1.5f);
        g.setFont (FontOptions (14.0f, Font::bold));
        glowText (g, title, Rectangle<float> ((float) r.getX() + 24.0f, (float) r.getY() + 8.0f, 200.0f, 26.0f), Justification::centredLeft, lilac, 0.8f);
        g.setColour (textC);
        g.drawText (title, r.getX() + 24, r.getY() + 8, 200, 26, Justification::centredLeft);
    };
    module (modView, "Spectrum");
    module (modCtl, "Reduction");

    // legend and status
    g.setFont (FontOptions (11.5f));
    const auto lg = modView.withTrimmedLeft (150).withHeight (26).translated (0, 8).withTrimmedRight (16);
    g.setColour (lilac); g.drawText ("Input", lg.getX(), lg.getY(), 70, 26, Justification::centredLeft);
    g.setColour (pink); g.drawText ("Noise floor", lg.getX() + 70, lg.getY(), 100, 26, Justification::centredLeft);
    g.setColour (mint); g.drawText ("Reduction", lg.getX() + 170, lg.getY(), 100, 26, Justification::centredLeft);
    g.setColour (textDim);
    String st = proc.isLearning() ? "Learning: keep the room quiet, no voice" : (proc.hasProfile() ? "Noise profile loaded" : "No profile: it follows the noise floor by itself");
    g.drawText (st, modCtl.getX() + 130, modCtl.getY() + 8, modCtl.getWidth() - 146, 26, Justification::centredRight);

    // graph
    const auto gr = modView.withTrimmedTop (40).reduced (20, 8).withTrimmedBottom (18).toFloat();
    g.setColour (chassis.withAlpha (0.6f)); g.fillRoundedRectangle (gr, 6.0f);
    const int nb = gf::Denoiser::kDisplay;
    auto xOf = [&] (int i) { return gr.getX() + gr.getWidth() * ((float) i + 0.5f) / (float) nb; };
    // frequency grid
    g.setFont (FontOptions (10.5f));
    for (double f : { 100.0, 1000.0, 10000.0 })
    {
        const float x = gr.getX() + gr.getWidth() * (float) (std::log (f / 30.0) / std::log (20000.0 / 30.0));
        g.setColour (panelEdge); g.drawVerticalLine ((int) x, gr.getY(), gr.getBottom());
        g.setColour (textDim); g.drawText (f >= 1000 ? String ((int) (f / 1000)) + "k" : String ((int) f), (int) x - 20, (int) gr.getBottom() + 1, 40, 14, Justification::centred);
    }
    const float topDb = -20.0f, botDb = -130.0f;                          // power in dB (10 log10 of the squared-bin power)
    auto yOf = [&] (float db) { return gr.getBottom() - gr.getHeight() * std::clamp ((db - botDb) / (topDb - botDb), 0.0f, 1.0f); };
    const float shift = 0.0f;
    auto curve = [&] (const float* v, float off, bool fill, Colour c)
    {
        Path p;
        for (int i = 0; i < nb; ++i) { const float x = xOf (i), y = yOf (v[i] + off + shift); if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y); }
        if (fill)
        {
            Path f (p); f.lineTo (xOf (nb - 1), gr.getBottom()); f.lineTo (xOf (0), gr.getBottom()); f.closeSubPath();
            g.setGradientFill (ColourGradient (c.withAlpha (0.35f), 0.0f, gr.getY(), c.withAlpha (0.02f), 0.0f, gr.getBottom(), false));
            g.fillPath (f);
        }
        g.setColour (c); g.strokePath (p, PathStrokeType (1.6f, PathStrokeType::curved));
    };
    curve (smoothIn, 0.0f, true, lilac);
    curve (smoothNz, 0.0f, false, pink);
    // reduction: gain in dB drawn from the top (0 dB) downwards
    Path gp;
    for (int i = 0; i < nb; ++i)
    {
        const float x = xOf (i), y = gr.getY() + gr.getHeight() * std::clamp (-smoothG[i] / 60.0f, 0.0f, 1.0f);
        if (i == 0) gp.startNewSubPath (x, y); else gp.lineTo (x, y);
    }
    g.setColour (mint.withAlpha (0.9f)); g.strokePath (gp, PathStrokeType (1.4f, PathStrokeType::curved));
    g.setColour (mint.withAlpha (0.7f)); g.setFont (FontOptions (10.5f));
    g.drawText ("0 dB", (int) gr.getX() + 4, (int) gr.getY() + 1, 40, 12, Justification::centredLeft);
    g.drawText ("-60 dB", (int) gr.getX() + 4, (int) gr.getBottom() - 13, 50, 12, Justification::centredLeft);
}
