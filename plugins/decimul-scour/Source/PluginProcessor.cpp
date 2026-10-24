#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
    juce::ParameterID pid(const juce::String& id) { return { id, 1 }; }
}

DecimulScourProcessor::DecimulScourProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DecimulScourProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    auto pct = [&params](const String& id, const String& name, float def)
    {
        params.push_back(std::make_unique<AudioParameterFloat>(
            pid(id), name, NormalisableRange<float>(0.0f, 100.0f, 0.1f), def, AudioParameterFloatAttributes().withLabel("%")));
    };
    auto flt = [&params](const String& id, const String& name, float lo, float hi, float def, const String& unit,
                          float step, float centre = 0.0f)
    {
        NormalisableRange<float> r(lo, hi, step);
        if (centre > 0.0f)
            r.setSkewForCentre(centre);
        params.push_back(std::make_unique<AudioParameterFloat>(pid(id), name, r, def, AudioParameterFloatAttributes().withLabel(unit)));
    };
    auto onoff = [&params](const String& id, const String& name, bool def)
    {
        params.push_back(std::make_unique<AudioParameterBool>(pid(id), name, def));
    };
    auto choice = [&params](const String& id, const String& name, const StringArray& items, int def)
    {
        params.push_back(std::make_unique<AudioParameterChoice>(pid(id), name, items, def));
    };

    // globales
    flt(ParamIDs::inGain, "Input", -24.0f, 12.0f, 0.0f, "dB", 0.1f);
    flt(ParamIDs::outGain, "Output", -24.0f, 12.0f, 0.0f, "dB", 0.1f);
    pct(ParamIDs::magnitude, "Magnitude", 100.0f);
    pct(ParamIDs::mix, "Mix", 100.0f);
    flt(ParamIDs::width, "Width", 0.0f, 200.0f, 100.0f, "%", 1.0f);
    onoff(ParamIDs::bypass, "Bypass", false);

    // EQ final
    onoff(ParamIDs::eqOn, "EQ On", true);
    flt(ParamIDs::eqLow, "Low Cut", 20.0f, 1000.0f, 20.0f, "Hz", 1.0f, 150.0f);
    flt(ParamIDs::eqHigh, "High Cut", 1000.0f, 20000.0f, 20000.0f, "Hz", 1.0f, 6000.0f);
    flt(ParamIDs::eqTone, "Tone", -100.0f, 100.0f, 0.0f, "", 1.0f);

    // NOISE
    onoff(ParamIDs::noiseOn, "Noise On", true);
    pct(ParamIDs::noiseAmt, "Noise Amount", 25.0f);
    choice(ParamIDs::noiseType, "Noise Type", { "Vinyl", "Tape", "Hum", "Static", "VHS", "Dust" }, 0);
    pct(ParamIDs::noiseTone, "Noise Tone", 60.0f);
    pct(ParamIDs::noiseFollow, "Noise Follow", 0.0f);
    pct(ParamIDs::noiseDuck, "Noise Duck", 0.0f);
    onoff(ParamIDs::noisePost, "Noise Post", false);
    pct(ParamIDs::noiseFlux, "Noise Flux", 0.0f);

    // WOBBLE
    onoff(ParamIDs::wobOn, "Wobble On", true);
    pct(ParamIDs::wobAmt, "Wobble Amount", 30.0f);
    pct(ParamIDs::wobBalance, "Wobble Wow-Flutter", 50.0f);
    flt(ParamIDs::wowRate, "Wow Rate", 0.1f, 3.0f, 0.6f, "Hz", 0.01f, 0.6f);
    flt(ParamIDs::flutRate, "Flutter Rate", 3.0f, 20.0f, 9.0f, "Hz", 0.1f, 9.0f);
    pct(ParamIDs::wobStereo, "Wobble Stereo", 30.0f);
    pct(ParamIDs::wobMix, "Wobble Mix", 100.0f);
    pct(ParamIDs::wobFlux, "Wobble Flux", 0.0f);

    // DISTORT
    onoff(ParamIDs::disOn, "Distort On", true);
    pct(ParamIDs::disAmt, "Distort Amount", 25.0f);
    choice(ParamIDs::disType, "Distort Type", { "Tube", "Tape", "Diode", "Fold" }, 0);
    flt(ParamIDs::disFocus, "Distort Focus", 20.0f, 6000.0f, 20.0f, "Hz", 1.0f, 400.0f);
    pct(ParamIDs::disMix, "Distort Mix", 100.0f);
    pct(ParamIDs::disFlux, "Distort Flux", 0.0f);

    // DIGITAL
    onoff(ParamIDs::digOn, "Digital On", true);
    pct(ParamIDs::digAmt, "Digital Amount", 20.0f);
    pct(ParamIDs::digBalance, "Digital Rate-Bits", 50.0f);
    flt(ParamIDs::digFocus, "Digital Focus", 20.0f, 6000.0f, 20.0f, "Hz", 1.0f, 400.0f);
    pct(ParamIDs::digSmooth, "Digital Smooth", 30.0f);
    pct(ParamIDs::digMix, "Digital Mix", 100.0f);
    pct(ParamIDs::digFlux, "Digital Flux", 0.0f);

    // SPACE
    onoff(ParamIDs::spaceOn, "Space On", true);
    pct(ParamIDs::spaceAmt, "Space Amount", 25.0f);
    pct(ParamIDs::spaceDecay, "Space Decay", 40.0f);
    flt(ParamIDs::spacePre, "Space Predelay", 0.0f, 200.0f, 20.0f, "ms", 1.0f, 40.0f);
    flt(ParamIDs::spaceFocus, "Space Focus", -100.0f, 100.0f, 0.0f, "", 1.0f);
    pct(ParamIDs::spaceStereo, "Space Stereo", 70.0f);
    pct(ParamIDs::spaceFlux, "Space Flux", 0.0f);

    // MAGNETIC
    onoff(ParamIDs::magOn, "Magnetic On", true);
    pct(ParamIDs::magAmt, "Magnetic Amount", 30.0f);
    pct(ParamIDs::magBalance, "Magnetic Wear-Flutter", 50.0f);
    flt(ParamIDs::magRate, "Magnetic Rate", 0.5f, 20.0f, 6.0f, "Hz", 0.1f, 6.0f);
    pct(ParamIDs::magDropouts, "Magnetic Dropouts", 20.0f);
    pct(ParamIDs::magStereo, "Magnetic Stereo", 50.0f);
    pct(ParamIDs::magFlux, "Magnetic Flux", 0.0f);

    return { params.begin(), params.end() };
}

