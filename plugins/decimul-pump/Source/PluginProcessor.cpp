#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
    juce::ParameterID pid(const juce::String& id) { return { id, 1 }; }

    struct RateDef { const char* name; double beats; };

    // Duración del ciclo en negras (beats). Las "T" son tresillos; el punto es puntillo.
    const std::array<RateDef, 13>& rates()
    {
        static const std::array<RateDef, 13> defs = {{
            { "2 bars", 8.0 },    { "1 bar", 4.0 },     { "1/2", 2.0 },       { "1/2T", 4.0 / 3.0 },
            { "1/4", 1.0 },       { "1/4.", 1.5 },      { "1/4T", 2.0 / 3.0 }, { "1/8", 0.5 },
            { "1/8.", 0.75 },     { "1/8T", 1.0 / 3.0 }, { "1/16", 0.25 },     { "1/16T", 1.0 / 6.0 },
            { "1/32", 0.125 },
        }};
        return defs;
    }

    constexpr int defaultRateIndex = 4;   // 1/4
}

int DecimulPumpProcessor::numRates() { return (int) rates().size(); }
juce::String DecimulPumpProcessor::rateName(int index) { return rates()[(size_t) std::clamp(index, 0, numRates() - 1)].name; }
double DecimulPumpProcessor::rateBeats(int index) { return rates()[(size_t) std::clamp(index, 0, numRates() - 1)].beats; }

DecimulPumpProcessor::DecimulPumpProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    curve.fillTable(sharedTable.data());
    table = sharedTable;
    tableVersion = sharedVersion;
}

juce::AudioProcessorValueTreeState::ParameterLayout DecimulPumpProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    StringArray rateNames;
    for (int i = 0; i < numRates(); ++i)
        rateNames.add(rateName(i));

    params.push_back(std::make_unique<AudioParameterChoice>(pid(ParamIDs::rate), "Rate", rateNames, defaultRateIndex));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::depth), "Depth", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel("%")));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::mix), "Mix", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel("%")));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::smooth), "Smooth", NormalisableRange<float>(0.0f, 50.0f, 0.1f, 0.5f), 3.0f,
        AudioParameterFloatAttributes().withLabel("ms")));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::nudge), "Nudge", NormalisableRange<float>(-50.0f, 50.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel("ms")));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::spread), "Stereo Spread", NormalisableRange<float>(-50.0f, 50.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel("%")));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::trigger), "Trigger", StringArray { "Sync", "MIDI", "Audio" }, 0));
    params.push_back(std::make_unique<AudioParameterInt>(
        pid(ParamIDs::midiNote), "MIDI Note", -1, 127, -1,
        AudioParameterIntAttributes().withStringFromValueFunction(
            [](int v, int) { return v < 0 ? String("Any") : MidiMessage::getMidiNoteName(v, true, true, 4); })));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::threshold), "Audio Threshold", NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -24.0f,
        AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::band), "Band", StringArray { "Full", "Low", "High" }, 0));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::crossover), "Crossover", NormalisableRange<float>(40.0f, 400.0f, 1.0f, 0.5f), 150.0f,
        AudioParameterFloatAttributes().withLabel("Hz")));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::bpm), "Tempo (no host)", NormalisableRange<float>(40.0f, 240.0f, 0.1f), 128.0f,
        AudioParameterFloatAttributes().withLabel("BPM")));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::outputTrim), "Output", NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::bypass), "Bypass", false));

    return { params.begin(), params.end() };
}

bool DecimulPumpProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
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

void DecimulPumpProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels = 2;

    crossoverFilter.prepare(spec);
    crossoverFilter.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    crossoverFilter.setCutoffFrequency(150.0f);

    outputGain.prepare(spec);
    outputGain.setRampDurationSeconds(0.02);
    // juce::dsp::Gain arranca en 0: sin esto la salida sube durante los primeros 20 ms.
    outputGain.setGainLinear(1.0f);
    outputGain.reset();

    lastCutoff = -1.0f;
    gainL = gainR = 1.0f;
    oneShotPhase = 1.0;
    pendingDelaySamples = -1;
    detLp1 = detLp2 = detEnv = 0.0f;
    detArmed = true;
    holdoffSamples = 0;
}

void DecimulPumpProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0)
        return;

    auto main = getBusBuffer(buffer, true, 0);
    if (main.getNumChannels() < 2)
        return;

    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };

    float* outL = main.getWritePointer(0);
    float* outR = main.getWritePointer(1);

    // ---- Curva: copia local si el message thread la cambió ----
    {
        const juce::SpinLock::ScopedTryLockType lock(curveLock);
        if (lock.isLocked() && tableVersion != sharedVersion)
        {
            table = sharedTable;
            tableVersion = sharedVersion;
        }
    }

    // ---- Tiempo ----
    double bpm = (double) get(ParamIDs::bpm);
    bool playing = false;
    bool havePpq = false;
    double hostPpq = 0.0;

    if (auto* ph = getPlayHead())
    {
        if (auto position = ph->getPosition())
        {
            if (auto b = position->getBpm(); b.hasValue() && *b > 1.0)
                bpm = *b;
            playing = position->getIsPlaying();
            if (auto p = position->getPpqPosition(); p.hasValue())
            {
                havePpq = true;
                hostPpq = *p;
            }
        }
    }
    hostSynced.store(playing && havePpq);

    const double beatsPerSample = bpm / 60.0 / currentSampleRate;
    const int rateIndex = (int) get(ParamIDs::rate);
    const double cycleBeats = rateBeats(rateIndex);
    const double nudgeMs = (double) get(ParamIDs::nudge);
    const double nudgeBeats = nudgeMs / 1000.0 * bpm / 60.0;
    const float depth = get(ParamIDs::depth) * 0.01f;
    const float mix = get(ParamIDs::mix) * 0.01f;
    const float spread = get(ParamIDs::spread) * 0.01f;
    const int trig = (int) get(ParamIDs::trigger);
    const int noteFilter = (int) std::lround(get(ParamIDs::midiNote));
    const int bandMode = (int) get(ParamIDs::band);
    const bool oneShot = trig != triggerSync;

    if (get(ParamIDs::bypass) > 0.5f)
    {
        uiGain.store(1.0f);
        outputPeakDb.store(juce::Decibels::gainToDecibels(main.getMagnitude(0, n), -100.0f));
        return;
    }

    const float smoothMs = get(ParamIDs::smooth);
    const float smoothCoeff = smoothMs < 0.05f
                                  ? 1.0f
                                  : 1.0f - std::exp(-1.0f / (smoothMs * 0.001f * (float) currentSampleRate));

    // Cursor de beats del modo sync: el del host si está reproduciendo; si no, un reloj
    // libre que sigue el tempo (así se escucha el efecto con el transporte parado).
    double cursor = (playing && havePpq) ? hostPpq : freePpq;

    // ---- Eventos MIDI de este bloque (solo las notas que disparan) ----
    constexpr int maxEvents = 64;
    int eventSamples[maxEvents];
    int numEvents = 0;
    if (trig == triggerMidi)
    {
        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();
            if (msg.isNoteOn() && (noteFilter < 0 || msg.getNoteNumber() == noteFilter) && numEvents < maxEvents)
                eventSamples[numEvents++] = metadata.samplePosition;
        }
    }
    int nextEvent = 0;

    // ---- Señal de detección (trigger por audio): sidechain si está activo, si no la entrada ----
    const float* detL = outL;
    const float* detR = outR;
    if (trig == triggerAudio)
    {
        if (auto* scBus = getBus(true, 1); scBus != nullptr && scBus->isEnabled())
        {
            auto sc = getBusBuffer(buffer, true, 1);
            if (sc.getNumChannels() >= 1)
            {
                detL = sc.getReadPointer(0);
                detR = sc.getNumChannels() > 1 ? sc.getReadPointer(1) : detL;
            }
        }
    }

    const float lpCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 200.0f / (float) currentSampleRate);
    const float atkCoeff = 1.0f - std::exp(-1.0f / (0.0005f * (float) currentSampleRate));
    const float relCoeff = 1.0f - std::exp(-1.0f / (0.05f * (float) currentSampleRate));
    const float thresholdLin = juce::Decibels::decibelsToGain(get(ParamIDs::threshold));
    const int holdoff = (int) (0.06 * currentSampleRate);
    const int nudgeSamples = (int) (std::max(0.0, nudgeMs) * 0.001 * currentSampleRate);

    auto fireTrigger = [&]
    {
        triggerCount.fetch_add(1);
        pendingDelaySamples = nudgeSamples;     // la curva arranca después del nudge
    };

    // ---- Crossover (modos Low / High) ----
    if (bandMode != bandFull)
    {
        const float cutoff = get(ParamIDs::crossover);
        if (cutoff != lastCutoff)
        {
            crossoverFilter.setCutoffFrequency(cutoff);
            lastCutoff = cutoff;
        }
    }

    auto lookup = [this](float phase)
    {
        const float pos = std::clamp(phase, 0.0f, 1.0f) * (float) PumpCurve::tableSize;
        const int i0 = std::min((int) pos, PumpCurve::tableSize - 1);
        const float f = pos - (float) i0;
        return table[(size_t) i0] + f * (table[(size_t) i0 + 1] - table[(size_t) i0]);
    };

    float lastPhase = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        // --- Disparos ---
        while (nextEvent < numEvents && eventSamples[nextEvent] <= i)
        {
            fireTrigger();
            ++nextEvent;
        }

        if (trig == triggerAudio)
        {
            const float det = 0.5f * (detL[i] + detR[i]);
            detLp1 += lpCoeff * (det - detLp1);
            detLp2 += lpCoeff * (detLp1 - detLp2);
            const float level = std::abs(detLp2);
            detEnv += (level > detEnv ? atkCoeff : relCoeff) * (level - detEnv);

            if (detArmed && holdoffSamples <= 0 && detEnv > thresholdLin)
            {
                fireTrigger();
                detArmed = false;
                holdoffSamples = holdoff;
            }
            if (!detArmed && detEnv < thresholdLin * 0.5f)
                detArmed = true;
            if (holdoffSamples > 0)
                --holdoffSamples;
        }

        // --- Fase dentro del ciclo ---
        float phaseL, phaseR;
        if (!oneShot)
        {
            const double x = (cursor - nudgeBeats) / cycleBeats;
            phaseL = (float) (x - std::floor(x));
            const float r = phaseL + spread;
            phaseR = r - std::floor(r);
            cursor += beatsPerSample;
        }
        else
        {
            if (pendingDelaySamples == 0)
            {
                oneShotPhase = 0.0;
                pendingDelaySamples = -1;
            }
            else if (pendingDelaySamples > 0)
            {
                --pendingDelaySamples;
            }

            phaseL = phaseR = (float) oneShotPhase;
            if (oneShotPhase < 1.0)
                oneShotPhase = std::min(1.0, oneShotPhase + beatsPerSample / cycleBeats);
        }
        lastPhase = phaseL;

        // --- Ganancia ---
        const float targetL = 1.0f - depth * (1.0f - lookup(phaseL));
        const float targetR = 1.0f - depth * (1.0f - lookup(phaseR));
        gainL += smoothCoeff * (targetL - gainL);
        gainR += smoothCoeff * (targetR - gainR);

        const float dryL = outL[i], dryR = outR[i];
        float wetL, wetR;

        if (bandMode == bandFull)
        {
            wetL = dryL * gainL;
            wetR = dryR * gainR;
        }
        else
        {
            float lowL, highL, lowR, highR;
            crossoverFilter.processSample(0, dryL, lowL, highL);
            crossoverFilter.processSample(1, dryR, lowR, highR);

            if (bandMode == bandLow)
            {
                wetL = lowL * gainL + highL;
                wetR = lowR * gainR + highR;
            }
            else
            {
                wetL = lowL + highL * gainL;
                wetR = lowR + highR * gainR;
            }
        }

        outL[i] = dryL + mix * (wetL - dryL);
        outR[i] = dryR + mix * (wetR - dryR);
    }

    if (!(playing && havePpq))
        freePpq += (double) n * beatsPerSample;
    else
        freePpq = cursor;

    // ---- Salida ----
    {
        juce::dsp::AudioBlock<float> block(main);
        juce::dsp::ProcessContextReplacing<float> context(block);
        outputGain.setGainDecibels(get(ParamIDs::outputTrim));
        outputGain.process(context);
    }

    uiPhase.store(lastPhase);
    uiGain.store(gainL);
    outputPeakDb.store(juce::Decibels::gainToDecibels(main.getMagnitude(0, n), -100.0f));
}

