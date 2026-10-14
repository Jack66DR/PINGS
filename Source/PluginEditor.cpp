#include "PluginEditor.h"
#include "BinaryData.h"

using namespace png::ui;

//==============================================================================
juce::Font png::ui::mono (float height)
{
    static juce::Typeface::Ptr stm = juce::Typeface::createSystemTypefaceFor (BinaryData::ShareTechMonoRegular_ttf,
                                                                             BinaryData::ShareTechMonoRegular_ttfSize);
    return juce::Font (juce::FontOptions (stm).withHeight (height * kFontScale));
}

static float textWidth (const juce::Font& f, const juce::String& s)
{
    return juce::GlyphArrangement::getStringWidth (f, s);
}

void png::ui::glowText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j,
                        const juce::Font& f, juce::Colour c, float alpha)
{
    g.setFont (f);
    static const float offs[8][2] = { { -1.6f, 0.0f }, { 1.6f, 0.0f }, { 0.0f, -1.6f }, { 0.0f, 1.6f },
                                      { -1.1f, -1.1f }, { 1.1f, 1.1f }, { -1.1f, 1.1f }, { 1.1f, -1.1f } };
    g.setColour (c.withAlpha (0.10f * alpha));
    for (auto& o : offs)
        g.drawText (text, r.translated (o[0], o[1]), j, false);
    g.setColour (c.withAlpha (alpha));
    g.drawText (text, r, j, false);
}

void png::ui::glowStroke (juce::Graphics& g, const juce::Path& p, float width, juce::Colour c, float alpha)
{
    const auto js = juce::PathStrokeType::curved;
    g.setColour (c.withAlpha (0.07f * alpha));
    g.strokePath (p, juce::PathStrokeType (width * 5.0f, js, juce::PathStrokeType::rounded));
    g.setColour (c.withAlpha (0.16f * alpha));
    g.strokePath (p, juce::PathStrokeType (width * 2.6f, js, juce::PathStrokeType::rounded));
    g.setColour (c.withAlpha (alpha));
    g.strokePath (p, juce::PathStrokeType (width, js, juce::PathStrokeType::rounded));
}

//==============================================================================
LookAndFeel::LookAndFeel (const Skin& s) : skin (s)
{
    setColour (juce::ResizableWindow::backgroundColourId, tube);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float startAngle, float endAngle, juce::Slider& s)
{
    const float size = (float) juce::jmin (w, h) - 16.0f;
    const float radius = size * 0.5f - 3.0f;
    const auto centre = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    constexpr int ticks = 31;
    const float r0 = radius + 2.0f, r1 = radius + 8.0f;
    for (int i = 0; i < ticks; ++i)
    {
        const float a = startAngle + (float) i / (ticks - 1) * (endAngle - startAngle);
        const bool on = a >= juce::jmin (from, angle) - 0.001f && a <= juce::jmax (from, angle) + 0.001f;
        juce::Path t;
        t.startNewSubPath (centre.x + r0 * std::sin (a), centre.y - r0 * std::cos (a));
        t.lineTo (centre.x + r1 * std::sin (a), centre.y - r1 * std::cos (a));
        if (on)
            glowStroke (g, t, 2.0f, cyan, 0.95f);
        else
        {
            g.setColour (cyan.withAlpha (0.22f));
            g.strokePath (t, juce::PathStrokeType (1.6f));
        }
    }

    juce::Path bezel;
    bezel.addEllipse (juce::Rectangle<float> (radius * 1.6f, radius * 1.6f).withCentre (centre));
    g.setColour (cyan.withAlpha (0.45f));
    g.strokePath (bezel, juce::PathStrokeType (1.5f));

    juce::Path needle;
    needle.startNewSubPath (centre.x + radius * 0.18f * std::sin (angle), centre.y - radius * 0.18f * std::cos (angle));
    needle.lineTo (centre.x + radius * 0.78f * std::sin (angle), centre.y - radius * 0.78f * std::cos (angle));
    glowStroke (g, needle, 2.6f, cyan);

    g.setColour (mag);
    g.fillRect (juce::Rectangle<float> (3.0f, 3.0f).withCentre (centre));
}

