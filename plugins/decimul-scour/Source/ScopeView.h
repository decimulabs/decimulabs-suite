#pragma once

#include <DecimuLookAndFeel.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

/**
    Pantalla de Scour: la forma de onda de la salida de los últimos ~1.6 segundos (mínimo y máximo
    cada 2.7 ms). Se ven de un vistazo el temblor del wobble, las caídas de volumen de la cinta,
    los chasquidos del ruido y la cola del reverb.
*/
class ScourScope : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    void pushFrames(const float* mins, const float* maxs, int count);
    void paint(juce::Graphics&) override;

private:
    static constexpr int historySize = 600;

    std::array<float, (size_t) historySize> lo {}, hi {};
    int head = 0;        // próxima posición a escribir
    int filled = 0;
};
