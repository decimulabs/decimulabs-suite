#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "PumpCurve.h"

namespace ParamIDs
{
    static const juce::String rate       = "rate";        // duración del ciclo (1/4, 1/8, ...)
    static const juce::String depth      = "depth";       // 0..100 %: profundidad del bombeo
    static const juce::String mix        = "mix";         // 0..100 %: dry/wet
    static const juce::String smooth     = "smooth";      // ms: suavizado de la ganancia
    static const juce::String nudge      = "nudge";       // ms: adelanta/atrasa la curva
    static const juce::String spread     = "spread";      // % de ciclo: desfasaje del canal R
    static const juce::String trigger    = "trigger";     // Sync / MIDI / Audio
    static const juce::String midiNote   = "midiNote";    // -1 = cualquier nota
    static const juce::String threshold  = "threshold";   // dB: umbral del trigger por audio
    static const juce::String band       = "band";        // Full / Low / High
    static const juce::String crossover  = "crossover";   // Hz
    static const juce::String bpm        = "bpm";         // tempo cuando el host no da uno
    static const juce::String outputTrim = "outputTrim";
    static const juce::String bypass     = "bypass";
}

/**
    DecimuLabs Pump — bombeo de volumen sincronizado al tempo, como un sidechain
    "de mentira" que no necesita rutear ningún kick. Una curva editable (un ciclo) se
    repite siguiendo el tiempo del DAW, o se dispara una vez por cada nota MIDI o por
    cada golpe de kick detectado en la entrada de audio (o en el sidechain).

    Hilos: el audio thread solo lee una copia local de la tabla de la curva (se
    refresca con un try-lock cuando cambia la versión); el message thread edita la
    curva bajo `curveLock`. Ver SPEC.md.
*/
class DecimulPumpProcessor : public juce::AudioProcessor
{
public:
    enum Trigger { triggerSync = 0, triggerMidi = 1, triggerAudio = 2 };
    enum Band { bandFull = 0, bandLow = 1, bandHigh = 2 };

    DecimulPumpProcessor();
    ~DecimulPumpProcessor() override = default;

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

    // ---- Curva (message thread) ----
    PumpCurve getCurve() const;
    void setCurve(const PumpCurve& curve, int presetIndex = -1);   // -1 = "custom"
    int getPresetIndex() const { return presetIndex.load(); }
    int getCurveVersion() const
    {
        const juce::SpinLock::ScopedLockType lock(curveLock);
        return sharedVersion;
    }

    // ---- Tabla de duraciones del Rate (en beats = negras) ----
    static int numRates();
    static juce::String rateName(int index);
    static double rateBeats(int index);

    // ---- Lecturas para la UI ----
    std::atomic<float> uiPhase { 0.0f };        // posición actual dentro del ciclo (0..1)
    std::atomic<float> uiGain { 1.0f };         // ganancia aplicada ahora (canal L)
    std::atomic<float> outputPeakDb { -100.0f };
    std::atomic<bool> hostSynced { false };     // el host está dando posición de reproducción
    std::atomic<int> triggerCount { 0 };        // cuántos disparos hubo (MIDI/audio), para la UI

private:
    double currentSampleRate = 44100.0;

    // Curva compartida con la UI
    mutable juce::SpinLock curveLock;
    PumpCurve curve;
    std::array<float, PumpCurve::tableSize + 1> sharedTable {};
    int sharedVersion = 1;
    std::atomic<int> presetIndex { 0 };

    // Copia que usa el audio thread
    std::array<float, PumpCurve::tableSize + 1> table {};
    int tableVersion = 0;

    // Fase y gain (audio thread)
    double freePpq = 0.0;
    double oneShotPhase = 1.0;                  // 1 = ciclo terminado (sin atenuar)
    int pendingDelaySamples = -1;               // trigger con nudge: arranca cuando llegue a 0
    float gainL = 1.0f, gainR = 1.0f;

    // Detector de kick (trigger por audio)
    float detLp1 = 0.0f, detLp2 = 0.0f;         // pasa-bajos de 2 polos sobre la señal de detección
    float detEnv = 0.0f;
    bool detArmed = true;
    int holdoffSamples = 0;

    juce::dsp::LinkwitzRileyFilter<float> crossoverFilter;
    float lastCutoff = -1.0f;
    juce::dsp::Gain<float> outputGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulPumpProcessor)
};
