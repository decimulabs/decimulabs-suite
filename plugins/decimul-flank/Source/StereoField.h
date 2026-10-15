#pragma once

#include <DecimuLookAndFeel.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

/**
    Vista del campo estéreo de Flank:
    - arriba, un goniómetro (cada punto es un par L/R de la salida: una voz mono es una
      línea vertical; cuanto más se abre hacia los costados, más ancha);
    - en el medio, una línea izquierda-derecha con la voz al centro y las dos copias en
      la posición que les da `Spread`;
    - abajo, la correlación entre canales y cuánto cae el nivel al pasar a mono.
*/
class FlankStereoField : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    struct State
    {
        float spread = 0.5f;          // 0..1
        float offsetMs = 0.0f;        // diferencia de tiempo entre las copias
        float space = 0.0f;           // 0..1
        float correlation = 1.0f;     // -1..+1
        float monoLossDb = 0.0f;
        bool monoCheck = false;
    };

    /** Suma pares (L, R) intercalados al rastro del goniómetro. */
    void pushPoints(const float* interleaved, int pairs);
    void setState(const State& s);

    void paint(juce::Graphics&) override;

private:
    static constexpr int maxPoints = 700;

    std::array<float, (size_t) maxPoints * 2> points {};
    int pointCount = 0;
    int pointHead = 0;
    State state;
};
