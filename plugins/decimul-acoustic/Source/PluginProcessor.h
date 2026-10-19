#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DecimuFx.h"

// IDs de parámetros, compartidos entre PluginProcessor.cpp y PluginEditor.cpp (evita
// duplicar los strings a mano en dos archivos, que es receta segura para un typo).
namespace ParamIDs
{
    static const juce::String inputTrim          = "inputTrim";
    static const juce::String lowGain            = "lowGain";
    static const juce::String lowMode            = "lowMode";
    static const juce::String lowEnabled         = "lowEnabled";
    static const juce::String highGain           = "highGain";
    static const juce::String highMode           = "highMode";
    static const juce::String highEnabled        = "highEnabled";
    static const juce::String compAmount         = "compAmount";
    static const juce::String compMode           = "compMode";
    static const juce::String compEnabled        = "compEnabled";
    static const juce::String spaceAMix          = "spaceAMix";
    static const juce::String spaceAType         = "spaceAType";
    static const juce::String spaceAPreDelay     = "spaceAPreDelay";
    static const juce::String spaceAEnabled      = "spaceAEnabled";
    static const juce::String spaceBMix          = "spaceBMix";
    static const juce::String spaceBType         = "spaceBType";
    static const juce::String spaceBPreDelay     = "spaceBPreDelay";
    static const juce::String spaceBEnabled      = "spaceBEnabled";
    static const juce::String echoMix            = "echoMix";
    static const juce::String echoDivision       = "echoDivision";
    static const juce::String echoFeedback       = "echoFeedback";
    static const juce::String echoEnabled        = "echoEnabled";
    static const juce::String blend              = "blend";
    static const juce::String outputTrim         = "outputTrim";
    static const juce::String limThreshold    = "limThreshold";
    static const juce::String bypass             = "bypass";
}

/**
    DecimuLabs Acoustic — multiefecto para voz/instrumentos acústicos.
    Ver plugins/decimul-acoustic/SPEC.md para la spec completa de parámetros
    y la cadena de señal.

    Estado actual ("hito de prueba"): el editor (ver PluginEditor.h/.cpp) es un layout
    mega básico inspirado en el banco de faders del Waves CLA Unplugged (sliders
    verticales + selector de carácter/tipo arriba de cada uno + meters a los costados),
    con estética propia — no copia su diseño gráfico, colores ni branding.

    v0.5: el DSP ya no es el "de referencia" de juce::dsp. EQ (shelves suavizados),
    compresor (ligado entre canales), reverbs (FDN, dos variantes) y echo (con filtro en
    el lazo) son propios y viven en shared/Source/DecimuFx.h, con tests en
    tests/EngineTest.cpp (ver "v0.5" en SPEC.md). Se quitaron los 3 parámetros que no
    hacían nada (Oversample y los dos Sync de pre-delay).

    v0.2 (generalizado desde el testing real de DecimuLabs Guitars, ver su SPEC.md):
    - Blend reubicado: Low/High/Dynamics son "core", SIEMPRE 100% activos — el dry tap
      se toma DESPUÉS del core (no antes), así Blend solo mezcla dry vs. los retornos
      de Space A/Space B/Echo, nunca diluye el EQ/compresor.
    - Low/High/Dynamics/Space A/Space B/Echo suman cada uno un booleano `...Enabled`
      con LED clickeable en la UI — apaga el módulo entero sin bajar el fader a cero.
    - Reverbs (Space A/B) con headroom interno (wetLevel tope 0.85) para no irse a rojo
      tan fácil con el fader alto.
    - Output: piso bajado de -24dB a -60dB.
    - Casillero de texto para escribir el BPM manual a mano, alternativa al botón Tap
      (mismo uso: fallback sin host).
*/
class DecimulAcousticProcessor : public juce::AudioProcessor
{
public:
    DecimulAcousticProcessor();
    ~DecimulAcousticProcessor() override;

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
    double getTailLengthSeconds() const override { return 7.0; } // reverbs (Vast llega a ~6.5 s)

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    /** Tap tempo manual (valor agregado #4 del SPEC): la UI llama a esto en cada click
        de un botón "Tap"; calcula el BPM a partir del intervalo entre clicks y lo deja
        en manualBpm para que Echo lo use cuando no hay tempo de host (standalone) o el
        usuario prefiera fijar uno propio. No es un parámetro APVTS automatizable a
        propósito: es un gesto transitorio de UI, no un valor que tenga sentido
        automatizar desde una DAW. Se persiste manualmente en el estado. */
    void tapTempo();

