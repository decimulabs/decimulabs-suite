#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "CarveEngine.h"

namespace ParamIDs
{
    static const juce::String amount     = "amount";      // 0..100 %: cuánto recorta
    static const juce::String scGain     = "scGain";      // dB: ganancia del sidechain antes de medirlo
    static const juce::String attack     = "attack";      // ms
    static const juce::String release    = "release";     // ms
    static const juce::String lowFreq    = "lowFreq";     // Hz: debajo de esto no recorta
    static const juce::String highFreq   = "highFreq";    // Hz: arriba de esto no recorta
    static const juce::String smooth     = "smooth";      // 0..100 %: esparce el recorte a bandas vecinas
    static const juce::String mix        = "mix";         // 0..100 %: dry/wet
    static const juce::String outputTrim = "outputTrim";  // dB
    static const juce::String bypass     = "bypass";
}

/**
    DecimuLabs Carve — le "hace lugar" a una pista según lo que suena en otra: mide el
    espectro del sidechain en 32 bandas y baja en la pista principal sólo las
    frecuencias que el sidechain está ocupando (ducking espectral).
    La técnica es la clásica de EQ dinámico con sidechain; no copia a ningún plugin.
*/
class DecimulCarveProcessor : public juce::AudioProcessor
{
public:
    DecimulCarveProcessor();
    ~DecimulCarveProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // ---- Lecturas para la UI (se escriben en el audio thread) ----
    std::array<std::atomic<float>, carve::numBands> uiLevelDb;   // nivel del sidechain por banda
    std::array<std::atomic<float>, carve::numBands> uiCutDb;     // recorte aplicado por banda (dB, positivo)
    std::atomic<bool> sidechainConnected { false };
    std::atomic<float> outputPeakDb { -100.0f };

private:
    carve::Engine engine;
    juce::SmoothedValue<float> outputGain { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulCarveProcessor)
};
