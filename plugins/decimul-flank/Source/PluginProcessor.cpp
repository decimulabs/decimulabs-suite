#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
    juce::ParameterID pid(const juce::String& id) { return { id, 1 }; }

    constexpr float rampSeconds = 0.05f;
}

DecimulFlankProcessor::DecimulFlankProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DecimulFlankProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    auto pct = AudioParameterFloatAttributes().withLabel("%");
    auto ms = AudioParameterFloatAttributes().withLabel("ms");

    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::detune), "Detune", NormalisableRange<float>(0.0f, 50.0f, 0.1f, 0.7f), 10.0f,
        AudioParameterFloatAttributes().withLabel("cents")));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::delay), "Delay", NormalisableRange<float>(3.0f, 40.0f, 0.1f, 0.6f), 14.0f, ms));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::offset), "Offset", NormalisableRange<float>(0.0f, 25.0f, 0.1f, 0.7f), 4.0f, ms));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::spread), "Spread", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 55.0f, pct));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::mix), "Mix", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f, pct));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::lowCut), "Low Cut", NormalisableRange<float>(20.0f, 600.0f, 1.0f, 0.5f), 150.0f,
        AudioParameterFloatAttributes().withLabel("Hz")));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::highCut), "High Cut", NormalisableRange<float>(2000.0f, 20000.0f, 1.0f, 0.4f), 10000.0f,
        AudioParameterFloatAttributes().withLabel("Hz")));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::space), "Space", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 20.0f, pct));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::mode), "Mode", StringArray { "Static", "Drift" }, 1));
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::monoCheck), "Mono Check", false));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::outputTrim), "Output", NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::bypass), "Bypass", false));

    return { params.begin(), params.end() };
}

bool DecimulFlankProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Salida siempre estéreo; la entrada puede ser una pista mono (la voz) o estéreo.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void DecimulFlankProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRateHz = sampleRate;

    shifterL.prepare(sampleRate);
    shifterR.prepare(sampleRate);
    // Dos LFO lentos por lado, de frecuencias inconmensurables para que no se repita enseguida.
    driftL.prepare(sampleRate, 0.83f, 1.70f, 0.0f, 1.3f);
    driftR.prepare(sampleRate, 1.07f, 2.30f, 2.1f, 0.4f);
    early.prepare(sampleRate);

    lowCutFilter.reset();
    highCutL.reset();
    highCutR.reset();
    lastLowCut = lastHighCut = -1.0f;

    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };
    const float srf = (float) sampleRate;

    auto prep = [&](auto& s, float value, float seconds = rampSeconds)
    {
        s.reset(sampleRate, (double) seconds);
        s.setCurrentAndTargetValue(value);
    };
    prep(smDetune, get(ParamIDs::detune));
    prep(smDelay, get(ParamIDs::delay) * 0.001f * srf);
    prep(smOffset, get(ParamIDs::offset) * 0.001f * srf);
    prep(smSpread, get(ParamIDs::spread) * 0.01f);
    prep(smMix, get(ParamIDs::mix) * 0.01f);
    prep(smSpace, get(ParamIDs::space) * 0.01f);
    prep(smOutput, juce::Decibels::decibelsToGain(get(ParamIDs::outputTrim)));
    prep(smMono, get(ParamIDs::monoCheck) > 0.5f ? 1.0f : 0.0f, 0.02f);
    prep(smBypass, get(ParamIDs::bypass) > 0.5f ? 1.0f : 0.0f, 0.02f);
    prep(smDrift, get(ParamIDs::mode) > 0.5f ? 1.0f : 0.0f, 0.03f);

    sumLR = sumLL = sumRR = 0.0;
    scopeFifo.reset();
    scopeDecimation = 0;
}

void DecimulFlankProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (n <= 0 || numChannels < 1)
        return;

    float* L = buffer.getWritePointer(0);
    float* R = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    // Entrada mono: la voz va a los dos lados por igual.
    if (R != nullptr && getMainBusNumInputChannels() == 1)
        juce::FloatVectorOperations::copy(R, L, n);

    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };
    const float srf = (float) sampleRateHz;

    // ---- Objetivos de los suavizadores ----
    smDetune.setTargetValue(get(ParamIDs::detune));
    smDelay.setTargetValue(get(ParamIDs::delay) * 0.001f * srf);
    smOffset.setTargetValue(get(ParamIDs::offset) * 0.001f * srf);
    smSpread.setTargetValue(get(ParamIDs::spread) * 0.01f);
    smMix.setTargetValue(get(ParamIDs::mix) * 0.01f);
    smSpace.setTargetValue(get(ParamIDs::space) * 0.01f);
    smOutput.setTargetValue(juce::Decibels::decibelsToGain(get(ParamIDs::outputTrim)));
    smMono.setTargetValue(get(ParamIDs::monoCheck) > 0.5f ? 1.0f : 0.0f);
    smBypass.setTargetValue(get(ParamIDs::bypass) > 0.5f ? 1.0f : 0.0f);
    smDrift.setTargetValue(get(ParamIDs::mode) > 0.5f ? 1.0f : 0.0f);

    const float lowCut = get(ParamIDs::lowCut);
    const float highCut = get(ParamIDs::highCut);
    if (lowCut != lastLowCut)
    {
        lowCutFilter.setHighpass(sampleRateHz, lowCut);
        lastLowCut = lowCut;
    }
    if (highCut != lastHighCut)
    {
        highCutL.setLowpass(sampleRateHz, highCut);
        highCutR.setLowpass(sampleRateHz, highCut);
        lastHighCut = highCut;
    }

    const float maxWindow = 0.030f * srf;
    const double corrCoeff = 1.0 - std::exp(-1.0 / (0.3 * sampleRateHz));

    float inPeak = 0.0f, outPeak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float dryL = L[i];
        const float dryR = R != nullptr ? R[i] : dryL;
        inPeak = std::max(inPeak, std::max(std::abs(dryL), std::abs(dryR)));

        const float detuneCents = smDetune.getNextValue();
        const float centerL = std::max(smDelay.getNextValue(), 4.0f);
        const float centerR = centerL + smOffset.getNextValue();
        const float spread = smSpread.getNextValue();
        const float mix = smMix.getNextValue();
        const float space = smSpace.getNextValue();
        const float driftAmount = smDrift.getNextValue();
        const float monoAmount = smMono.getNextValue();
        const float bypassAmount = smBypass.getNextValue();
        const float outGain = smOutput.getNextValue();

        // Las copias salen de la voz (suma a mono) sin graves.
        const float source = lowCutFilter.process(0.5f * (dryL + dryR));

        // Detune estático (pitch shifter de línea de retardo) y detune móvil (retardo
        // modulado): las dos corren siempre y el modo es una mezcla entre ambas.
        const float ratioDown = flank::centsToRatio(-detuneCents);
        const float ratioUp = flank::centsToRatio(detuneCents);
        const float windowL = std::clamp(2.0f * (centerL - 3.0f), 2.0f, maxWindow);
        const float windowR = std::clamp(2.0f * (centerR - 3.0f), 2.0f, maxWindow);
        const float deviation = ratioUp - 1.0f;

        const float staticL = shifterL.process(source, ratioDown, centerL, windowL);
        const float staticR = shifterR.process(source, ratioUp, centerR, windowR);
        const float driftOutL = driftL.process(source, centerL, deviation);
        const float driftOutR = driftR.process(source, centerR, deviation);

        float copyL = staticL + driftAmount * (driftOutL - staticL);
        float copyR = staticR + driftAmount * (driftOutR - staticR);
        copyL = highCutL.process(copyL);
        copyR = highCutR.process(copyR);

        // Las copias se corren hacia los costados: spread 0 = las dos al centro,
        // spread 1 = una a cada lado (ley de pan de potencia constante).
        const float theta = (1.0f - spread) * (flank::kPi * 0.25f);
        const float cs = std::cos(theta), sn = std::sin(theta);
        const float wetL = copyL * cs + copyR * sn;
        const float wetR = copyL * sn + copyR * cs;

        // Mix de potencia constante: a cada canal le llega la seca y una copia, así que al
        // subir Mix la seca baja lo que suben las copias y el volumen casi no cambia.
        const float mixAngle = mix * (flank::kPi * 0.25f);
        const float dryGain = std::cos(mixAngle);
        const float wetGain = std::sin(mixAngle);

        float ambL = 0.0f, ambR = 0.0f;
        early.process(source, ambL, ambR);
        const float spaceGain = space * 0.6f;

        float outL = dryL * dryGain + wetL * wetGain + ambL * spaceGain;
        float outR = dryR * dryGain + wetR * wetGain + ambR * spaceGain;

        // Escuchar en mono para ver si la voz se cae
        const float monoSum = 0.5f * (outL + outR);
        outL += monoAmount * (monoSum - outL);
        outR += monoAmount * (monoSum - outR);

        outL *= outGain;
        outR *= outGain;

        // Bypass con un fundido corto
        outL += bypassAmount * (dryL - outL);
        outR += bypassAmount * (dryR - outR);

        L[i] = outL;
        if (R != nullptr)
            R[i] = outR;

        outPeak = std::max(outPeak, std::max(std::abs(outL), std::abs(outR)));

        // Correlación entre canales
        sumLR += corrCoeff * ((double) outL * (double) outR - sumLR);
        sumLL += corrCoeff * ((double) outL * (double) outL - sumLL);
        sumRR += corrCoeff * ((double) outR * (double) outR - sumRR);

        // Goniómetro: un par cada 4 muestras
        if ((++scopeDecimation & 3) == 0)
        {
            int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
            scopeFifo.prepareToWrite(1, start1, size1, start2, size2);
            if (size1 > 0)
            {
                scopeBuffer[(size_t) start1 * 2] = outL;
                scopeBuffer[(size_t) start1 * 2 + 1] = outR;
                scopeFifo.finishedWrite(1);
            }
        }
    }

    inputPeakDb.store(juce::Decibels::gainToDecibels(inPeak, -100.0f));
    outputPeakDb.store(juce::Decibels::gainToDecibels(outPeak, -100.0f));

    const double power = sumLL + sumRR;
    if (power > 1.0e-12)
    {
        correlation.store((float) std::clamp(sumLR / std::sqrt(std::max(sumLL * sumRR, 1.0e-24)), -1.0, 1.0));
        const double monoPower = std::max((sumLL + sumRR + 2.0 * sumLR) / (2.0 * power), 1.0e-6);
        monoLossDb.store((float) (10.0 * std::log10(monoPower)));
    }
    else
    {
        correlation.store(1.0f);
        monoLossDb.store(0.0f);
    }
}

int DecimulFlankProcessor::readScope(float* destInterleaved, int maxPairs)
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    scopeFifo.prepareToRead(std::min(maxPairs, scopeFifo.getNumReady()), start1, size1, start2, size2);

    int copied = 0;
    for (int i = 0; i < size1; ++i, ++copied)
    {
        destInterleaved[copied * 2] = scopeBuffer[(size_t) (start1 + i) * 2];
        destInterleaved[copied * 2 + 1] = scopeBuffer[(size_t) (start1 + i) * 2 + 1];
    }
    for (int i = 0; i < size2; ++i, ++copied)
    {
        destInterleaved[copied * 2] = scopeBuffer[(size_t) (start2 + i) * 2];
        destInterleaved[copied * 2 + 1] = scopeBuffer[(size_t) (start2 + i) * 2 + 1];
    }

    scopeFifo.finishedRead(size1 + size2);
    return copied;
}

juce::AudioProcessorEditor* DecimulFlankProcessor::createEditor()
{
    return new DecimulFlankEditor(*this);
}

void DecimulFlankProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (!state.isValid())
        return;

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void DecimulFlankProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DecimulFlankProcessor();
}
