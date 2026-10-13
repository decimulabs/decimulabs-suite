#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <memory>
#include <vector>

/**
    La curva de "bombeo": cuánto volumen hay (0 = silencio, 1 = sin atenuar) a lo largo de
    UN ciclo (un beat, un compás, lo que diga el Rate). El eje x va de 0 a 1 y es la
    posición dentro del ciclo; x = 0 es el instante del "kick".

    Es una poligonal de puntos con una curvatura por tramo (`bend`), no una spline: cada
    tramo sale de un punto y llega al siguiente, y `bend` le da forma:
        bend = 0  recta
        bend > 0  arranca despacio y termina rápido (convexa)
        bend < 0  arranca rápido y se asienta (cóncava) — la típica recuperación del pump
    El primer punto está siempre en x = 0 y el último en x = 1.
*/
struct CurvePoint
{
    float x = 0.0f;
    float y = 1.0f;
    float bend = 0.0f;   // forma del tramo que SALE de este punto (el último la ignora)
};

class PumpCurve
{
public:
    static constexpr int tableSize = 1024;     // la tabla tiene tableSize + 1 entradas
    static constexpr float minSpacing = 0.004f;

    PumpCurve();

    std::vector<CurvePoint> points;

    float evaluate(float x) const;
    void fillTable(float* table) const;        // tableSize + 1 valores

    /** Ordena, acota a [0,1] y fija los extremos en x = 0 y x = 1. */
    void sanitize();

    std::unique_ptr<juce::XmlElement> toXml() const;
    static PumpCurve fromXml(const juce::XmlElement& xml);

    static int numPresets();
    static juce::String presetName(int index);
    static PumpCurve preset(int index);
};
