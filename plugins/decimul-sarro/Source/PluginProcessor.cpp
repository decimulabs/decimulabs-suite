#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
    juce::ParameterID pid(const juce::String& id) { return { id, 1 }; }
}

DecimulSarroProcessor::DecimulSarroProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DecimulSarroProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    auto flt = [&params](const String& id, const String& name, float lo, float hi, float def, const String& unit,
                          float step, float centre = 0.0f)
    {
        NormalisableRange<float> r(lo, hi, step);
        if (centre > 0.0f)
            r.setSkewForCentre(centre);
        params.push_back(std::make_unique<AudioParameterFloat>(pid(id), name, r, def, AudioParameterFloatAttributes().withLabel(unit)));
    };

    flt(ParamIDs::depth, "Depth", 0.0f, 100.0f, 50.0f, "%", 1.0f);
    flt(ParamIDs::sharpness, "Sharpness", 0.0f, 100.0f, 50.0f, "%", 1.0f);
    flt(ParamIDs::sensitivity, "Sensitivity", 0.0f, 100.0f, 50.0f, "%", 1.0f);
    flt(ParamIDs::attack, "Attack", 1.0f, 200.0f, 10.0f, "ms", 0.1f, 20.0f);
    flt(ParamIDs::release, "Release", 20.0f, 1500.0f, 150.0f, "ms", 1.0f, 250.0f);
    flt(ParamIDs::lowFocus, "Low Focus", 20.0f, 2000.0f, 80.0f, "Hz", 1.0f, 250.0f);
    flt(ParamIDs::highFocus, "High Focus", 2000.0f, 20000.0f, 16000.0f, "Hz", 1.0f, 8000.0f);
    flt(ParamIDs::link, "Stereo Link", 0.0f, 100.0f, 100.0f, "%", 1.0f);
    flt(ParamIDs::mix, "Mix", 0.0f, 100.0f, 100.0f, "%", 1.0f);
    flt(ParamIDs::trim, "Trim", -24.0f, 12.0f, 0.0f, "dB", 0.1f);
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::delta), "Delta", false));
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::bypass), "Bypass", false));

    return { params.begin(), params.end() };
}

bool DecimulSarroProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
           && layouts.getMainInputChannelSet() == out;
}

sarro::Params DecimulSarroProcessor::readParams() const
{
    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };
    sarro::Params p;
    p.depth = get(ParamIDs::depth);
    p.sharpness = get(ParamIDs::sharpness);
    p.sensitivity = get(ParamIDs::sensitivity);
    p.attackMs = get(ParamIDs::attack);
    p.releaseMs = get(ParamIDs::release);
    p.lowHz = get(ParamIDs::lowFocus);
    p.highHz = get(ParamIDs::highFocus);
    p.link = get(ParamIDs::link);
    p.mix = get(ParamIDs::mix);
    p.trimDb = get(ParamIDs::trim);
    p.delta = get(ParamIDs::delta) > 0.5f;
    p.bypass = get(ParamIDs::bypass) > 0.5f;
    return p;
}

void DecimulSarroProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRateHz = sampleRate;
    engine.prepare(sampleRate);
    engine.setParams(readParams());
    setLatencySamples(engine.latencySamples());
}

void DecimulSarroProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (n <= 0 || numChannels < 1)
        return;

    float* L = buffer.getWritePointer(0);
    float* R = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    float inPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        inPeak = std::max(inPeak, buffer.getMagnitude(ch, 0, n));

    engine.setParams(readParams());
    engine.process(L, R, n);

    float outPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        outPeak = std::max(outPeak, buffer.getMagnitude(ch, 0, n));
    inputPeakDb.store(juce::Decibels::gainToDecibels(inPeak, -100.0f));
    outputPeakDb.store(juce::Decibels::gainToDecibels(outPeak, -100.0f));
}

juce::AudioProcessorEditor* DecimulSarroProcessor::createEditor()
{
    return new DecimulSarroEditor(*this);
}

void DecimulSarroProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (!state.isValid())
        return;
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void DecimulSarroProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DecimulSarroProcessor();
}
