#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "RakeEngine.h"

namespace ParamIDs
{
    static const juce::String note     = "note";
    static const juce::String fine     = "fine";
    static const juce::String chord    = "chord";
    static const juce::String feedback = "feedback";
    static const juce::String damping  = "damping";
    static const juce::String lfoRate  = "lfoRate";
    static const juce::String lfoDepth = "lfoDepth";
    static const juce::String stereo   = "stereo";
    static const juce::String mix      = "mix";
    static const juce::String output   = "output";
}

/** DecimuLabs Rake (Comb Filter): peines afinados en una nota o un acorde. */
class DecimulRakeProcessor : public juce::AudioProcessor
{
public:
    DecimulRakeProcessor();
    ~DecimulRakeProcessor() override = default;

    void prepareToPlay(double sampleRate, int) override { engine.prepare(sampleRate); sr = sampleRate; }
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
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static const juce::StringArray& chordNames();
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> lfoPhase { 0.0f };       // para que el rastrillo tiemble en pantalla
    std::atomic<float> peakL { -100.0f }, peakR { -100.0f };

private:
    rake::Engine engine;
    double sr = 48000.0;
    double uiPhase = 0.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulRakeProcessor)
};
