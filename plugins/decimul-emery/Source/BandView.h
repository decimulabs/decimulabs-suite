#pragma once

#include <DecimuLookAndFeel.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

/**
    Pantalla de Emery: los últimos ~2.5 segundos de las dos secciones, una arriba de la otra
    (SIBILANCIA y GOLPES). En cada una: el nivel de la banda que vigila el detector (relleno gris),
    la línea del umbral (color de acento) y, colgando desde arriba en rojo, cuántos dB se están
    bajando. Se ve de un vistazo si el umbral está bien puesto.
*/
class EmeryView : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    struct Section
    {
        std::array<float, 900> level {};       // dBFS de la banda
        std::array<float, 900> reduction {};   // dB que se bajan
        float thresholdDb = -30.0f;
        float maxReductionDb = 12.0f;
        bool on = true;
        juce::String title;
    };

    /** Suma cuadros nuevos al historial. */
    void pushFrames(const float* essLevel, const float* essRed, const float* popLevel, const float* popRed, int count);

    void setSection(bool pop, float thresholdDb, float maxReductionDb, bool on);
    void setTitles(const juce::String& ess, const juce::String& pop);

    void paint(juce::Graphics&) override;

private:
    static constexpr int historySize = 900;

    void paintSection(juce::Graphics&, juce::Rectangle<float> area, const Section&, juce::Colour accent) const;

    Section sections[2];
    int head = 0;        // próxima posición a escribir
    int filled = 0;
};
