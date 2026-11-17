#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "engine/ym2612.h"

/**
    DecimuLabs FM — sintetizador que imita por dentro al chip YM2612 de la Mega Drive.
    El sonido sale SOLO de escribir registros del chip (engine/ym2612.c, el mismo motor
    que la página decimulabs.com/sinte-fm). Seis voces, como los seis canales del chip.
*/
class DecimulFMProcessor : public juce::AudioProcessor
{
public:
    DecimulFMProcessor();
    ~DecimulFMProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    /** Registros del canal 1 tal como quedaron (para mostrar en la UI). */
    std::array<std::atomic<uint8_t>, 256> regView {};

private:
    struct Voice { int note = -1; uint32_t stamp = 0; };

    void syncRegisters(bool force);
    void writeReg(int port, int addr, int value);
    void noteOn(int note);
    void noteOff(int note);
    void allNotesOff();
    void setPitch(int channel);
    float p(const juce::String& id) const { return apvts.getRawParameterValue(id)->load(); }

    YM chip {};
    std::array<Voice, 6> voices {};
    uint32_t stampCounter = 0;
    float bendSemis = 0.0f;
    std::array<int, 512> written {};   // último valor escrito en cada registro (-1 = nunca)
    int currentProgram = 0;
    juce::SmoothedValue<float> smGain;
};
