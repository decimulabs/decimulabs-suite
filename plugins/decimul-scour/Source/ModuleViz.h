#pragma once

#include "PluginProcessor.h"
#include <DecimuLookAndFeel.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

/**
    Visualizador de un módulo de Scour (el dibujo de arriba de cada columna):
      NOISE     la forma de onda del ruido que se genera (con sus chasquidos y ráfagas)
      WOBBLE    una curva lenta con cuánto se mueve el retardo (el temblor de tono)
      DISTORT   la curva de transferencia (entrada contra salida) de la forma elegida
      DIGITAL   una onda sinusoidal escalonada por la frecuencia de muestreo y los bits
      SPACE     la cola del reverb: predelay y caída
      MAGNETIC  el historial de nivel de la cinta, con las caídas en rojo
    DISTORT, DIGITAL y SPACE se dibujan con los valores de las perillas; los otros tres con datos
    en vivo del motor (Frame). Si el módulo está apagado el dibujo se apaga.
*/
class ModuleViz : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    enum class Kind { noise, wobble, distort, digital, space, magnetic };

    ModuleViz(Kind kind, juce::AudioProcessorValueTreeState& state);

    void pushFrame(const DecimulScourProcessor::Frame& frame);
    void paint(juce::Graphics&) override;

private:
    static constexpr int historySize = 200;

    float param(const juce::String& id) const { return apvts.getRawParameterValue(id)->load(); }
    float effectiveAmount(const juce::String& amountID, const juce::String& onID) const;

    void paintNoise(juce::Graphics&, juce::Rectangle<float> area, juce::Colour c) const;
    void paintWobble(juce::Graphics&, juce::Rectangle<float> area, juce::Colour c) const;
    void paintDistort(juce::Graphics&, juce::Rectangle<float> area, juce::Colour c) const;
    void paintDigital(juce::Graphics&, juce::Rectangle<float> area, juce::Colour c) const;
    void paintSpace(juce::Graphics&, juce::Rectangle<float> area, juce::Colour c) const;
    void paintMagnetic(juce::Graphics&, juce::Rectangle<float> area, juce::Colour c) const;

    Kind kind;
    juce::AudioProcessorValueTreeState& apvts;

    // historiales (los módulos con datos en vivo)
    std::array<float, (size_t) historySize> histA {}, histB {};    // ruido: mínimo y máximo / wobble y cinta: valor en histA
    int head = 0, filled = 0;
    int decimate = 1, decimateCount = 0;
    float accMin = 0.0f, accMax = 0.0f, accGain = 1.0f, accDelay = 0.0f;
    mutable float noiseScale = 1.0e-4f;
};
