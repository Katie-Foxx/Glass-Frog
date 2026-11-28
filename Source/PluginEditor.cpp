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

// Lit line or arc: three wider, very faint strokes under the sharp one (same soft light as Rain Frog's lit lines).
void glowStroke (Graphics& g, const Path& p, Colour c, float width, float strength = 1.0f)
{
    for (float e : { 8.0f, 5.0f, 2.6f })
    {
        g.setColour (c.withAlpha (0.05f * strength));
        g.strokePath (p, PathStrokeType (width + e, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

// Rain Frog's recipe for lit boxes and button outlines: a few widening, very faint layers behind the shape.
void glowBox (Graphics& g, Rectangle<float> r, float corner, Colour c, float strength = 1.0f, float spread = 4.5f)
{
    for (int i = 4; i >= 1; --i)
    {
        const float e = spread * (float) i / 4.0f;
        g.setColour (c.withAlpha (0.028f * strength));
        g.fillRoundedRectangle (r.expanded (e), corner + e);
    }
}
void glowOutline (Graphics& g, Rectangle<float> r, float corner, Colour c, float strength = 1.0f)
{
    for (float w : { 7.0f, 5.0f, 3.0f })
    {
        g.setColour (c.withAlpha (0.022f * strength));
        g.drawRoundedRectangle (r, corner, w);
    }
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
class MaidFrogEditor::Pill : public Component
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
class MaidFrogEditor::Knob : public Component
{
public:
    Knob (AudioProcessorValueTreeState& s, const char* id, const String& name, const String& suffix = {}, int decimals = 2) : title (name)
    {
        slider.setSliderStyle (Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
        slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
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
        // Classic Lismok knob: dark body, lilac progress arc around it, pink pointer. No inner cap circle.
        void drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s) override
        {
            const float d = (float) std::min (w, h), r = d * 0.5f - 3.0f;
            const Point<float> c ((float) x + w * 0.5f, (float) y + h * 0.5f);
            const float ang = a0 + pos * (a1 - a0);
            Path track, val;
            track.addCentredArc (c.x, c.y, r, r, 0.0f, a0, a1, true);
            val.addCentredArc (c.x, c.y, r, r, 0.0f, a0, ang, true);
            g.setColour (panelEdge);
            g.strokePath (track, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
            if (pos > 0.004f) glowStroke (g, val, s.isMouseOverOrDragging() ? pink : lilac, 3.0f, 1.6f);
            g.setColour (s.isMouseOverOrDragging() ? pink : lilac);
            g.strokePath (val, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
            const float rb = r - 7.0f;
            g.setColour (Colours::black.withAlpha (0.5f));
            g.fillEllipse (c.x - rb, c.y - rb + 2.0f, rb * 2.0f, rb * 2.0f);
            g.setGradientFill (ColourGradient (Colour (0xff4a4560), c.x - rb * 0.6f, c.y - rb * 0.8f, Colour (0xff1c1a27), c.x + rb * 0.7f, c.y + rb * 0.9f, false));
            g.fillEllipse (c.x - rb, c.y - rb, rb * 2.0f, rb * 2.0f);
            g.setColour (panelEdge.brighter (0.2f));
            g.drawEllipse (c.x - rb, c.y - rb, rb * 2.0f, rb * 2.0f, 1.0f);
            const Point<float> u (std::cos (ang - MathConstants<float>::halfPi), std::sin (ang - MathConstants<float>::halfPi));
            { Path pl; pl.startNewSubPath (c + u * (rb * 0.18f)); pl.lineTo (c + u * (rb * 0.97f)); glowStroke (g, pl, pink, 2.6f, 2.2f); }
            g.setColour (pink);
            g.drawLine ({ c + u * (rb * 0.18f), c + u * (rb * 0.97f) }, 2.6f);
        }
    } look;
    Slider slider;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> att;
    String title;
};

// ---- noise library panel (opens when the frog is clicked) ---------------------------------------------------------------------
class MaidFrogEditor::Library : public Component, public ListBoxModel, private Timer
{
public:
    Library (MaidFrogEditor& e, MaidFrogProcessor& p) : ed (e), proc (p)
    {
        auto pill = [this] (std::unique_ptr<Pill>& b, const char* label, std::function<void()> f)
        {
            b = std::make_unique<Pill>(); b->label = label; b->onClick = std::move (f); addAndMakeVisible (*b);
        };
        pill (closeBtn, "Close", [this] { close(); });
        pill (prevBtn, "<", [this] { proc.stepEntry (-1); refresh(); });
        pill (nextBtn, ">", [this] { proc.stepEntry (1); refresh(); });
        pill (downBtn, "-", [this] { nudge (-1); });
        pill (upBtn, "+", [this] { nudge (1); });
        pill (matchBtn, "Match Level", [this] { doMatch(); });
        pill (saveBtn, "Save Current", [this] { doSave(); });
        pill (renameBtn, "Rename", [this] { doRename(); });
        pill (deleteBtn, "Delete", [this] { doDelete(); });
        list.setModel (this);
        list.setRowHeight (28);
        list.setColour (ListBox::backgroundColourId, chassis.withAlpha (0.6f));
        list.setColour (ListBox::outlineColourId, panelEdge);
        list.setOutlineThickness (1);
        addAndMakeVisible (list);
        proc.rescanLibrary();
        setWantsKeyboardFocus (true);
        startTimerHz (10);
        refresh();
    }
    ~Library() override { stopTimer(); }

    void resized() override
    {
        panelR = getLocalBounds().withSizeKeepingCentre (620, 330);
        const int x0 = panelR.getX(), y0 = panelR.getY();
        closeBtn->setBounds (panelR.getRight() - 100, y0 + 12, 80, 26);
        list.setBounds (x0 + 20, y0 + 54, 270, 256);
        const int rx = x0 + 310;
        prevBtn->setBounds (rx, y0 + 56, 40, 30);        nameR = { rx + 46, y0 + 56, 198, 30 };      nextBtn->setBounds (rx + 250, y0 + 56, 40, 30);
        downBtn->setBounds (rx, y0 + 124, 40, 30);       levelR = { rx + 46, y0 + 124, 198, 30 };    upBtn->setBounds (rx + 250, y0 + 124, 40, 30);
        matchBtn->setBounds (rx, y0 + 168, 290, 30);
        statusR = { rx, y0 + 204, 290, 52 };
        saveBtn->setBounds (rx, y0 + 270, 92, 28); renameBtn->setBounds (rx + 99, y0 + 270, 92, 28); deleteBtn->setBounds (rx + 198, y0 + 270, 92, 28);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colours::black.withAlpha (0.55f));
        glowOutline (g, panelR.toFloat().reduced (0.5f), 14.0f, lilac, 0.8f);
        g.setColour (panel); g.fillRoundedRectangle (panelR.toFloat(), 14.0f);
        g.setColour (panelEdge); g.drawRoundedRectangle (panelR.toFloat().reduced (0.5f), 14.0f, 1.0f);
        const int x0 = panelR.getX(), y0 = panelR.getY();
        g.setFont (FontOptions (15.0f, Font::bold));
        glowText (g, "Noise Library", Rectangle<float> ((float) x0 + 22.0f, (float) y0 + 10.0f, 300.0f, 30.0f), Justification::centredLeft, lilac, 0.9f);
        g.setColour (textC); g.drawText ("Noise Library", x0 + 22, y0 + 10, 300, 30, Justification::centredLeft);
        g.setFont (FontOptions (11.5f)); g.setColour (textDim);
        g.drawText ("Saved background noises (mic, room). Pick one, then Match so its level is the one of this take.", x0 + 170, y0 + 14, 330, 24, Justification::centredLeft);

        auto box = [&] (Rectangle<int> r, bool lit)
        {
            g.setColour (chassis.withAlpha (0.7f)); g.fillRoundedRectangle (r.toFloat(), 8.0f);
            g.setColour (lit ? pink : panelEdge); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
        };
        const bool has = proc.hasActive();
        const int idx = proc.activeIndex();
        box (nameR, false);
        g.setFont (FontOptions (13.0f, Font::bold));
        g.setColour (has ? (idx >= 0 ? pink : textC) : textDim);
        g.drawFittedText (has ? proc.activeName() + (idx < 0 ? " (not saved)" : String()) : String ("No profile"), nameR.reduced (8, 0), Justification::centred, 1);
        g.setFont (FontOptions (11.5f)); g.setColour (textDim);
        g.drawText ("Noise", nameR.getX(), nameR.getY() - 18, nameR.getWidth(), 16, Justification::centred);
        g.drawText ("Level (drag, double-click = 0)", levelR.getX() - 40, levelR.getY() - 18, levelR.getWidth() + 80, 16, Justification::centred);
        box (levelR, dragging);
        g.setFont (FontOptions (14.0f, Font::bold)); g.setColour (has ? textC : textDim);
        const double gdb = proc.activeGainDb();
        g.drawText (has ? String (gdb >= 0 ? "+" : "") + String (gdb, 1) + " dB" : String ("-"), levelR, Justification::centred);
        g.setFont (FontOptions (11.5f)); g.setColour (status.startsWith ("Matched") ? mint : textDim);
        g.drawFittedText (status, statusR, Justification::topLeft, 4);
        for (auto* c : getChildren())
            if (auto* b = dynamic_cast<Pill*> (c))
            {
                if (b->lit) glowBox (g, b->getBounds().toFloat().reduced (1.0f), 8.0f, pink, 0.8f, 4.0f);
                else glowOutline (g, b->getBounds().toFloat().reduced (1.0f), 8.0f, lilac);
            }
    }

    // list
    int getNumRows() override { return (int) infos.size(); }
    void paintListBoxItem (int row, Graphics& g, int w, int h, bool) override
    {
        if (row < 0 || row >= (int) infos.size()) return;
        const bool on = row == proc.activeIndex();
        if (on) { g.setColour (pink.withAlpha (0.25f)); g.fillRect (0, 0, w, h); }
        g.setFont (FontOptions (13.0f, on ? Font::bold : Font::plain));
        g.setColour (on ? Colours::white : textC);
        g.drawText (infos[(size_t) row].name, 10, 0, w - 90, h, Justification::centredLeft);
        g.setColour (textDim); g.setFont (FontOptions (11.5f));
        const double v = infos[(size_t) row].gainDb;
        g.drawText (String (v >= 0 ? "+" : "") + String (v, 1) + " dB", w - 80, 0, 70, h, Justification::centredRight);
    }
    void listBoxItemClicked (int row, const MouseEvent&) override { proc.selectEntry (row); status = {}; refresh(); }
    void paintOverChildren (Graphics& g) override
    {
        if (infos.empty())
        {
            g.setColour (textDim); g.setFont (FontOptions (12.0f));
            g.drawFittedText ("Empty. Use Learn Noise on a quiet passage, then open this panel and Save Current.", list.getBounds().reduced (18, 40), Justification::centredTop, 4);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (levelR.contains (e.getPosition()) && proc.hasActive())
        {
            dragging = true; dragY = e.y; dragStart = proc.activeGainDb();
            if (e.getNumberOfClicks() >= 2) { proc.setActiveGainDb (0.0, true); dragStart = 0.0; refresh(); }
        }
        else if (! panelR.contains (e.getPosition())) close();
    }
    void mouseDrag (const MouseEvent& e) override
    {
        if (dragging) { proc.setActiveGainDb (dragStart + (dragY - e.y) * (e.mods.isShiftDown() ? 0.02 : 0.1), false); repaint (levelR); }
    }
    void mouseUp (const MouseEvent&) override
    {
        if (dragging) { dragging = false; proc.setActiveGainDb (proc.activeGainDb(), true); refresh(); }
    }
    bool keyPressed (const KeyPress& k) override { if (k == KeyPress::escapeKey) { close(); return true; } return false; }

private:
    void close() { MessageManager::callAsync ([e = SafePointer<MaidFrogEditor> (&ed)] { if (e != nullptr) e->library.reset(); }); }   // deleted later, outside the click that asked for it
    void refresh()
    {
        proc.syncLearned();
        infos = proc.libraryInfo();
        list.updateContent();
        list.repaint();
        const bool has = proc.hasActive(), saved = proc.activeIndex() >= 0;
        for (auto* b : { matchBtn.get(), downBtn.get(), upBtn.get(), saveBtn.get() }) b->setAlpha (has ? 1.0f : 0.4f);
        for (auto* b : { renameBtn.get(), deleteBtn.get() }) b->setAlpha (saved ? 1.0f : 0.4f);
        for (auto* b : { prevBtn.get(), nextBtn.get() }) b->setAlpha (infos.empty() ? 0.4f : 1.0f);
        if (status.isEmpty() && ! has) status = "No noise in use. Learn one from a quiet passage (Learn Noise), then save it here with a name.";
        else if (status.isEmpty() && ! saved) status = "This noise is not saved yet. Press Save Current and give it a name (for example the mic).";
        repaint();
    }
    void timerCallback() override
    {
        const int idxBefore = lastIdx, gBefore = lastCount;
        lastIdx = proc.activeIndex(); lastCount = (int) proc.libraryInfo().size();
        if (waiting > 0)
        {
            const int r = proc.pollMatch();
            if (r == 2) { waiting = 0; status = "Matched: the stored noise now sits at " + String (proc.activeGainDb() >= 0 ? "+" : "") + String (proc.activeGainDb(), 1) + " dB."; }
            else if (r == 3) { waiting = 0; status = "No noise floor measured yet: let the track play first (it needs some pauses to hear the noise)."; }
            else if (--waiting == 0) status = "Nothing heard: start playback, then press Match Level again.";
            refresh();
        }
        else if (idxBefore != lastIdx || gBefore != lastCount || proc.hasActive() != hadActive) { hadActive = proc.hasActive(); if (proc.hasActive()) status = {}; refresh(); }
        else if (! dragging) repaint (levelR);
    }
    void nudge (int dir)
    {
        if (! proc.hasActive()) return;
        proc.setActiveGainDb (proc.activeGainDb() + dir * (ModifierKeys::currentModifiers.isShiftDown() ? 0.1 : 0.5), true);
        refresh();
    }
    void doMatch()
    {
        if (! proc.hasActive()) { status = "Pick or learn a noise first."; refresh(); return; }
        proc.requestMatch(); waiting = 40; status = "Measuring the noise floor of this take... keep it playing."; refresh();
    }
    void askName (const String& title, const String& def, std::function<void (const String&)> done)
    {
        auto* w = new AlertWindow (title, "Name (for example SM7B, NT1, 102):", MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", def);
        w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        w->enterModalState (true, ModalCallbackFunction::create ([w, done = std::move (done)] (int r)
                                                                 { if (r == 1) done (w->getTextEditorContents ("name")); }), true);
    }
    void doSave()
    {
        proc.syncLearned();
        if (! proc.hasActive()) { status = "Nothing to save: learn a noise first."; refresh(); return; }
        const String def = proc.activeIndex() >= 0 ? proc.activeName() : "Mic " + String ((int) infos.size() + 1);
        askName ("Save noise", def, [safe = SafePointer<Library> (this)] (const String& n)
                 { if (safe != nullptr) { safe->status = safe->proc.saveCurrent (n) ? "Saved as " + n.trim() + "." : "Could not save."; safe->refresh(); } });
    }
    void doRename()
    {
        const int i = proc.activeIndex();
        if (i < 0) return;
        askName ("Rename noise", proc.activeName(), [safe = SafePointer<Library> (this), i] (const String& n)
                 { if (safe != nullptr) { safe->proc.renameEntry (i, n); safe->status = {}; safe->refresh(); } });
    }
    void doDelete()
    {
        const int i = proc.activeIndex();
        if (i < 0) return;
        AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::QuestionIcon).withTitle ("Delete noise")
                                    .withMessage ("Delete \"" + proc.activeName() + "\" from the library?").withButton ("Delete").withButton ("Cancel").withAssociatedComponent (this),
                                [safe = SafePointer<Library> (this), i] (int r)
                                { if (safe != nullptr && r == 1) { safe->proc.deleteEntry (i); safe->status = {}; safe->refresh(); } });
    }

    MaidFrogEditor& ed;
    MaidFrogProcessor& proc;
    std::unique_ptr<Pill> closeBtn, prevBtn, nextBtn, downBtn, upBtn, matchBtn, saveBtn, renameBtn, deleteBtn;
    ListBox list;
    std::vector<MaidFrogProcessor::NoiseInfo> infos;
    Rectangle<int> panelR, nameR, levelR, statusR;
    String status;
    bool dragging = false, hadActive = false;
    int dragY = 0, waiting = 0, lastIdx = -2, lastCount = -1;
    double dragStart = 0.0;
};

// ---- editor -------------------------------------------------------------------------------------------------------
MaidFrogEditor::MaidFrogEditor (MaidFrogProcessor& p)
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
MaidFrogEditor::~MaidFrogEditor() { stopTimer(); library.reset(); setLookAndFeel (nullptr); }

void MaidFrogEditor::openLibrary()
{
    if (library != nullptr) return;
    library = std::make_unique<Library> (*this, proc);
    addAndMakeVisible (*library);
    library->setBounds (getLocalBounds());
    library->grabKeyboardFocus();
}
void MaidFrogEditor::mouseUp (const MouseEvent& e)
{
    if (library == nullptr && frogArea().contains (e.getPosition())) openLibrary();
}
void MaidFrogEditor::mouseMove (const MouseEvent& e)
{
    setMouseCursor (library == nullptr && frogArea().contains (e.getPosition()) ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
}

void MaidFrogEditor::toggleParam (const char* id)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
}

void MaidFrogEditor::timerCallback()
{
    proc.syncLearned();
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
    repaint (frogArea());
    repaint (modView);
    repaint (modCtl.getX(), modCtl.getY(), modCtl.getWidth(), 34);
}

void MaidFrogEditor::resized()
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

void MaidFrogEditor::paint (Graphics& g)
{
    g.fillAll (chassis);
    g.setGradientFill (ColourGradient (lilacDeep.withAlpha (0.20f), 0.0f, 0.0f, Colours::transparentBlack, 0.0f, 170.0f, false));
    g.fillRect (0, 0, getWidth(), getHeight());
    g.setFont (makeWordmark (46.0f));
    glowText (g, "Lismok", Rectangle<float> (28, 14, 170, 56), Justification::centredLeft, lilac, 1.4f);
    g.setColour (lilac);
    g.drawText ("Lismok", 28, 14, 170, 56, Justification::centredLeft);
    // the divider sits exactly halfway between the end of "Lismok" and the start of the name
    GlyphArrangement ga;
    ga.addLineOfText (makeWordmark (46.0f), "Lismok", 0.0f, 0.0f);
    const float wmW = ga.getBoundingBox (0, -1, true).getWidth();
    const float divX = 28.0f + wmW + 16.0f, nameX = divX + 2.0f + 16.0f;
    g.setColour (panelEdge);
    g.fillRect (divX, 24.0f, 2.0f, 38.0f);
    glowText (g, "Maid Frog", Rectangle<float> (nameX, 14, 300, 56), Justification::centredLeft, textC, 1.0f);
    g.setColour (textC);
    g.drawText ("Maid Frog", Rectangle<float> (nameX, 14, 300, 56), Justification::centredLeft);
    if (frog.isValid())
        g.drawImage (frog, Rectangle<float> ((float) getWidth() - 132.0f, 6.0f, 108.0f, 76.0f), RectanglePlacement::centred);

    auto module = [&] (Rectangle<int> r, const String& title)
    {
        glowOutline (g, r.toFloat().reduced (0.5f), 14.0f, lilac, 0.5f);
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 14.0f);
        g.setColour (panelEdge);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 14.0f, 1.0f);
        glowBox (g, { (float) r.getX() + 14.0f, (float) r.getY() + 14.0f, 3.0f, 14.0f }, 1.5f, lilac, 0.9f, 3.5f);
        g.setColour (lilacDeep);
        g.fillRoundedRectangle ((float) r.getX() + 14.0f, (float) r.getY() + 14.0f, 3.0f, 14.0f, 1.5f);
        g.setFont (FontOptions (14.0f, Font::bold));
        glowText (g, title, Rectangle<float> ((float) r.getX() + 24.0f, (float) r.getY() + 8.0f, 200.0f, 26.0f), Justification::centredLeft, lilac, 0.8f);
        g.setColour (textC);
        g.drawText (title, r.getX() + 24, r.getY() + 8, 200, 26, Justification::centredLeft);
    };
    module (modView, "Spectrum");
    module (modCtl, "Reduction");

    // button glows, drawn behind the buttons (the same recipe as Rain Frog): lit ones glow pink, the others have a faint outline
    for (auto* c : getChildren())
        if (auto* b = dynamic_cast<Pill*> (c))
            if (b->isVisible())
            {
                if (b->lit) glowBox (g, b->getBounds().toFloat().reduced (1.0f), 8.0f, pink, 0.8f, 4.0f);
                else glowOutline (g, b->getBounds().toFloat().reduced (1.0f), 8.0f, lilac);
            }

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
        glowStroke (g, p, c, 1.6f, 1.0f);
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
    glowStroke (g, gp, mint, 1.4f, 0.8f);
    g.setColour (mint.withAlpha (0.9f)); g.strokePath (gp, PathStrokeType (1.4f, PathStrokeType::curved));
    g.setColour (mint.withAlpha (0.7f)); g.setFont (FontOptions (10.5f));
    g.drawText ("0 dB", (int) gr.getX() + 4, (int) gr.getY() + 1, 40, 12, Justification::centredLeft);
    g.drawText ("-60 dB", (int) gr.getX() + 4, (int) gr.getBottom() - 13, 50, 12, Justification::centredLeft);
}
