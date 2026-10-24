#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "ScourDsp.h"

namespace ParamIDs
{
    // globales
    static const juce::String inGain = "inGain", outGain = "outGain", magnitude = "magnitude", mix = "mix",
                              width = "width", bypass = "bypass";
    // EQ final
    static const juce::String eqOn = "eqOn", eqLow = "eqLow", eqHigh = "eqHigh", eqTone = "eqTone";
    // ruido
    static const juce::String noiseOn = "noiseOn", noiseAmt = "noiseAmt", noiseType = "noiseType", noiseTone = "noiseTone",
                              noiseFollow = "noiseFollow", noiseDuck = "noiseDuck", noisePost = "noisePost", noiseFlux = "noiseFlux";
    // wobble
    static const juce::String wobOn = "wobOn", wobAmt = "wobAmt", wobBalance = "wobBalance", wowRate = "wowRate",
                              flutRate = "flutRate", wobStereo = "wobStereo", wobMix = "wobMix", wobFlux = "wobFlux";
    // distort
    static const juce::String disOn = "disOn", disAmt = "disAmt", disType = "disType", disFocus = "disFocus",
                              disMix = "disMix", disFlux = "disFlux";
    // digital
    static const juce::String digOn = "digOn", digAmt = "digAmt", digBalance = "digBalance", digFocus = "digFocus",
                              digSmooth = "digSmooth", digMix = "digMix", digFlux = "digFlux";
    // space
    static const juce::String spaceOn = "spaceOn", spaceAmt = "spaceAmt", spaceDecay = "spaceDecay", spacePre = "spacePre",
                              spaceFocus = "spaceFocus", spaceStereo = "spaceStereo", spaceFlux = "spaceFlux";
    // magnetic
    static const juce::String magOn = "magOn", magAmt = "magAmt", magBalance = "magBalance", magRate = "magRate",
                              magDropouts = "magDropouts", magStereo = "magStereo", magFlux = "magFlux";
}

/**
    DecimuLabs Scour (nombre de trabajo) — efecto lo-fi de seis módulos (NOISE, WOBBLE, DISTORT,
    DIGITAL, SPACE y MAGNETIC), cada uno con su encendido, su cantidad y FLUX (una deriva
    aleatoria lenta), más MAGNITUDE para subir todo junto, un EQ final y ancho/mix/salida.
    Latencia cero, mono o estéreo. Ver SPEC.md y ScourDsp.h.
*/
class DecimulScourProcessor : public juce::AudioProcessor
{
public:
    DecimulScourProcessor();
    ~DecimulScourProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // ---- Lecturas para la UI ----
    std::atomic<float> inputPeakDb { -100.0f };
    std::atomic<float> outputPeakDb { -100.0f };

    struct Frame { float minV = 0.0f, maxV = 0.0f; };

    /** Saca los cuadros nuevos de la onda de salida (hilo de la UI). */
    int readFrames(Frame* dest, int maxFrames);

private:
    static constexpr int frameChunk = 128;     // un cuadro cada 128 muestras (~2.7 ms a 48 kHz)

    scour::Params readParams() const;

    double sampleRateHz = 48000.0;
    scour::ScourEngine engine;

    juce::SmoothedValue<float> smBypass;

    juce::AbstractFifo frameFifo { 4096 };
    std::array<Frame, 4096> frameBuffer {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulScourProcessor)
};
