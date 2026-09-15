#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ParamIDs
{
    static const juce::String inputTrim          = "inputTrim";
    static const juce::String lowGain            = "lowGain";
    static const juce::String lowMode            = "lowMode";
    static const juce::String highGain           = "highGain";
    static const juce::String highMode           = "highMode";
    static const juce::String compAmount         = "compAmount";
    static const juce::String compMode           = "compMode";
    static const juce::String compOversample     = "compOversample";
    static const juce::String spaceAMix          = "spaceAMix";
    static const juce::String spaceAType         = "spaceAType";
    static const juce::String spaceAPreDelay     = "spaceAPreDelay";
    static const juce::String spaceAPreDelaySync = "spaceAPreDelaySync";
    static const juce::String spaceBMix          = "spaceBMix";
    static const juce::String spaceBType         = "spaceBType";
    static const juce::String spaceBPreDelay     = "spaceBPreDelay";
    static const juce::String spaceBPreDelaySync = "spaceBPreDelaySync";
    static const juce::String echoMix            = "echoMix";
    static const juce::String echoDivision       = "echoDivision";
    static const juce::String blend              = "blend";
    static const juce::String outputTrim         = "outputTrim";
    static const juce::String bypass             = "bypass";
}

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

    // --- Tone: High ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::highGain), "High", dbRange(-12.0f, 12.0f), 0.0f, dbAttr));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::highMode), "High Character", StringArray { "Silk", "Sheen", "Crisp" }, 0));

    // --- Dynamics ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::compAmount), "Dynamics", pctRange, 0.0f, pctAttr));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::compMode), "Dynamics Character", StringArray { "Glue", "Snap", "Squash" }, 0));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::compOversample), "Oversample", StringArray { "Off", "2x", "4x" }, 0));

    // --- Space A (reverb 1) ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::spaceAMix), "Space A", pctRange, 0.0f, pctAttr));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::spaceAType), "Space A Type", StringArray { "Room", "Hall", "Cathedral" }, 0));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::spaceAPreDelay), "Space A Pre-Delay", dbRange(0.0f, 500.0f), 0.0f, msAttr));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::spaceAPreDelaySync), "Space A Pre-Delay Sync", false));

    // --- Space B (reverb 2) ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::spaceBMix), "Space B", pctRange, 0.0f, pctAttr));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::spaceBType), "Space B Type", StringArray { "Close", "Wide", "Vast" }, 0));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::spaceBPreDelay), "Space B Pre-Delay", dbRange(0.0f, 500.0f), 0.0f, msAttr));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::spaceBPreDelaySync), "Space B Pre-Delay Sync", false));

    // --- Echo ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::echoMix), "Echo", pctRange, 0.0f, pctAttr));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::echoDivision), "Echo Division", StringArray { "Slap", "8th", "Quarter" }, 0));

    // --- Blend / Output ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::blend), "Blend", pctRange, 100.0f, pctAttr));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::outputTrim), "Output", dbRange(-24.0f, 24.0f), 0.0f, dbAttr));

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
    spec.numChannels = (juce::uint32) getTotalNumOutputChannels();

    inputGain.prepare(spec);
    outputGain.prepare(spec);
    inputGain.setRampDurationSeconds(0.02);
    outputGain.setRampDurationSeconds(0.02);

    // TODO: preparar cadenas DSP reales (EQ/compresor/reverbs/delay) acá.
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

void DecimulAcousticProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const bool bypassed = apvts.getRawParameterValue(ParamIDs::bypass)->load() > 0.5f;
    const float inTrimDb = apvts.getRawParameterValue(ParamIDs::inputTrim)->load();
    const float outTrimDb = apvts.getRawParameterValue(ParamIDs::outputTrim)->load();

    // Widen mono->stereo (componente "Mono-to-Stereo"): si el host nos dio 1 canal de
    // entrada pero el bus de salida es stereo, duplicamos antes de procesar para que
    // todo lo que venga después (reverbs, blend) trabaje siempre en stereo real.
    if (getMainBusNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples());

    if (!bypassed)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);

        inputGain.setGainDecibels(inTrimDb);
        inputGain.process(context);

        // TODO: Tone (low/high) -> Dynamics -> Space A/B -> Echo -> Blend

        outputGain.setGainDecibels(outTrimDb);
        outputGain.process(context);
    }

    inputPeakDb.store(juce::Decibels::gainToDecibels(buffer.getMagnitude(0, buffer.getNumSamples())));
    outputPeakDb.store(inputPeakDb.load());
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
