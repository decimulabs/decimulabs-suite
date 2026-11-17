#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
    juce::ParameterID pid(const juce::String& id) { return { id, 1 }; }
}

DecimulViseProcessor::DecimulViseProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DecimulViseProcessor::createParameterLayout()
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

    flt(ParamIDs::threshold, "Threshold", -40.0f, 0.0f, -18.0f, "dB", 0.1f);
    flt(ParamIDs::ratio, "Ratio", 1.1f, 20.0f, 4.0f, ":1", 0.1f, 4.0f);
    flt(ParamIDs::attack, "Attack", 0.1f, 50.0f, 10.0f, "ms", 0.1f, 8.0f);
    flt(ParamIDs::release, "Release", 20.0f, 1500.0f, 300.0f, "ms", 1.0f, 250.0f);
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::autoRelease), "Auto Release", false));
    flt(ParamIDs::knee, "Knee", 0.0f, 12.0f, 6.0f, "dB", 0.1f);
    flt(ParamIDs::makeup, "Makeup", 0.0f, 24.0f, 0.0f, "dB", 0.1f);
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::autoMakeup), "Auto Makeup", false));
    flt(ParamIDs::mix, "Mix", 0.0f, 100.0f, 100.0f, "%", 1.0f);
    flt(ParamIDs::scHp, "Sidechain HPF", 20.0f, 400.0f, 20.0f, "Hz", 1.0f, 90.0f);
    flt(ParamIDs::link, "Stereo Link", 0.0f, 100.0f, 100.0f, "%", 1.0f);
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::rms), "RMS Detector", false));
    flt(ParamIDs::outputTrim, "Output", -24.0f, 12.0f, 0.0f, "dB", 0.1f);
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::bypass), "Bypass", false));

    return { params.begin(), params.end() };
}

bool DecimulViseProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
           && layouts.getMainInputChannelSet() == out;
}

vise::Params DecimulViseProcessor::readParams() const
{
    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };
    vise::Params p;
    p.thresholdDb = get(ParamIDs::threshold);
    p.ratio = get(ParamIDs::ratio);
    p.attackMs = get(ParamIDs::attack);
    p.releaseMs = get(ParamIDs::release);
    p.autoRelease = get(ParamIDs::autoRelease) > 0.5f;
    p.kneeDb = get(ParamIDs::knee);
    p.makeupDb = get(ParamIDs::makeup);
    p.autoMakeup = get(ParamIDs::autoMakeup) > 0.5f;
    p.mix = get(ParamIDs::mix);
    p.scHpHz = get(ParamIDs::scHp);
    p.link = get(ParamIDs::link);
    p.rms = get(ParamIDs::rms) > 0.5f;
    return p;
}

void DecimulViseProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRateHz = sampleRate;
    engine.setParams(readParams());
    engine.prepare(sampleRate);

    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };
    smOutput.reset(sampleRate, 0.05);
    smOutput.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(get(ParamIDs::outputTrim)));
    smBypass.reset(sampleRate, 0.02);
    smBypass.setCurrentAndTargetValue(get(ParamIDs::bypass) > 0.5f ? 1.0f : 0.0f);

    frameFifo.reset();
}

void DecimulViseProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (n <= 0 || numChannels < 1)
        return;

    float* L = buffer.getWritePointer(0);
    float* R = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };
    engine.setParams(readParams());
    smOutput.setTargetValue(juce::Decibels::decibelsToGain(get(ParamIDs::outputTrim)));
    smBypass.setTargetValue(get(ParamIDs::bypass) > 0.5f ? 1.0f : 0.0f);

    float inPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        inPeak = std::max(inPeak, buffer.getMagnitude(ch, 0, n));

    float dryL[frameChunk], dryR[frameChunk];
    for (int start = 0; start < n; start += frameChunk)
    {
        const int len = std::min(frameChunk, n - start);
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

        float gr = 0.0f, inDb = -120.0f;
        engine.takeMeters(gr, inDb);
        int s1 = 0, z1 = 0, s2 = 0, z2 = 0;
        frameFifo.prepareToWrite(1, s1, z1, s2, z2);
        if (z1 > 0)
        {
            frameBuffer[(size_t) s1] = { inDb, gr };
            frameFifo.finishedWrite(1);
        }
    }

    float outPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        outPeak = std::max(outPeak, buffer.getMagnitude(ch, 0, n));
    inputPeakDb.store(juce::Decibels::gainToDecibels(inPeak, -100.0f));
    outputPeakDb.store(juce::Decibels::gainToDecibels(outPeak, -100.0f));
    currentGrDb.store(smBypass.getCurrentValue() > 0.5f ? 0.0f : engine.currentGr());
}

int DecimulViseProcessor::readFrames(Frame* dest, int maxFrames)
{
    int s1 = 0, z1 = 0, s2 = 0, z2 = 0;
    frameFifo.prepareToRead(std::min(maxFrames, frameFifo.getNumReady()), s1, z1, s2, z2);
    int copied = 0;
    for (int i = 0; i < z1; ++i) dest[copied++] = frameBuffer[(size_t) (s1 + i)];
    for (int i = 0; i < z2; ++i) dest[copied++] = frameBuffer[(size_t) (s2 + i)];
    frameFifo.finishedRead(z1 + z2);
    return copied;
}

juce::AudioProcessorEditor* DecimulViseProcessor::createEditor()
{
    return new DecimulViseEditor(*this);
}

void DecimulViseProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (!state.isValid())
        return;
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void DecimulViseProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DecimulViseProcessor();
}
