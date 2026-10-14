// The PINGS engine. No JUCE, so the offline test can run it on its own.
//
// The sound is Mutable Instruments' Rings, by Emilie Gillet, unmodified (ThirdParty/mutable, MIT licence).
// This file is the part that makes it a keyboard instrument:
//
//   MIDI note-ons ─► strum queue (one strum at a time, STRUM apart) ─┐
//   audio in ─► IN GAIN ─► noise gate ─► (EXCITER = INPUT) ──────────┼─► Rings Part, or the
//                                         └─► onset detector ─► strum ┘    Disastrous Peace string synth
//                                                                               │ out (odd)  aux (even)
//   host rate ─► resample to 48 kHz ─► 24-sample blocks ─► resample back ◄───────┘
//
// Rings runs at 48 kHz in blocks of 24, exactly as on the module, whatever the host's rate.
#pragma once

#include "rings/dsp/part.h"
#include "rings/dsp/string_synth_part.h"
#include "rings/dsp/onset_detector.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <new>
#include <atomic>
#include <cmath>
#include <string>

namespace png
{
    constexpr int kNumModels = 7;
    inline const char* const kModelName[kNumModels] = { "MODAL", "SYMPATHETIC", "INHARMONIC", "FM VOICE",
                                                        "CHORDS", "KARPLUSVERB", "DISASTROUS PEACE" };
    constexpr int kDisastrousPeace = 6;

    constexpr int kNumFx = 6;
    inline const char* const kFxName[kNumFx] = { "FORMANT", "CHORUS", "REVERB", "FORMANT 2", "ENSEMBLE", "REVERB 2" };

    constexpr int kMaxVoices = 4;
    constexpr double kInternalRate = 48000.0;
    constexpr int kBlock = (int) rings::kMaxBlockSize;          // 24 samples, 0.5 ms
    constexpr int kMinStrumBlocks = 12;                         // 6 ms: Rings' note filter needs ~5 ms between strums
    constexpr int kOnsetInhibitBlocks = 80;                     // 40 ms between strums found in the input
    constexpr int kLatency = 40;                                // internal samples between input and output resampling
    constexpr int kScope = 256;