bool DecimulScourProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
           && layouts.getMainInputChannelSet() == out;
}

scour::Params DecimulScourProcessor::readParams() const
{
    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };
    auto on = [&get](const juce::String& id) { return get(id) > 0.5f; };
    scour::Params p;
    p.inGainDb = get(ParamIDs::inGain); p.outGainDb = get(ParamIDs::outGain);
    p.magnitude = get(ParamIDs::magnitude); p.mix = get(ParamIDs::mix); p.width = get(ParamIDs::width);

    p.noiseOn = on(ParamIDs::noiseOn); p.noiseAmt = get(ParamIDs::noiseAmt); p.noiseType = (int) get(ParamIDs::noiseType);
    p.noiseTone = get(ParamIDs::noiseTone); p.noiseFollow = get(ParamIDs::noiseFollow); p.noiseDuck = get(ParamIDs::noiseDuck);
    p.noisePost = on(ParamIDs::noisePost); p.noiseFlux = get(ParamIDs::noiseFlux);

    p.wobOn = on(ParamIDs::wobOn); p.wobAmt = get(ParamIDs::wobAmt); p.wobBalance = get(ParamIDs::wobBalance);
    p.wowRate = get(ParamIDs::wowRate); p.flutRate = get(ParamIDs::flutRate); p.wobStereo = get(ParamIDs::wobStereo);
    p.wobMix = get(ParamIDs::wobMix); p.wobFlux = get(ParamIDs::wobFlux);

    p.disOn = on(ParamIDs::disOn); p.disAmt = get(ParamIDs::disAmt); p.disType = (int) get(ParamIDs::disType);
    p.disFocus = get(ParamIDs::disFocus); p.disMix = get(ParamIDs::disMix); p.disFlux = get(ParamIDs::disFlux);

    p.digOn = on(ParamIDs::digOn); p.digAmt = get(ParamIDs::digAmt); p.digBalance = get(ParamIDs::digBalance);
    p.digFocus = get(ParamIDs::digFocus); p.digSmooth = get(ParamIDs::digSmooth); p.digMix = get(ParamIDs::digMix);
    p.digFlux = get(ParamIDs::digFlux);

    p.spaceOn = on(ParamIDs::spaceOn); p.spaceAmt = get(ParamIDs::spaceAmt); p.spaceDecay = get(ParamIDs::spaceDecay);
    p.spacePre = get(ParamIDs::spacePre); p.spaceFocus = get(ParamIDs::spaceFocus); p.spaceStereo = get(ParamIDs::spaceStereo);
    p.spaceFlux = get(ParamIDs::spaceFlux);

    p.magOn = on(ParamIDs::magOn); p.magAmt = get(ParamIDs::magAmt); p.magBalance = get(ParamIDs::magBalance);
    p.magRate = get(ParamIDs::magRate); p.magDropouts = get(ParamIDs::magDropouts); p.magStereo = get(ParamIDs::magStereo);
    p.magFlux = get(ParamIDs::magFlux);

    p.eqOn = on(ParamIDs::eqOn); p.eqLow = get(ParamIDs::eqLow); p.eqHigh = get(ParamIDs::eqHigh); p.eqTone = get(ParamIDs::eqTone);
    return p;
}

void DecimulScourProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRateHz = sampleRate;
    engine.prepare(sampleRate);
    engine.setParams(readParams());

    smBypass.reset(sampleRate, 0.02);
    smBypass.setCurrentAndTargetValue(apvts.getRawParameterValue(ParamIDs::bypass)->load() > 0.5f ? 1.0f : 0.0f);

    frameFifo.reset();
}

void DecimulScourProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (n <= 0 || numChannels < 1)
        return;

    float* L = buffer.getWritePointer(0);
    float* R = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    engine.setParams(readParams());
    smBypass.setTargetValue(apvts.getRawParameterValue(ParamIDs::bypass)->load() > 0.5f ? 1.0f : 0.0f);

    float inPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        inPeak = std::max(inPeak, buffer.getMagnitude(ch, 0, n));

    // un cuadro de pantalla por cada tramo de 128 muestras
    float dryL[frameChunk], dryR[frameChunk], monoR[frameChunk];
    for (int start = 0; start < n; start += frameChunk)
    {
        const int len = std::min(frameChunk, n - start);
        std::copy(L + start, L + start + len, dryL);
        if (R != nullptr)
            std::copy(R + start, R + start + len, dryR);

        if (R != nullptr)
            engine.process(L + start, R + start, len);
        else
        {
            // mono: se procesa como estéreo y se suman los dos lados
            std::copy(L + start, L + start + len, monoR);
            engine.process(L + start, monoR, len);
            for (int i = 0; i < len; ++i)
                L[start + i] = 0.5f * (L[start + i] + monoR[i]);
        }
        const auto viz = engine.takeViz();

        float lo = 0.0f, hi = 0.0f;
        for (int i = 0; i < len; ++i)
        {
            const float byp = smBypass.getNextValue();
            L[start + i] = L[start + i] * (1.0f - byp) + dryL[i] * byp;
            if (R != nullptr)
                R[start + i] = R[start + i] * (1.0f - byp) + dryR[i] * byp;
            lo = std::min(lo, L[start + i]);
            hi = std::max(hi, L[start + i]);
        }

        int s1 = 0, z1 = 0, s2 = 0, z2 = 0;
        frameFifo.prepareToWrite(1, s1, z1, s2, z2);
        if (z1 > 0)
        {
            frameBuffer[(size_t) s1] = { lo, hi, viz.noiseMin, viz.noiseMax, viz.wobDelayMs, viz.magGain };
            frameFifo.finishedWrite(1);
        }
    }

    float outPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        outPeak = std::max(outPeak, buffer.getMagnitude(ch, 0, n));
    inputPeakDb.store(juce::Decibels::gainToDecibels(inPeak, -100.0f));
    outputPeakDb.store(juce::Decibels::gainToDecibels(outPeak, -100.0f));
}

int DecimulScourProcessor::readFrames(Frame* dest, int maxFrames)
{
    int s1 = 0, z1 = 0, s2 = 0, z2 = 0;
    frameFifo.prepareToRead(std::min(maxFrames, frameFifo.getNumReady()), s1, z1, s2, z2);
    int copied = 0;
    for (int i = 0; i < z1; ++i) dest[copied++] = frameBuffer[(size_t) (s1 + i)];
    for (int i = 0; i < z2; ++i) dest[copied++] = frameBuffer[(size_t) (s2 + i)];
    frameFifo.finishedRead(z1 + z2);
    return copied;
}

juce::AudioProcessorEditor* DecimulScourProcessor::createEditor()
{
    return new DecimulScourEditor(*this);
}

void DecimulScourProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (!state.isValid())
        return;
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void DecimulScourProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DecimulScourProcessor();
}