//==============================================================================
static constexpr float kLabelSize = 17.0f, kValueSize = 15.0f, kKnobSize = 64.0f;

Knob::Knob (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& text)
    : label (text)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.setTitle (text);
    slider.getProperties().set ("bipolar", juce::String (paramId) == ParamIDs::frequency);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    param = state.getParameter (paramId);
    if (param != nullptr)
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
}

void Knob::resized()
{
    const float top = kLabelSize * kFontScale + 12.0f;
    slider.setBounds (juce::Rectangle<float> (kKnobSize + 16.0f, kKnobSize + 16.0f)
                          .withCentre ({ getWidth() * 0.5f, top + kKnobSize * 0.5f + 4.0f })
                          .toNearestInt());
}

void Knob::paint (juce::Graphics& g)
{
    const float w = (float) getWidth();
    const float labelH = kLabelSize * kFontScale;
    const float vy = labelH + 12.0f + kKnobSize + 18.0f;
    const juce::String value = param != nullptr ? param->getCurrentValueAsText() : juce::String();
    glowText (g, label, { 0.0f, 0.0f, w, labelH + 4.0f }, juce::Justification::centred,
              mono (kLabelSize).withExtraKerningFactor (0.14f), cyan);
    glowText (g, value.toUpperCase(), { 0.0f, vy, w, kValueSize * kFontScale + 6.0f }, juce::Justification::centred,
              mono (kValueSize), cyan, 0.8f);
}

//==============================================================================
Segments::Segments (juce::AudioProcessorValueTreeState& state, const char* paramId, juce::StringArray l, int cols, juce::Colour c)
    : labels (std::move (l)), columns (juce::jmax (1, cols)), lit (c)
{
    param = state.getParameter (paramId);
    setTitle (param != nullptr ? param->getName (40) : juce::String (paramId));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int Segments::selected() const
{
    if (param == nullptr || labels.size() < 2) return 0;
    return juce::roundToInt (param->getValue() * (float) (labels.size() - 1));
}

juce::Rectangle<float> Segments::cell (int i) const
{
    const int rows = (labels.size() + columns - 1) / columns;
    const float cw = (float) getWidth() / (float) columns, ch = (float) getHeight() / (float) rows;
    return { (float) (i % columns) * cw, (float) (i / columns) * ch, cw, ch };
}

void Segments::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < labels.size(); ++i)
        if (cell (i).contains (e.position) && param != nullptr)
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost ((float) i / (float) juce::jmax (1, labels.size() - 1));
            param->endChangeGesture();
            repaint();
            return;
        }
}

void Segments::paint (juce::Graphics& g)
{
    const int sel = selected();
    const float a = live ? 1.0f : 0.3f;
    const auto f = mono (columns >= 3 ? 13.0f : (labels.size() > 4 ? 16.0f : 15.0f)).withExtraKerningFactor (columns >= 3 ? 0.04f : 0.12f);
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto r = cell (i).reduced (3.0f);
        const juce::Colour c = (i == labels.size() - 1 && lastIsSpecial) ? mag : lit;
        if (i == sel)
        {
            g.setColour (c.withAlpha (0.18f * a));
            g.fillRect (r.expanded (2.0f));
            g.setColour (c.withAlpha (a));
            g.fillRect (r);
            g.setColour (tube);
            g.setFont (f);
            g.drawText (labels[i], r, juce::Justification::centred);
        }
        else
        {
            juce::Path box;
            box.addRectangle (r);
            g.setColour (c.withAlpha (0.35f * a));
            g.strokePath (box, juce::PathStrokeType (1.0f));
            glowText (g, labels[i], r, juce::Justification::centred, f, c, 0.75f * a);
        }
    }
}

