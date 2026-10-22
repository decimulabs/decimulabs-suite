#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "EmeryDsp.h"

namespace ParamIDs
{
    static const juce::String essOn      = "essOn";
    static const juce::String essFreq    = "essFreq";      // Hz: centro de la banda de las eses
    static const juce::String essThresh  = "essThresh";    // dBFS: nivel de la banda a partir del cual baja
    static const juce::String essAmount  = "essAmount";    // dB: reducción máxima
    static const juce::String essWide    = "essWide";      // false = solo la banda, true = todo el sonido
    static const juce::String essListen  = "essListen";

    static const juce::String popOn      = "popOn";
    static const juce::String popFreq    = "popFreq";      // Hz: corte de la banda grave
    static const juce::String popThresh  = "popThresh";
    static const juce::String popAmount  = "popAmount";
    static const juce::String popWide    = "popWide";
    static const juce::String popListen  = "popListen";

    static const juce::String outputTrim = "outputTrim";
    static const juce::String bypass     = "bypass";
}

/**
    DecimuLabs Emery (nombre de trabajo) — de-esser y de-popper para voces: baja las eses ásperas
    (SIBILANCIA) y los golpes de aire de la p y la b (GOLPES), cada sección con su frecuencia,
    umbral y reducción máxima. Sin latencia, canales ligados. Ver SPEC.md y EmeryDsp.h.
*/
class DecimulEmeryProcessor : public juce::AudioProcessor
{
public:
    DecimulEmeryProcessor();
    ~DecimulEmeryProcessor() override = default;

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
    std::atomic<float> outputPeakDb { -100.0f };

    struct Frame
    {
        float essBandDb = -120.0f, essReductionDb = 0.0f;
        float popBandDb = -120.0f, popReductionDb = 0.0f;
    };

    /** Saca los cuadros nuevos del historial que dibuja la pantalla (hilo de la UI). */
    int readFrames(Frame* dest, int maxFrames);

private:
    static constexpr int frameChunk = 128;     // un cuadro cada 128 muestras (~2.7 ms a 48 kHz)

    double sampleRateHz = 48000.0;
    emery::DeEsserPopper engine;

    juce::SmoothedValue<float> smOutput, smBypass;

    juce::AbstractFifo frameFifo { 4096 };
    std::array<Frame, 4096> frameBuffer {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulEmeryProcessor)
};
