#pragma once

#include "DecimuChrome.h"

/**
    LED cuadrado clickeable — el mismo dibujo que DecimuChrome::paintLed, pero como
    juce::Button real, para usar como on/off de un módulo entero (en vez de tener que
    bajar su fader a cero, que no es intuitivo para alguien que no conoce el plugin —
    feedback directo de testing de DecimuLabs Guitars). Compatible con
    AudioProcessorValueTreeState::ButtonAttachment como cualquier juce::Button.
*/
class DecimuLedButton : public juce::Button
{
public:
    DecimuLedButton() : juce::Button({}) { setClickingTogglesState(true); }

    void paintButton(juce::Graphics& g, bool isMouseOverButton, bool) override
    {
        auto bounds = getLocalBounds().toFloat();
        const auto colour = getToggleState() ? DecimuColours::accentPrimary
                                              : DecimuColours::textMuted.withAlpha(isMouseOverButton ? 0.6f : 0.35f);
        DecimuChrome::paintLed(g, bounds, colour);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimuLedButton)
};
