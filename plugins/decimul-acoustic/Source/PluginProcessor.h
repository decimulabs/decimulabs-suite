#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

/**
    Decimul Acoustic — multiefecto para voz/instrumentos acústicos.
    Ver plugins/decimul-acoustic/SPEC.md para la spec completa de parámetros
    y la cadena de señal.

    Estado actual: todos los parámetros están cableados en el
    AudioProcessorValueTreeState (listo para bindear la UI), pero processBlock()
    todavía es un placeholder (trim de input/output con ganancia suavizada +
    soporte mono→estéreo) — el DSP real (EQ/compresor/reverbs/delay basados en
    Airwindows, ver docs/research/dsp-references.md) se suma en la próxima etapa.
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

    /** Tap tempo manual (valor agregado #4 del SPEC): la UI llama a esto en cada click
        de un botón "Tap"; calcula el BPM a partir del intervalo entre clicks y lo deja
        en manualBpm para que Echo/Space A/B lo usen cuando no hay tempo de host
        (standalone) o el usuario prefiera fijar uno propio. No es un parámetro APVTS
        automatizable a propósito: es un gesto transitorio de UI, no un valor que tenga
        sentido automatizar desde una DAW. Se persiste manualmente en el estado. */
    void tapTempo();

    std::atomic<float> manualBpm { 120.0f };

    // Meters de solo lectura para la UI (input/output peak, gain reduction, correlación).
    // Se actualizan en processBlock() y se leen desde el editor con un Timer.
    std::atomic<float> inputPeakDb { -100.0f };
    std::atomic<float> outputPeakDb { -100.0f };
    std::atomic<float> gainReductionDb { 0.0f };
    std::atomic<float> stereoCorrelation { 1.0f };

private:
    double currentSampleRate = 44100.0;
    double lastTapTimeMs = 0.0;

    // Gain suavizado (evita clicks de zipper-noise en automatización) — mismo patrón
    // (juce::dsp::Gain con rampa) que van a usar las etapas de EQ/compresor reales.
    juce::dsp::Gain<float> inputGain, outputGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulAcousticProcessor)
};
