#pragma once

#include "DecimuLookAndFeel.h"

/**
    Selector de N opciones apiladas verticalmente, cada una con un punto de color +
    texto (click para elegir) — el reemplazo de decimulabs para un ComboBox cuando
    querés que las 2-4 opciones estén siempre visibles, tipo "Warm/Deep/Air-Cut".

    No se sincroniza solo con un AudioParameterChoice (no hay una Attachment de JUCE
    para este tipo de componente) — el que lo usa arma la lista de opciones,
    escucha `onChange` para escribir el parámetro, y llama a `setSelectedIndex()`
    periódicamente (p. ej. desde un Timer a 30Hz) para reflejar cambios de host/preset.
    Ver plugins/decimul-acoustic/Source/PluginEditor.cpp para el patrón completo.
*/
class DecimuLedStackSelector : public juce::Component,
                               public juce::SettableTooltipClient
{
public:
    void setOptions(const juce::StringArray& items);
    void setSelectedIndex(int index);
    int getSelectedIndex() const { return selected; }

    std::function<void(int)> onChange;

    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    juce::StringArray options;
    int selected = 0;
};
