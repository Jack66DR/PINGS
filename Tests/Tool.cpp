// PINGS_Tool
//   render <out.wav> [modal|strings|peace|fed]   a little performance through the full plugin
//   snapshot <out.png> [crt|peace|boot]          renders the editor to a PNG for a layout check

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

static void setParam (PingsProcessor& p, const char* id, float value)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

struct Hit { double at; std::vector<int> notes; };

// Dm, Bb, Gm, A, as arpeggios or chords, Logic naming
static std::vector<Hit> score (bool chords)
{
    const std::vector<std::vector<int>> prog = { { 50, 57, 62, 65 }, { 46, 53, 58, 62 }, { 43, 50, 55, 58 }, { 45, 52, 57, 61 } };
    std::vector<Hit> s;
    for (int bar = 0; bar < 8; ++bar)
    {
        const auto& c = prog[(size_t) (bar % 4)];
        const double t0 = bar * 3.0;
        if (chords)
        {
            s.push_back ({ t0, c });
            s.push_back ({ t0 + 1.5, { c[2] + 12 } });
        }
        else
            for (int k = 0; k < 8; ++k)
                s.push_back ({ t0 + k * 0.375, { c[(size_t) ((k * 3) % 4)] + (k >= 4 ? 12 : 0) } });
    }
    return s;
}

static int render (const juce::File& out, const juce::String& preset)
{
    constexpr double sr = 44100.0;   // deliberately not 48 kHz, to exercise the resampling
    constexpr int block = 512;
    const double seconds = 26.0;

    PingsProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, block);
    std::vector<Hit> hits;
    if (preset == "modal")
    {
        setParam (proc, ParamIDs::model, 0.0f);
        setParam (proc, ParamIDs::voices, 3.0f);   // 4 voices
        setParam (proc, ParamIDs::structure, 0.42f);
        setParam (proc, ParamIDs::brightness, 0.55f);
        setParam (proc, ParamIDs::damping, 0.72f);
        setParam (proc, ParamIDs::position, 0.25f);
        hits = score (false);
    }
    else if (preset == "strings")
    {
        setParam (proc, ParamIDs::model, 5.0f);    // Karplusverb
        setParam (proc, ParamIDs::voices, 3.0f);
        setParam (proc, ParamIDs::structure, 0.15f);
        setParam (proc, ParamIDs::brightness, 0.45f);
        setParam (proc, ParamIDs::damping, 0.8f);
        setParam (proc, ParamIDs::position, 0.4f);
        setParam (proc, ParamIDs::strum, 45.0f);
        hits = score (true);
    }
    else if (preset == "peace")
    {
        setParam (proc, ParamIDs::model, 6.0f);    // Disastrous Peace
        setParam (proc, ParamIDs::voices, 1.0f);
        setParam (proc, ParamIDs::fx, 4.0f);       // ensemble
        setParam (proc, ParamIDs::structure, 0.45f);
        setParam (proc, ParamIDs::brightness, 0.55f);
        setParam (proc, ParamIDs::damping, 0.6f);
        setParam (proc, ParamIDs::position, 0.7f);
        for (auto& h : score (true)) if (h.notes.size() > 1) hits.push_back ({ h.at, { h.notes[0] } });
    }
    else   // fed: a buzzy voice into the sympathetic strings, notes from the keys
    {
        setParam (proc, ParamIDs::model, 1.0f);
        setParam (proc, ParamIDs::voices, 1.0f);
        setParam (proc, ParamIDs::exciter, 1.0f);
        setParam (proc, ParamIDs::structure, 0.3f);
        setParam (proc, ParamIDs::damping, 0.75f);
        setParam (proc, ParamIDs::dry, -18.0f);
        for (auto& h : score (true)) if (h.notes.size() > 1) hits.push_back ({ h.at, { h.notes[0] } });
    }
    proc.prepareToPlay (sr, block);

    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> all (2, total), buf (2, block);
    juce::MidiBuffer midi;
    size_t next = 0;
    double ph = 0.0;
    for (int s = 0; s < total; s += block)
    {
        const int n = juce::jmin (block, total - s);
        const double t = (double) s / sr;
        midi.clear();
        while (next < hits.size() && hits[next].at <= t)
        {
            for (int k : hits[next].notes) midi.addEvent (juce::MidiMessage::noteOn (1, k, (juce::uint8) 100), 0);
            ++next;
        }
        buf.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            if (preset == "fed")
            {
                const double ti = (double) (s + i) / sr;
                ph += 110.0 * (1.0 + 0.05 * std::sin (ti * 1.7)) / sr;
                ph -= std::floor (ph);
                const double env = std::fmod (ti, 2.0) < 1.4 ? 1.0 : 0.0;
                x = (float) (env * 0.15 * (ph < 0.3 ? 1.0 : -0.43));
            }
            buf.setSample (0, i, x);
            buf.setSample (1, i, x);
        }
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            all.copyFrom (ch, s, buf, ch, 0, n);
    }

    const float pk = all.getMagnitude (0, total);
    std::printf ("rendered %.0f s (%s), peak %.3f (%.1f dBFS), rms %.1f dBFS\n", seconds, preset.toRawUTF8(), pk,
                 juce::Decibels::gainToDecibels (pk), juce::Decibels::gainToDecibels (all.getRMSLevel (0, 0, total)));
    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (out.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.release(), sr, 2, 24, {}, 0));
    if (w == nullptr) return 1;
    w->writeFromAudioSampleBuffer (all, 0, total);
    std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}

static int snapshot (const juce::File& out, const juce::String& look)
{
    juce::ScopedJuceInitialiser_GUI gui;
    PingsProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    if (look == "peace")
    {
        setParam (proc, ParamIDs::model, 6.0f);
        setParam (proc, ParamIDs::fx, 4.0f);
        setParam (proc, ParamIDs::structure, 0.45f);
    }
    else
    {
        setParam (proc, ParamIDs::voices, 3.0f);
        setParam (proc, ParamIDs::damping, 0.7f);
    }
    proc.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    proc.booted = true;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (1200, 800);
    auto* editor = dynamic_cast<PingsEditor*> (ed.get());

    const int blocks = 200;
    const int notes[] = { 50, 57, 62, 65, 69, 74, 62, 57 };
    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        if (b > 100 && b % 12 == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, notes[(b / 12) % 8], (juce::uint8) 100), 0);
        if (b == blocks - 4)
            midi.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100), 0);   // held while the picture is taken
        if (look == "boot" && editor != nullptr && b == blocks - 3 * 50)
            editor->boot();
        buf.clear();
        proc.processBlock (buf, midi);
        if (b % 3 == 0 && editor != nullptr)
            editor->pump();
    }
    if (editor != nullptr && look != "boot")
        for (int k = 0; k < 40; ++k)
        {
            editor->pump (0.02);
            if (std::fmod (editor->clock(), 1.0) < 0.3) break;
        }

    auto img = ed->createComponentSnapshot (ed->getLocalBounds());
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    ed.reset();
    return 0;
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage: PINGS_Tool render <out.wav> [modal|strings|peace|fed]\n"
                     "       PINGS_Tool snapshot <out.png> [crt|peace|boot]\n");
        return 2;
    }
    const juce::String mode (argv[1]);
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[2])));
    if (mode == "render")
        return render (out, argc > 3 ? juce::String (argv[3]) : juce::String ("modal"));
    if (mode == "snapshot")
        return snapshot (out, argc > 3 ? juce::String (argv[3]) : juce::String ("crt"));
    return 2;
}
