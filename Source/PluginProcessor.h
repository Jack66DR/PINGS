#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"

namespace ParamIDs
{
    inline constexpr const char* model      = "model";
    inline constexpr const char* voices     = "voices";
    inline constexpr const char* fx         = "fx";
    inline constexpr const char* frequency  = "frequency";
    inline constexpr const char* structure  = "structure";
    inline constexpr const char* brightness = "brightness";
    inline constexpr const char* damping    = "damping";
    inline constexpr const char* position   = "position";
    inline constexpr const char* strum      = "strum";
    inline constexpr const char* exciter    = "exciter";
    inline constexpr const char* onset      = "onset";
    inline constexpr const char* inGain     = "ingain";
    inline constexpr const char* dry        = "dry";
    inline constexpr const char* out        = "out";
}

class PingsProcessor : public juce::AudioProcessor
{
public:
    PingsProcessor();
    ~PingsProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PINGS"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<png::Engine> engine { std::make_unique<png::Engine>() };   // big: Rings' buffers live in here

    bool booted = false;

private:
    juce::AudioBuffer<float> wetBuf;
    juce::SmoothedValue<float> dryS, outS;
    float bend = 0.0f;
    std::vector<int> pendingNotes;

    enum P { pModel, pVoices, pFx, pFrequency, pStructure, pBrightness, pDamping, pPosition, pStrum, pExciter, pOnset,
             pInGain, pDry, pOut, kNumP };
    std::atomic<float>* p[kNumP] {};
    float get (P which) const { return p[which]->load(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PingsProcessor)
};
