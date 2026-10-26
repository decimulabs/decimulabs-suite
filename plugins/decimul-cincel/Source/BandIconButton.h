#pragma once

#include <DecimuLookAndFeel.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

/**
    Botón con ícono, como en el Pro-Q: el de encendido (prende / apaga la banda) y los
    auriculares (solo). El de solo es "mantener apretado": se escucha mientras está apretado y,
    arrastrando a los costados, se barre la frecuencia; al soltar vuelve al EQ normal.
*/
class BandIconButton : public juce::Button
{
public:
    enum class Icon { Power, Headphones };

    explicit BandIconButton(Icon i) : juce::Button({}), icon(i) {}

    /** Para el solo: apretar, arrastrar, soltar. */
    std::function<void()> onPress, onRelease;
    std::function<void(int dx)> onDragged;

    /** Color del ícono encendido (el de la banda). */
    void setOnColour(juce::Colour c)
    {
        onColour = c;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        juce::Button::mouseDown(e);
        if (onPress)
            onPress();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        juce::Button::mouseDrag(e);
        if (onDragged)
            onDragged(e.getDistanceFromDragStartX());
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        juce::Button::mouseUp(e);
        if (onRelease)
            onRelease();
    }

    void paintButton(juce::Graphics& g, bool isMouseOver, bool isDown) override
    {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        const bool on = getToggleState() || (icon == Icon::Headphones && isDown);
        // Apagado se nota: la banda apagada tiene el botón en rojo apagado, no solo gris.
        const auto offColour = icon == Icon::Power ? DecimuColours::accentRec.withAlpha(0.85f) : DecimuColours::textMuted;
        const auto c = on ? onColour : offColour;

        g.setColour(on ? c.withAlpha(0.18f) : (isMouseOver ? DecimuColours::panel.brighter(0.15f) : DecimuColours::panel));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(on ? c : offColour.withAlpha(isMouseOver ? 0.9f : 0.6f));
        g.drawRoundedRectangle(r, 5.0f, on ? 1.6f : 1.0f);

        const float s = std::min(r.getWidth(), r.getHeight()) * 0.62f;
        const auto box = juce::Rectangle<float>(s, s).withCentre(r.getCentre());
        g.setColour(c);
        juce::Path p;
        if (icon == Icon::Power)
        {
            const auto cc = box.getCentre();
            const float rad = s * 0.42f;
            p.addCentredArc(cc.x, cc.y + s * 0.04f, rad, rad, 0.0f, 0.75f, juce::MathConstants<float>::twoPi - 0.75f, true);
            g.strokePath(p, juce::PathStrokeType(s * 0.12f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.drawLine(cc.x, box.getY(), cc.x, cc.y + s * 0.05f, s * 0.12f);
        }
        else
        {
            const auto cc = box.getCentre();
            const float rad = s * 0.42f;
            p.addCentredArc(cc.x, cc.y + s * 0.08f, rad, rad, 0.0f, -juce::MathConstants<float>::halfPi * 1.05f,
                            juce::MathConstants<float>::halfPi * 1.05f, true);
            g.strokePath(p, juce::PathStrokeType(s * 0.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const float cw = s * 0.24f, ch = s * 0.36f;
            g.fillRoundedRectangle(cc.x - rad - cw * 0.5f, cc.y + s * 0.02f, cw, ch, cw * 0.35f);
            g.fillRoundedRectangle(cc.x + rad - cw * 0.5f, cc.y + s * 0.02f, cw, ch, cw * 0.35f);
        }
    }

private:
    Icon icon;
    juce::Colour onColour = DecimuColours::accentPrimary;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandIconButton)
};
