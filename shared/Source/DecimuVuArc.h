#pragma once

#include "DecimuLookAndFeel.h"

/**
    VU meter en arco (semicírculo, aguja) dentro de una ventanita tipo vidrio — el
    gesto decorativo-pero-vivo del header de cada plugin de la suite. Alimentalo con
    el nivel real de salida (en dB) desde un Timer; no es un VU de precisión con
    balística real, es una lectura aproximada con onda visual de "medidor analógico".
*/
class DecimuVuArc : public juce::Component
{
public:
    void setLevelDb(float db) { levelDb = db; }
    void paint(juce::Graphics&) override;

private:
    float levelDb = -100.0f;
};
