#pragma once

#include <DecimuLookAndFeel.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

/**
    Pantalla de Vise: los últimos ~2.5 segundos. Arriba, en rojo, cuánto se está comprimiendo
    (colgando desde el borde, escala de 0 a 24 dB); abajo el nivel de entrada (relleno gris) y la
    línea del umbral. Se ve de un vistazo si el umbral está bien puesto y cuánto aprieta.
    En la esquina, la reducción actual en números.
*/
class ViseView : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    /** Suma cuadros nuevos (nivel de entrada en dBFS y reducción en dB positiva) al historial. */
    void pushFrames(const float* inDb, const float* grDb, int count);
    void setThreshold(float thresholdDb) { threshold = thresholdDb; }

    void paint(juce::Graphics&) override;

private:
    static constexpr int historySize = 900;

    std::array<float, (size_t) historySize> levels {};
    std::array<float, (size_t) historySize> reductions {};
    int head = 0;        // próxima posición a escribir
    int filled = 0;
    float threshold = -18.0f;
    float lastGr = 0.0f;
};
