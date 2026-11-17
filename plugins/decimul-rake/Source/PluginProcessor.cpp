#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::ParameterID pid(const juce::String& id) { return { id, 1 }; }
}

const juce::StringArray& DecimulRakeProcessor::chordNames()
{
    static const juce::StringArray names { "UNISONO", "QUINTA", "MAYOR", "MENOR", "SUS4", "SEPTIMA", "OCTAVAS" };
    return names;
}

DecimulRakeProcessor::DecimulRakeProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DecimulRakeProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back(std::make_unique<AudioParameterInt>(pid(ParamIDs::note), "Note", 24, 96, 57,
        AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) { return MidiMessage::getMidiNoteName(v, true, true, 4); })));
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::fine), "Fine", NormalisableRange<float>(-50.0f, 50.0f, 1.0f), 0.0f,
                                                      AudioParameterFloatAttributes().withLabel("ct")));
    p.push_back(std::make_unique<AudioParameterChoice>(pid(ParamIDs::chord), "Chord", chordNames(), 0));
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::feedback), "Feedback", NormalisableRange<float>(-99.0f, 99.0f, 1.0f), 85.0f,
                                                      AudioParameterFloatAttributes().withLabel("%")));
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::damping), "Damping", NormalisableRange<float>(0.0f, 100.0f, 1.0f), 30.0f,
                                                      AudioParameterFloatAttributes().withLabel("%")));
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::lfoRate), "LFO Rate", NormalisableRange<float>(0.02f, 10.0f, 0.01f, 0.35f), 0.4f,
                                                      AudioParameterFloatAttributes().withLabel("Hz")));
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::lfoDepth), "LFO Depth", NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f,
                                                      AudioParameterFloatAttributes().withLabel("%")));
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::stereo), "Stereo", NormalisableRange<float>(0.0f, 180.0f, 1.0f), 90.0f,
                                                      AudioParameterFloatAttributes().withLabel("deg")));
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::mix), "Mix", NormalisableRange<float>(0.0f, 100.0f, 1.0f), 60.0f,
                                                      AudioParameterFloatAttributes().withLabel("%")));
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::output), "Output", NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f,
                                                      AudioParameterFloatAttributes().withLabel("dB")));
    return { p.begin(), p.end() };
}

void DecimulRakeProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0 || buffer.getNumChannels() < 2)
        return;
    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };
    rake::Params p;
    p.note = get(ParamIDs::note) + get(ParamIDs::fine) * 0.01f;
    p.chord = (int) get(ParamIDs::chord);
    p.feedback = get(ParamIDs::feedback) * 0.01f;
    p.damping = get(ParamIDs::damping) * 0.01f;
    p.lfoRateHz = get(ParamIDs::lfoRate);
    p.lfoDepth = get(ParamIDs::lfoDepth) * 0.01f;
    p.stereoPhase = get(ParamIDs::stereo) / 360.0f;
    p.mix = get(ParamIDs::mix) * 0.01f;
    p.outputDb = get(ParamIDs::output);
    engine.process(buffer.getWritePointer(0), buffer.getWritePointer(1), n, p);

    uiPhase = std::fmod(uiPhase + 2.0 * rake::pi * p.lfoRateHz * n / sr, 2.0 * rake::pi);
    lfoPhase.store((float) uiPhase);
    peakL.store(juce::Decibels::gainToDecibels(buffer.getMagnitude(0, 0, n), -100.0f));
    peakR.store(juce::Decibels::gainToDecibels(buffer.getMagnitude(1, 0, n), -100.0f));
}

juce::AudioProcessorEditor* DecimulRakeProcessor::createEditor() { return new DecimulRakeEditor(*this); }

void DecimulRakeProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DecimulRakeProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DecimulRakeProcessor(); }
