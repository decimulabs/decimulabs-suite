#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::ParameterID pid(const juce::String& id) { return { id, 1 }; }
}

DecimulFuelleProcessor::DecimulFuelleProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DecimulFuelleProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = [&](const String& id, const String& name, float def)
    {
        p.push_back(std::make_unique<AudioParameterFloat>(pid(id), name, NormalisableRange<float>(0.0f, 100.0f, 1.0f), def,
                                                          AudioParameterFloatAttributes().withLabel("%")));
    };
    pct(ParamIDs::size, "Size", 70.0f);
    pct(ParamIDs::damping, "Damping", 40.0f);
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::predelay), "Predelay", NormalisableRange<float>(0.0f, 200.0f, 1.0f), 20.0f,
                                                      AudioParameterFloatAttributes().withLabel("ms")));
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::lowCut), "Low Cut", NormalisableRange<float>(20.0f, 1000.0f, 1.0f, 0.4f), 200.0f,
                                                      AudioParameterFloatAttributes().withLabel("Hz")));
    pct(ParamIDs::width, "Width", 100.0f);
    pct(ParamIDs::duck, "Duck", 70.0f);
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::threshold), "Threshold", NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -30.0f,
                                                      AudioParameterFloatAttributes().withLabel("dB")));
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::release), "Release", NormalisableRange<float>(20.0f, 3000.0f, 1.0f, 0.4f), 400.0f,
                                                      AudioParameterFloatAttributes().withLabel("ms")));
    pct(ParamIDs::mix, "Mix", 30.0f);
    return { p.begin(), p.end() };
}

void DecimulFuelleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0 || buffer.getNumChannels() < 2)
        return;
    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };
    fuelle::Params p;
    p.size = get(ParamIDs::size) * 0.01f;
    p.damping = get(ParamIDs::damping) * 0.01f;
    p.predelayMs = get(ParamIDs::predelay);
    p.lowCutHz = get(ParamIDs::lowCut);
    p.width = get(ParamIDs::width) * 0.01f;
    p.duck = get(ParamIDs::duck) * 0.01f;
    p.thresholdDb = get(ParamIDs::threshold);
    p.releaseMs = get(ParamIDs::release);
    p.mix = get(ParamIDs::mix) * 0.01f;
    engine.process(buffer.getWritePointer(0), buffer.getWritePointer(1), n, p);
    duckGain.store(engine.duckGain());
    peakL.store(juce::Decibels::gainToDecibels(buffer.getMagnitude(0, 0, n), -100.0f));
    peakR.store(juce::Decibels::gainToDecibels(buffer.getMagnitude(1, 0, n), -100.0f));
}

juce::AudioProcessorEditor* DecimulFuelleProcessor::createEditor() { return new DecimulFuelleEditor(*this); }

void DecimulFuelleProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DecimulFuelleProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DecimulFuelleProcessor(); }
