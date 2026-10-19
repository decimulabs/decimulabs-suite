#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DecimuFx.h"

// IDs de parámetros, compartidos entre PluginProcessor.cpp y PluginEditor.cpp.
namespace ParamIDs
{
    static const juce::String inputTrim     = "inputTrim";
    static const juce::String gateAmount    = "gateAmount";
    static const juce::String gateMode      = "gateMode";
    static const juce::String driveAmount   = "driveAmount";
    static const juce::String driveMode     = "driveMode";
    static const juce::String driveEnabled  = "driveEnabled";
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
    "real" es cada etapa del DSP (v0.5: reverb, echo, modulación y filtros son
    propios, de shared/Source/DecimuFx.h; Drive y Cab siguen siendo simples).

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
    double getTailLengthSeconds() const override { return 5.0; } // Space Hall ~3.2 s

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

    /** Fija manualBpm directamente (casillero de texto en la UI, alternativa al Tap —
        "no solo el tap"). Mismo uso que tapTempo(): fallback sin host. Clampeado a
        20-300 BPM. */
    void setManualBpm(float bpm);

    std::atomic<float> manualBpm { 120.0f };

    /** Testing real (segunda ronda): el tempo manual/Tap "no hacía nada" en Echo
        porque, con host activo (Reaper), el tempo del host siempre tiene prioridad —
        correcto por diseño, pero confuso si el usuario quiere forzar un BPM propio con
        el host corriendo. Este toggle (UI, no parámetro APVTS) deja elegir: on = el
        host manda cuando da un tempo válido (comportamiento de antes); off = manualBpm/
        Tap mandan siempre, se ignora el host. Default: on. */
    std::atomic<bool> hostSyncEnabled { true };

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
    // v0.5: el detector del gate es ÚNICO y ligado (el nivel es el mayor de L y R), así los
    // dos canales abren y cierran juntos y la imagen no se mueve.
    float gateEnvelope = 0.0f;
    float gateGain = 0.0f;
    int gateHoldCounter = 0;
    bool gateIsOpen = false;

    // --- Drive ---
    // Oversampling 4x (dos pasadas half-band) SOLO para pre-gain+waveshaper: con hasta
    // +40dB de pre-gain antes del shaper, procesar a la sample rate original generaba
    // armónicos por encima de Nyquist que se "doblaban" (aliasing) — en testing real
    // esto sonaba a ring-mod/sintetizador en vez de fuzz, sobre todo en Tube/Fuzz
    // combinados con Cab (Combo/Stack). Subir y bajar siempre (no solo cuando Drive
    // está activo) mantiene la latencia del plugin constante.
    juce::dsp::Oversampling<float> driveOversampling { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };

    // v0.5: compensación de nivel del Drive CALIBRADA (antes 1/sqrt(preGain), una fórmula a
    // ojo que dejaba el volumen -18 dB abajo al 100%). Tabla en dB, por modo y por cantidad
    // (21 puntos de 0 a 100 %): con una senoidal de referencia (-15 dBFS de pico) la salida
    // tiene el mismo nivel RMS que la entrada. Se calcula al construir.
    static constexpr int driveTablePoints = 21;
    float driveMakeupDb[3][driveTablePoints] = {};
    void computeDriveTable();
    float driveMakeupFor(int mode, float amount01) const;
    float drivePreGainPrev = 1.0f, drivePostGainPrev = 1.0f; // rampas entre bloques

    // --- Tone: tilt EQ (low-shelf + high-shelf opuestos) + peak fijo para el mid-sweep ---
    // (DSP propio, ver shared/Source/DecimuFx.h: mismas fórmulas que juce::dsp::IIR pero
    // suavizadas)
    decimu::fx::EqBand toneLowShelf, toneHighShelf, toneMidPeak;

    // --- Cab: EQ fija en cadena, doble pasada por filtro (pendiente más pronunciada,
    // más parecido a la caída real de un speaker) — no es un cab-sim con IR ---
    decimu::fx::EqBand cabLowCut[2], cabHighCut[2], cabPresence;

    // --- Modulation: chorus/flanger estéreo propio (el derecho va desfasado) y phaser de
    // 6 etapas propio ---
    decimu::fx::ChorusFlanger chorusFlanger;
    decimu::fx::Phaser phaser;
    float modMixPrev = 0.0f;

    // --- Space: un solo reverb FDN con pre-delay suavizado (a diferencia de los dos de Acoustic) ---
    decimu::fx::FdnReverb space;
    decimu::fx::SmoothDelay spacePreDelay;

    // --- Echo: delay con feedback filtrado, tempo-synced — mismo código que Acoustic ---
    decimu::fx::StereoEcho echo;

    juce::SmoothedValue<float> blendSmoothed;

    juce::AudioBuffer<float> dryBuffer, scratchBuffer, returnScratch;

    double getCurrentBpm() const;
    float computeEchoDelayMs(int divisionIndex, double bpm) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulGuitarsProcessor)
};
