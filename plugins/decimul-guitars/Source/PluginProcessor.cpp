#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    juce::ParameterID pid(const juce::String& id) { return { id, 1 }; }
}

DecimulGuitarsProcessor::DecimulGuitarsProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

DecimulGuitarsProcessor::~DecimulGuitarsProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout DecimulGuitarsProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    auto dbRange = [](float lo, float hi) { return NormalisableRange<float>(lo, hi, 0.1f); };
    auto pctRange = NormalisableRange<float>(0.0f, 100.0f, 0.1f);
    auto dbAttr = AudioParameterFloatAttributes().withLabel("dB");
    auto pctAttr = AudioParameterFloatAttributes().withLabel("%");

    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::inputTrim), "Input", dbRange(-24.0f, 24.0f), 0.0f, dbAttr));

    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::gateAmount), "Gate", pctRange, 0.0f, pctAttr));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::gateMode), "Gate Character", StringArray { "Fast", "Med", "Slow" }, 0));

    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::driveAmount), "Drive", pctRange, 0.0f, pctAttr));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::driveMode), "Drive Character", StringArray { "Tube", "Fuzz", "Boost" }, 0));

    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::toneTilt), "Tone", dbRange(-12.0f, 12.0f), 0.0f, dbAttr));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::toneMode), "Tone Character", StringArray { "Scoop", "Flat", "Bright" }, 1));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::toneMidFreq), "Mid Sweep", NormalisableRange<float>(300.0f, 3000.0f, 1.0f, 0.4f), 800.0f,
        AudioParameterFloatAttributes().withLabel("Hz")));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::toneEnabled), "Tone Enabled", true));

    // Cab es on/off (no blend): o está 100% aplicado o no hace nada — mezclar
    // "un poco" de color de gabinete no tiene sentido sonoro (feedback de testing).
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::cabMode), "Cab Character", StringArray { "Combo", "Stack", "DI" }, 0));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::cabEnabled), "Cab Enabled", false));

    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::modAmount), "Modulation", pctRange, 0.0f, pctAttr));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::modMode), "Modulation Character", StringArray { "Chorus", "Flanger", "Phaser" }, 0));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::modRate), "Mod Rate", NormalisableRange<float>(0.1f, 5.0f, 0.01f, 0.5f), 0.8f,
        AudioParameterFloatAttributes().withLabel("Hz")));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::modEnabled), "Mod Enabled", true));

    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::spaceAmount), "Space", pctRange, 0.0f, pctAttr));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::spaceMode), "Space Character", StringArray { "Room", "Plate", "Hall" }, 0));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::spacePreDelay), "Space Pre-Delay", dbRange(0.0f, 500.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel("ms")));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::spaceEnabled), "Space Enabled", true));

    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::echoAmount), "Echo", pctRange, 0.0f, pctAttr));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::echoDivision), "Echo Division", StringArray { "Slap", "8th", "Quarter" }, 0));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::echoFeedback), "Echo Feedback", pctRange, 35.0f, pctAttr));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::echoEnabled), "Echo Enabled", true));

    // Blend solo mezcla dry (post-core) contra los retornos de Space+Mod+Echo — Gate/
    // Drive/Tone/Cab son "core" y quedan siempre 100% activos, Blend no los diluye
    // (decisión tomada tras el primer testing real, ver comentario al inicio del .h).
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::blend), "Blend", pctRange, 100.0f, pctAttr));
    // Piso bajado de -24 a -60dB: a -24 todavía se escuchaba señal con el fader al
    // mínimo (feedback de testing) — -60dB es, a fines prácticos, silencio.
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::outputTrim), "Output", dbRange(-60.0f, 24.0f), 0.0f, dbAttr));

    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::bypass), "Bypass", false));

    return { params.begin(), params.end() };
}

void DecimulGuitarsProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels = (juce::uint32) juce::jmax(1, getTotalNumOutputChannels());

    inputGain.prepare(spec);
    outputGain.prepare(spec);
    inputGain.setRampDurationSeconds(0.02);
    outputGain.setRampDurationSeconds(0.02);

    for (int ch = 0; ch < gateMaxChannels; ++ch)
    {
        gateEnvelope[ch] = 0.0f;
        gateGain[ch] = 0.0f;
        gateHoldCounter[ch] = 0;
        gateIsOpen[ch] = false;
    }
    waveShaper.prepare(spec);

    toneLowShelf.prepare(spec);
    toneHighShelf.prepare(spec);
    toneMidPeak.prepare(spec);
    cabLowCut.prepare(spec);
    cabHighCut.prepare(spec);
    cabPresence.prepare(spec);

    chorusFlanger.prepare(spec);
    phaser.prepare(spec);
    space.prepare(spec);
    space.reset();

    spacePreDelayLine.setMaximumDelayInSamples((int) (0.6 * sampleRate));
    spacePreDelayLine.prepare(spec);
    spacePreDelayLine.reset();

    delayLine.setMaximumDelayInSamples((int) (2.0 * sampleRate));
    delayLine.prepare(spec);
    delayLine.reset();

    dryBuffer.setSize((int) spec.numChannels, samplesPerBlock);
    scratchBuffer.setSize((int) spec.numChannels, samplesPerBlock);
    returnScratch.setSize((int) spec.numChannels, samplesPerBlock);
}

void DecimulGuitarsProcessor::releaseResources()
{
}

bool DecimulGuitarsProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn = layouts.getMainInputChannelSet();

    if (mainOut != juce::AudioChannelSet::stereo())
        return false;

    // Acepta mono (DI/mic de amp) o stereo de entrada.
    return mainIn == juce::AudioChannelSet::stereo() || mainIn == juce::AudioChannelSet::mono();
}

double DecimulGuitarsProcessor::getCurrentBpm() const
{
    if (auto* ph = getPlayHead())
    {
        if (auto position = ph->getPosition(); position.hasValue())
        {
            if (auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 1.0)
                return *bpm;
        }
    }
    return (double) manualBpm.load();
}

float DecimulGuitarsProcessor::computeEchoDelayMs(int divisionIndex, double bpm) const
{
    switch (divisionIndex)
    {
        case 0: return 120.0f;
        case 1: return (float) (60000.0 / bpm / 2.0);
        case 2: return (float) (60000.0 / bpm);
        default: return 120.0f;
    }
}

void DecimulGuitarsProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    const bool bypassed = apvts.getRawParameterValue(ParamIDs::bypass)->load() > 0.5f;

    if (getMainBusNumInputChannels() == 1 && numChannels > 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);

    if (bypassed)
    {
        inputPeakDb.store(juce::Decibels::gainToDecibels(buffer.getMagnitude(0, numSamples)));
        outputPeakDb.store(inputPeakDb.load());
        return;
    }

    if (dryBuffer.getNumSamples() < numSamples || dryBuffer.getNumChannels() != numChannels)
    {
        dryBuffer.setSize(numChannels, numSamples, false, false, true);
        scratchBuffer.setSize(numChannels, numSamples, false, false, true);
        returnScratch.setSize(numChannels, numSamples, false, false, true);
    }

    // --- Input Trim ---
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        inputGain.setGainDecibels(apvts.getRawParameterValue(ParamIDs::inputTrim)->load());
        inputGain.process(context);
    }
    inputPeakDb.store(juce::Decibels::gainToDecibels(buffer.getMagnitude(0, numSamples)));

    // --- Gate: hysteresis + hold propios (ver nota en el .h) ---
    {
        const float amount = apvts.getRawParameterValue(ParamIDs::gateAmount)->load() / 100.0f;
        const int mode = (int) apvts.getRawParameterValue(ParamIDs::gateMode)->load();

        const float openThresholdDb = juce::jmap(amount, 0.0f, 1.0f, -80.0f, -20.0f);
        const float closeThresholdDb = openThresholdDb - 6.0f; // histéresis: evita chattering

        float attackMs = 3.0f, releaseMs = 60.0f, holdMs = 15.0f;
        if (mode == 1) { attackMs = 6.0f;  releaseMs = 150.0f; holdMs = 30.0f; }
        if (mode == 2) { attackMs = 12.0f; releaseMs = 300.0f; holdMs = 60.0f; }

        const float envAttackCoeff = 1.0f - std::exp(-1.0f / (0.2f * (float) currentSampleRate * 0.001f));
        const float envReleaseCoeff = 1.0f - std::exp(-1.0f / (10.0f * (float) currentSampleRate * 0.001f));
        const float gainAttackCoeff = 1.0f - std::exp(-1.0f / (attackMs * (float) currentSampleRate * 0.001f));
        const float gainReleaseCoeff = 1.0f - std::exp(-1.0f / (releaseMs * (float) currentSampleRate * 0.001f));
        const int holdSamples = (int) (holdMs * 0.001 * currentSampleRate);

        const float preMag = buffer.getMagnitude(0, numSamples);

        for (int ch = 0; ch < numChannels && ch < gateMaxChannels; ++ch)
        {
            auto* data = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                const float level = std::abs(data[i]);
                const float envCoeff = level > gateEnvelope[ch] ? envAttackCoeff : envReleaseCoeff;
                gateEnvelope[ch] += envCoeff * (level - gateEnvelope[ch]);
                const float envelopeDb = juce::Decibels::gainToDecibels(gateEnvelope[ch], -100.0f);

                if (!gateIsOpen[ch] && envelopeDb > openThresholdDb)
                {
                    gateIsOpen[ch] = true;
                    gateHoldCounter[ch] = holdSamples;
                }
                else if (gateIsOpen[ch])
                {
                    if (envelopeDb > closeThresholdDb)
                        gateHoldCounter[ch] = holdSamples;
                    else if (gateHoldCounter[ch] > 0)
                        --gateHoldCounter[ch];
                    else
                        gateIsOpen[ch] = false;
                }

                const float targetGain = gateIsOpen[ch] ? 1.0f : 0.0f;
                const float gainCoeff = targetGain > gateGain[ch] ? gainAttackCoeff : gainReleaseCoeff;
                gateGain[ch] += gainCoeff * (targetGain - gateGain[ch]);

                data[i] *= gateGain[ch];
            }
        }

        const float postMag = buffer.getMagnitude(0, numSamples);
        gainReductionDb.store(juce::jmax(0.0f, juce::Decibels::gainToDecibels(preMag)
                                             - juce::Decibels::gainToDecibels(postMag)));
    }

    // --- Drive ---
    {
        const float amount = apvts.getRawParameterValue(ParamIDs::driveAmount)->load() / 100.0f;
        if (amount > 0.001f)
        {
            const int mode = (int) apvts.getRawParameterValue(ParamIDs::driveMode)->load();
            // Rango subido de +23dB a +40dB tope: con niveles típicos de guitarra
            // (bien por debajo de 0dBFS), +23dB no alcanzaba para que Fuzz fuzzeara
            // de verdad (feedback de testing).
            const float preGainDb = amount * 40.0f;
            const float preGain = juce::Decibels::decibelsToGain(preGainDb);

            if (mode == 0)      waveShaper.functionToUse = [](float x) { return std::tanh(x); };
            else if (mode == 1) waveShaper.functionToUse = [](float x)
                                 { return x >= 0.0f ? 1.0f - std::exp(-x) : -1.0f + std::exp(x); };
            else                waveShaper.functionToUse = [](float x) { return std::tanh(x * 0.35f); };

            buffer.applyGain(preGain);
            juce::dsp::AudioBlock<float> block(buffer);
            juce::dsp::ProcessContextReplacing<float> context(block);
            waveShaper.process(context);
            buffer.applyGain(1.0f / std::sqrt(preGain));
        }
    }

    // --- Tone: tilt EQ + mid-sweep peak fijo ---
    if (apvts.getRawParameterValue(ParamIDs::toneEnabled)->load() > 0.5f)
    {
        const float tiltDb = apvts.getRawParameterValue(ParamIDs::toneTilt)->load();
        const int mode = (int) apvts.getRawParameterValue(ParamIDs::toneMode)->load();
        const float lowFreq = mode == 0 ? 250.0f : (mode == 1 ? 400.0f : 500.0f);
        const float highFreq = mode == 0 ? 3000.0f : (mode == 1 ? 2000.0f : 1500.0f);
        const float midFreq = apvts.getRawParameterValue(ParamIDs::toneMidFreq)->load();

        *toneLowShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(
            currentSampleRate, lowFreq, 0.7071f, juce::Decibels::decibelsToGain(-tiltDb * 0.5f));
        *toneHighShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(
            currentSampleRate, highFreq, 0.7071f, juce::Decibels::decibelsToGain(tiltDb * 0.5f));
        *toneMidPeak.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            currentSampleRate, midFreq, 1.0f, juce::Decibels::decibelsToGain(4.0f));

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        toneLowShelf.process(context);
        toneHighShelf.process(context);
        toneMidPeak.process(context);
    }

    // --- Cab: on/off, 100% wet cuando está prendido (nunca blend parcial — no tiene
    // sentido sonoro mezclar "un poco" de color de gabinete, feedback de testing).
    // Doble pasada por cada filtro (4º orden, ~24dB/oct) para una caída más parecida
    // a un speaker real que la pendiente suave (2º orden) de la primera versión.
    // Aproximación con EQ fija, sigue sin ser un cab-sim con impulse response. ---
    if (apvts.getRawParameterValue(ParamIDs::cabEnabled)->load() > 0.5f)
    {
        const int mode = (int) apvts.getRawParameterValue(ParamIDs::cabMode)->load();

        if (mode != 2) // DI = sin coloración de cab
        {
            const float lowCutFreq = mode == 0 ? 120.0f : 90.0f;
            const float highCutFreq = mode == 0 ? 6500.0f : 5500.0f;
            const float presenceFreq = mode == 0 ? 3000.0f : 2500.0f;
            const float presenceGainDb = mode == 0 ? 2.0f : 3.0f;

            *cabLowCut.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(currentSampleRate, lowCutFreq);
            *cabHighCut.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(currentSampleRate, highCutFreq);
            *cabPresence.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                currentSampleRate, presenceFreq, 1.0f, juce::Decibels::decibelsToGain(presenceGainDb));

            juce::dsp::AudioBlock<float> block(buffer);
            juce::dsp::ProcessContextReplacing<float> context(block);
            cabLowCut.process(context);
            cabLowCut.process(context);   // doble pasada: 4º orden
            cabHighCut.process(context);
            cabHighCut.process(context);  // doble pasada: 4º orden
            cabPresence.process(context);
        }
    }

    // A partir de acá, `buffer` es el "core" (Gate->Drive->Tone->Cab aplicados),
    // SIEMPRE 100% activo — Blend no lo toca. Dry tap para Blend: recién ahora,
    // después del core (antes se tomaba antes del core, y eso diluía Drive/Tone con
    // Blend < 100%; decisión revisada tras el primer testing real).
    for (int ch = 0; ch < numChannels; ++ch)
    {
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);
        scratchBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);
    }

    // --- Space (un solo reverb, con pre-delay real) ---
    {
        const bool enabled = apvts.getRawParameterValue(ParamIDs::spaceEnabled)->load() > 0.5f;
        const float mix = apvts.getRawParameterValue(ParamIDs::spaceAmount)->load() / 100.0f;
        if (enabled && mix > 0.0001f)
        {
            const int type = (int) apvts.getRawParameterValue(ParamIDs::spaceMode)->load();
            juce::dsp::Reverb::Parameters p;
            if (type == 0)      { p.roomSize = 0.35f; p.damping = 0.5f; } // Room
            else if (type == 1) { p.roomSize = 0.5f;  p.damping = 0.25f; p.width = 0.6f; } // Plate (más angosto)
            else                { p.roomSize = 0.85f; p.damping = 0.3f; } // Hall
            if (type != 1) p.width = 1.0f;
            // Headroom: con Hall + fader alto, el reverb sumaba tanta energía que el
            // Output se iba a rojo con facilidad (feedback de testing). Se limita el
            // wetLevel efectivo a un tope de 0.85 en vez de 0-1 completo.
            p.wetLevel = mix * 0.85f;
            p.dryLevel = 0.0f;
            space.setParameters(p);

            for (int ch = 0; ch < numChannels; ++ch)
                returnScratch.copyFrom(ch, 0, scratchBuffer, ch, 0, numSamples);

            juce::dsp::AudioBlock<float> block(returnScratch);
            auto subBlock = block.getSubBlock(0, (size_t) numSamples);
            juce::dsp::ProcessContextReplacing<float> context(subBlock);

            const float preDelayMs = apvts.getRawParameterValue(ParamIDs::spacePreDelay)->load();
            if (preDelayMs > 0.01f)
            {
                spacePreDelayLine.setDelay(juce::jmin((float) spacePreDelayLine.getMaximumDelayInSamples(),
                                                       preDelayMs * 0.001f * (float) currentSampleRate));
                spacePreDelayLine.process(context);
            }
            space.process(context);

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.addFrom(ch, 0, returnScratch, ch, 0, numSamples);
        }
    }

    // --- Modulation: Chorus/Flanger (misma clase, distintos parámetros) o Phaser ---
    {
        const bool enabled = apvts.getRawParameterValue(ParamIDs::modEnabled)->load() > 0.5f;
        const float mix = apvts.getRawParameterValue(ParamIDs::modAmount)->load() / 100.0f;
        if (enabled && mix > 0.0001f)
        {
            const int mode = (int) apvts.getRawParameterValue(ParamIDs::modMode)->load();
            const float rateHz = apvts.getRawParameterValue(ParamIDs::modRate)->load();

            for (int ch = 0; ch < numChannels; ++ch)
                returnScratch.copyFrom(ch, 0, scratchBuffer, ch, 0, numSamples);

            juce::dsp::AudioBlock<float> block(returnScratch);
            auto subBlock = block.getSubBlock(0, (size_t) numSamples);
            juce::dsp::ProcessContextReplacing<float> context(subBlock);

            if (mode == 2)
            {
                // Depth/feedback subidos (antes 0.7/0.3): en testing el Phaser sonaba
                // muy sutil, poco distinguible de los otros dos modos.
                phaser.setRate(rateHz);
                phaser.setDepth(0.95f);
                phaser.setCentreFrequency(700.0f);
                phaser.setFeedback(0.6f);
                phaser.setMix(1.0f);
                phaser.process(context);
            }
            else
            {
                chorusFlanger.setRate(rateHz);
                if (mode == 0) { chorusFlanger.setCentreDelay(7.5f); chorusFlanger.setFeedback(0.0f); chorusFlanger.setDepth(0.25f); }
                else           { chorusFlanger.setCentreDelay(2.0f); chorusFlanger.setFeedback(0.5f); chorusFlanger.setDepth(0.4f); }
                chorusFlanger.setMix(1.0f);
                chorusFlanger.process(context);
            }

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.addFrom(ch, 0, returnScratch, ch, 0, numSamples, mix);
        }
    }

    // --- Echo: delay con feedback, tempo-synced — mismo código que Acoustic ---
    {
        const bool enabled = apvts.getRawParameterValue(ParamIDs::echoEnabled)->load() > 0.5f;
        const float mix = apvts.getRawParameterValue(ParamIDs::echoAmount)->load() / 100.0f;
        if (enabled && mix > 0.0001f)
        {
            const int division = (int) apvts.getRawParameterValue(ParamIDs::echoDivision)->load();
            const float feedback = apvts.getRawParameterValue(ParamIDs::echoFeedback)->load() / 100.0f;
            const double bpm = getCurrentBpm();
            const float delayMs = computeEchoDelayMs(division, bpm);
            const float delaySamples = juce::jlimit(1.0f, (float) delayLine.getMaximumDelayInSamples(),
                                                      delayMs * 0.001f * (float) currentSampleRate);
            delayLine.setDelay(delaySamples);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* dry = scratchBuffer.getReadPointer(ch);
                auto* out = buffer.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    const float delayed = delayLine.popSample(ch);
                    delayLine.pushSample(ch, dry[i] + delayed * feedback);
                    out[i] += delayed * mix;
                }
            }
        }
    }

    // --- Blend (dry/wet) ---
    {
        const float wetAmount = apvts.getRawParameterValue(ParamIDs::blend)->load() / 100.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* wet = buffer.getWritePointer(ch);
            auto* dry = dryBuffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                wet[i] = dry[i] * (1.0f - wetAmount) + wet[i] * wetAmount;
        }
    }

    // --- Output Trim ---
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        outputGain.setGainDecibels(apvts.getRawParameterValue(ParamIDs::outputTrim)->load());
        outputGain.process(context);
    }

    outputPeakDb.store(juce::Decibels::gainToDecibels(buffer.getMagnitude(0, numSamples)));
}

juce::AudioProcessorEditor* DecimulGuitarsProcessor::createEditor()
{
    return new DecimulGuitarsEditor(*this);
}

void DecimulGuitarsProcessor::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double delta = now - lastTapTimeMs;
    if (delta >= 200.0 && delta <= 2000.0)
        manualBpm.store((float) (60000.0 / delta));
    lastTapTimeMs = now;
}

void DecimulGuitarsProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
        {
            xml->setAttribute("manualBpm", (double) manualBpm.load());
            copyXmlToBinary(*xml, destData);
        }
    }
}

void DecimulGuitarsProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        manualBpm.store((float) xml->getDoubleAttribute("manualBpm", 120.0));
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DecimulGuitarsProcessor();
}
