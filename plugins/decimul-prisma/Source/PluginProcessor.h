#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/PrismaEngine.h"
#include "Analyzer.h"
#include "CurveMath.h"
#include "InstanceRegistry.h"
#include <array>
#include <atomic>

namespace PrismaIDs
{
    // Por banda: "b1_freq" ... "b24_spec".
    enum BandKey { used = 0, on, freq, gain, q, shape, slope, place, dyn, range, thr, autoThr, sc, spec, numBandKeys };
    juce::String band(int index, BandKey key);

    static const juce::String mode = "mode";
    static const juce::String lpRes = "lpRes";
    static const juce::String character = "character";
    static const juce::String gainScale = "gainScale";
    static const juce::String autoGain = "autoGain";
    static const juce::String gainQ = "gainQ";
    static const juce::String attack = "attack";
    static const juce::String release = "release";
    static const juce::String output = "output";
    static const juce::String pan = "pan";
    static const juce::String phase = "phase";
    static const juce::String bypass = "bypass";
}

/**
    DecimuLabs Prisma (nombre de trabajo) — EQ paramétrico de 24 bandas en la línea del FabFilter
    Pro-Q: curva interactiva sobre el analizador, nueve formas de filtro con pendientes de 6 a
    96 dB/oct y brickwall, modos de latencia cero / fase natural / fase lineal, EQ dinámico y
    dinámica espectral por banda, bandas en estéreo/izquierda/derecha/centro/lados, solo de banda,
    sidechain, auto gain, carácter, EQ Match, lista de instancias con detección de choques.
    El audio lo hace el motor de Source/dsp (C++ puro, probado sin JUCE); acá están los parámetros,
    el estado, los presets, el A/B, el deshacer y la conexión con la pantalla.
*/
class DecimulPrismaProcessor : public juce::AudioProcessor,
                               private juce::Timer
{
public:
    DecimulPrismaProcessor();
    ~DecimulPrismaProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
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
    void updateTrackProperties(const TrackProperties& properties) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::UndoManager undoManager { 30000, 30 };
    juce::AudioProcessorValueTreeState apvts;

    // ---- Bandas (hilo de mensajes) ----
    prisma::BandParams readBand(int index) const;
    void writeBand(int index, const prisma::BandParams& b);
    void readAllBands(std::array<prisma::BandParams, prisma::maxBands>& out) const;
    int firstFreeBand() const;
    juce::RangedAudioParameter* bandParam(int index, PrismaIDs::BandKey key) const;
    float globalValue(const juce::String& id) const { return apvts.getRawParameterValue(id)->load(); }
    void setGlobal(const juce::String& id, float plainValue);

    // ---- Escuchar una banda sola (no es parámetro: no se automatiza) ----
    std::atomic<int> soloBand { -1 };

    // ---- Lecturas para la pantalla ----
    SpectrumAnalyzer preAnalyzer, postAnalyzer, scAnalyzer;
    std::atomic<float> outPeakL { -100.0f }, outPeakR { -100.0f };
    std::atomic<bool> sidechainConnected { false };
    std::atomic<bool> editorOpen { false };
    curves::Curve totalCurve {};
    double getSampleRateHz() const { return currentRate; }
    const prisma::DynamicsTelemetry& dynamicsTelemetry() const { return engine.telemetry; }
    float autoGainDb() const { return engine.autoGainDb(); }
    int latencySamples() const { return reportedLatency; }

    // Ajustes del analizador y de la pantalla: van en el estado (se guardan con el proyecto).
    juce::ValueTree uiState();
    int instanceId = 0;
    juce::String trackName;

    // ---- Presets, A/B ----
    juce::StringArray factoryPresetNames() const;
    void loadFactoryPreset(int index);
    juce::File userPresetFolder() const;
    bool saveUserPreset(const juce::File& file);
    bool loadPresetFile(const juce::File& file);
    juce::String currentPresetName { "Init" };

    void switchAB();
    void copyCurrentToOther();
    int currentAB() const { return abSlot; }

    /** Borra todas las bandas (y vuelve los globales a su valor inicial si `globalsToo`). */
    void resetAll(bool globalsToo);

private:
    void timerCallback() override;
    int predictedLatency() const;
    void fillBands(std::array<prisma::BandParams, prisma::maxBands>& out) const;
    void fillGlobals(prisma::GlobalParams& g) const;

    std::array<std::array<std::atomic<float>*, PrismaIDs::numBandKeys>, prisma::maxBands> bandRaw {};
    std::atomic<float>* rawMode = nullptr;
    std::atomic<float>* rawLpRes = nullptr;
    std::atomic<float>* rawCharacter = nullptr;
    std::atomic<float>* rawGainScale = nullptr;
    std::atomic<float>* rawAutoGain = nullptr;
    std::atomic<float>* rawGainQ = nullptr;
    std::atomic<float>* rawAttack = nullptr;
    std::atomic<float>* rawRelease = nullptr;
    std::atomic<float>* rawOutput = nullptr;
    std::atomic<float>* rawPan = nullptr;
    std::atomic<float>* rawPhase = nullptr;
    std::atomic<float>* rawBypass = nullptr;

    prisma::Engine engine;
    std::array<prisma::BandParams, prisma::maxBands> audioBands {};
    double currentRate = 48000.0;
    bool stereoLayout = true;
    bool lastOffline = false;
    int reportedLatency = -1;

    std::array<prisma::BandParams, prisma::maxBands> curveBands {};
    double curveScale = 1.0;
    bool curveGainQ = false, curveValid = false;

    juce::ValueTree abStates[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulPrismaProcessor)
};
