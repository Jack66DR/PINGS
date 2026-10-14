// Offline checks for the PINGS engine. Build with -DPNG_BUILD_TESTS=ON.

#include "Engine.h"

#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
 #include <xmmintrin.h>
 #include <pmmintrin.h>
#endif

static constexpr int block = 256;
static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++failures;
}

struct Out { std::vector<float> l, r; double sr = 48000.0; uint32_t strikes = 0; };

using InFn = std::function<float (double t)>;
using ParFn = std::function<void (png::Params&, png::Engine&, double t)>;

static Out run (double seconds, InFn in, ParFn par, double sampleRate = 48000.0)
{
    auto e = std::make_unique<png::Engine>();
    e->prepare (sampleRate, 1);
    Out o;
    o.sr = sampleRate;
    const size_t total = (size_t) (seconds * sampleRate);
    o.l.resize (total);
    o.r.resize (total);
    std::vector<float> il (block), ir (block);
    png::Params p;
    for (size_t s = 0; s < total; s += block)
    {
        const int n = (int) std::min<size_t> (block, total - s);
        par (p, *e, (double) s / sampleRate);
        for (int i = 0; i < n; ++i)
            il[(size_t) i] = ir[(size_t) i] = in ((double) (s + (size_t) i) / sampleRate);
        e->process (il.data(), ir.data(), o.l.data() + s, o.r.data() + s, n, p);
    }
    o.strikes = e->uiStrikes.load();
    return o;
}

static float rms (const Out& o, double t0, double t1)
{
    const size_t a = (size_t) (t0 * o.sr), b = std::min (o.l.size(), (size_t) (t1 * o.sr));
    double acc = 0.0;
    for (size_t i = a; i < b; ++i) acc += 0.5 * ((double) o.l[i] * o.l[i] + (double) o.r[i] * o.r[i]);
    return (float) std::sqrt (acc / (double) std::max<size_t> (1, b - a));
}

static float peak (const Out& o)
{
    float m = 0.0f;
    for (size_t i = 0; i < o.l.size(); ++i) m = std::max ({ m, std::abs (o.l[i]), std::abs (o.r[i]) });
    return m;
}

static bool finite (const Out& o)
{
    for (size_t i = 0; i < o.l.size(); ++i)
        if (! std::isfinite (o.l[i]) || ! std::isfinite (o.r[i])) return false;
    return true;
}

static float db (float x) { return 20.0f * std::log10 (std::max (x, 1.0e-9f)); }

// Energy at one frequency (Goertzel), on the mono sum.
static float tone (const Out& o, double hz, double t0, double t1)
{
    const size_t a = (size_t) (t0 * o.sr), b = std::min (o.l.size(), (size_t) (t1 * o.sr));
    const double w = 2.0 * M_PI * hz / o.sr, c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = a; i < b; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double) (i - a) / (double) (b - a));
        const double s0 = win * (o.l[i] + o.r[i]) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return (float) (std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / (double) (b - a));
}

// The strongest peak in a band, by scanning in 0.25 Hz steps.
static float strongest (const Out& o, double lo, double hi, double t0, double t1)
{
    float best = 0.0f, at = 0.0f;
    for (double f = lo; f <= hi; f += 0.25)
    {
        const float m = tone (o, f, t0, t1);
        if (m > best) { best = m; at = (float) f; }
    }
    return at;
}

static double hzOf (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); }

