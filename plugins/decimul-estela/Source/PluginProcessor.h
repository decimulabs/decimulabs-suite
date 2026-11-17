#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "EstelaEngine.h"

namespace ParamIDs
{
    static const juce::String sync      = "sync";
    static const juce::String division  = "division";
    static const juce::String timeMs    = "timeMs";
    static const juce::String feedback  = "feedback";
    static const juce::String pingPong  = "pingPong";
    static const juce::String lowCut    = "lowCut";
    static const juce::String highCut   = "highCut";
    static const juce::String wobble    = "wobble";
    static const juce::String duck      = "duck";
    static const juce::String threshold = "threshold";
    static const juce::String release   = "release";
    static const juce::String mix       = "mix";
}

/** DecimuLabs Estela (Ducking Delay): delay que se agacha mientras suena la señal. */
class DecimulEstelaProcessor : public juce::AudioProcessor
{
public:
    DecimulEstelaProcessor();
    ~DecimulEstelaProcessor() override = default;

    void prepareToPlay(double sampleRate, int) override { engine.prepare(sampleRate); }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
               && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
    }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static const juce::StringArray& divisionNames();
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> duckGain { 1.0f };
    std::atomic<float> peakL { -100.0f }, peakR { -100.0f };
    std::atomic<float> uiTimeMs { 375.0f };

private:
    estela::Engine engine;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulEstelaProcessor)
};
