#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    // versionHint: JUCE 8 lo usa para poder distinguir parámetros al cargar el estado
    // de una versión vieja del plugin (por si algún día se cambia el rango/tipo de un
    // parámetro). Arrancamos todos en 1; el día que se modifique alguno "en el lugar"
    // en vez de agregar uno nuevo, ese parámetro sube su versionHint.
    juce::ParameterID pid(const juce::String& id) { return { id, 1 }; }
}

DecimulAcousticProcessor::DecimulAcousticProcessor()
    : AudioProcessor(BusesProperties()
                          // Igual que los dos componentes del CLA Unplugged original
                          // (mono-to-stereo y stereo-to-stereo): declaramos stereo por
                          // default, pero isBusesLayoutSupported() también acepta mono
                          // in -> stereo out, típico para un mic/DI de guitarra o voz.
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

DecimulAcousticProcessor::~DecimulAcousticProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout DecimulAcousticProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    auto dbRange = [](float lo, float hi) { return NormalisableRange<float>(lo, hi, 0.1f); };
    auto pctRange = NormalisableRange<float>(0.0f, 100.0f, 0.1f);
    auto dbAttr = AudioParameterFloatAttributes().withLabel("dB");
    auto pctAttr = AudioParameterFloatAttributes().withLabel("%");
    auto msAttr = AudioParameterFloatAttributes().withLabel("ms");

    // --- Input ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::inputTrim), "Input", dbRange(-24.0f, 24.0f), 0.0f, dbAttr));

    // --- Tone: Low ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::lowGain), "Low", dbRange(-12.0f, 12.0f), 0.0f, dbAttr));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::lowMode), "Low Character", StringArray { "Warm", "Deep", "Air-Cut" }, 0));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::lowEnabled), "Low Enabled", true));

    // --- Tone: High ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::highGain), "High", dbRange(-12.0f, 12.0f), 0.0f, dbAttr));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::highMode), "High Character", StringArray { "Silk", "Sheen", "Crisp" }, 0));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::highEnabled), "High Enabled", true));

    // --- Dynamics ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::compAmount), "Dynamics", pctRange, 0.0f, pctAttr));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::compMode), "Dynamics Character", StringArray { "Glue", "Snap", "Squash" }, 0));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::compOversample), "Oversample", StringArray { "Off", "2x", "4x" }, 0));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::compEnabled), "Dynamics Enabled", true));

    // --- Space A (reverb 1) ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::spaceAMix), "Space A", pctRange, 0.0f, pctAttr));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::spaceAType), "Space A Type", StringArray { "Room", "Hall", "Cathedral" }, 0));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::spaceAPreDelay), "Space A Pre-Delay", dbRange(0.0f, 500.0f), 0.0f, msAttr));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::spaceAPreDelaySync), "Space A Pre-Delay Sync", false));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::spaceAEnabled), "Space A Enabled", true));

    // --- Space B (reverb 2) ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::spaceBMix), "Space B", pctRange, 0.0f, pctAttr));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::spaceBType), "Space B Type", StringArray { "Close", "Wide", "Vast" }, 0));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::spaceBPreDelay), "Space B Pre-Delay", dbRange(0.0f, 500.0f), 0.0f, msAttr));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::spaceBPreDelaySync), "Space B Pre-Delay Sync", false));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::spaceBEnabled), "Space B Enabled", true));

    // --- Echo ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::echoMix), "Echo", pctRange, 0.0f, pctAttr));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::echoDivision), "Echo Division", StringArray { "Slap", "8th", "Quarter" }, 0));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::echoFeedback), "Echo Feedback", pctRange, 35.0f, pctAttr));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::echoEnabled), "Echo Enabled", true));

    // --- Blend / Output ---
    // Blend solo mezcla dry (post-core) contra los retornos de Space A/B/Echo — Low/
    // High/Dynamics son "core" y quedan siempre 100% activos (ver nota v0.2 en el .h).
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::blend), "Blend", pctRange, 100.0f, pctAttr));
    // Piso bajado de -24 a -60dB (mismo ajuste que Guitars v0.2).
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::outputTrim), "Output", dbRange(-60.0f, 24.0f), 0.0f, dbAttr));

    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::bypass), "Bypass", false));

    return { params.begin(), params.end() };
}

void DecimulAcousticProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
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

    lowFilter.prepare(spec);
    highFilter.prepare(spec);

    compressor.prepare(spec);

    reverbA.prepare(spec);
    reverbB.prepare(spec);
    reverbA.reset();
    reverbB.reset();

    delayLine.setMaximumDelayInSamples((int) (2.0 * sampleRate));
    delayLine.prepare(spec);
    delayLine.reset();

    preDelayA.setMaximumDelayInSamples((int) (0.6 * sampleRate));
    preDelayB.setMaximumDelayInSamples((int) (0.6 * sampleRate));
    preDelayA.prepare(spec);
    preDelayB.prepare(spec);
    preDelayA.reset();
    preDelayB.reset();

    dryBuffer.setSize((int) spec.numChannels, samplesPerBlock);
    scratchBuffer.setSize((int) spec.numChannels, samplesPerBlock);
    reverbScratch.setSize((int) spec.numChannels, samplesPerBlock);
}

void DecimulAcousticProcessor::releaseResources()
{
}

bool DecimulAcousticProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn = layouts.getMainInputChannelSet();

    if (mainOut != juce::AudioChannelSet::stereo())
        return false;

    // Acepta mono (mic/DI directo, como el componente "Mono-to-Stereo" del original)
    // o stereo de entrada.
    return mainIn == juce::AudioChannelSet::stereo() || mainIn == juce::AudioChannelSet::mono();
}

double DecimulAcousticProcessor::getCurrentBpm() const
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

float DecimulAcousticProcessor::computeEchoDelayMs(int divisionIndex, double bpm) const
{
    switch (divisionIndex)
    {
        case 0: return 120.0f;                              // Slap: fijo, no tempo-synced (slapback clásico)
        case 1: return (float) (60000.0 / bpm / 2.0);        // 8th
        case 2: return (float) (60000.0 / bpm);              // Quarter
        default: return 120.0f;
    }
}

void DecimulAcousticProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    const bool bypassed = apvts.getRawParameterValue(ParamIDs::bypass)->load() > 0.5f;

    // Widen mono->stereo (componente "Mono-to-Stereo"): si el host nos dio 1 canal de
    // entrada pero el bus de salida es stereo, duplicamos antes de procesar para que
    // todo lo que venga después (reverbs, blend) trabaje siempre en stereo real.
    if (getMainBusNumInputChannels() == 1 && numChannels > 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);

    if (bypassed)
    {
        inputPeakDb.store(juce::Decibels::gainToDecibels(buffer.getMagnitude(0, numSamples)));
        outputPeakDb.store(inputPeakDb.load());
        return;
    }

    // Buffers de trabajo pueden necesitar resize si el host cambia el tamaño de bloque
    // en caliente (poco común, pero pasa en algunos hosts al hacer bounce/freeze).
    if (dryBuffer.getNumSamples() < numSamples || dryBuffer.getNumChannels() != numChannels)
    {
        dryBuffer.setSize(numChannels, numSamples, false, false, true);
        scratchBuffer.setSize(numChannels, numSamples, false, false, true);
        reverbScratch.setSize(numChannels, numSamples, false, false, true);
    }

    // --- Input Trim ---
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        inputGain.setGainDecibels(apvts.getRawParameterValue(ParamIDs::inputTrim)->load());
        inputGain.process(context);
    }

    inputPeakDb.store(juce::Decibels::gainToDecibels(buffer.getMagnitude(0, numSamples)));

    // --- Tone: Low + High shelving, 3 "carácter" por banda ---
    if (apvts.getRawParameterValue(ParamIDs::lowEnabled)->load() > 0.5f)
    {
        const float lowGainDb = apvts.getRawParameterValue(ParamIDs::lowGain)->load();
        const int lowMode = (int) apvts.getRawParameterValue(ParamIDs::lowMode)->load();
        const float lowFreq = lowMode == 0 ? 200.0f : (lowMode == 1 ? 80.0f : 400.0f); // Warm/Deep/Air-Cut
        *lowFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(
            currentSampleRate, lowFreq, 0.7071f, juce::Decibels::decibelsToGain(lowGainDb));

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        lowFilter.process(context);
    }
    if (apvts.getRawParameterValue(ParamIDs::highEnabled)->load() > 0.5f)
    {
        const float highGainDb = apvts.getRawParameterValue(ParamIDs::highGain)->load();
        const int highMode = (int) apvts.getRawParameterValue(ParamIDs::highMode)->load();
        const float highFreq = highMode == 0 ? 8000.0f : (highMode == 1 ? 5000.0f : 10000.0f); // Silk/Sheen/Crisp
        *highFilter.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(
            currentSampleRate, highFreq, 0.7071f, juce::Decibels::decibelsToGain(highGainDb));

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        highFilter.process(context);
    }

    // --- Dynamics: compresor mapeado desde compAmount (0-100%) + compMode ---
    if (apvts.getRawParameterValue(ParamIDs::compEnabled)->load() > 0.5f)
    {
        const float amount = apvts.getRawParameterValue(ParamIDs::compAmount)->load();
        const int mode = (int) apvts.getRawParameterValue(ParamIDs::compMode)->load();

        // Presets de carácter (Glue/Snap/Squash) inspirados en el comportamiento de
        // Push/Spank/Wall del original, no en su circuito real.
        float ratio = 2.0f, attackMs = 30.0f, releaseMs = 250.0f;
        if (mode == 1) { ratio = 4.0f; attackMs = 5.0f;  releaseMs = 80.0f;  } // Snap
        if (mode == 2) { ratio = 8.0f; attackMs = 1.0f;  releaseMs = 400.0f; } // Squash

        const float thresholdDb = -(amount / 100.0f) * 24.0f;
        const float makeupDb = (amount / 100.0f) * 6.0f;

        compressor.setThreshold(thresholdDb);
        compressor.setRatio(ratio);
        compressor.setAttack(attackMs);
        compressor.setRelease(releaseMs);

        const float preMag = buffer.getMagnitude(0, numSamples);

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        compressor.process(context);
        buffer.applyGain(juce::Decibels::decibelsToGain(makeupDb));

        const float postMag = buffer.getMagnitude(0, numSamples);
        gainReductionDb.store(juce::jmax(0.0f, juce::Decibels::gainToDecibels(preMag)
                                             - juce::Decibels::gainToDecibels(postMag)));
    }

    // A partir de acá, `buffer` es el "core" (Low/High/Dynamics aplicados — SIEMPRE
    // 100% activo, Blend no lo toca). Dry tap para Blend: recién ahora, después del
    // core (antes se tomaba antes del core, y eso diluía el EQ/compresor con
    // Blend < 100%; decisión revisada tras el testing real de Guitars v0.2, ver nota
    // en el .h). scratchBuffer queda para mandar el core en paralelo a Space A/B/Echo.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);
        scratchBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);
    }

    // --- Space A (reverb 1) ---
    {
        const bool enabled = apvts.getRawParameterValue(ParamIDs::spaceAEnabled)->load() > 0.5f;
        const float mix = apvts.getRawParameterValue(ParamIDs::spaceAMix)->load() / 100.0f;
        if (enabled && mix > 0.0001f)
        {
            const int type = (int) apvts.getRawParameterValue(ParamIDs::spaceAType)->load();
            juce::dsp::Reverb::Parameters p;
            // Room / Hall / Cathedral: de más chico/seco a más grande/húmedo.
            if (type == 0)      { p.roomSize = 0.3f; p.damping = 0.5f; }
            else if (type == 1) { p.roomSize = 0.6f; p.damping = 0.4f; }
            else                { p.roomSize = 0.9f; p.damping = 0.2f; }
            p.width = 1.0f;
            // Headroom (mismo ajuste que Guitars v0.2): tope 0.85 en vez de 0-1
            // completo, para no irse a rojo tan fácil con el fader alto.
            p.wetLevel = mix * 0.85f;
            p.dryLevel = 0.0f;
            reverbA.setParameters(p);

            for (int ch = 0; ch < numChannels; ++ch)
                reverbScratch.copyFrom(ch, 0, scratchBuffer, ch, 0, numSamples);

            juce::dsp::AudioBlock<float> block(reverbScratch);
            auto subBlock = block.getSubBlock(0, (size_t) numSamples);
            juce::dsp::ProcessContextReplacing<float> context(subBlock);

            const float preDelayMs = apvts.getRawParameterValue(ParamIDs::spaceAPreDelay)->load();
            if (preDelayMs > 0.01f)
            {
                preDelayA.setDelay(juce::jmin((float) preDelayA.getMaximumDelayInSamples(),
                                               preDelayMs * 0.001f * (float) currentSampleRate));
                preDelayA.process(context);
            }
            reverbA.process(context);

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.addFrom(ch, 0, reverbScratch, ch, 0, numSamples);
        }
    }

    // --- Space B (reverb 2) ---
    {
        const bool enabled = apvts.getRawParameterValue(ParamIDs::spaceBEnabled)->load() > 0.5f;
        const float mix = apvts.getRawParameterValue(ParamIDs::spaceBMix)->load() / 100.0f;
        if (enabled && mix > 0.0001f)
        {
            const int type = (int) apvts.getRawParameterValue(ParamIDs::spaceBType)->load();
            juce::dsp::Reverb::Parameters p;
            // Close / Wide / Vast: de más chico/seco a más grande/húmedo.
            if (type == 0)      { p.roomSize = 0.2f; p.damping = 0.6f; }
            else if (type == 1) { p.roomSize = 0.5f; p.damping = 0.4f; }
            else                { p.roomSize = 0.95f; p.damping = 0.1f; }
            p.width = 1.0f;
            p.wetLevel = mix * 0.85f; // headroom, ver Space A
            p.dryLevel = 0.0f;
            reverbB.setParameters(p);

            for (int ch = 0; ch < numChannels; ++ch)
                reverbScratch.copyFrom(ch, 0, scratchBuffer, ch, 0, numSamples);

            juce::dsp::AudioBlock<float> block(reverbScratch);
            auto subBlock = block.getSubBlock(0, (size_t) numSamples);
            juce::dsp::ProcessContextReplacing<float> context(subBlock);

            const float preDelayMs = apvts.getRawParameterValue(ParamIDs::spaceBPreDelay)->load();
            if (preDelayMs > 0.01f)
            {
                preDelayB.setDelay(juce::jmin((float) preDelayB.getMaximumDelayInSamples(),
                                               preDelayMs * 0.001f * (float) currentSampleRate));
                preDelayB.process(context);
            }
            reverbB.process(context);

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.addFrom(ch, 0, reverbScratch, ch, 0, numSamples);
        }
    }

    // --- Echo: delay con feedback, tempo-synced ---
    {
        const bool enabled = apvts.getRawParameterValue(ParamIDs::echoEnabled)->load() > 0.5f;
        const float mix = apvts.getRawParameterValue(ParamIDs::echoMix)->load() / 100.0f;
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

    // --- Correlación estéreo (meter de solo lectura) ---
    if (numChannels >= 2)
    {
        auto* left = buffer.getReadPointer(0);
        auto* right = buffer.getReadPointer(1);
        double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            sumLR += (double) left[i] * (double) right[i];
            sumLL += (double) left[i] * (double) left[i];
            sumRR += (double) right[i] * (double) right[i];
        }
        const double denom = std::sqrt(sumLL * sumRR);
        stereoCorrelation.store(denom > 1.0e-9 ? (float) (sumLR / denom) : 1.0f);
    }
}

juce::AudioProcessorEditor* DecimulAcousticProcessor::createEditor()
{
    return new DecimulAcousticEditor(*this);
}

void DecimulAcousticProcessor::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double delta = now - lastTapTimeMs;

    // Descarta el primer tap de una secuencia nueva, o si pasó demasiado tiempo desde
    // el último (>2s: asumimos que el usuario está empezando una secuencia nueva, no
    // continuando la anterior). Rango válido: 30-300 BPM (200ms-2000ms de intervalo).
    if (delta >= 200.0 && delta <= 2000.0)
        manualBpm.store((float) (60000.0 / delta));

    lastTapTimeMs = now;
}

void DecimulAcousticProcessor::setManualBpm(float bpm)
{
    manualBpm.store(juce::jlimit(20.0f, 300.0f, bpm));
}

void DecimulAcousticProcessor::getStateInformation(juce::MemoryBlock& destData)
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

void DecimulAcousticProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        manualBpm.store((float) xml->getDoubleAttribute("manualBpm", 120.0));
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DecimulAcousticProcessor();
}
