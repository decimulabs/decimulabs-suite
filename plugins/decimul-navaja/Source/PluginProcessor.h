#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "NavajaEngine.h"

namespace ParamIDs
{
    static const juce::String gain     = "gain";
    static const juce::String pan      = "pan";
    static const juce::String width    = "width";
    static const juce::String invertL  = "invertL";
    static const juce::String invertR  = "invertR";
    static const juce::String swap     = "swap";
    static const juce::String mono     = "mono";
    static const juce::String bassMono = "bassMono";
    static const juce::String bassHz   = "bassHz";
    static const juce::String dc       = "dc";
}

/** DecimuLabs Navaja: utilidad estéreo (ganancia, paneo, ancho, fase, mono, graves en mono, DC). */
class DecimulNavajaProcessor : public juce::AudioProcessor
{
public:
    DecimulNavajaProcessor();
    ~DecimulNavajaProcessor() override = default;

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

    std::atomic<float> correlation { 1.0f };
    std::atomic<float> peakL { -100.0f }, peakR { -100.0f };

private:
    navaja::Engine engine;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulNavajaProcessor)
};
