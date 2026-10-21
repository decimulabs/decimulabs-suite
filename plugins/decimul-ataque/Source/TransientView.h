#pragma once

#include <DecimuLookAndFeel.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

/**
    Pantalla de Ataque: los últimos ~2.5 segundos. Arriba, el nivel de la señal (relleno) y,
    abajo, la ganancia que el plugin está aplicando (de -12 a +12 dB): hacia arriba (color de
    acento) donde acentúa un ataque o alarga una cola, hacia abajo (rojo) donde lo suaviza o lo
    corta. Se ve de un vistazo qué está tocando cada perilla.
*/
class AtaqueView : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    /** Suma cuadros nuevos (nivel en dBFS y ganancia en dB) al historial. */
    void pushFrames(const float* levelsDb, const float* gainsDb, int count);

    void paint(juce::Graphics&) override;

private:
    static constexpr int historySize = 900;

    std::array<float, (size_t) historySize> levels {};
    std::array<float, (size_t) historySize> gains {};
    int head = 0;        // próxima posición a escribir
    int filled = 0;
};
