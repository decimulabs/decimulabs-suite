#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "SarroDsp.h"

namespace ParamIDs
{
    static const juce::String depth       = "depth";          // %
    static const juce::String sharpness   = "sharpness";      // %
    static const juce::String sensitivity = "sensitivity";    // %
    static const juce::String attack      = "attack";         // ms
    static const juce::String release     = "release";        // ms
    static const juce::String lowFocus    = "lowFocus";       // Hz
    static const juce::String highFocus   = "highFocus";      // Hz
    static const juce::String link        = "link";           // %
    static const juce::String mix         = "mix";            // %
    static const juce::String trim        = "trim";           // dB
    static const juce::String delta       = "delta";
    static const juce::String bypass      = "bypass";
}

/**
    DecimuLabs Sarro (nombre de trabajo) — supresor de resonancias: baja solo los picos que
    sobresalen del espectro (la aspereza de una voz, el "ring" de una guitarra, los silbidos de un
    micrófono) y deja el resto. Procesa con STFT, así que tiene latencia (se informa al DAW).
    Ver SPEC.md y SarroDsp.h.
*/
class DecimulSarroProcessor : public juce::AudioProcessor
{
public:
    DecimulSarroProcessor();
    ~DecimulSarroProcessor() override = default;

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
    double getTailLengthSeconds() const override { return (double) engine.latencySamples() / sampleRateHz; }

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

    /** Último espectro de entrada (dBFS) y reducción (dB, positiva), kDisplay puntos de 20 Hz a 20 kHz. */
    void getDisplay(float* spec, float* red) const { engine.getDisplay(spec, red); }
    float getMaxReduction() const { return engine.lastMaxReduction(); }
    int getLatencyForUi() const { return engine.latencySamples(); }
    double getSampleRateForUi() const { return sampleRateHz; }

private:
    sarro::Params readParams() const;

    double sampleRateHz = 48000.0;
    sarro::ResonanceSuppressor engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulSarroProcessor)
};
