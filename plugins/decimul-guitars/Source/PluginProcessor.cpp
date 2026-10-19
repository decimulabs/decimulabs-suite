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
    computeDriveTable();
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
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::driveEnabled), "Drive Enabled", true));

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

namespace
{
    // Curvas del Drive (iguales a las de siempre; ahora sin std::function).
    inline float shapeSample(int mode, float x)
    {
        if (mode == 0) return std::tanh(x);                                         // Tube
        if (mode == 1) return x >= 0.0f ? 1.0f - std::exp(-x) : -1.0f + std::exp(x); // Fuzz
        return std::tanh(x * 0.35f);                                                // Boost
    }
}

void DecimulGuitarsProcessor::computeDriveTable()
{
    constexpr int cycle = 512;
    constexpr float refPeak = 0.178f; // -15 dBFS de pico: nivel típico de guitarra
    const double inRms = refPeak / std::sqrt(2.0);
    for (int mode = 0; mode < 3; ++mode)
        for (int k = 0; k < driveTablePoints; ++k)
        {
            const float amount = (float) k / (float) (driveTablePoints - 1);
            const float pre = juce::Decibels::decibelsToGain(amount * 40.0f);
            double sum = 0.0;
            for (int n = 0; n < cycle; ++n)
            {
                const float x = refPeak * std::sin(juce::MathConstants<float>::twoPi * (float) n / (float) cycle);
                const double y = shapeSample(mode, x * pre);
                sum += y * y;
            }
            const double outRms = std::sqrt(sum / cycle);
            driveMakeupDb[mode][k] = (float) (20.0 * std::log10(inRms / std::max(outRms, 1.0e-9)));
        }
}

