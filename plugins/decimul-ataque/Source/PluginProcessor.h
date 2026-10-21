#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "TransientDsp.h"

namespace ParamIDs
{
    static const juce::String attack     = "attack";      // %: -100 (suaviza el golpe) .. +100 (lo acentúa)
    static const juce::String sustain    = "sustain";     // %: -100 (corta la cola) .. +100 (la alarga)
    static const juce::String speed      = "speed";       // %: qué tan rápido detecta (50 = normal)
    static const juce::String outputTrim = "outputTrim";
    static const juce::String bypass     = "bypass";
}

/**
    DecimuLabs Ataque (nombre de trabajo) — modulador de transientes: más o menos pegada
    (ATTACK) y más o menos cola (SUSTAIN) de baterías, guitarras acústicas, bajos, etc.
    Sin latencia, con los canales ligados (la imagen estéreo no se mueve) y con el mismo
    efecto sin importar el nivel del golpe. Ver SPEC.md y TransientDsp.h.
*/
class DecimulAtaqueProcessor : public juce::AudioProcessor
{
public:
    DecimulAtaqueProcessor();
    ~DecimulAtaqueProcessor() override = default;

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

    // ---- Lecturas para la UI ----
    std::atomic<float> inputPeakDb { -100.0f };
    std::atomic<float> outputPeakDb { -100.0f };

    struct Frame
    {
        float levelDb = -120.0f;   // nivel (envolvente rápida) en dBFS
        float gainDb = 0.0f;       // ganancia aplicada en ese momento
    };

    /** Saca los cuadros nuevos del historial que dibuja la pantalla (hilo de la UI). */
    int readFrames(Frame* dest, int maxFrames);

private:
    static constexpr int frameChunk = 128;     // un cuadro cada 128 muestras (~2.7 ms a 48 kHz)

    double sampleRateHz = 48000.0;
    ataque::TransientShaper shaper;

    juce::SmoothedValue<float> smOutput, smBypass;

    juce::AbstractFifo frameFifo { 4096 };
    std::array<Frame, 4096> frameBuffer {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulAtaqueProcessor)
};
