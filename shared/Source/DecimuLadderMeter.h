#pragma once

#include "DecimuLookAndFeel.h"

/**
    Meter tipo ladder (varios segmentos LED verde/amarillo/rojo), sin ballistics ni
    peak-hold reales a propósito — es una representación visual simple del nivel en
    dB, no un medidor de precisión. Actualizalo desde un Timer con setLevelDb() +
    repaint().
*/
class DecimuLadderMeter : public juce::Component
{
public:
    void setLevelDb(float db) { levelDb = db; }
    void paint(juce::Graphics&) override;

private:
    float levelDb = -100.0f;
};
