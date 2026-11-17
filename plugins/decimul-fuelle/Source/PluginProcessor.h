#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "FuelleEngine.h"

namespace ParamIDs
{
    static const juce::String size      = "size";
    static const juce::String damping   = "damping";
    static const juce::String predelay  = "predelay";
    static const juce::String lowCut    = "lowCut";
    static const juce::String width     = "width";
    static const juce::String duck      = "duck";
    static const juce::String threshold = "threshold";
    static const juce::String release   = "release";
    static const juce::String mix       = "mix";
}

/** DecimuLabs Fuelle (Ducking Reverb): reverb que se agacha mientras suena la señal y respira en los silencios. */
class DecimulFuelleProcessor : public juce::AudioProcessor
{
public:
    DecimulFuelleProcessor();
    ~DecimulFuelleProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> duckGain { 1.0f };
    std::atomic<float> peakL { -100.0f }, peakR { -100.0f };

private:
    fuelle::Engine engine;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulFuelleProcessor)
};
