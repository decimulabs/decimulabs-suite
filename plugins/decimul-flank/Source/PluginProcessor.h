#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "FlankDsp.h"

namespace ParamIDs
{
    static const juce::String detune     = "detune";      // centésimos: cuánto se desafinan las copias
    static const juce::String delay      = "delay";       // ms: retardo de las copias
    static const juce::String offset     = "offset";      // ms extra de la copia derecha (Haas)
    static const juce::String spread     = "spread";      // %: cuánto se corren las copias hacia los costados
    static const juce::String mix        = "mix";         // %: cuánto de las copias
    static const juce::String lowCut     = "lowCut";      // Hz: pasa-altos de lo que entra a las copias
    static const juce::String highCut    = "highCut";     // Hz: pasa-bajos de las copias
    static const juce::String space      = "space";       // %: ambiente (reflexiones tempranas)
    static const juce::String mode       = "mode";        // Static / Drift
    static const juce::String monoCheck  = "monoCheck";   // escuchar la salida sumada a mono
    static const juce::String outputTrim = "outputTrim";
    static const juce::String bypass     = "bypass";
}

/**
    DecimuLabs Flank (nombre de trabajo) — ensanchador de voces. De la voz (en mono)
    saca dos copias, una un poco más grave y otra un poco más aguda, con un retardo
    corto, y las corre hacia los costados: la voz queda al medio y las copias la
    "flanquean". Más un poco de ambiente (reflexiones tempranas) y una diferencia de
    tiempo entre los lados (efecto Haas), que es el "espaciador".

    Sin latencia: la señal seca pasa sin retardo y las copias se suman. Ver SPEC.md.

    Hilos: todo corre en el audio thread; la UI lee medidores por atómicos y el
    goniómetro por una cola sin bloqueos (`readScope`).
*/
class DecimulFlankProcessor : public juce::AudioProcessor
{
public:
    enum Mode { modeStatic = 0, modeDrift = 1 };

    DecimulFlankProcessor();
    ~DecimulFlankProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.2; }

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
    std::atomic<float> correlation { 1.0f };      // -1 (opuestas) .. +1 (mono)
    std::atomic<float> monoLossDb { 0.0f };       // cuánto baja el nivel al sumar L+R

    /** Saca pares (L, R) de la cola del goniómetro (hilo de la UI). Devuelve cuántos pares copió. */
    int readScope(float* destInterleaved, int maxPairs);

private:
    double sampleRateHz = 44100.0;

    // Filtros de las copias
    flank::Biquad lowCutFilter, highCutL, highCutR;
    float lastLowCut = -1.0f, lastHighCut = -1.0f;

    // Voces: las dos formas de desafinar corren siempre y se mezclan según el modo,
    // así cambiar de modo no hace saltos.
    flank::MicroShifter shifterL, shifterR;
    flank::DriftDelay driftL, driftR;
    flank::EarlyReflections early;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smDetune, smDelay, smOffset, smSpread,
        smMix, smSpace, smOutput, smMono, smBypass, smDrift;

    // Correlación (promedios móviles de L*R, L*L, R*R)
    double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;

    // Cola del goniómetro
    static constexpr int scopeCapacity = 4096;
    juce::AbstractFifo scopeFifo { scopeCapacity };
    std::array<float, (size_t) scopeCapacity * 2> scopeBuffer {};
    int scopeDecimation = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulFlankProcessor)
};
