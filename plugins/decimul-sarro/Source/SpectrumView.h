#pragma once

#include "SarroDsp.h"
#include <DecimuLookAndFeel.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

/**
    Pantalla de Sarro: el espectro de la entrada (relleno gris), el espectro después de bajar los
    picos (línea de color) y, colgando desde arriba en rojo, cuántos dB se bajan en cada frecuencia.
    Eje de frecuencia logarítmico de 20 Hz a 20 kHz. Las franjas oscuras de los costados son lo que
    queda fuera de LOW FOCUS y HIGH FOCUS (ahí no actúa).
*/
class SpectrumView : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    static constexpr int kPoints = sarro::ResonanceSuppressor::kDisplay;

    /** Toma una lectura nueva del motor (espectro dBFS y reducción dB positiva). */
    void update(const float* specDb, const float* redDb);
    void setFocus(float lowHz, float highHz) { focusLow = lowHz; focusHigh = highHz; }

    void paint(juce::Graphics&) override;

private:
    std::array<float, (size_t) kPoints> spec {}, red {};
    float focusLow = 80.0f, focusHigh = 16000.0f;
    bool hasData = false;

    float xOfFreq(juce::Rectangle<float> plot, float hz) const;
};
