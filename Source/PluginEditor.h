#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace png::ui
{
    const juce::Colour tube { 0xff01070a };   // the tube, switched on: cold blue-black
    const juce::Colour cyan { 0xff4df3ff };   // icy cyan phosphor
    const juce::Colour mag  { 0xffff40d6 };   // hot magenta: anything struck

    inline constexpr float kFontScale = 1.25f;
    juce::Font mono (float height);

    struct Skin
    {
        double t = 0.0;
        bool blink() const { return std::fmod (t, 1.0) < 0.55; }
    };

    void glowText (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification,
                   const juce::Font&, juce::Colour = cyan, float alpha = 1.0f);
    void glowStroke (juce::Graphics&, const juce::Path&, float width, juce::Colour = cyan, float alpha = 1.0f);

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        explicit LookAndFeel (const Skin&);
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;
        const Skin& skin;
    };

    class Knob : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& label);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::RangedAudioParameter* param = nullptr;
        juce::String label;
    };

    // A row (or grid) of lit tiles for a choice or on/off parameter. Click to pick.
    class Segments : public juce::Component
    {
    public:
        Segments (juce::AudioProcessorValueTreeState&, const char* paramId, juce::StringArray labels, int columns,
                  juce::Colour lit = cyan);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void setLive (bool b) { if (b != live) { live = b; repaint(); } }
        void setLastSpecial (bool b) { lastIsSpecial = b; }
        int selected() const;

    private:
        juce::Rectangle<float> cell (int i) const;
        juce::RangedAudioParameter* param = nullptr;
        juce::StringArray labels;
        int columns = 1;
        juce::Colour lit;
        bool live = true, lastIsSpecial = false;
    };

    // The pond: every strike drops a ring. Voice lamps across the top, a scope along the bottom.
    class Pond : public juce::Component
    {
    public:
        Pond (PingsProcessor&, const Skin&);
        void paint (juce::Graphics&) override;
        void tick (double dt);

    private:
        PingsProcessor& proc;
        const Skin& skin;
        struct Ripple { float age = 99.0f, life = 2.0f; int note = 0, voice = 0; float x = 0.5f; };
        std::array<Ripple, 24> ripples {};
        int ripW = 0;
        uint32_t seen = 0;
        std::array<float, png::kMaxVoices> lamp {};
        std::array<float, png::kScope> scope {};
        int model = 0, voices = 2, chord = 3, lastNote = -1;
        float lastAge = 99.0f;
    };

    // Four octaves of keys: click to strike, MIDI shows in magenta.
    class Keys : public juce::Component
    {
    public:
        Keys (PingsProcessor&, const Skin&);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void tick (double dt);

        static constexpr int kFirst = 36, kLast = 83;

    private:
        int noteAt (juce::Point<float>) const;
        juce::Rectangle<float> keyRect (int note) const;
        static bool isBlack (int note);

        PingsProcessor& proc;
        const Skin& skin;
        std::array<bool, 128> held {};
        std::array<float, 128> flash {};
        uint32_t seen = 0;
    };

    class Title : public juce::Component
    {
    public:
        explicit Title (const Skin& s) : skin (s) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;
        void tick();
        void kick() { glitch = juce::jmax (glitch, 2); }
        static juce::Font font();

    private:
        const Skin& skin;
        int glitch = 0;
        bool lastBlink = false;
        juce::Random random;
    };

    class Glass : public juce::Component
    {
    public:
        explicit Glass (const Skin&);
        void paint (juce::Graphics&) override;
        void resized() override;
        void boot (double now) { bootStart = now; }
        bool booting() const { return skin.t - bootStart < kBootSecs; }
        static constexpr double kBootSecs = 3.2;

    private:
        const Skin& skin;
        juce::Image lines;
        double bootStart = -100.0;
    };
}

class PingsEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PingsEditor (PingsProcessor&);
    ~PingsEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void pump (double dt = 1.0 / 30.0) { advance (dt); }
    void boot() { glass.boot (skin.t); }
    double clock() const { return skin.t; }

private:
    class Panel : public juce::Component
    {
    public:
        void paint (juce::Graphics&) override;
    };

    void timerCallback() override { advance (1.0 / 30.0); }
    void advance (double dt);

    PingsProcessor& proc;
    png::ui::Skin skin;
    png::ui::LookAndFeel lnf { skin };
    Panel panel;
    png::ui::Pond pond;
    png::ui::Keys keys;
    png::ui::Segments models, voices, fx, exciter, onset;
    png::ui::Title title { skin };
    png::ui::Glass glass { skin };

    std::vector<std::unique_ptr<png::ui::Knob>> knobs;
    bool wasBooting = false;
    int lastModel = -1;

    static constexpr int kBaseW = 1200, kBaseH = 800;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PingsEditor)
};