//==============================================================================
// Pond geometry, in its own 392 x 476 space.
static constexpr float kPondTop = 78.0f, kPondBottom = 372.0f, kScopeMid = 430.0f;

Pond::Pond (PingsProcessor& p, const Skin& s) : proc (p), skin (s)
{
    setTitle ("Every strike drops a ring in the pond");
    setInterceptsMouseClicks (false, false);
    seen = proc.engine->uiStrikes.load();
}

void Pond::tick (double dt)
{
    auto& e = *proc.engine;
    model = juce::roundToInt (proc.apvts.getRawParameterValue (ParamIDs::model)->load());
    voices = juce::roundToInt (proc.apvts.getRawParameterValue (ParamIDs::voices)->load()) + 1;
    chord = e.uiChord.load();
    const float damping = proc.apvts.getRawParameterValue (ParamIDs::damping)->load();

    for (auto& r : ripples) r.age += (float) dt;
    for (auto& l : lamp) l *= 0.9f;
    lastAge += (float) dt;

    const uint32_t now = e.uiStrikes.load();
    if (now - seen > 16) seen = now - 16;
    for (; seen != now; ++seen)
    {
        const int note = e.uiStrikeNote[(size_t) (seen % 16)].load();
        const int v = juce::jlimit (0, png::kMaxVoices - 1, e.uiStrikeVoice[(size_t) (seen % 16)].load());
        auto& r = ripples[(size_t) ripW];
        ripW = (ripW + 1) % (int) ripples.size();
        r.age = 0.0f;
        r.life = 0.8f + 3.5f * damping;
        r.note = note;
        r.voice = v;
        r.x = juce::jlimit (0.08f, 0.92f, (float) (note - 24) / 84.0f);
        lamp[(size_t) v] = 1.0f;
        lastNote = note;
        lastAge = 0.0f;
    }

    const int w = e.uiScopeW.load();
    for (int k = 0; k < png::kScope; ++k)
        scope[(size_t) k] = e.uiScope[(size_t) ((w + k) % png::kScope)].load();
    repaint();
}

