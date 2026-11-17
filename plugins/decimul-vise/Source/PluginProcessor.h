#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "ViseDsp.h"

namespace ParamIDs
{
    static const juce::String threshold   = "threshold";     // dBFS
    static const juce::String ratio       = "ratio";         // x:1
    static const juce::String attack      = "attack";        // ms
    static const juce::String release     = "release";       // ms
    static const juce::String autoRelease = "autoRelease";
    static const juce::String knee        = "knee";          // dB
    static const juce::String makeup      = "makeup";        // dB
    static const juce::String autoMakeup  = "autoMakeup";
    static const juce::String mix         = "mix";           // %
    static const juce::String scHp        = "scHp";          // Hz (20 = apagado)
    static const juce::String link        = "link";          // %
    static const juce::String rms         = "rms";
    static const juce::String outputTrim  = "outputTrim";    // dB
    static const juce::String bypass      = "bypass";
}

/**
    DecimuLabs Vise (nombre de trabajo) — compresor de bus: "pegamento" para grupos y master con
    pocas perillas. Ataque y caída (con caída automática), rodilla, mezcla paralela, pasa-altos del
    sidechain, enlace estéreo y detector de pico o RMS. Sin latencia. Ver SPEC.md y ViseDsp.h.
*/
class DecimulViseProcessor : public juce::AudioProcessor
{
public:
    DecimulViseProcessor();
    ~DecimulViseProcessor() override = default;

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
    std::atomic<float> currentGrDb { 0.0f };

    struct Frame
    {
        float inDb = -120.0f;      // pico de la entrada en dBFS
        float grDb = 0.0f;         // reducción (positiva)
    };

    /** Saca los cuadros nuevos del historial que dibuja la pantalla (hilo de la UI). */
    int readFrames(Frame* dest, int maxFrames);

private:
    static constexpr int frameChunk = 128;     // un cuadro cada 128 muestras (~2.7 ms a 48 kHz)

    vise::Params readParams() const;

    double sampleRateHz = 48000.0;
    vise::BusCompressor engine;

    juce::SmoothedValue<float> smOutput, smBypass;

    juce::AbstractFifo frameFifo { 4096 };
    std::array<Frame, 4096> frameBuffer {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulViseProcessor)
};
