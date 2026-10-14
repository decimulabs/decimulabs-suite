#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    juce::ParameterID pid(const juce::String& id) { return { id, 1 }; }
}

DecimulCarveProcessor::DecimulCarveProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (auto& v : uiLevelDb) v.store(-120.0f);
    for (auto& v : uiCutDb) v.store(0.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout DecimulCarveProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::amount), "Amount", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 60.0f,
        AudioParameterFloatAttributes().withLabel("%")));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::scGain), "Sidechain Gain", NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::attack), "Attack", NormalisableRange<float>(0.1f, 100.0f, 0.1f, 0.4f), 8.0f,
        AudioParameterFloatAttributes().withLabel("ms")));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::release), "Release", NormalisableRange<float>(5.0f, 1000.0f, 1.0f, 0.4f), 150.0f,
        AudioParameterFloatAttributes().withLabel("ms")));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::lowFreq), "Low Freq", NormalisableRange<float>(20.0f, 5000.0f, 1.0f, 0.3f), 20.0f,
        AudioParameterFloatAttributes().withLabel("Hz")));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::highFreq), "High Freq", NormalisableRange<float>(500.0f, 20000.0f, 1.0f, 0.3f), 20000.0f,
        AudioParameterFloatAttributes().withLabel("Hz")));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::smooth), "Smooth", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 25.0f,
        AudioParameterFloatAttributes().withLabel("%")));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::mix), "Mix", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel("%")));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::outputTrim), "Output", NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::bypass), "Bypass", false));

    return { params.begin(), params.end() };
}

bool DecimulCarveProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // El sidechain es opcional: apagado o estéreo.
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet(true, 1);
        if (!sc.isDisabled() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void DecimulCarveProcessor::prepareToPlay(double sampleRate, int)
{
    engine.prepare(sampleRate);
    outputGain.reset(sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(
        apvts.getRawParameterValue(ParamIDs::outputTrim)->load()));
}

void DecimulCarveProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0)
        return;

    auto main = getBusBuffer(buffer, true, 0);
    if (main.getNumChannels() < 2)
        return;

    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };

    if (get(ParamIDs::bypass) > 0.5f)
    {
        for (auto& v : uiCutDb) v.store(0.0f);
        outputPeakDb.store(juce::Decibels::gainToDecibels(main.getMagnitude(0, n), -100.0f));
        return;
    }

    // ---- Sidechain ----
    const float* scL = nullptr;
    const float* scR = nullptr;
    if (getBusCount(true) > 1)
    {
        if (auto* scBus = getBus(true, 1); scBus != nullptr && scBus->isEnabled())
        {
            auto sc = getBusBuffer(buffer, true, 1);
            if (sc.getNumChannels() >= 1)
            {
                scL = sc.getReadPointer(0);
                scR = sc.getNumChannels() > 1 ? sc.getReadPointer(1) : scL;
            }
        }
    }
    sidechainConnected.store(scL != nullptr);

    carve::Params p;
    p.amount = get(ParamIDs::amount) * 0.01f;
    p.scGainDb = get(ParamIDs::scGain);
    p.attackMs = get(ParamIDs::attack);
    p.releaseMs = get(ParamIDs::release);
    p.lowHz = get(ParamIDs::lowFreq);
    p.highHz = get(ParamIDs::highFreq);
    p.smooth = get(ParamIDs::smooth) * 0.01f;
    p.mix = get(ParamIDs::mix) * 0.01f;

    float* l = main.getWritePointer(0);
    float* r = main.getWritePointer(1);
    engine.process(l, r, scL, scR, n, p);

    outputGain.setTargetValue(juce::Decibels::decibelsToGain(get(ParamIDs::outputTrim)));
    for (int i = 0; i < n; ++i)
    {
        const float g = outputGain.getNextValue();
        l[i] *= g;
        r[i] *= g;
    }

    const auto& lv = engine.levelsDb();
    const auto& cut = engine.cutsDb();
    for (int b = 0; b < carve::numBands; ++b)
    {
        uiLevelDb[(size_t) b].store(lv[(size_t) b]);
        uiCutDb[(size_t) b].store(cut[(size_t) b]);
    }
    outputPeakDb.store(juce::Decibels::gainToDecibels(main.getMagnitude(0, n), -100.0f));
}

juce::AudioProcessorEditor* DecimulCarveProcessor::createEditor()
{
    return new DecimulCarveEditor(*this);
}

void DecimulCarveProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (!state.isValid())
        return;
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void DecimulCarveProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DecimulCarveProcessor();
}
