#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include "HitDetector.h"
#include "SpectralFingerprint.h"

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
    // Huella (rechazo de bleed)
    static const juce::String fingerprint = "fingerprint";  // rechazar los golpes que no se parecen a la huella
    static const juce::String match       = "match";        // parecido mínimo (0..1)
    static const juce::String fpWindow    = "fpWindow";     // ms: ventana de análisis de la huella
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
    // Captura
    static const juce::String capLength   = "capLength";    // ms: largo máximo de cada golpe grabado
    static const juce::String capLayers   = "capLayers";    // capas de velocity al crear las muestras
    static const juce::String capRR       = "capRR";        // alternancias (round-robin) por capa
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

    - v0.3 **Huella**: aprende la forma espectral de los golpes del instrumento y
      rechaza los que no se le parecen (el bleed de otros instrumentos).
    - v0.4 **Captura**: graba cada golpe aceptado y arma con ellos muestras repartidas
      en capas por velocity y round-robin, que carga en el reproductor.

    Hilos:
    - Audio thread: detección, MIDI, voces del reproductor, aprendizaje y grabación.
    - Message thread: carga de muestras (en un hilo de fondo, resultado por AsyncUpdater),
      armado y escritura de las muestras capturadas, UI. El audio thread agarra las
      muestras con un try-lock y nunca libera memoria: las muestras reemplazadas se
      guardan unos segundos en `retired`.
*/
class DecimulTriggerProcessor : public juce::AudioProcessor,
                                 private juce::AsyncUpdater,
                                 private juce::Timer
{
public:
    static constexpr int numSlots = 8;
    static constexpr int maxVoices = 16;
    static constexpr int maxTakes = 64;
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

    // ---------------- Huella (message thread) ----------------
    /** Empieza a aprender: la huella pasa a ser el promedio de los golpes que lleguen
        hasta `stopLearning()` (o hasta 24 golpes). Mientras aprende no se rechaza nada. */
    void startLearning() { learnCommand.store(1); }
    void stopLearning() { learnCommand.store(2); }
    void clearFingerprint() { learnCommand.store(3); }

    bool isLearning() const { return learning.load(); }
    int getLearnedHits() const { return learnedHits.load(); }
    bool hasFingerprint() const { return templateValid.load(); }
    int getFingerprintHits() const { return templateHits.load(); }
    void getFingerprint(FingerprintBank::Vector& out) const;
    bool getLastHitVector(FingerprintBank::Vector& out) const;

    std::atomic<float> lastSimilarity { -1.0f };   // parecido del último golpe con la huella (-1 = sin dato)
    std::atomic<int> rejectedCount { 0 };

    // ---------------- Captura (message thread) ----------------
    void setCapturing(bool shouldCapture);
    bool isCapturing() const { return capturing.load(); }
    int getTakeCount() const { return takeCount.load(); }
    void clearTakes();
    void getTakeVelocities(std::vector<int>& out) const;

    struct ExportResult
    {
        int files = 0;
        juce::File folder;
        juce::String message;
    };

    /** Arma las muestras con los golpes capturados: las reparte en `layers` capas de
        velocity con `roundRobin` alternancias cada una (layers * roundRobin <= 8), las
        escribe como WAV de 24 bits y las carga en el reproductor con sus rangos. Los
        archivos van a `rootFolder` (por defecto Documentos/DecimuLabs Trigger). */
    ExportResult exportTakes(const juce::String& baseName, int layers, int roundRobin,
                             const juce::File& rootFolder = {});

    // ---------------- Monitor de golpes (para la UI) ----------------
    struct ScopeFrame
    {
        float level = 0.0f;     // pico lineal de la señal de detección en el cuadro
        int velocity = 0;       // > 0: golpe emitido; < 0: golpe rechazado por la huella
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
    void commitFingerprint();
    int decisionSamplesFor(int scanSamples, bool fingerprintOn, int fpWindowSamples) const;

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

    struct Take
    {
        std::vector<float> l, r;
        int length = 0;
        int preRoll = 0;
        int velocity = 0;
        float peak = 0.0f;
    };

    double currentSampleRate = 44100.0;
    std::atomic<bool> reloadAllRequested { false };
    std::atomic<bool> shuttingDown { false };

    // --- Detección ---
    HitDetector detector;
    DetectionBiquad detHighpass, detLowpass;
    float lastLowCut = -1.0f, lastHighCut = -1.0f;

    // --- Huella ---
    FingerprintBank bank;
    std::atomic<int> learnCommand { 0 };                      // 1 empezar, 2 cerrar, 3 borrar
    std::atomic<bool> learning { false };
    std::atomic<int> learnedHits { 0 };
    std::array<std::atomic<float>, FingerprintBank::numBands> templateVec;
    std::atomic<bool> templateValid { false };
    std::atomic<int> templateHits { 0 };
    std::array<std::atomic<float>, FingerprintBank::numBands> lastVec;
    std::atomic<bool> lastVecValid { false };
    std::array<double, FingerprintBank::numBands> learnSum {};   // solo audio thread
    int learnCount = 0;

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

    // --- Captura ---
    static constexpr int capRingSize = 1 << 13;
    std::vector<Take> takes;                                 // se reserva una vez (message thread)
    int takeCapacity = 0;
    std::atomic<bool> capturing { false };
    std::atomic<int> takeCount { 0 };
    std::atomic<int> blockCounter { 0 };
    std::vector<float> capRingL, capRingR;
    int capWrite = 0;
    int recTake = -1;                                        // toma en grabación (audio thread)
    int recQuiet = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulTriggerProcessor)
};
