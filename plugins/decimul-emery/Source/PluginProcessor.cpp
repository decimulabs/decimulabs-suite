#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
    juce::ParameterID pid(const juce::String& id) { return { id, 1 }; }
}

DecimulEmeryProcessor::DecimulEmeryProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DecimulEmeryProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    auto hz = AudioParameterFloatAttributes().withLabel("Hz");
    auto db = AudioParameterFloatAttributes().withLabel("dB");

    // Rango logarítmico para las frecuencias (skew por el punto medio).
    auto logRange = [](float lo, float hi, float mid)
    {
        NormalisableRange<float> r(lo, hi, 1.0f);
        r.setSkewForCentre(mid);
        return r;
    };

    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::essOn), "Sibilance On", true));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::essFreq), "Sibilance Frequency", logRange(2000.0f, 12000.0f, 5000.0f), 6500.0f, hz));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::essThresh), "Sibilance Threshold", NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -30.0f, db));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::essAmount), "Sibilance Amount", NormalisableRange<float>(0.0f, 24.0f, 0.1f), 10.0f, db));
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::essWide), "Sibilance Wide", false));
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::essListen), "Sibilance Listen", false));

    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::popOn), "Pops On", true));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::popFreq), "Pops Frequency", logRange(40.0f, 300.0f, 110.0f), 120.0f, hz));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::popThresh), "Pops Threshold", NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -26.0f, db));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::popAmount), "Pops Amount", NormalisableRange<float>(0.0f, 24.0f, 0.1f), 12.0f, db));
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::popWide), "Pops Wide", false));
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::popListen), "Pops Listen", false));

    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::outputTrim), "Output", NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f, db));
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::bypass), "Bypass", false));

    return { params.begin(), params.end() };
}

bool DecimulEmeryProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
           && layouts.getMainInputChannelSet() == out;
}

void DecimulEmeryProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRateHz = sampleRate;

    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };
    engine.ess.freqTarget = get(ParamIDs::essFreq);
    engine.pop.freqTarget = get(ParamIDs::popFreq);
    engine.prepare(sampleRate);

    smOutput.reset(sampleRate, 0.05);
    smOutput.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(get(ParamIDs::outputTrim)));
    smBypass.reset(sampleRate, 0.02);
    smBypass.setCurrentAndTargetValue(get(ParamIDs::bypass) > 0.5f ? 1.0f : 0.0f);

    frameFifo.reset();
}

void DecimulEmeryProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (n <= 0 || numChannels < 1)
        return;

    float* L = buffer.getWritePointer(0);
    float* R = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };
    engine.ess.on = get(ParamIDs::essOn) > 0.5f;
    engine.ess.freqTarget = get(ParamIDs::essFreq);
    engine.ess.thresholdDb = get(ParamIDs::essThresh);
    engine.ess.maxReductionDb = get(ParamIDs::essAmount);
    engine.ess.wide = get(ParamIDs::essWide) > 0.5f;
    engine.listenEss = get(ParamIDs::essListen) > 0.5f;

    engine.pop.on = get(ParamIDs::popOn) > 0.5f;
    engine.pop.freqTarget = get(ParamIDs::popFreq);
    engine.pop.thresholdDb = get(ParamIDs::popThresh);
    engine.pop.maxReductionDb = get(ParamIDs::popAmount);
    engine.pop.wide = get(ParamIDs::popWide) > 0.5f;
    engine.listenPop = get(ParamIDs::popListen) > 0.5f;

    smOutput.setTargetValue(juce::Decibels::decibelsToGain(get(ParamIDs::outputTrim)));
    smBypass.setTargetValue(get(ParamIDs::bypass) > 0.5f ? 1.0f : 0.0f);

    for (int start = 0; start < n; start += frameChunk)
    {
        const int len = std::min(frameChunk, n - start);
        float dryL[frameChunk], dryR[frameChunk];
        std::copy(L + start, L + start + len, dryL);
        if (R != nullptr)
            std::copy(R + start, R + start + len, dryR);

        engine.process(L + start, R != nullptr ? R + start : nullptr, len);

        for (int i = 0; i < len; ++i)
        {
            const float out = smOutput.getNextValue();
            const float byp = smBypass.getNextValue();
            L[start + i] = (L[start + i] * out) * (1.0f - byp) + dryL[i] * byp;
            if (R != nullptr)
                R[start + i] = (R[start + i] * out) * (1.0f - byp) + dryR[i] * byp;
        }

        int s1 = 0, z1 = 0, s2 = 0, z2 = 0;
        frameFifo.prepareToWrite(1, s1, z1, s2, z2);
        if (z1 > 0)
        {
            frameBuffer[(size_t) s1] = { engine.ess.envDb, engine.ess.reductionDb, engine.pop.envDb, engine.pop.reductionDb };
            frameFifo.finishedWrite(1);
        }
    }

    float outPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        outPeak = std::max(outPeak, buffer.getMagnitude(ch, 0, n));
    outputPeakDb.store(juce::Decibels::gainToDecibels(outPeak, -100.0f));
}

int DecimulEmeryProcessor::readFrames(Frame* dest, int maxFrames)
{
    int s1 = 0, z1 = 0, s2 = 0, z2 = 0;
    frameFifo.prepareToRead(std::min(maxFrames, frameFifo.getNumReady()), s1, z1, s2, z2);
    int copied = 0;
    for (int i = 0; i < z1; ++i) dest[copied++] = frameBuffer[(size_t) (s1 + i)];
    for (int i = 0; i < z2; ++i) dest[copied++] = frameBuffer[(size_t) (s2 + i)];
    frameFifo.finishedRead(z1 + z2);
    return copied;
}

juce::AudioProcessorEditor* DecimulEmeryProcessor::createEditor()
{
    return new DecimulEmeryEditor(*this);
}

void DecimulEmeryProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (!state.isValid())
        return;
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void DecimulEmeryProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DecimulEmeryProcessor();
}