int main()
{
    // the plugin flushes denormals (JUCE ScopedNoDenormals), as the module's chip did; so does the test
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
    _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
#elif defined(__aarch64__)
    { uint64_t fpcr; asm volatile ("mrs %0, fpcr" : "=r" (fpcr)); fpcr |= (1ull << 24); asm volatile ("msr fpcr, %0" :: "r" (fpcr)); }
#endif
    std::printf ("PINGS offline engine test\n");
    auto silent = [] (double) { return 0.0f; };

    {
        std::printf ("silence\n");
        auto o = run (2.0, silent, [] (png::Params&, png::Engine&, double) {});
        check (peak (o) < 1.0e-4f, "no notes, no input: silence");
    }

    {
        std::printf ("pitch, at three sample rates\n");
        for (double rate : { 48000.0, 44100.0, 96000.0 })
        {
            auto o = run (2.0, silent, [] (png::Params& p, png::Engine& e, double t)
            {
                if (t == 0.0) e.noteOn (57);
                p.model = 2; p.voices = 1; p.structure = 0.0f; p.brightness = 0.35f; p.damping = 0.7f;
            }, rate);
            const float f = strongest (o, 200.0, 240.0, 0.2, 1.5);
            std::printf ("    %.0f Hz host: strongest near A2 (220 Hz) at %.2f Hz\n", rate, f);
            check (std::abs (f / 220.0f - 1.0f) < 0.006f, rate == 48000.0 ? "A2 plays at 220 Hz at 48 kHz"
                                                       : rate == 44100.0 ? "and at 44.1 kHz (resampled)" : "and at 96 kHz (resampled)");
        }
    }

    {
        std::printf ("FREQUENCY and the pitch wheel\n");
        auto o = run (2.0, silent, [] (png::Params& p, png::Engine& e, double t)
        {
            if (t == 0.0) e.noteOn (57);
            p.model = 2; p.voices = 1; p.structure = 0.0f; p.brightness = 0.35f; p.damping = 0.7f; p.frequency = 12.0f;
        });
        const float f = strongest (o, 420.0, 460.0, 0.2, 1.5);
        std::printf ("    +12: %.2f Hz\n", f);
        check (std::abs (f / 440.0f - 1.0f) < 0.006f, "FREQUENCY +12 transposes up an octave");
    }

    {
        std::printf ("every model\n");
        for (int m = 0; m < png::kNumModels; ++m)
        {
            auto o = run (3.0, silent, [m] (png::Params& p, png::Engine& e, double t)
            {
                if (std::fmod (t, 0.75) < 0.006) e.noteOn (48 + (int) (t / 0.75) * 5);
                p.model = m; p.voices = 4; p.fx = 4; p.damping = 0.8f;
            });
            std::printf ("    %-17s rms %.1f dB, peak %.3f, %u strikes\n", png::kModelName[m], db (rms (o, 0.0, 3.0)), peak (o), o.strikes);
            check (finite (o) && peak (o) < 1.0f && rms (o, 0.0, 3.0) > 0.003f, png::kModelName[m]);
        }
    }

    {
        std::printf ("chords\n");
        auto o = run (2.5, silent, [] (png::Params& p, png::Engine& e, double t)
        {
            if (t == 0.0) { e.noteOn (48); e.noteOn (52); e.noteOn (55); }
            p.model = 2; p.voices = 4; p.structure = 0.0f; p.brightness = 0.35f; p.damping = 0.75f; p.strumMs = 20.0f;
        });
        const float c = tone (o, hzOf (48), 0.3, 1.8), e = tone (o, hzOf (52), 0.3, 1.8), g = tone (o, hzOf (55), 0.3, 1.8);
        const float off = tone (o, hzOf (50), 0.3, 1.8);
        std::printf ("    C %.1f dB, E %.1f dB, G %.1f dB, D (not played) %.1f dB, %u strikes\n", db (c), db (e), db (g), db (off), o.strikes);
        check (o.strikes == 3, "three notes, three strums");
        check (db (std::min ({ c, e, g })) - db (off) > 12.0f, "a held chord rings with every note in it");
    }

    {
        std::printf ("input\n");
        uint32_t s = 5;
        auto noise = [&s] (double t) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return t > 0.2 ? ((float) (s >> 8) / 8388608.0f - 1.0f) * 0.25f : 0.0f; };
        auto o = run (2.0, noise, [] (png::Params& p, png::Engine& e, double t)
        {
            if (t == 0.0) e.noteOn (57);
            p.model = 0; p.voices = 1; p.useInput = true;
        });
        std::printf ("    noise into MODAL: %.1f dB\n", db (rms (o, 0.5, 2.0)));
        check (rms (o, 0.5, 2.0) > 0.003f, "EXCITER INPUT: the input excites the resonator");

        auto ignored = run (2.0, noise, [] (png::Params& p, png::Engine& e, double t) { if (t == 0.0) e.noteOn (57); p.model = 0; p.voices = 1; });
        check (rms (ignored, 1.0, 2.0) < rms (o, 1.0, 2.0) * 0.1f, "EXCITER INTERNAL: the input is ignored");

        auto clicks = run (3.0, [] (double t) { return std::fmod (t, 0.5) < 0.003 ? 0.6f : 0.0f; },
                           [] (png::Params& p, png::Engine&, double) { p.model = 1; p.voices = 2; p.useInput = true; p.onset = true; });
        std::printf ("    6 clicks in, %u strums found\n", clicks.strikes);
        check (clicks.strikes >= 4 && clicks.strikes <= 7, "ONSET: hits in the input strum the voices");
    }

    {
        std::printf ("safety\n");
        uint32_t s = 9;
        auto o = run (5.0, [&s] (double) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return ((float) (s >> 8) / 8388608.0f - 1.0f) * 0.9f; },
                      [] (png::Params& p, png::Engine& e, double t)
                      {
                          if (std::fmod (t, 0.05) < 0.006) e.noteOn (30 + (int) (t * 20) % 60);
                          p.model = (int) (t * 1.4) % png::kNumModels; p.voices = 4; p.useInput = true; p.inputGain = 4.0f;
                          p.damping = 1.0f; p.brightness = 1.0f; p.structure = 1.0f; p.position = 1.0f; p.bend = 2.0f;
                      });
        std::printf ("    peak %.3f\n", peak (o));
        check (finite (o), "no NaN or inf, hammering every model with loud noise and fast notes");
        check (peak (o) <= 1.2f, "Rings' own limiter keeps it in bounds");
    }

    {
        std::printf ("cost\n");
        for (int m : { 0, 2, 6 })
        {
            const auto t0 = std::chrono::steady_clock::now();
            auto o = run (10.0, silent, [m] (png::Params& p, png::Engine& e, double t)
            {
                if (std::fmod (t, 0.25) < 0.006) e.noteOn (48 + (int) (t * 4) % 24);
                p.model = m; p.voices = 4; p.fx = 2;
            }, 44100.0);
            const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            std::printf ("    %-17s 4 voices, 10 s in %.3f s (%.1f%% of one core)\n", png::kModelName[m], secs, 10.0 * secs);
            check (secs / 10.0 < 0.08, "under 8% of a core");
        }
    }

    std::printf (failures == 0 ? "ALL PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
