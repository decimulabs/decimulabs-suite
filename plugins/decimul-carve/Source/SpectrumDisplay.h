#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include "CarveEngine.h"

/**
    Pantalla central de Carve: 32 columnas (una por banda). Desde abajo sube el nivel
    del sidechain en esa banda (gris); desde arriba cuelga el recorte que Carve le
    está aplicando a la pista principal (color de acento). El rango Low/High se ve
    como zonas apagadas a los costados.
*/
class CarveSpectrumDisplay : public juce::Component
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

private:
    std::array<float, carve::numBands> level {};
    std::array<float, carve::numBands> cut {};
    float rangeLow = 20.0f, rangeHigh = 20000.0f;
    bool scConnected = false;
    juce::Font monoFont { juce::FontOptions(12.0f) };
};
