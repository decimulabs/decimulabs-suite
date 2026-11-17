#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::ParameterID pid(const juce::String& id) { return { id, 1 }; }
}

DecimulNavajaProcessor::DecimulNavajaProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DecimulNavajaProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::gain), "Gain", NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
                                                      AudioParameterFloatAttributes().withLabel("dB")));
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::pan), "Pan", NormalisableRange<float>(-100.0f, 100.0f, 1.0f), 0.0f));
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::width), "Width", NormalisableRange<float>(0.0f, 200.0f, 1.0f), 100.0f,
                                                      AudioParameterFloatAttributes().withLabel("%")));
    p.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::invertL), "Invert L", false));
    p.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::invertR), "Invert R", false));
    p.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::swap), "Swap L/R", false));
    p.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::mono), "Mono", false));
    p.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::bassMono), "Bass Mono", false));
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::bassHz), "Bass Mono Freq", NormalisableRange<float>(40.0f, 400.0f, 1.0f, 0.5f), 120.0f,
                                                      AudioParameterFloatAttributes().withLabel("Hz")));
    p.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::dc), "DC Filter", false));
    return { p.begin(), p.end() };
}

void DecimulNavajaProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0 || buffer.getNumChannels() < 2)
        return;
    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };
    navaja::Params p;
    p.gainDb = get(ParamIDs::gain);
    p.pan = get(ParamIDs::pan) * 0.01f;
    p.width = get(ParamIDs::width) * 0.01f;
    p.invertL = get(ParamIDs::invertL) > 0.5f;
    p.invertR = get(ParamIDs::invertR) > 0.5f;
    p.swap = get(ParamIDs::swap) > 0.5f;
    p.mono = get(ParamIDs::mono) > 0.5f;
    p.bassMono = get(ParamIDs::bassMono) > 0.5f;
    p.bassMonoHz = get(ParamIDs::bassHz);
    p.dcFilter = get(ParamIDs::dc) > 0.5f;
    engine.process(buffer.getWritePointer(0), buffer.getWritePointer(1), n, p);
    correlation.store(engine.correlation());
    peakL.store(juce::Decibels::gainToDecibels(buffer.getMagnitude(0, 0, n), -100.0f));
    peakR.store(juce::Decibels::gainToDecibels(buffer.getMagnitude(1, 0, n), -100.0f));
}

juce::AudioProcessorEditor* DecimulNavajaProcessor::createEditor() { return new DecimulNavajaEditor(*this); }

void DecimulNavajaProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DecimulNavajaProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DecimulNavajaProcessor(); }
