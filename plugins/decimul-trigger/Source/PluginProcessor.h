#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include "HitDetector.h"

namespace ParamIDs
{
    // Detección
    static const juce::String threshold   = "threshold";    // dB: pico mínimo para que cuente como golpe
    static const juce::String maxLevel    = "maxLevel";     // dB: el pico que da la velocity máxima
    static const juce::String scan        = "scan";         // ms: ventana de medición de la intensidad
    static const juce::String retrigger   = "retrigger";    // ms: bloqueo y decaimiento de la máscara
    static const juce::String lowCut      = "lowCut";       // Hz: pasa-altos de la señal de detección
    static const juce::String highCut     = "highCut";      // Hz: pasa-bajos de la señal de detección
    static const juce::String listen      = "listen";       // oír la señal de detección (para ajustar)
    // Velocity
    static const juce::String minVel      = "minVel";
    static const juce::String maxVel      = "maxVel";
    static const juce::String curve       = "curve";
    static const juce::String fixedVel    = "fixedVel";     // velocity siempre = maxVel
    // MIDI
    static const juce::String note        = "note";
    static const juce::String channel     = "channel";
    static const juce::String noteLen     = "noteLen";      // ms
    // Reproductor propio
    static const juce::String sampler     = "sampler";      // Off / Reinforce / Replace
    static const juce::String sampleLevel = "sampleLevel";  // dB
    static const juce::String origLevel   = "origLevel";    // dB, nivel del audio original en Reinforce
    static const juce::String velSens     = "velSens";      // % cuánto sigue el volumen a la velocity
    static const juce::String align       = "align";        // retrasa el original para que coincida
    static const juce::String choke       = "choke";        // un golpe nuevo corta los anteriores
    // Salida
    static const juce::String outputTrim  = "outputTrim";
    static const juce::String bypass      = "bypass";
}

/** Una muestra cargada, ya al sample rate del host. */
struct TriggerSample
{
    juce::AudioBuffer<float> audio;     // 1 o 2 canales
};

/**
    Trigger de batería (nombre de trabajo): detecta los golpes del audio de entrada, mide
    su intensidad y los convierte en notas MIDI con velocity. Opcionalmente reproduce
    muestras propias (capas por velocity y round-robin) para reforzar o reemplazar el
    golpe original.

    Hilos:
    - Audio thread: detección, MIDI, voces del reproductor.
    - Message thread: carga de muestras (en un hilo de fondo, resultado por AsyncUpdater),
      UI. El audio thread agarra las muestras con un try-lock y nunca libera memoria: las
      muestras reemplazadas se guardan unos segundos en `retired`.
*/
class DecimulTriggerProcessor : public juce::AudioProcessor,
                                 private juce::AsyncUpdater,
                                 private juce::Timer
{
public:
    static constexpr int numSlots = 8;
    static constexpr int maxVoices = 16;
    static constexpr double maxSampleSeconds = 10.0;
    enum SamplerMode { samplerOff = 0, samplerReinforce = 1, samplerReplace = 2 };

    DecimulTriggerProcessor();
    ~DecimulTriggerProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
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

    // ---------------- Muestras (message thread) ----------------
    struct SlotInfo
    {
        juce::File file;
        juce::String name;
        bool loaded = false;
        bool loading = false;
        juce::String error;
    };

    void loadSample(int slot, const juce::File& file);
    void clearSample(int slot);
    SlotInfo getSlotInfo(int slot) const { return slotInfo[(size_t) slot]; }
    int getResidentSamples() const;

    void setVelocityRange(int slot, int lo, int hi);
    void getVelocityRange(int slot, int& lo, int& hi) const;

    // ---------------- Monitor de golpes (para la UI) ----------------
    struct ScopeFrame
    {
        float level = 0.0f;     // pico lineal de la señal de detección en el cuadro
        int velocity = 0;       // > 0 si en este cuadro se emitió un golpe
    };
    static constexpr int scopeFramesPerSecond = 200;

    /** Saca de la cola los cuadros nuevos (hilo de la UI). Devuelve cuántos copió. */
    int readScopeFrames(ScopeFrame* dest, int maxFrames);

    std::atomic<int> lastVelocity { 0 };
    std::atomic<int> hitCount { 0 };
    std::atomic<int> lastSlotPlayed { -1 };
    std::atomic<float> outputPeakDb { -100.0f };

    double getSampleRate() const { return currentSampleRate; }

private:
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void startLoading(int slot, const juce::File& file);
    void updateFilters(float lowHz, float highHz);
    void updateReportedLatency();

    struct LoadResult
    {
        int slot = 0;
        int token = 0;
        std::shared_ptr<TriggerSample> sample;
        juce::String error;
    };

    struct Voice
    {
        const float* left = nullptr;
        const float* right = nullptr;
        int length = 0;
        int pos = 0;
        float gain = 0.0f;
        int fadeLeft = 0;       // > 0: está saliendo (choke)
        int fadeTotal = 1;
        juce::uint64 order = 0;
        bool active = false;
    };

    struct PendingOff { juce::int64 due; int note; int channel; };

    double currentSampleRate = 44100.0;
    std::atomic<bool> reloadAllRequested { false };
    std::atomic<bool> shuttingDown { false };

    // --- Detección ---
    HitDetector detector;
    DetectionBiquad detHighpass, detLowpass;
    float lastLowCut = -1.0f, lastHighCut = -1.0f;

    // --- Retardo del audio original (modo align) ---
    static constexpr int delayBufferSize = 1 << 14;
    std::vector<float> delayL, delayR;
    int delayWrite = 0;
    int reportedLatency = -1;     // solo message thread

    // --- MIDI ---
    juce::int64 sampleCounter = 0;
    std::vector<PendingOff> pendingOffs;

    // --- Monitor ---
    juce::AbstractFifo scopeFifo { 4096 };
    std::array<ScopeFrame, 4096> scopeBuffer {};
    int scopeFrameSamples = 240;
    int scopeFrameLeft = 240;
    float scopeFramePeak = 0.0f;
    int scopeFrameVelocity = 0;

    // --- Muestras y voces ---
    mutable juce::SpinLock slotLock;
    std::array<std::shared_ptr<TriggerSample>, numSlots> slots;
    std::array<std::atomic<int>, numSlots> velLo, velHi;
    std::array<SlotInfo, numSlots> slotInfo;                 // solo message thread
    std::array<std::atomic<int>, numSlots> loadToken;
    std::array<juce::uint64, numSlots> lastUsedTick {};      // solo audio thread
    juce::uint64 tickCounter = 0;
    std::array<Voice, maxVoices> voices;

    struct Retired { std::shared_ptr<TriggerSample> sample; juce::uint32 atMs; };
    std::vector<Retired> retired;                            // solo message thread

    juce::CriticalSection resultsLock;
    std::vector<LoadResult> pendingResults;
    std::vector<std::pair<int, juce::File>> pendingLoadRequests;
    juce::ThreadPool loaderPool { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulTriggerProcessor)
};