void Pond::paint (juce::Graphics& g)
{
    g.fillAll (tube);
    const float w = (float) getWidth(), h = (float) getHeight();
    const auto readout = mono (15.0f).withExtraKerningFactor (0.16f);
    const auto small = mono (13.0f).withExtraKerningFactor (0.12f);
    const bool dp = model == png::kDisastrousPeace;

    // ---- header: the model, and the chord when it uses one
    glowText (g, png::kModelName[juce::jlimit (0, png::kNumModels - 1, model)], { 16.0f, 8.0f, 260.0f, 22.0f },
              juce::Justification::centredLeft, readout, dp ? mag : cyan, 0.95f);
    if (model == 4 || dp)
        glowText (g, juce::String ("CHORD ") + png::kChordName[juce::jlimit (0, png::kNumChords - 1, chord)],
                  { w - 196.0f, 8.0f, 180.0f, 22.0f }, juce::Justification::centredRight, readout, mag, 0.9f);

    // ---- voice lamps
    const float lw = (w - 32.0f - 3.0f * 8.0f) / 4.0f;
    for (int v = 0; v < png::kMaxVoices; ++v)
    {
        const auto r = juce::Rectangle<float> (16.0f + (float) v * (lw + 8.0f), 38.0f, lw, 26.0f);
        const bool on = v < voices;
        const float l = lamp[(size_t) v];
        if (on && l > 0.05f)
        {
            g.setColour (mag.withAlpha (0.15f * l));
            g.fillRect (r.expanded (2.0f));
            g.setColour (mag.withAlpha (0.85f * l));
            g.fillRect (r);
        }
        juce::Path box;
        box.addRectangle (r);
        g.setColour ((on ? cyan : cyan.withAlpha (0.25f)).withAlpha (on ? 0.6f : 0.15f));
        g.strokePath (box, juce::PathStrokeType (1.0f));
        const juce::String name = juce::String ("V") + juce::String (v + 1) + (v % 2 == 0 ? " ODD" : " EVEN");
        glowText (g, name, r, juce::Justification::centred, small, on ? (l > 0.5f ? tube : cyan) : cyan, on ? 0.85f : 0.2f);
    }

    // ---- the pond
    const auto pond = juce::Rectangle<float> (16.0f, kPondTop, w - 32.0f, kPondBottom - kPondTop);
    g.setColour (cyan.withAlpha (0.06f));
    for (float y = pond.getY() + 12.0f; y < pond.getBottom(); y += 18.0f)
        for (float x = pond.getX() + 6.0f; x < pond.getRight(); x += 9.0f)
            g.fillRect (x, y, 3.0f, 1.0f);

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (pond.toNearestInt());
        const float cy = pond.getCentreY();
        for (auto& r : ripples)
        {
            if (r.age > r.life) continue;
            const float u = r.age / r.life;
            const float alpha = std::pow (1.0f - u, 1.5f);
            const auto c = mag.interpolatedWith (cyan, juce::jlimit (0.0f, 1.0f, r.age / 0.35f));
            const float cx = pond.getX() + r.x * pond.getWidth();
            for (int ring = 0; ring < 3; ++ring)
            {
                const float rad = 6.0f + (r.age - (float) ring * 0.12f) * 130.0f;
                if (rad <= 2.0f) continue;
                juce::Path p;
                p.addEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 0.9f).withCentre ({ cx, cy }));
                glowStroke (g, p, 1.3f, c, alpha * (1.0f - 0.3f * (float) ring));
            }
            if (r.age < 0.25f)
            {
                g.setColour (mag.withAlpha (1.0f - r.age / 0.25f));
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ cx, cy }));
            }
        }
    }

    if (lastNote >= 0 && lastAge < 2.5f)
        glowText (g, png::noteName (lastNote), { 16.0f, kPondBottom - 40.0f, w - 32.0f, 34.0f }, juce::Justification::centred,
                  mono (26.0f).withExtraKerningFactor (0.2f), mag, juce::jlimit (0.0f, 1.0f, 1.0f - lastAge / 2.5f));
    else if (lastNote < 0)
        glowText (g, skin.blink() ? "PLAY SOMETHING" : "", { 16.0f, pond.getCentreY() - 12.0f, w - 32.0f, 24.0f },
                  juce::Justification::centred, readout, cyan, 0.6f);

    juce::Path pf;
    pf.addRectangle (pond);
    g.setColour (cyan.withAlpha (0.3f));
    g.strokePath (pf, juce::PathStrokeType (1.0f));

    // ---- scope
    glowText (g, "OUT", { 16.0f, kScopeMid - 44.0f, 60.0f, 18.0f }, juce::Justification::centredLeft, small, cyan, 0.6f);
    {
        juce::Path s;
        const float x0 = 16.0f, sw = w - 32.0f;
        for (int k = 0; k < png::kScope; ++k)
        {
            const float v = juce::jlimit (-1.0f, 1.0f, scope[(size_t) k] * 2.5f);
            const float x = x0 + sw * (float) k / (float) (png::kScope - 1);
            const float y = kScopeMid - v * 26.0f;
            if (k == 0) s.startNewSubPath (x, y); else s.lineTo (x, y);
        }
        glowStroke (g, s, 1.2f, cyan, 0.85f);
    }

    juce::Path frame;
    frame.addRectangle (getLocalBounds().toFloat().reduced (1.0f));
    glowStroke (g, frame, 1.4f, cyan, 0.85f);
    (void) h;
}

//==============================================================================
Keys::Keys (PingsProcessor& p, const Skin& s) : proc (p), skin (s)
{
    setTitle ("Keys: click to strike a note; MIDI shows here too");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    seen = proc.engine->uiStrikes.load();
}

