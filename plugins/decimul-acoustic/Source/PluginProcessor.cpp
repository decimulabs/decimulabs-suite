#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ParamIDs
{
    static const juce::String inputTrim      = "inputTrim";
    static const juce::String lowGain        = "lowGain";
    static const juce::String lowMode        = "lowMode";
    static const juce::String highGain       = "highGain";
    static const juce::String highMode       = "highMode";
    static const juce::String compAmount     = "compAmount";
    static const juce::String compMode       = "compMode";
    static const juce::String compOversample = "compOversample";
    static const juce::String spaceAMix      = "spaceAMix";
    static const juce::String spaceAType     = "spaceAType";
    static const juce::String spaceAPreDelay = "spaceAPreDelay";
    static const juce::String spaceBMix      = "spaceBMix";
    static const juce::String spaceBType     = "spaceBType";
    static const juce::String spaceBPreDelay = "spaceBPreDelay";
    static const juce::String echoMix        = "echoMix";
    static const juce::String echoDivision   = "echoDivision";
    static const juce::String blend          = "blend";
    static const juce::String outputTrim     = "outputTrim";
    static const juce::String bypass         = "bypass";
}

DecimulAcousticProcessor::DecimulAcousticProcessor()
    : AudioProcessor(BusesProperties()
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

    // --- Input ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        ParamIDs::inputTrim, "Input", dbRange(-24.0f, 24.0f), 0.0f, "dB"));

    // --- Tone: Low ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        ParamIDs::lowGain, "Low", dbRange(-12.0f, 12.0f), 0.0f, "dB"));
    params.push_back(std::make_unique<AudioParameterChoice>(
        ParamIDs::lowMode, "Low Character", StringArray { "Warm", "Deep", "Air-Cut" }, 0));

    // --- Tone: High ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        ParamIDs::highGain, "High", dbRange(-12.0f, 12.0f), 0.0f, "dB"));
    params.push_back(std::make_unique<AudioParameterChoice>(
        ParamIDs::highMode, "High Character", StringArray { "Silk", "Sheen", "Crisp" }, 0));

    // --- Dynamics ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        ParamIDs::compAmount, "Dynamics", pctRange, 0.0f, "%"));
    params.push_back(std::make_unique<AudioParameterChoice>(
        ParamIDs::compMode, "Dynamics Character", StringArray { "Glue", "Snap", "Squash" }, 0));
    params.push_back(std::make_unique<AudioParameterChoice>(
        ParamIDs::compOversample, "Oversample", StringArray { "Off", "2x", "4x" }, 0));

    // --- Space A (reverb 1) ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        ParamIDs::spaceAMix, "Space A", pctRange, 0.0f, "%"));
    params.push_back(std::make_unique<AudioParameterChoice>(
        ParamIDs::spaceAType, "Space A Type", StringArray { "Room", "Hall", "Cathedral" }, 0));
    params.push_back(std::make_unique<AudioParameterFloat>(
        ParamIDs::spaceAPreDelay, "Space A Pre-Delay", dbRange(0.0f, 500.0f), 0.0f, "ms"));

    // --- Space B (reverb 2) ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        ParamIDs::spaceBMix, "Space B", pctRange, 0.0f, "%"));
    params.push_back(std::make_unique<AudioParameterChoice>(
        ParamIDs::spaceBType, "Space B Type", StringArray { "Close", "Wide", "Vast" }, 0));
    params.push_back(std::make_unique<AudioParameterFloat>(
        ParamIDs::spaceBPreDelay, "Space B Pre-Delay", dbRange(0.0f, 500.0f), 0.0f, "ms"));

    // --- Echo ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        ParamIDs::echoMix, "Echo", pctRange, 0.0f, "%"));
    params.push_back(std::make_unique<AudioParameterChoice>(
        ParamIDs::echoDivision, "Echo Division", StringArray { "Slap", "8th", "Quarter" }, 0));

    // --- Blend / Output ---
    params.push_back(std::make_unique<AudioParameterFloat>(
        ParamIDs::blend, "Blend", pctRange, 100.0f, "%"));
    params.push_back(std::make_unique<AudioParameterFloat>(
        ParamIDs::outputTrim, "Output", dbRange(-24.0f, 24.0f), 0.0f, "dB"));

    params.push_back(std::make_unique<AudioParameterBool>(
        ParamIDs::bypass, "Bypass", false));

    return { params.begin(), params.end() };
}

void DecimulAcousticProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    // TODO: preparar cadenas DSP reales (EQ/compresor/reverbs/delay) acá.
}

void DecimulAcousticProcessor::releaseResources()
{
}

bool DecimulAcousticProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void DecimulAcousticProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Placeholder: pass-through + trim de input/output, y actualización de meters,
    // para que el editor tenga algo real que mostrar mientras se construye la UI.
    const bool bypassed = apvts.getRawParameterValue(ParamIDs::bypass)->load() > 0.5f;

    const float inTrimDb = apvts.getRawParameterValue(ParamIDs::inputTrim)->load();
    const float outTrimDb = apvts.getRawParameterValue(ParamIDs::outputTrim)->load();

    if (!bypassed)
    {
        buffer.applyGain(juce::Decibels::decibelsToGain(inTrimDb));
        // TODO: Tone (low/high) -> Dynamics -> Space A/B -> Echo -> Blend
        buffer.applyGain(juce::Decibels::decibelsToGain(outTrimDb));
    }

    inputPeakDb.store(juce::Decibels::gainToDecibels(buffer.getMagnitude(0, buffer.getNumSamples())));
    outputPeakDb.store(inputPeakDb.load());
}

juce::AudioProcessorEditor* DecimulAcousticProcessor::createEditor()
{
    return new DecimulAcousticEditor(*this);
}

void DecimulAcousticProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
}

void DecimulAcousticProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DecimulAcousticProcessor();
}