// =============================== Curva ===============================

PumpCurve DecimulPumpProcessor::getCurve() const
{
    const juce::SpinLock::ScopedLockType lock(curveLock);
    return curve;
}

void DecimulPumpProcessor::setCurve(const PumpCurve& newCurve, int preset)
{
    PumpCurve clean = newCurve;
    clean.sanitize();

    {
        const juce::SpinLock::ScopedLockType lock(curveLock);
        curve = clean;
        curve.fillTable(sharedTable.data());
        ++sharedVersion;
    }
    presetIndex.store(preset);
}

juce::AudioProcessorEditor* DecimulPumpProcessor::createEditor()
{
    return new DecimulPumpEditor(*this);
}

// =============================== Estado ===============================

void DecimulPumpProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (!state.isValid())
        return;

    if (auto xml = state.createXml())
    {
        xml->addChildElement(getCurve().toXml().release());
        xml->setAttribute("presetIndex", presetIndex.load());
        copyXmlToBinary(*xml, destData);
    }
}

void DecimulPumpProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;

    if (auto* curveXml = xml->getChildByName("CURVE"))
    {
        setCurve(PumpCurve::fromXml(*curveXml), xml->getIntAttribute("presetIndex", -1));
        xml->removeChildElement(curveXml, true);
    }

    apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DecimulPumpProcessor();
}