bool Keys::isBlack (int note)
{
    const int pc = note % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

static constexpr float kKeysH = 92.0f;

juce::Rectangle<float> Keys::keyRect (int note) const
{
    int whites = 0;
    for (int n = kFirst; n <= kLast; ++n)
        if (! isBlack (n)) ++whites;
    const float ww = (float) getWidth() / (float) whites;
    int idx = 0;
    for (int n = kFirst; n < note; ++n)
        if (! isBlack (n)) ++idx;
    if (! isBlack (note))
        return { idx * ww, 2.0f, ww, kKeysH };
    const float bw = ww * 0.62f;
    return { idx * ww - bw * 0.5f, 2.0f, bw, kKeysH * 0.6f };
}

int Keys::noteAt (juce::Point<float> pt) const
{
    for (int n = kFirst; n <= kLast; ++n)
        if (isBlack (n) && keyRect (n).contains (pt)) return n;
    for (int n = kFirst; n <= kLast; ++n)
        if (! isBlack (n) && keyRect (n).contains (pt)) return n;
    return -1;
}

void Keys::mouseDown (const juce::MouseEvent& e)
{
    const int n = noteAt (e.position);
    if (n >= 0)
        proc.engine->uiNoteOn (n);
}

void Keys::tick (double dt)
{
    const uint64_t lo = proc.engine->uiHeldLo.load(), hi = proc.engine->uiHeldHi.load();
    for (int k = 0; k < 64; ++k)
    {
        held[(size_t) k] = ((lo >> k) & 1u) != 0;
        held[(size_t) (k + 64)] = ((hi >> k) & 1u) != 0;
    }
    for (auto& f : flash) f = juce::jmax (0.0f, f - (float) dt * 2.5f);
    // a strike lights its key for a moment; FREQUENCY is taken back off so the key you played lights
    const int transpose = juce::roundToInt (proc.apvts.getRawParameterValue (ParamIDs::frequency)->load());
    const uint32_t now = proc.engine->uiStrikes.load();
    if (now - seen > 16) seen = now - 16;
    for (; seen != now; ++seen)
    {
        const int n = proc.engine->uiStrikeNote[(size_t) (seen % 16)].load() - transpose;
        if (n >= 0 && n < 128) flash[(size_t) n] = 1.0f;
    }
    repaint();
}

void Keys::paint (juce::Graphics& g)
{
    g.fillAll (tube);
    for (int pass = 0; pass < 2; ++pass)
        for (int n = kFirst; n <= kLast; ++n)
        {
            if (isBlack (n) != (pass == 1)) continue;
            auto r = keyRect (n).reduced (pass == 0 ? 1.5f : 0.0f, 0.0f);
            const float f = held[(size_t) n] ? 1.0f : flash[(size_t) n];
            g.setColour (pass == 0 ? tube : tube.brighter (0.05f));
            g.fillRect (r);
            if (f > 0.02f)
            {
                g.setColour (mag.withAlpha (0.2f * f));
                g.fillRect (r.expanded (2.0f));
                g.setColour (mag.withAlpha (f));
                g.fillRect (r);
            }
            g.setColour (cyan.withAlpha (pass == 0 ? 0.35f : 0.55f));
            g.drawRect (r, 1.0f);
        }
    const auto f = mono (13.0f).withExtraKerningFactor (0.1f);
    for (int n = kFirst; n <= kLast; n += 12)
    {
        const auto r = keyRect (n);
        glowText (g, png::noteName (n), { r.getX(), kKeysH + 6.0f, 40.0f, 18.0f }, juce::Justification::centredLeft, f, cyan, 0.6f);
    }
}

//==============================================================================
juce::Font Title::font() { return mono (46.0f).withExtraKerningFactor (0.42f); }

void Title::tick()
{
    const bool b = skin.blink();
    if (glitch > 0)
    {
        --glitch;
        repaint();
    }
    else if (random.nextFloat() < 0.003f)
        glitch = 2 + random.nextInt (3);
    else if (b != lastBlink)
        repaint();
    lastBlink = b;
}

void Title::paint (juce::Graphics& g)
{
    const auto f = font();
    const auto area = juce::Rectangle<float> (6.0f, 0.0f, (float) getWidth() - 6.0f, (float) getHeight());
    const juce::String word ("PINGS");
    if (glitch <= 0)
        glowText (g, word, area, juce::Justification::centredLeft, f, cyan);
    else
    {
        static const char* junk = "#%&@$01<>/\\|=+*?";
        juce::String bent;
        for (int k = 0; k < word.length(); ++k)
            bent << (random.nextFloat() < 0.25f ? juce::String::charToString ((juce::juce_wchar) junk[random.nextInt (16)])
                                                : word.substring (k, k + 1));
        int y = 0;
        while (y < getHeight())
        {
            const int band = 6 + random.nextInt (18);
            const float dx = random.nextFloat() < 0.5f ? (random.nextFloat() * 2.0f - 1.0f) * 12.0f : 0.0f;
            g.saveState();
            g.reduceClipRegion (0, y, getWidth(), band);
            glowText (g, bent, area.translated (dx, 0.0f), juce::Justification::centredLeft, f, cyan);
            g.restoreState();
            y += band;
        }
    }
    if (skin.blink())
    {
        const float x = 6.0f + textWidth (f, word) + 6.0f;
        const auto r = juce::Rectangle<float> (x, getHeight() * 0.5f - 20.0f, 24.0f, 40.0f);
        g.setColour (mag.withAlpha (0.18f));
        g.fillRect (r);
        g.setColour (mag);
        g.fillRect (r.reduced (3.0f));
    }
}

//==============================================================================
Glass::Glass (const Skin& s) : skin (s)
{
    setInterceptsMouseClicks (false, false);
}

void Glass::resized()
{
    lines = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
    juce::Graphics g (lines);
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    for (int y = 0; y < getHeight(); y += 3)
        g.fillRect (0, y, getWidth(), 1);
    const float w = (float) getWidth(), h = (float) getHeight();
    juce::ColourGradient v (juce::Colours::transparentBlack, w * 0.5f, h * 0.5f,
                            juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    v.addColour (0.6, juce::Colours::transparentBlack);
    g.setGradientFill (v);
    g.fillRect (getLocalBounds());
}

void Glass::paint (juce::Graphics& g)
{
    const double age = skin.t - bootStart;
    if (age >= kBootSecs)
    {
        g.drawImageAt (lines, 0, 0);
        return;
    }
    const float fade = age < 2.7 ? 1.0f : (float) (1.0 - (age - 2.7) / (kBootSecs - 2.7));
    g.setColour (tube.withAlpha (juce::jlimit (0.0f, 1.0f, fade)));
    g.fillRect (getLocalBounds());

    static const char* boot[] = {
        "MANNERS! DSP  INTERFACE SYSTEM",
        "",
        "PINGS  RESONATOR",
        "MODELS ................ 7, NONE SECRET",
        "VOICES ................ 4",
        "AFTER ................. RINGS, EMILIE GILLET",
        "POND .................. STILL",
        "",
        "> STRIKE A KEY" };
    const int totalChars = (int) (age * 95.0);
    const auto f = mono (20.0f).withExtraKerningFactor (0.08f);
    int used = 0;
    float y = 200.0f, curX = 250.0f, curY = 200.0f;
    for (auto* line : boot)
    {
        const juce::String s (line);
        const int show = juce::jlimit (0, s.length(), totalChars - used);
        used += s.length() + 4;
        if (show > 0)
            glowText (g, s.substring (0, show), { 250.0f, y, 800.0f, 30.0f }, juce::Justification::centredLeft, f,
                      s.contains ("STILL") ? mag : cyan, fade);
        curX = 250.0f + textWidth (f, s.substring (0, show)) + 4.0f;
        curY = y;
        if (show < s.length() || totalChars - used < 0)
            break;
        y += 34.0f;
    }
    if (std::fmod (age, 0.4) < 0.22)
    {
        g.setColour (mag.withAlpha (fade));
        g.fillRect (curX, curY + 5.0f, 12.0f, 21.0f);
    }
    g.drawImageAt (lines, 0, 0);
}

//==============================================================================
void PingsEditor::Panel::paint (juce::Graphics& g)
{
    g.fillAll (tube);
    glowText (g, "MANNERS! DSP  //  RESONATOR", { 60.0f, 92.0f, 640.0f, 24.0f },
              juce::Justification::centredLeft, mono (15.0f).withExtraKerningFactor (0.3f), cyan, 0.8f);
    glowText (g, "DOUBLE-CLICK A KNOB TO RESET", { 600.0f, 92.0f, 544.0f, 24.0f },
              juce::Justification::centredRight, mono (13.0f).withExtraKerningFactor (0.08f), cyan, 0.6f);
    glowText (g, "STRIKE IT. OR FEED IT", { 620.0f, 44.0f, 524.0f, 24.0f },
              juce::Justification::centredRight, mono (14.0f).withExtraKerningFactor (0.3f), mag, 0.8f);

    auto box = [&g] (juce::Rectangle<float> r, const char* name)
    {
        const auto f = mono (14.0f).withExtraKerningFactor (0.3f);
        const juce::String label = juce::String ("[ ") + name + " ]";
        const float tw = textWidth (f, label) + 12.0f;
        const float tx = r.getX() + 18.0f;
        juce::Path p;
        p.startNewSubPath (tx, r.getY());
        p.lineTo (r.getX(), r.getY());
        p.lineTo (r.getX(), r.getBottom());
        p.lineTo (r.getRight(), r.getBottom());
        p.lineTo (r.getRight(), r.getY());
        p.lineTo (tx + tw, r.getY());
        glowStroke (g, p, 1.4f, cyan, 0.85f);
        glowText (g, label, { tx, r.getY() - 11.0f, tw, 22.0f }, juce::Justification::centred, f, cyan);
    };
    box ({ 40.0f, 140.0f, 346.0f, 466.0f }, "RESONATOR");
    box ({ 814.0f, 140.0f, 346.0f, 466.0f }, "MODEL");
    box ({ 40.0f, 626.0f, 560.0f, 166.0f }, "KEYS");
    box ({ 614.0f, 626.0f, 546.0f, 166.0f }, "IN AND OUT");

    const auto cap = mono (13.0f).withExtraKerningFactor (0.2f);
    glowText (g, "VOICES", { 836.0f, 380.0f, 200.0f, 18.0f }, juce::Justification::centredLeft, cap, cyan, 0.7f);
    glowText (g, "PEACE FX", { 836.0f, 452.0f, 200.0f, 18.0f }, juce::Justification::centredLeft, cap, mag, 0.8f);
    glowText (g, "EXCITER", { 636.0f, 646.0f, 200.0f, 18.0f }, juce::Justification::centredLeft, cap, cyan, 0.7f);
    glowText (g, "ONSET STRUM", { 636.0f, 710.0f, 200.0f, 18.0f }, juce::Justification::centredLeft, cap, cyan, 0.7f);

    g.setColour (cyan.withAlpha (0.35f));
    g.fillRect (56.0f, 120.0f, 1088.0f, 1.0f);
}

//==============================================================================
static juce::StringArray namesOf (const char* const* names, int n)
{
    juce::StringArray s;
    for (int i = 0; i < n; ++i) s.add (names[i]);
    return s;
}

PingsEditor::PingsEditor (PingsProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), pond (p, skin), keys (p, skin),
      models (p.apvts, ParamIDs::model, namesOf (png::kModelName, png::kNumModels), 1),
      voices (p.apvts, ParamIDs::voices, { "1", "2", "3", "4" }, 4),
      fx (p.apvts, ParamIDs::fx, namesOf (png::kFxName, png::kNumFx), 3, mag),
      exciter (p.apvts, ParamIDs::exciter, { "INTERNAL", "INPUT" }, 2),
      onset (p.apvts, ParamIDs::onset, { "OFF", "ON" }, 2, mag)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (panel);
    panel.setBounds (0, 0, kBaseW, kBaseH);
    models.setLastSpecial (true);

    auto addKnob = [this] (const char* id, const char* label, juce::Rectangle<int> bounds)
    {
        auto k = std::make_unique<png::ui::Knob> (proc.apvts, id, label);
        panel.addAndMakeVisible (*k);
        k->setBounds (bounds);
        knobs.push_back (std::move (k));
    };

    const int colW = 160, rowH = 146;
    auto cell = [&] (int x0, int col, int y) { return juce::Rectangle<int> (x0 + 13 + col * colW, y, colW, rowH); };

    // RESONATOR: the module's five knobs, and STRUM
    addKnob (ParamIDs::frequency,  "FREQUENCY",  cell (40, 0, 166));
    addKnob (ParamIDs::structure,  "STRUCTURE",  cell (40, 1, 166));
    addKnob (ParamIDs::brightness, "BRIGHTNESS", cell (40, 0, 312));
    addKnob (ParamIDs::damping,    "DAMPING",    cell (40, 1, 312));
    addKnob (ParamIDs::position,   "POSITION",   cell (40, 0, 458));
    addKnob (ParamIDs::strum,      "STRUM",      cell (40, 1, 458));

    // MODEL
    for (auto* c : { &models, &voices, &fx, &exciter, &onset })
        panel.addAndMakeVisible (*c);
    models.setBounds (834, 160, 306, 210);
    voices.setBounds (834, 400, 306, 38);
    fx.setBounds (834, 472, 306, 76);

    // KEYS, IN AND OUT
    panel.addAndMakeVisible (keys);
    keys.setBounds (60, 650, 520, 122);
    exciter.setBounds (634, 664, 210, 36);
    onset.setBounds (634, 728, 210, 36);
    addKnob (ParamIDs::inGain, "IN GAIN", { 852, 648, 100, 140 });
    addKnob (ParamIDs::dry,    "DRY",     { 952, 648, 100, 140 });
    addKnob (ParamIDs::out,    "OUT",     { 1052, 648, 100, 140 });

    panel.addAndMakeVisible (pond);
    pond.setBounds (404, 130, 392, 476);
    panel.addAndMakeVisible (title);
    title.setBounds (50, 24, 560, 76);
    panel.addAndMakeVisible (glass);
    glass.setBounds (0, 0, kBaseW, kBaseH);

    if (! proc.booted)
    {
        proc.booted = true;
        glass.boot (skin.t);
    }

    setResizable (true, true);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setResizeLimits (720, 480, 1500, 1000);
    setSize (1050, 700);
    startTimerHz (30);
}

PingsEditor::~PingsEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PingsEditor::paint (juce::Graphics& g)
{
    g.fillAll (tube);
}

void PingsEditor::resized()
{
    panel.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kBaseW));
}

void PingsEditor::advance (double dt)
{
    skin.t += dt;
    pond.tick (dt);
    keys.tick (dt);
    title.tick();

    const int m = juce::roundToInt (proc.apvts.getRawParameterValue (ParamIDs::model)->load());
    if (m != lastModel)
    {
        fx.setLive (m == png::kDisastrousPeace);
        if (lastModel >= 0) title.kick();
        lastModel = m;
    }
    for (auto* c : { &models, &voices, &fx, &exciter, &onset })
        c->repaint();

    const bool booting = glass.booting();
    if (booting || wasBooting)
        glass.repaint();
    wasBooting = booting;
}
