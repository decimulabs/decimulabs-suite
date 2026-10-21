#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
    juce::ParameterID pid(const juce::String& id) { return { id, 1 }; }
}

DecimulAtaqueProcessor::DecimulAtaqueProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DecimulAtaqueProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    auto pct = AudioParameterFloatAttributes().withLabel("%");

    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::attack), "Attack", NormalisableRange<float>(-100.0f, 100.0f, 1.0f), 0.0f, pct));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::sustain), "Sustain", NormalisableRange<float>(-100.0f, 100.0f, 1.0f), 0.0f, pct));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::speed), "Speed", NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f, pct));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::outputTrim), "Output", NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::bypass), "Bypass", false));

    return { params.begin(), params.end() };
}

bool DecimulAtaqueProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
           && layouts.getMainInputChannelSet() == out;
}

void DecimulAtaqueProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRateHz = sampleRate;
    shaper.prepare(sampleRate);

    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };
    smOutput.reset(sampleRate, 0.05);
    smOutput.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(get(ParamIDs::outputTrim)));
    smBypass.reset(sampleRate, 0.02);
    smBypass.setCurrentAndTargetValue(get(ParamIDs::bypass) > 0.5f ? 1.0f : 0.0f);

    frameFifo.reset();
}

void DecimulAtaqueProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (n <= 0 || numChannels < 1)
        return;

    float* L = buffer.getWritePointer(0);
    float* R = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };
    shaper.set(get(ParamIDs::attack) * 0.01f, get(ParamIDs::sustain) * 0.01f, get(ParamIDs::speed) * 0.01f);
    smOutput.setTargetValue(juce::Decibels::decibelsToGain(get(ParamIDs::outputTrim)));
    smBypass.setTargetValue(get(ParamIDs::bypass) > 0.5f ? 1.0f : 0.0f);

    float inPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        inPeak = std::max(inPeak, buffer.getMagnitude(ch, 0, n));

    // Copia de la entrada solo para el fundido del bypass (sin asignar memoria: se rehace por muestra).
    for (int start = 0; start < n; start += frameChunk)
    {
        const int len = std::min(frameChunk, n - start);
        float dryL[frameChunk], dryR[frameChunk];
        std::copy(L + start, L + start + len, dryL);
        if (R != nullptr)
            std::copy(R + start, R + start + len, dryR);

        shaper.process(L + start, R != nullptr ? R + start : nullptr, len);

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
            frameBuffer[(size_t) s1] = { shaper.getFastLevelDb(), shaper.getGainDb() };
            frameFifo.finishedWrite(1);
        }
    }

    float outPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        outPeak = std::max(outPeak, buffer.getMagnitude(ch, 0, n));
    inputPeakDb.store(juce::Decibels::gainToDecibels(inPeak, -100.0f));
    outputPeakDb.store(juce::Decibels::gainToDecibels(outPeak, -100.0f));
}

int DecimulAtaqueProcessor::readFrames(Frame* dest, int maxFrames)
{
    int s1 = 0, z1 = 0, s2 = 0, z2 = 0;
    frameFifo.prepareToRead(std::min(maxFrames, frameFifo.getNumReady()), s1, z1, s2, z2);
    int copied = 0;
    for (int i = 0; i < z1; ++i) dest[copied++] = frameBuffer[(size_t) (s1 + i)];
    for (int i = 0; i < z2; ++i) dest[copied++] = frameBuffer[(size_t) (s2 + i)];
    frameFifo.finishedRead(z1 + z2);
    return copied;
}

juce::AudioProcessorEditor* DecimulAtaqueProcessor::createEditor()
{
    return new DecimulAtaqueEditor(*this);
}

void DecimulAtaqueProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (!state.isValid())
        return;
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void DecimulAtaqueProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DecimulAtaqueProcessor();
}
