#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String pctText (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

    juce::String dbText (float v, int)
    {
        if (v <= -59.9f) return "-inf dB";
        return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
    }

    float dbToGain (float db) { return db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (db); }
}

juce::AudioProcessorValueTreeState::ParameterLayout PingsProcessor::createLayout()
{
    using namespace juce;
    using F = AudioParameterFloat;
    using C = AudioParameterChoice;
    using A = AudioParameterFloatAttributes;
    AudioProcessorValueTreeState::ParameterLayout l;

    auto pct = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<F> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def,
                                    A().withStringFromValueFunction (pctText));
    };
    auto gain = [] (const char* id, const char* name, float lo, float maxDb, float def)
    {
        return std::make_unique<F> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, maxDb, 0.0f, lo < -30.0f ? 2.0f : 1.0f), def,
                                    A().withStringFromValueFunction (dbText));
    };

    // MODEL
    {
        StringArray m, v, f;
        for (auto* n : png::kModelName) m.add (n);
        for (auto* n : { "1", "2", "3", "4" }) v.add (n);
        for (auto* n : png::kFxName) f.add (n);
        l.add (std::make_unique<C> (ParameterID { ParamIDs::model, 1 }, "Model", m, 0));
        l.add (std::make_unique<C> (ParameterID { ParamIDs::voices, 1 }, "Voices", v, 1));
        l.add (std::make_unique<C> (ParameterID { ParamIDs::fx, 1 }, "Peace FX", f, 1));
    }

    // RESONATOR: Rings' five knobs, plus how lazily a chord is strummed
    l.add (std::make_unique<F> (ParameterID { ParamIDs::frequency, 1 }, "Frequency", NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f,
                                A().withStringFromValueFunction ([] (float v, int)
                                {
                                    const int s = juce::roundToInt (v);
                                    return s == 0 ? juce::String ("0 ST") : (s > 0 ? "+" : "") + juce::String (s) + " ST";
                                })));
    l.add (pct (ParamIDs::structure, "Structure", 0.25f));
    l.add (pct (ParamIDs::brightness, "Brightness", 0.5f));
    l.add (pct (ParamIDs::damping, "Damping", 0.5f));
    l.add (pct (ParamIDs::position, "Position", 0.3f));
    l.add (std::make_unique<F> (ParameterID { ParamIDs::strum, 1 }, "Strum", NormalisableRange<float> (6.0f, 150.0f, 0.0f, 0.5f), 12.0f,
                                A().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " MS"; })));

    // INPUT
    l.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::exciter, 1 }, "Exciter Input", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::onset, 1 }, "Onset Strum", false));
    l.add (gain (ParamIDs::inGain, "In Gain", -24.0f, 24.0f, 0.0f));

    // OUTPUT
    l.add (gain (ParamIDs::dry, "Dry", -60.0f, 6.0f, -60.0f));
    l.add (gain (ParamIDs::out, "Out", -60.0f, 12.0f, 0.0f));

    return l;
}

PingsProcessor::PingsProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    const char* ids[kNumP] = { ParamIDs::model, ParamIDs::voices, ParamIDs::fx, ParamIDs::frequency, ParamIDs::structure,
                               ParamIDs::brightness, ParamIDs::damping, ParamIDs::position, ParamIDs::strum,
                               ParamIDs::exciter, ParamIDs::onset, ParamIDs::inGain, ParamIDs::dry, ParamIDs::out };
    for (int i = 0; i < kNumP; ++i)
        p[i] = apvts.getRawParameterValue (ids[i]);
    pendingNotes.reserve (128);
}

bool PingsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void PingsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine->prepare (sampleRate, (uint32_t) juce::Random::getSystemRandom().nextInt());
    wetBuf.setSize (2, juce::jmax (1, samplesPerBlock));
    dryS.reset (sampleRate, 0.03); dryS.setCurrentAndTargetValue (dbToGain (get (pDry)));
    outS.reset (sampleRate, 0.03); outS.setCurrentAndTargetValue (dbToGain (get (pOut)));
}

void PingsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;   // Rings' resonators decay into denormals; the module's chip flushed them
    const int n = buffer.getNumSamples();

    // notes pressed together strum upward, the way a hand would
    pendingNotes.clear();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            pendingNotes.push_back (m.getNoteNumber());
        else if (m.isNoteOff())
            engine->noteOff (m.getNoteNumber());
        else if (m.isPitchWheel())
            bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            engine->allNotesOff();
    }
    std::sort (pendingNotes.begin(), pendingNotes.end());
    for (int k : pendingNotes)
        engine->noteOn (k);
    midi.clear();

    if (n == 0)
        return;
    if (getTotalNumInputChannels() == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);
    if (wetBuf.getNumSamples() < n)
        wetBuf.setSize (2, n, false, false, true);

    png::Params ep;
    ep.model = juce::roundToInt (get (pModel));
    ep.voices = juce::roundToInt (get (pVoices)) + 1;
    ep.fx = juce::roundToInt (get (pFx));
    ep.frequency = get (pFrequency);
    ep.structure = get (pStructure);
    ep.brightness = get (pBrightness);
    ep.damping = get (pDamping);
    ep.position = get (pPosition);
    ep.strumMs = get (pStrum);
    ep.useInput = get (pExciter) > 0.5f;
    ep.onset = get (pOnset) > 0.5f;
    ep.inputGain = juce::Decibels::decibelsToGain (get (pInGain));
    ep.bend = bend;

    dryS.setTargetValue (dbToGain (get (pDry)));
    outS.setTargetValue (dbToGain (get (pOut)));

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
    auto* wL = wetBuf.getWritePointer (0);
    auto* wR = wetBuf.getWritePointer (1);
    engine->process (L, R, wL, wR, n, ep);

    for (int i = 0; i < n; ++i)
    {
        const float d = dryS.getNextValue(), o = outS.getNextValue();
        L[i] = juce::jlimit (-1.5f, 1.5f, (L[i] * d + wL[i]) * o);
        R[i] = juce::jlimit (-1.5f, 1.5f, (R[i] * d + wR[i]) * o);
    }
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);
}

void PingsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PingsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* PingsProcessor::createEditor()
{
    return new PingsEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PingsProcessor();
}
