#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

/**
    Decimul Acoustic — multiefecto para voz/instrumentos acústicos.
    Ver plugins/decimul-acoustic/SPEC.md para la spec completa de parámetros
    y la cadena de señal.

    Estado actual: todos los parámetros están cableados en el
    AudioProcessorValueTreeState (listo para bindear la UI), pero processBlock()
    todavía es un placeholder pass-through — el DSP real (EQ/compresor/reverbs/delay
    basados en Airwindows, ver docs/research/dsp-references.md) se suma en la
    próxima etapa.
*/
class DecimulAcousticProcessor : public juce::AudioProcessor
{
public:
    DecimulAcousticProcessor();
    ~DecimulAcousticProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; } // reverbs

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Meters de solo lectura para la UI (input/output peak, gain reduction, correlación).
    // Se actualizan en processBlock() y se leen desde el editor con un Timer.
    std::atomic<float> inputPeakDb { -100.0f };
    std::atomic<float> outputPeakDb { -100.0f };
    std::atomic<float> gainReductionDb { 0.0f };
    std::atomic<float> stereoCorrelation { 1.0f };

private:
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulAcousticProcessor)
};