float DecimulGuitarsProcessor::driveMakeupFor(int mode, float amount01) const
{
    const float pos = juce::jlimit(0.0f, 1.0f, amount01) * (float) (driveTablePoints - 1);
    const int i = juce::jmin((int) pos, driveTablePoints - 2);
    const float f = pos - (float) i;
    const int m = juce::jlimit(0, 2, mode);
    return driveMakeupDb[m][i] + (driveMakeupDb[m][i + 1] - driveMakeupDb[m][i]) * f;
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
    // juce::dsp::Gain arranca en 0: sin esto el audio entraba con un fade-in de 20 ms.
    inputGain.setGainLinear(1.0f);
    outputGain.setGainLinear(1.0f);
    inputGain.reset();
    outputGain.reset();

    gateEnvelope = 0.0f;
    gateGain = 0.0f;
    gateHoldCounter = 0;
    gateIsOpen = false;
    drivePreGainPrev = 1.0f;
    drivePostGainPrev = 1.0f;
    driveOversampling.initProcessing((size_t) samplesPerBlock);
    driveOversampling.reset();
    setLatencySamples((int) std::ceil(driveOversampling.getLatencyInSamples()));

    using Band = decimu::fx::EqBand;
    toneLowShelf.prepare(sampleRate, Band::Type::lowShelf);
    toneHighShelf.prepare(sampleRate, Band::Type::highShelf);
    toneMidPeak.prepare(sampleRate, Band::Type::peak, 1.0f);
    for (auto& f : cabLowCut) f.prepare(sampleRate, Band::Type::highpass);
    for (auto& f : cabHighCut) f.prepare(sampleRate, Band::Type::lowpass);
    cabPresence.prepare(sampleRate, Band::Type::peak, 1.0f);

    chorusFlanger.prepare(sampleRate);
    phaser.prepare(sampleRate);
    modMixPrev = 0.0f;
    space.prepare(sampleRate, 0);
    spacePreDelay.prepare(sampleRate, 0.6);
    echo.prepare(sampleRate);

    blendSmoothed.reset(sampleRate, 0.02);
    blendSmoothed.setCurrentAndTargetValue(apvts.getRawParameterValue(ParamIDs::blend)->load() / 100.0f);

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
    if (hostSyncEnabled.load())
    {
        if (auto* ph = getPlayHead())
        {
            if (auto position = ph->getPosition(); position.hasValue())
            {
                if (auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 1.0)
                    return *bpm;
            }
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

    // --- Gate: hysteresis + hold propios (ver nota en el .h). v0.5: detector ligado (L y R) ---
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

        for (int i = 0; i < numSamples; ++i)
        {
            float level = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                level = juce::jmax(level, std::abs(buffer.getReadPointer(ch)[i]));

            const float envCoeff = level > gateEnvelope ? envAttackCoeff : envReleaseCoeff;
            gateEnvelope += envCoeff * (level - gateEnvelope);
            const float envelopeDb = juce::Decibels::gainToDecibels(gateEnvelope, -100.0f);

            if (!gateIsOpen && envelopeDb > openThresholdDb)
            {
                gateIsOpen = true;
                gateHoldCounter = holdSamples;
            }
            else if (gateIsOpen)
            {
                if (envelopeDb > closeThresholdDb)
                    gateHoldCounter = holdSamples;
                else if (gateHoldCounter > 0)
                    --gateHoldCounter;
                else
                    gateIsOpen = false;
            }

            const float targetGain = gateIsOpen ? 1.0f : 0.0f;
            const float gainCoeff = targetGain > gateGain ? gainAttackCoeff : gainReleaseCoeff;
            gateGain += gainCoeff * (targetGain - gateGain);

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer(ch)[i] *= gateGain;
        }

        const float postMag = buffer.getMagnitude(0, numSamples);
        gainReductionDb.store(juce::jmax(0.0f, juce::Decibels::gainToDecibels(preMag)
                                             - juce::Decibels::gainToDecibels(postMag)));
    }

    // --- Drive: pre-gain+waveshaper SIEMPRE pasan por oversampling 4x (ver nota en el
    // .h) para que el pre-gain alto de Tube/Fuzz no aliasee. Sube/baja siempre (no solo
    // con Drive activo) para que la latencia del plugin sea constante. v0.5: la ganancia
    // pre/post se rampa entre bloques (sin escalones) y el nivel está calibrado. ---
    {
        juce::dsp::AudioBlock<float> block(buffer);
        auto oversampledBlock = driveOversampling.processSamplesUp(block);

        const bool enabled = apvts.getRawParameterValue(ParamIDs::driveEnabled)->load() > 0.5f;
        const float amount = apvts.getRawParameterValue(ParamIDs::driveAmount)->load() / 100.0f;
        if (enabled && amount > 0.001f)
        {
            const int mode = juce::jlimit(0, 2, (int) apvts.getRawParameterValue(ParamIDs::driveMode)->load());
            // Rango hasta +40dB de pre-gain (feedback de testing: con +23dB Fuzz no fuzzeaba).
            const float preGain = juce::Decibels::decibelsToGain(amount * 40.0f);
            const float postGain = juce::Decibels::decibelsToGain(driveMakeupFor(mode, amount));

            const size_t osSamples = oversampledBlock.getNumSamples();
            const float invN = 1.0f / (float) osSamples;
            for (size_t ch = 0; ch < oversampledBlock.getNumChannels(); ++ch)
            {
                auto* data = oversampledBlock.getChannelPointer(ch);
                for (size_t i = 0; i < osSamples; ++i)
                {
                    const float t = (float) i * invN;
                    const float pre = drivePreGainPrev + (preGain - drivePreGainPrev) * t;
                    const float post = drivePostGainPrev + (postGain - drivePostGainPrev) * t;
                    data[i] = shapeSample(mode, data[i] * pre) * post;
                }
            }
            drivePreGainPrev = preGain;
            drivePostGainPrev = postGain;
        }
        else
        {
            drivePreGainPrev = 1.0f;
            drivePostGainPrev = 1.0f;
        }

        driveOversampling.processSamplesDown(block);
    }

    float* chL = buffer.getWritePointer(0);
    float* chR = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    // --- Tone: tilt EQ + mid-sweep peak fijo (frecuencias y ganancias suavizadas) ---
    if (apvts.getRawParameterValue(ParamIDs::toneEnabled)->load() > 0.5f)
    {
        const float tiltDb = apvts.getRawParameterValue(ParamIDs::toneTilt)->load();
        const int mode = (int) apvts.getRawParameterValue(ParamIDs::toneMode)->load();
        const float lowFreq = mode == 0 ? 250.0f : (mode == 1 ? 400.0f : 500.0f);
        const float highFreq = mode == 0 ? 3000.0f : (mode == 1 ? 2000.0f : 1500.0f);
        const float midFreq = apvts.getRawParameterValue(ParamIDs::toneMidFreq)->load();

        toneLowShelf.set(lowFreq, -tiltDb * 0.5f);
        toneHighShelf.set(highFreq, tiltDb * 0.5f);
        toneMidPeak.set(midFreq, 4.0f);

        toneLowShelf.process(chL, chR, numSamples);
        toneHighShelf.process(chL, chR, numSamples);
        toneMidPeak.process(chL, chR, numSamples);
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

            for (auto& f : cabLowCut)  { f.set(lowCutFreq, 0.0f);  f.process(chL, chR, numSamples); }  // doble pasada: 4º orden
            for (auto& f : cabHighCut) { f.set(highCutFreq, 0.0f); f.process(chL, chR, numSamples); }
            cabPresence.set(presenceFreq, presenceGainDb);
            cabPresence.process(chL, chR, numSamples);
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

    if (numChannels > 1)
    {
        // --- Space (un solo reverb FDN, con pre-delay real). Siempre corre: al apagarlo o
        // bajar el mix la cola se drena sola (no vuelve audio viejo). ---
        {
            struct SpacePreset { float size, rt60, hf, er, mod, width; };
            static const SpacePreset kSpace[3] = { { 0.5f, 0.5f, 0.55f, 0.9f, 0.3f, 1.0f },   // Room
                                                   { 0.85f, 1.6f, 0.8f, 0.0f, 0.5f, 0.6f },   // Plate (brillante, más angosto)
                                                   { 1.5f, 3.2f, 0.4f, 0.3f, 0.8f, 1.0f } };  // Hall
            const bool enabled = apvts.getRawParameterValue(ParamIDs::spaceEnabled)->load() > 0.5f;
            const float mix = apvts.getRawParameterValue(ParamIDs::spaceAmount)->load() / 100.0f;
            const auto& p = kSpace[juce::jlimit(0, 2, (int) apvts.getRawParameterValue(ParamIDs::spaceMode)->load())];

            space.setPreset(p.size, p.rt60, p.hf, p.er, p.mod, p.width);
            space.setActive(enabled && mix > 0.0001f, mix);
            spacePreDelay.setMs(apvts.getRawParameterValue(ParamIDs::spacePreDelay)->load());

            for (int ch = 0; ch < numChannels; ++ch)
                returnScratch.copyFrom(ch, 0, scratchBuffer, ch, 0, numSamples);
            float* sl = returnScratch.getWritePointer(0);
            float* sr = returnScratch.getWritePointer(1);
            spacePreDelay.process(sl, sr, numSamples);
            space.process(sl, sr, sl, sr, numSamples);
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.addFrom(ch, 0, returnScratch, ch, 0, numSamples);
        }

        // --- Modulation: chorus/flanger estéreo propio o phaser propio ---
        {
            const bool enabled = apvts.getRawParameterValue(ParamIDs::modEnabled)->load() > 0.5f;
            const float mix = enabled ? apvts.getRawParameterValue(ParamIDs::modAmount)->load() / 100.0f : 0.0f;
            const int mode = (int) apvts.getRawParameterValue(ParamIDs::modMode)->load();
            const float rateHz = apvts.getRawParameterValue(ParamIDs::modRate)->load();

            if (mix > 0.0001f || modMixPrev > 0.0001f)
            {
                float* wl = returnScratch.getWritePointer(0);
                float* wr = returnScratch.getWritePointer(1);
                const float* il = scratchBuffer.getReadPointer(0);
                const float* ir = scratchBuffer.getReadPointer(1);

                if (mode == 2)
                {
                    // Phaser propio de 6 etapas, centro 1200 Hz y feedback 0.75 (mismos valores de
                    // la v0.4, que ya se había subido para que se note sobre la guitarra).
                    phaser.setRate(rateHz);
                    phaser.setShape(1200.0f, 0.75f);
                    phaser.process(il, ir, wl, wr, numSamples);
                }
                else
                {
                    chorusFlanger.setMode(mode == 1);
                    chorusFlanger.setRate(rateHz);
                    chorusFlanger.process(il, ir, wl, wr, numSamples);
                }

                for (int ch = 0; ch < numChannels; ++ch)
                    buffer.addFromWithRamp(ch, 0, returnScratch.getReadPointer(ch), numSamples, modMixPrev, mix);
            }
            modMixPrev = mix;
        }

        // --- Echo: delay con feedback filtrado, tempo-synced — mismo código que Acoustic ---
        {
            const bool enabled = apvts.getRawParameterValue(ParamIDs::echoEnabled)->load() > 0.5f;
            const float mix = apvts.getRawParameterValue(ParamIDs::echoAmount)->load() / 100.0f;
            const int division = (int) apvts.getRawParameterValue(ParamIDs::echoDivision)->load();
            const float feedback = apvts.getRawParameterValue(ParamIDs::echoFeedback)->load() / 100.0f;
            echo.set(computeEchoDelayMs(division, getCurrentBpm()), feedback, mix, enabled && mix > 0.0001f);
            echo.process(scratchBuffer.getReadPointer(0), scratchBuffer.getReadPointer(1),
                         buffer.getWritePointer(0), buffer.getWritePointer(1), numSamples);
        }
    }

    // --- Blend (dry/wet), suavizado ---
    {
        blendSmoothed.setTargetValue(apvts.getRawParameterValue(ParamIDs::blend)->load() / 100.0f);
        for (int i = 0; i < numSamples; ++i)
        {
            const float wetAmount = blendSmoothed.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* wet = buffer.getWritePointer(ch);
                wet[i] = dryBuffer.getReadPointer(ch)[i] * (1.0f - wetAmount) + wet[i] * wetAmount;
            }
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

void DecimulGuitarsProcessor::setManualBpm(float bpm)
{
    manualBpm.store(juce::jlimit(20.0f, 300.0f, bpm));
}

void DecimulGuitarsProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
        {
            xml->setAttribute("manualBpm", (double) manualBpm.load());
            xml->setAttribute("hostSyncEnabled", hostSyncEnabled.load());
            copyXmlToBinary(*xml, destData);
        }
    }
}

void DecimulGuitarsProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        manualBpm.store((float) xml->getDoubleAttribute("manualBpm", 120.0));
        hostSyncEnabled.store(xml->getBoolAttribute("hostSyncEnabled", true));
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DecimulGuitarsProcessor();
}
