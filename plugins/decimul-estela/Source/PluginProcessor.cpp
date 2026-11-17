#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::ParameterID pid(const juce::String& id) { return { id, 1 }; }
    // Duración de cada figura en negras.
    const double divisionBeats[] = { 2.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 0.375 };
}

const juce::StringArray& DecimulEstelaProcessor::divisionNames()
{
    static const juce::StringArray names { "1/2", "1/4", "1/4.", "1/4T", "1/8", "1/8.", "1/8T", "1/16", "1/16." };
    return names;
}

DecimulEstelaProcessor::DecimulEstelaProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DecimulEstelaProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = [&](const String& id, const String& name, float def)
    {
        p.push_back(std::make_unique<AudioParameterFloat>(pid(id), name, NormalisableRange<float>(0.0f, 100.0f, 1.0f), def,
                                                          AudioParameterFloatAttributes().withLabel("%")));
    };
    p.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::sync), "Sync", true));
    p.push_back(std::make_unique<AudioParameterChoice>(pid(ParamIDs::division), "Division", divisionNames(), 5));
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::timeMs), "Time", NormalisableRange<float>(10.0f, 2500.0f, 1.0f, 0.4f), 375.0f,
                                                      AudioParameterFloatAttributes().withLabel("ms")));
    pct(ParamIDs::feedback, "Feedback", 40.0f);
    p.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::pingPong), "Ping-Pong", false));
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::lowCut), "Low Cut", NormalisableRange<float>(20.0f, 2000.0f, 1.0f, 0.4f), 150.0f,
                                                      AudioParameterFloatAttributes().withLabel("Hz")));
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::highCut), "High Cut", NormalisableRange<float>(500.0f, 20000.0f, 1.0f, 0.4f), 6000.0f,
                                                      AudioParameterFloatAttributes().withLabel("Hz")));
    pct(ParamIDs::wobble, "Wobble", 0.0f);
    pct(ParamIDs::duck, "Duck", 70.0f);
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::threshold), "Threshold", NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -30.0f,
                                                      AudioParameterFloatAttributes().withLabel("dB")));
    p.push_back(std::make_unique<AudioParameterFloat>(pid(ParamIDs::release), "Release", NormalisableRange<float>(20.0f, 2000.0f, 1.0f, 0.4f), 300.0f,
                                                      AudioParameterFloatAttributes().withLabel("ms")));
    pct(ParamIDs::mix, "Mix", 30.0f);
    return { p.begin(), p.end() };
}

void DecimulEstelaProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0 || buffer.getNumChannels() < 2)
        return;
    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm(); b.hasValue() && *b > 1.0)
                bpm = *b;

    estela::Params p;
    if (get(ParamIDs::sync) > 0.5f)
    {
        const int d = juce::jlimit(0, 8, (int) get(ParamIDs::division));
        p.timeMs = (float) (divisionBeats[d] * 60000.0 / bpm);
    }
    else
        p.timeMs = get(ParamIDs::timeMs);
    p.feedback = get(ParamIDs::feedback) * 0.0095f;          // 100 % = 0.95
    p.pingPong = get(ParamIDs::pingPong) > 0.5f;
    p.lowCutHz = get(ParamIDs::lowCut);
    p.highCutHz = get(ParamIDs::highCut);
    p.wobble = get(ParamIDs::wobble) * 0.01f;
    p.duck = get(ParamIDs::duck) * 0.01f;
    p.thresholdDb = get(ParamIDs::threshold);
    p.releaseMs = get(ParamIDs::release);
    p.mix = get(ParamIDs::mix) * 0.01f;
    engine.process(buffer.getWritePointer(0), buffer.getWritePointer(1), n, p);

    uiTimeMs.store(p.timeMs);
    duckGain.store(engine.duckGain());
    peakL.store(juce::Decibels::gainToDecibels(buffer.getMagnitude(0, 0, n), -100.0f));
    peakR.store(juce::Decibels::gainToDecibels(buffer.getMagnitude(1, 0, n), -100.0f));
}

juce::AudioProcessorEditor* DecimulEstelaProcessor::createEditor() { return new DecimulEstelaEditor(*this); }

void DecimulEstelaProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DecimulEstelaProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DecimulEstelaProcessor(); }
