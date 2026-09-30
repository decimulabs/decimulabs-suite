#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// IDs de parámetros, compartidos entre PluginProcessor.cpp y PluginEditor.cpp.
namespace ParamIDs
{
    static const juce::String inputTrim     = "inputTrim";
    static const juce::String gateAmount    = "gateAmount";
    static const juce::String gateMode      = "gateMode";
    static const juce::String driveAmount   = "driveAmount";
    static const juce::String driveMode     = "driveMode";
    static const juce::String toneTilt      = "toneTilt";
    static const juce::String toneMode      = "toneMode";
    static const juce::String toneMidFreq   = "toneMidFreq";
    static const juce::String toneEnabled   = "toneEnabled";
    static const juce::String cabEnabled    = "cabEnabled";
    static const juce::String cabMode       = "cabMode";
    static const juce::String modAmount     = "modAmount";
    static const juce::String modMode       = "modMode";
    static const juce::String modRate       = "modRate";
    static const juce::String modEnabled    = "modEnabled";
    static const juce::String spaceAmount   = "spaceAmount";
    static const juce::String spaceMode     = "spaceMode";
    static const juce::String spacePreDelay = "spacePreDelay";
    static const juce::String spaceEnabled  = "spaceEnabled";
    static const juce::String echoAmount    = "echoAmount";
    static const juce::String echoDivision  = "echoDivision";
    static const juce::String echoFeedback  = "echoFeedback";
    static const juce::String echoEnabled   = "echoEnabled";
    static const juce::String blend         = "blend";
    static const juce::String outputTrim    = "outputTrim";
    static const juce::String bypass        = "bypass";
}

/**
    DecimuLabs Guitars — multiefecto para guitarra eléctrica. Hermano de
    DecimuLabs Acoustic (mismo `shared/`), con módulos propios de guitarra en vez de
    los de voz/acústica. Ver SPEC.md para la cadena de señal completa y qué tan
    "real" es cada etapa del DSP (todo es REFERENCIA con juce::dsp, no la
    calidad final).

    Cadena de señal (revisada tras el primer testing, ver CHANGELOG en SPEC.md):
    Gate→Drive→Tone→Cab son el "core", SIEMPRE 100% activos (no los toca Blend) —
    como pedales en serie, no efectos que se puedan diluir. El dry tap para Blend se
    toma DESPUÉS del core, no antes: Blend solo mezcla ese core seco contra
    core+retornos de Space/Mod/Echo. Cab es on/off (`cabEnabled`), no un blend — o
    está 100% aplicado o no está, nunca "un poquito" (no tiene sentido sonoro mezclar
    parcialmente una simulación de gabinete). Tone/Mod/Space/Echo tienen además un
    booleano `...Enabled` propio (LED clickeable en la UI) para apagar el módulo
    entero sin tener que bajarle el fader a cero.
*/
class DecimulGuitarsProcessor : public juce::AudioProcessor
{
public:
    DecimulGuitarsProcessor();
    ~DecimulGuitarsProcessor() override;

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
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    /** Tap tempo manual — mismo patrón que Acoustic: método del processor, no
        parámetro APVTS (es un gesto de UI, no algo automatizable), para el fallback
        de tempo de Echo cuando no hay host (standalone). */
    void tapTempo();
    std::atomic<float> manualBpm { 120.0f };

    std::atomic<float> inputPeakDb { -100.0f };
    std::atomic<float> outputPeakDb { -100.0f };
    std::atomic<float> gainReductionDb { 0.0f };

private:
    double currentSampleRate = 44100.0;
    double lastTapTimeMs = 0.0;

    juce::dsp::Gain<float> inputGain, outputGain;

    // --- Gate: implementación propia (no juce::dsp::NoiseGate) ---
    // El NoiseGate de juce::dsp no tiene hold ni histéresis entre umbral de apertura
    // y cierre, y en testing real eso producía un "pop" audible en modo Fast: el
    // gate abría y cerraba varias veces seguidas justo en el ataque de una cuerda
    // (chattering). Esta versión mantiene el gate abierto un mínimo de tiempo
    // (`gateHoldSamples`) y usa un umbral de cierre más bajo que el de apertura
    // (histéresis de 6dB), como un gate de hardware real.
    static constexpr int gateMaxChannels = 2;
    float gateEnvelope[gateMaxChannels] = {};
    float gateGain[gateMaxChannels] = {};
    int gateHoldCounter[gateMaxChannels] = {};
    bool gateIsOpen[gateMaxChannels] = {};

    // --- Drive ---
    juce::dsp::WaveShaper<float> waveShaper;

    // --- Tone: tilt EQ (low-shelf + high-shelf opuestos) + peak fijo para el mid-sweep ---
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> toneLowShelf, toneHighShelf, toneMidPeak;

    // --- Cab: EQ fija en cadena, doble pasada por filtro (pendiente más pronunciada,
    // más parecido a la caída real de un speaker) — no es un cab-sim con IR ---
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> cabLowCut, cabHighCut, cabPresence;

    // --- Modulation: Chorus/Flanger comparten la misma clase con distintos
    // parámetros; Phaser es la clase dedicada de juce::dsp ---
    juce::dsp::Chorus<float> chorusFlanger;
    juce::dsp::Phaser<float> phaser;

    // --- Space: un solo reverb con pre-delay real (a diferencia de los dos de Acoustic) ---
    juce::dsp::Reverb space;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> spacePreDelayLine { 48000 };

    // --- Echo: delay con feedback, tempo-synced — mismo código que Acoustic ---
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 192000 };

    juce::AudioBuffer<float> dryBuffer, scratchBuffer, returnScratch;

    double getCurrentBpm() const;
    float computeEchoDelayMs(int divisionIndex, double bpm) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulGuitarsProcessor)
};
