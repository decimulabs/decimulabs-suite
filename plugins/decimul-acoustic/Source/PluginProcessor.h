#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

/**
    DecimuLabs Acoustic — multiefecto para voz/instrumentos acústicos.
    Ver plugins/decimul-acoustic/SPEC.md para la spec completa de parámetros
    y la cadena de señal.

    Estado actual ("hito de prueba"): el editor es el juce::GenericAudioProcessorEditor
    (perillas automáticas, sin diseño visual todavía) y el DSP de este archivo es una
    implementación de REFERENCIA con clases built-in de juce::dsp (IIR shelving, Compressor,
    Reverb tipo Freeverb, DelayLine con feedback) — sirve para escuchar y validar que la
    cadena de señal tiene sentido, pero NO es la calidad final: eso viene después,
    reemplazando cada etapa por el DSP de Airwindows (ver docs/research/dsp-references.md).

    Limitaciones conocidas de esta implementación de referencia (a propósito, para no
    repetir el error de documentar algo que el código no hace):
    - Los coeficientes de los filtros de Tone y los parámetros de Reverb/Compressor se
      recalculan una vez por bloque, sin interpolación — mover esos controles rápido
      durante reproducción puede sonar a "zipper" leve. Input/Output sí están suavizados
      (juce::dsp::Gain con rampa).
    - `spaceAPreDelay(Sync)` / `spaceBPreDelay(Sync)` y `compOversample` están declarados
      como parámetros pero todavía NO afectan el audio (pre-delay real y oversampling
      quedan para la integración con Airwindows).
    - El feedback del Echo es una constante interna (0.35), todavía no es un parámetro.
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
        en manualBpm para que Echo lo use cuando no hay tempo de host (standalone) o el
        usuario prefiera fijar uno propio. No es un parámetro APVTS automatizable a
        propósito: es un gesto transitorio de UI, no un valor que tenga sentido
        automatizar desde una DAW. Se persiste manualmente en el estado. */
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

    // --- Input / Output: gain suavizado (evita zipper noise en automatización) ---
    juce::dsp::Gain<float> inputGain, outputGain;

    // --- Tone: shelving de 2 bandas, 3 "carácter" por banda (ver .cpp para las
    // frecuencias elegidas por modo) ---
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> lowFilter, highFilter;

    // --- Dynamics: compresor real, mapeado desde compAmount/compMode ---
    juce::dsp::Compressor<float> compressor;

    // --- Space A / Space B: reverbs en paralelo (Freeverb vía juce::dsp::Reverb) ---
    juce::dsp::Reverb reverbA, reverbB;

    // --- Echo: delay con feedback, tempo-synced (host BPM o manualBpm) ---
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 192000 };

    // Buffers de trabajo (dry tap + acumulador de retornos wet), redimensionados en
    // prepareToPlay. Ver processBlock: dry = señal post Input Trim, pre Tone/Dynamics;
    // wet = Tone+Dynamics ("core") + retorno de Space A + retorno de Space B + retorno
    // de Echo; Blend hace crossfade dry/wet antes de Output Trim.
    juce::AudioBuffer<float> dryBuffer, scratchBuffer, reverbScratch;

    double getCurrentBpm() const;
    float computeEchoDelayMs(int divisionIndex, double bpm) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulAcousticProcessor)
};
