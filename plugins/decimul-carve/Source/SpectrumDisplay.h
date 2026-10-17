#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include "CarveEngine.h"

/**
    Pantalla central de Carve: 32 columnas (una por banda). Desde abajo sube el nivel
    del sidechain en esa banda (gris); desde arriba cuelga el recorte que Carve le
    está aplicando a la pista principal (color de acento). El rango Low/High se ve
    como zonas apagadas a los costados.

    Escalas en dB: a la izquierda la del recorte (0 a -24 dB, hacia abajo desde arriba),
    a la derecha la del sidechain (0 a -60 dBFS, hacia arriba desde abajo). Arriba a la
    derecha dice cuánto es el recorte máximo y en qué frecuencia; al pasar el mouse por
    una banda muestra sus valores exactos.
*/
class CarveSpectrumDisplay : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    void setData(const std::array<float, carve::numBands>& levelsDb,
                 const std::array<float, carve::numBands>& cutsDb)
    {
        level = levelsDb;
        cut = cutsDb;
    }

    void setRange(float lowHz, float highHz) { rangeLow = lowHz; rangeHigh = highHz; }
    void setSidechainConnected(bool connected) { scConnected = connected; }
    void setMonoFont(const juce::Font& f) { monoFont = f; }

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plotArea() const;

    std::array<float, carve::numBands> level {};
    std::array<float, carve::numBands> cut {};
    float rangeLow = 20.0f, rangeHigh = 20000.0f;
    bool scConnected = false;
    int hoverBand = -1;
    juce::Font monoFont { juce::FontOptions(12.0f) };
};