    // Note names the Logic way: MIDI 60 is C3.
    inline std::string noteName (int midi)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        midi = std::clamp (midi, 0, 127);
        return std::string (names[midi % 12]) + std::to_string (midi / 12 - 2);
    }

    // Rings' chord names, for the CHORDS model and Disastrous Peace (STRUCTURE picks one).
    constexpr int kNumChords = rings::kNumChords;
    inline const char* const kChordName[kNumChords] = { "OCT", "5", "SUS4", "MIN", "MIN7", "MIN9", "MIN11",
                                                        "6/9", "MAJ9", "MAJ7", "MAJ" };

    struct Params
    {
        int model = 0, voices = 2, fx = 0;           // voices is 1..4
        float frequency = 0.0f;                      // transpose, semitones
        float structure = 0.25f, brightness = 0.5f, damping = 0.5f, position = 0.3f;
        float strumMs = 10.0f;                       // gap between the notes of a chord
        float inputGain = 1.0f;
        float bend = 0.0f;                           // semitones, from the pitch wheel
        bool useInput = false, onset = false;
    };

    class Engine
    {
    public:
        Engine() : core (makeCore(), &freeCore)
        {
            for (auto& s : uiScope) s.store (0.0f);
            for (auto& s : uiStrikeNote) s.store (0);
            for (auto& s : uiStrikeVoice) s.store (0);
        }

        void prepare (double hostRate, uint32_t seed)
        {
            (void) seed;
            hostSr = hostRate;
            ratio = hostSr / kInternalRate;
            core->part.Init (core->reverbA);
            core->synth.Init (core->reverbB);
            curModel = -1; curVoices = -1; curFx = -1;
            core->onset.Init (8.0f / (float) kInternalRate, 160.0f / (float) kInternalRate, 1600.0f / (float) kInternalRate,
                        (float) kInternalRate / (float) kBlock, 0.01f);
            hostCount = 0;
            nextT = 0.0;
            inFill = 0;
            outCount = 0;
            outRing.fill ({ 0.0f, 0.0f });
            hist.fill (0.0f);
            queueCount = 0;
            strumWait = 0;
            inLevel = 0.0f;
            first = true;
            activeVoice = 0;
            stepCounter = 0;
            chord = 3;
            level = 0.0f;
        }

        // From the audio thread (MIDI). Notes wait their turn to be strummed.
        void noteOn (int n)
        {
            if (n < 0 || n > 127 || queueCount >= (int) queue.size()) return;
            queue[(size_t) queueCount++] = n;
            heldBits (n, true);
        }
        void noteOff (int n) { heldBits (n, false); }
        void allNotesOff() { uiHeldLo.store (0); uiHeldHi.store (0); }

        // From the editor's keys, on the message thread. A single-producer ring the audio thread drains.
        void uiNoteOn (int n)
        {
            const uint32_t w = uiQW.load();
            if (w - uiQR.load() >= (uint32_t) uiQ.size()) return;
            uiQ[(size_t) (w % uiQ.size())].store (n);
            uiQW.store (w + 1);
        }

        void process (const float* inL, const float* inR, float* outL, float* outR, int n, const Params& p)
        {
            // drain the editor's notes into the strum queue
            for (uint32_t r = uiQR.load(), w = uiQW.load(); r != w; ++r)
            {
                if (queueCount < (int) queue.size())
                    queue[(size_t) queueCount++] = uiQ[(size_t) (r % uiQ.size())].load();
                uiQR.store (r + 1);
            }

            for (int i = 0; i < n; ++i)
            {
                // ---- host rate to 48 kHz: hermite between the last few host samples
                hist[(size_t) (hostCount & 7)] = (inL[i] + inR[i]) * 0.5f;
                const int64_t newest = hostCount++;
                while ((int64_t) std::floor (nextT) + 2 <= newest)
                {
                    const int64_t k = (int64_t) std::floor (nextT);
                    const float t = (float) (nextT - (double) k);
                    inBlock[(size_t) inFill++] = herm (h (k - 1), h (k), h (k + 1), h (k + 2), t);
                    nextT += ratio;
                    if (inFill == kBlock)
                    {
                        runBlock (p);
                        inFill = 0;
                    }
                }

                // ---- 48 kHz back to host rate, a fixed 40 internal samples behind
                const double u = (double) newest / ratio - kLatency;
                const int64_t k = (int64_t) std::floor (u);
                if (k - 1 >= 0 && k + 2 < outCount && k - 1 >= outCount - (int64_t) outRing.size())
                {
                    const float t = (float) (u - (double) k);
                    const auto& a = ring (k - 1); const auto& b = ring (k); const auto& c = ring (k + 1); const auto& d = ring (k + 2);
                    outL[i] = herm (a[0], b[0], c[0], d[0], t);
                    outR[i] = herm (a[1], b[1], c[1], d[1], t);
                }
                else
                {
                    outL[i] = 0.0f;
                    outR[i] = 0.0f;
                }
            }
            uiLevel.store (level);
        }

        // ---- telemetry for the editor
        std::atomic<uint32_t> uiStrikes { 0 };               // a ripple for every strike
        std::array<std::atomic<int>, 16> uiStrikeNote, uiStrikeVoice;
        std::array<std::atomic<float>, kScope> uiScope;
        std::atomic<int> uiScopeW { 0 };
        std::atomic<int> uiChord { 3 };
        std::atomic<float> uiLevel { 0.0f };
        std::atomic<uint64_t> uiHeldLo { 0 }, uiHeldHi { 0 };

        static constexpr int latencySamples (double hostRate) { return (int) (kLatency * hostRate / kInternalRate) + 3; }

    private:
        static float herm (float xm1, float x0, float x1, float x2, float t)
        {
            const float c = (x1 - xm1) * 0.5f;
            const float v = x0 - x1;
            const float w = c + v;
            const float a = w + v + (x2 - x0) * 0.5f;
            const float b = w + a;
            return ((((a * t) - b) * t + c) * t + x0);
        }

        float h (int64_t k) const { return k < 0 || k < hostCount - 8 ? 0.0f : hist[(size_t) (k & 7)]; }
        const std::array<float, 2>& ring (int64_t k) const { return outRing[(size_t) (k % (int64_t) outRing.size())]; }

        void heldBits (int n, bool on)
        {
            auto& a = n < 64 ? uiHeldLo : uiHeldHi;
            const uint64_t bit = (uint64_t) 1 << (n & 63);
            const uint64_t v = a.load();
            a.store (on ? (v | bit) : (v & ~bit));
        }

        void runBlock (const Params& p)
        {
            const int model = std::clamp (p.model, 0, kNumModels - 1);
            const int voices = std::clamp (p.voices, 1, kMaxVoices);
            const bool dp = model == kDisastrousPeace;
            if (model != curModel)
            {
                if (! dp) core->part.set_model ((rings::ResonatorModel) model);
                curModel = model;
            }
            if (voices != curVoices)
            {
                core->part.set_polyphony (voices);
                core->synth.set_polyphony (voices);
                curVoices = voices;
                if (activeVoice >= voices) activeVoice = 0;
            }
            if (p.fx != curFx)
            {
                core->synth.set_fx ((rings::FxType) std::clamp (p.fx, 0, kNumFx - 1));
                curFx = p.fx;
            }

            // the knobs move smoothly, the way the module's ADC filters them
            const float c = first ? 1.0f : 0.06f;
            first = false;
            sm[0] += c * (p.structure - sm[0]);
            sm[1] += c * (p.brightness - sm[1]);
            sm[2] += c * (p.damping - sm[2]);
            sm[3] += c * (p.position - sm[3]);
            // all four stop at 0.9995, as the module already does for STRUCTURE: at exactly 1.0 its table
            // lookups read one entry past the end
            rings::Patch patch { std::clamp (sm[0], 0.0f, 0.9995f), std::clamp (sm[1], 0.0f, 0.9995f),
                                 std::clamp (sm[2], 0.0f, 0.9995f), std::clamp (sm[3], 0.0f, 0.9995f) };

            // STRUCTURE picks the chord, with the module's hysteresis
            const float ch = std::clamp (p.structure, 0.0f, 1.0f) * (float) (kNumChords - 1);
            const float hys = ch - (float) chord > 0.0f ? -0.1f : 0.1f;
            chord = std::clamp ((int) (ch + hys + 0.5f), 0, kNumChords - 1);
            uiChord.store (chord);

            // input: the module's own noise gate, so hiss doesn't keep the strings singing
            if (p.useInput)
                for (int i = 0; i < kBlock; ++i)
                {
                    const float s = inBlock[(size_t) i] * p.inputGain;
                    const float err = s * s - inLevel;
                    inLevel += err * (err > 0.0f ? 0.1f : 0.0001f);
                    const float g = inLevel <= kGate ? inLevel / kGate : 1.0f;
                    gated[(size_t) i] = g * s;
                }
            else
                std::fill (gated.begin(), gated.end(), 0.0f);

            const bool hit = p.useInput && p.onset && core->onset.Process (gated.data(), (size_t) kBlock);

            // strums: queued notes first, one at a time, then onsets in the input
            bool strum = false;
            if (strumWait > 0)
                --strumWait;
            if (strumWait == 0 && queueCount > 0)
            {
                note = queue[0];
                std::copy (queue.begin() + 1, queue.begin() + queueCount, queue.begin());
                --queueCount;
                strum = true;
                strumWait = std::max (kMinStrumBlocks, (int) (p.strumMs * 0.001f * (float) (kInternalRate / kBlock)));
            }
            else if (strumWait == 0 && hit)
            {
                strum = true;
                strumWait = kOnsetInhibitBlocks;
            }

            rings::PerformanceState ps;
            ps.strum = strum;
            ps.internal_exciter = ! p.useInput;
            ps.internal_strum = false;
            ps.internal_note = false;
            ps.tonic = std::round (p.frequency);
            ps.note = (float) note;
            ps.fm = std::clamp (p.bend, -48.0f, 48.0f);
            ps.chord = chord;

            if (dp)
                core->synth.Process (ps, patch, gated.data(), out.data(), aux.data(), (size_t) kBlock);
            else
                core->part.Process (ps, patch, gated.data(), out.data(), aux.data(), (size_t) kBlock);

            if (strum)
            {
                // mirror the module's voice allocation, so the screen can show which voice rang
                if (voices > 1 && (voices & 1))
                {
                    static constexpr int pattern[8] = { 1, 0, 2, 1, 0, 2, 1, 0 };
                    activeVoice = pattern[stepCounter % 8];
                    stepCounter = (stepCounter + 1) % 8;
                }
                else
                    activeVoice = (activeVoice + 1) % voices;
                const uint32_t s = uiStrikes.load();
                uiStrikeNote[(size_t) (s % 16)].store (note + (int) ps.tonic);
                uiStrikeVoice[(size_t) (s % 16)].store (activeVoice);
                uiStrikes.store (s + 1);
            }

            float pk = 0.0f;
            for (int i = 0; i < kBlock; ++i)
            {
                // the module's outputs, ODD on the left and EVEN on the right
                auto& slot = outRing[(size_t) (outCount % (int64_t) outRing.size())];
                slot[0] = out[(size_t) i];
                slot[1] = aux[(size_t) i];
                ++outCount;
                pk = std::max (pk, std::max (std::abs (out[(size_t) i]), std::abs (aux[(size_t) i])));
            }
            level = std::max (pk, level * 0.97f);
            // a short scope trace: one point per block
            const int w = uiScopeW.load();
            uiScope[(size_t) w].store (0.5f * (out[0] + aux[0]));
            uiScopeW.store ((w + 1) % kScope);
        }

        static constexpr float kGate = 0.00003f;

        // Rings' objects, in zeroed memory. On the module they're globals, which the chip zeroes at power-on,
        // and some of their state (the reverb's filters, the strum pattern counter) is never set by Init().
        // Ordinary plugin memory isn't zeroed, so they get the same clean start here explicitly.
        struct Core
        {
            rings::Part part;
            rings::StringSynthPart synth;
            rings::OnsetDetector onset;
            uint16_t reverbA[32768];
            uint16_t reverbB[32768];
        };
        static Core* makeCore()
        {
            void* m = std::calloc (1, sizeof (Core));
            if (m == nullptr) throw std::bad_alloc();
            return new (m) Core;   // default-initialised: their empty constructors leave the zeroes alone
        }
        static void freeCore (Core* c)
        {
            if (c == nullptr) return;
            c->~Core();
            std::free (c);
        }
        std::unique_ptr<Core, void (*) (Core*)> core;

        double hostSr = 48000.0, ratio = 1.0, nextT = 0.0;
        int64_t hostCount = 0, outCount = 0;
        std::array<float, 8> hist {};
        std::array<float, kBlock> inBlock {}, gated {}, out {}, aux {};
        int inFill = 0;
        std::array<std::array<float, 2>, 512> outRing {};

        std::array<int, 64> queue {};
        int queueCount = 0, strumWait = 0, note = 57;
        std::array<std::atomic<int>, 64> uiQ;
        std::atomic<uint32_t> uiQW { 0 }, uiQR { 0 };

        int curModel = -1, curVoices = -1, curFx = -1, chord = 3, activeVoice = 0, stepCounter = 0;
        float sm[4] {}, inLevel = 0.0f, level = 0.0f;
        bool first = true;
    };
}
