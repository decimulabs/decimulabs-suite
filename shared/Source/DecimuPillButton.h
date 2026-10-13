#pragma once

#include "DecimuLookAndFeel.h"

/**
    Botón "pastilla" de la suite: texto corto dentro de un rectángulo redondeado, con
    el borde y el texto en el color de acento cuando está encendido (toggle) o apretado.
    Sirve para transport, toggles de modo, A/B, etc. — todo lo que no es un LED
    (DecimuLedButton) ni un fader/knob. Compatible con ButtonAttachment si se usa como
    toggle (`setClickingTogglesState(true)`); si no, usalo con `onClick` y manejá el
    estado a mano con `setToggleState(..., dontSendNotification)`.
*/
class DecimuPillButton : public juce::Button
{
public:
    explicit DecimuPillButton(const juce::String& label = {}) : juce::Button(label) {}

    void setEmphasis(bool big) { bigText = big; repaint(); }

    void paintButton(juce::Graphics& g, bool isMouseOver, bool isDown) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        const bool on = getToggleState();
        const auto accent = DecimuColours::accentPrimary;

        g.setColour(on ? accent.withAlpha(0.16f)
                       : (isMouseOver ? DecimuColours::panel.brighter(0.15f) : DecimuColours::panel));
        g.fillRoundedRectangle(bounds, 5.0f);

        g.setColour(on ? accent : DecimuColours::textMuted.withAlpha(isDown ? 1.0f : 0.55f));
        g.drawRoundedRectangle(bounds, 5.0f, on ? 1.6f : 1.0f);

        float fontHeight = bigText ? 22.0f : 15.0f;
        if (auto* laf = dynamic_cast<DecimuLookAndFeel*>(&getLookAndFeel()))
            g.setFont(laf->monoFont(fontHeight));
        else
            g.setFont(fontHeight);

        g.setColour(on ? accent : DecimuColours::text);
        g.drawText(getButtonText(), bounds.reduced(4.0f, 0.0f), juce::Justification::centred);
    }

private:
    bool bigText = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimuPillButton)
};
