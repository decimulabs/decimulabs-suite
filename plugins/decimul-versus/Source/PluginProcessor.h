#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include "BandAverager.h"
#include "LoudnessAnalyzer.h"
#include "ReferenceTrack.h"
#include "TruePeakMeter.h"

namespace ParamIDs
{
    static const juce::String refActive   = "refActive";   // false = A, true = B (fuera de ABX)
    static const juce::String slot        = "slot";        // lado B: slot 1..8
    static const juce::String sourceA     = "sourceA";     // lado A: 0 = mezcla del DAW, 1..8 = slot
    static const juce::String abxMode     = "abxMode";     // test ciego ABX
    static const juce::String matchMode   = "matchMode";   // Auto (LUFS) / Off
    static const juce::String trim        = "trim";        // trim manual del lado B, dB
    static const juce::String monitor     = "monitor";     // Stereo / Mono / Side
    static const juce::String band        = "band";        // Full / Low / Mid / High
    static const juce::String followHost  = "followHost";
    static const juce::String loopEnabled = "loopEnabled";
    static const juce::String outputTrim  = "outputTrim";
    static const juce::String bypass      = "bypass";
}

/**
    DecimuLabs Versus (AB) — comparador A/B de mezclas y masterings contra canciones de
    referencia. Va al final de la cadena del master: deja pasar tu mezcla (A) o, con
    un click, reproduce una referencia (B) con el volumen igualado por LUFS, para que
    la comparación sea justa (lo más fuerte siempre suena "mejor"). Ver SPEC.md.

    Dos voces: el lado A es la mezcla del DAW o cualquier slot; el lado B es un slot.
    Cuando A también es un slot, los dos comparten el cabezal (misma posición), para
    comparar versiones de un mismo tema sin desalinearlas. El test ciego ABX usa estas
    mismas dos voces.

    Hilos:
    - Audio thread: processBlock (reproducción, crossfade, medición de la mezcla).
    - Message thread: carga/descarga de referencias, UI, y el Timer que mantiene
      residente en memoria el audio de los slots en uso. La decodificación y el
      análisis corren en un hilo de fondo (ThreadPool de 1) y el resultado se aplica
      en el message thread vía AsyncUpdater.
    - Una referencia nunca la libera el audio thread: el intercambio de slots se hace
      bajo `slotLock` (SpinLock) y el audio thread solo usa try-lock (si no lo
      consigue, ese bloque suena la mezcla).
*/
class DecimulVersusProcessor : public juce::AudioProcessor,
                                private juce::AsyncUpdater,
                                private juce::Timer
{
public:
    static constexpr int numSlots = 8;
    static constexpr int maxResidentSlots = 4;   // slots con el audio decodificado en memoria

    DecimulVersusProcessor();
    ~DecimulVersusProcessor() override;

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

    // ---------------- API para la UI (message thread) ----------------
    struct SlotInfo
    {
        juce::String name;
        juce::File file;
        bool loaded = false;
        bool loading = false;
        juce::String error;
    };

    void loadReference(int slot, const juce::File& file);
    void clearReference(int slot);
    SlotInfo getSlotInfo(int slot) const { return slotInfo[(size_t) slot]; }
    std::shared_ptr<const ReferenceTrack> getTrack(int slot) const;
    int getResidentCount() const;

    int getSelectedSlot() const;          // lado B
    int getSourceASlot() const;           // lado A: -1 = mezcla, 0..7 = slot
    double getSampleRate() const { return currentSampleRate; }

    void setUserPlaying(bool shouldPlay) { userPlaying.store(shouldPlay); }
    bool isUserPlaying() const { return userPlaying.load(); }
    void seekSeconds(double seconds);
    void setLoopRegion(int slot, double startSeconds, double endSeconds);
    void getLoopRegion(int slot, double& startSeconds, double& endSeconds) const;
    double getPlayheadSeconds() const;
    bool isReferenceRunning() const { return referenceRunning.load(); }

    void resetMixAnalysis() { resetMixRequested.store(true); }

    /** Busca en la referencia (lado B) el tramo que más se parece, en balance tonal y
        evolución de nivel, a los últimos segundos de la mezcla, y salta ahí. Heurística:
        no detecta la estructura musical, compara el "perfil sonoro". Devuelve false (y
        explica en `report`) si no hay datos suficientes. */
    bool alignToMix(juce::String& report);

    // ---------------- Test ciego ABX ----------------
    void abxSetListen(int which) { abxListen.store(juce::jlimit(0, 2, which)); }   // 0 = A, 1 = B, 2 = X
    int abxGetListen() const { return abxListen.load(); }
    void abxAnswer(bool guessXIsB);
    void abxReset();
    int abxGetCorrect() const { return abxCorrect.load(); }
    int abxGetTotal() const { return abxTotal.load(); }
    void abxForceXForTesting(bool xIsB) { abxXIsB.store(xIsB); }

    // ---------------- Lecturas para los medidores de la UI ----------------
    std::atomic<float> inputPeakDb { -100.0f };
    std::atomic<float> outputPeakDb { -100.0f };

    std::atomic<float> mixIntegratedLufs { -100.0f };
    std::atomic<float> mixShortTermLufs { -100.0f };
    std::atomic<float> mixLoudnessRange { 0.0f };
    std::atomic<float> mixTruePeakDb { -100.0f };
    std::atomic<float> mixWidthPct { 0.0f };
    std::atomic<float> mixCorrelation { 1.0f };
    std::atomic<bool> mixBandsValid { false };
    std::array<std::atomic<float>, VersusBands::numBands> mixBandDb;       // promedio
    std::array<std::atomic<float>, VersusBands::numBands> mixLiveDb;       // último frame
    std::atomic<bool> mixLiveActive { false };

    std::atomic<float> appliedRefGainDb { 0.0f };    // lado B
    std::atomic<float> appliedGainADb { 0.0f };      // lado A (si es un slot)
    std::atomic<bool> refGainLimitedByPeak { false };

private:
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void startLoading(int slot, const juce::File& file, bool keepPosition);
    void maintainResidency();

    struct LoadResult
    {
        int slot = 0;
        int token = 0;
        bool keepPosition = false;
        std::shared_ptr<ReferenceTrack> track;
        juce::String error;
    };

    double currentSampleRate = 44100.0;
    std::atomic<bool> reloadAllRequested { false };
    std::atomic<bool> shuttingDown { false };

    // --- Referencias ---
    mutable juce::SpinLock slotLock;
    std::array<std::shared_ptr<ReferenceTrack>, numSlots> slots;
    std::array<SlotInfo, numSlots> slotInfo;                      // solo message thread
    std::array<std::atomic<int>, numSlots> loadToken;
    std::array<juce::uint64, numSlots> lastUsed {};               // solo message thread
    juce::uint64 useTick = 0;
    juce::CriticalSection resultsLock;
    std::vector<LoadResult> pendingResults;
    std::vector<std::pair<int, juce::File>> pendingLoadRequests;  // setStateInformation puede venir de cualquier hilo
    juce::ThreadPool loaderPool { 1 };

    // --- Transporte / posición (el audio thread es dueño de currentPos) ---
    std::atomic<bool> userPlaying { false };
    std::atomic<bool> referenceRunning { false };
    std::atomic<juce::int64> seekRequest { -1 };
    std::atomic<juce::int64> playheadSamples { 0 };
    std::atomic<int> resetPosSlot { -1 };
    std::array<std::atomic<juce::int64>, numSlots> slotPos;
    std::array<std::atomic<float>, numSlots> loopStartSec, loopEndSec;

    juce::int64 currentPos = 0;
    int currentSlot = 0;                // lado B
    int prevSlot = -1;
    juce::int64 prevPos = 0;
    int xfadeLeft = 0, xfadeTotal = 1;
    int currentSrcA = -1;               // lado A: -1 = mezcla
    int declickLeft = 0, declickTotal = 1;
    float lastAL = 0.0f, lastAR = 0.0f;
    bool wasPlaying = false;

    // --- ABX ---
    std::atomic<int> abxListen { 0 };
    std::atomic<bool> abxXIsB { false };
    std::atomic<int> abxCorrect { 0 }, abxTotal { 0 };
    juce::Random abxRandom;

    // --- Audio ---
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> abMix, gainA, gainB;
    juce::AudioBuffer<float> sideA, sideB;
    std::vector<float> monoScratch;

    juce::dsp::Gain<float> outputGain;
    juce::dsp::LinkwitzRileyFilter<float> lowLP, midHP, midLP, highHP;
    int lastBand = 0;

    // --- Análisis de la mezcla (audio thread) ---
    LoudnessAnalyzer mixLoudness;
    BandAverager mixBands;
    TruePeakMeter mixTruePeak;
    std::atomic<bool> resetMixRequested { false };
    double accLL = 0.0, accRR = 0.0, accLR = 0.0, accM = 0.0, accS = 0.0;
    float mixPeakLin = 0.0f, mixTruePeakLin = 0.0f;
    int publishCounter = 0;

    // Últimos frames de espectro de la mezcla (para alinear secciones).
    static constexpr int featureRingFrames = 128;
    juce::SpinLock featureLock;
    std::vector<float> featureRing;     // featureRingFrames * numBands
    int featureHead = 0, featureCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulVersusProcessor)
};