    /** Fija manualBpm directamente (casillero de texto en la UI, alternativa al Tap
        pedido en testing — "no solo el tap"). Mismo uso que tapTempo(): fallback sin
        host, no reemplaza el tempo del host cuando este existe (esa prioridad es
        correcta y ya se confirmó en testing con Guitars). Clampeado a 20-300 BPM. */
    void setManualBpm(float bpm);

    std::atomic<float> manualBpm { 120.0f };

    /** Generalizado desde Guitars (2da ronda de testing real): el tempo manual/Tap
        "no hacía nada" en Echo porque, con host activo, el tempo del host siempre
        tiene prioridad — correcto por diseño, pero confuso si el usuario quiere forzar
        un BPM propio con el host corriendo. Toggle de UI (no parámetro APVTS): on =
        el host manda cuando da un tempo válido (comportamiento de antes); off =
        manualBpm/Tap mandan siempre. Default: on. */
    std::atomic<bool> hostSyncEnabled { true };

    // Meters de solo lectura para la UI (input/output peak, gain reduction, correlación).
    // Se actualizan en processBlock() y se leen desde el editor con un Timer.
    std::atomic<float> inputPeakDb { -100.0f };
    std::atomic<float> outputPeakDb { -100.0f };
    std::atomic<float> gainReductionDb { 0.0f };
    std::atomic<float> stereoCorrelation { 1.0f };

private:
    double currentSampleRate = 44100.0;
    double lastTapTimeMs = 0.0;

    // --- Input / Output: gain suavizado (evita zipper noise en automatización) ---
    juce::dsp::Gain<float> inputGain, outputGain;

    // --- Tone: shelving de 2 bandas, 3 "carácter" por banda (ver .cpp para las
    // frecuencias elegidas por modo). DSP propio (shared/Source/DecimuFx.h): mismas
    // fórmulas que juce::dsp::IIR pero con frecuencia/ganancia suavizadas, sin escalones
    // al mover la perilla y sin crear objetos en el hilo de audio. ---
    decimu::fx::EqBand lowBand, highBand;

    // --- Dynamics: compresor propio con detector ligado entre canales (v0.5) ---
    decimu::fx::LinkedCompressor compressor;

    // --- Space A / Space B: dos reverbs FDN en paralelo (variantes distintas), cada uno
    // con su pre-delay suavizado ---
    decimu::fx::FdnReverb reverbA, reverbB;
    decimu::fx::SmoothDelay preDelayA, preDelayB;

    // --- Echo: delay con feedback filtrado, tempo-synced (host BPM o manualBpm) ---
    decimu::fx::StereoEcho echo;

    juce::SmoothedValue<float> blendSmoothed;

    // Limiter de salida (v0.6): última etapa, después de Output; una sola perilla (Threshold).
    // Tiene 1 ms de anticipación (latencia informada al host).
    decimu::fx::Limiter limiter;

    // Buffers de trabajo (dry tap + acumulador de retornos wet), redimensionados en
    // prepareToPlay. Ver processBlock: dry = señal post Input Trim, pre Tone/Dynamics;
    // wet = Tone+Dynamics ("core") + retorno de Space A + retorno de Space B + retorno
    // de Echo; Blend hace crossfade dry/wet antes de Output Trim.
    juce::AudioBuffer<float> dryBuffer, scratchBuffer, reverbScratch;

    double getCurrentBpm() const;
    float computeEchoDelayMs(int divisionIndex, double bpm) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulAcousticProcessor)
};
