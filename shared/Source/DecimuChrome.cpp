#include "DecimuChrome.h"

namespace DecimuChrome
{

void paintChassisBackground(juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius)
{
    juce::ColourGradient base(juce::Colour(0xff1c1f2b), bounds.getX(), bounds.getY(),
                               juce::Colour(0xff14161f), bounds.getX(), bounds.getBottom(), false);
    base.addColour(0.6, juce::Colour(0xff14161f));
    g.setGradientFill(base);
    g.fillRoundedRectangle(bounds, cornerRadius);

    // Brillo diagonal sutil, como luz reflejando en metal cepillado.
    juce::ColourGradient sheen(juce::Colours::transparentWhite, bounds.getX(), bounds.getY(),
                                juce::Colours::transparentWhite, bounds.getRight(), bounds.getBottom(), false);
    sheen.addColour(0.42, juce::Colours::transparentWhite);
    sheen.addColour(0.5, juce::Colours::white.withAlpha(0.05f));
    sheen.addColour(0.58, juce::Colours::transparentWhite);
    g.setGradientFill(sheen);
    g.fillRoundedRectangle(bounds, cornerRadius);

    g.setColour(juce::Colour(0xff2a2e3d));
    g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawLine(bounds.getX() + cornerRadius, bounds.getY() + 1.0f, bounds.getRight() - cornerRadius, bounds.getY() + 1.0f, 1.0f);
}

void paintCornerScrews(juce::Graphics& g, juce::Rectangle<float> bounds, float inset)
{
    constexpr float d = 13.0f;
    const juce::Point<float> positions[] = {
        { bounds.getX() + inset,          bounds.getY() + inset },
        { bounds.getRight() - inset - d,  bounds.getY() + inset },
        { bounds.getX() + inset,          bounds.getBottom() - inset - d },
        { bounds.getRight() - inset - d,  bounds.getBottom() - inset - d },
    };

    for (auto& p : positions)
    {
        juce::Rectangle<float> screw(p.x, p.y, d, d);

        juce::ColourGradient grad(juce::Colour(0xff6a7186), screw.getX() + d * 0.32f, screw.getY() + d * 0.28f,
                                   juce::Colour(0xff0e0f14), screw.getRight(), screw.getBottom(), true);
        grad.addColour(0.55, juce::Colour(0xff363b4a));
        g.setGradientFill(grad);
        g.fillEllipse(screw);
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.drawEllipse(screw, 1.0f);

        auto c = screw.getCentre();
        const float r = d * 0.32f;
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.drawLine(c.x - r, c.y - r, c.x + r, c.y + r, 1.0f);
        g.drawLine(c.x - r, c.y + r, c.x + r, c.y - r, 1.0f);
    }
}

void paintRackHandles(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    constexpr float w = 16.0f;
    auto paintOne = [&](juce::Rectangle<float> r)
    {
        g.setColour(juce::Colour(0xff0a0b10));
        g.fillRoundedRectangle(r, 5.0f);
        for (float yy = r.getY() + 3.0f; yy < r.getBottom(); yy += 6.0f)
        {
            g.setColour(juce::Colour(0xff1c1f2b));
            g.fillRect(juce::Rectangle<float>(r.getX() + 1.0f, yy, r.getWidth() - 2.0f, 3.0f));
        }
        g.setColour(juce::Colours::black);
        g.drawRoundedRectangle(r, 5.0f, 1.0f);
    };

    paintOne(juce::Rectangle<float>(bounds.getX() - w * 0.5f, bounds.getY() + 10.0f, w, bounds.getHeight() - 20.0f));
    paintOne(juce::Rectangle<float>(bounds.getRight() - w * 0.5f, bounds.getY() + 10.0f, w, bounds.getHeight() - 20.0f));
}

void paintCompartment(juce::Graphics& g, juce::Rectangle<float> bounds, bool tinted, float cornerRadius)
{
    g.setColour(tinted ? DecimuColours::accentBlue.withAlpha(0.10f)
                        : juce::Colours::white.withAlpha(0.015f));
    g.fillRoundedRectangle(bounds, cornerRadius);

    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawRoundedRectangle(bounds.reduced(0.5f).translated(0, 1.0f), cornerRadius, 2.0f);

    g.setColour(tinted ? DecimuColours::accentBlue.withAlpha(0.3f)
                        : juce::Colour(0xff2a2e3d));
    g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, 1.0f);

    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawLine(bounds.getX() + cornerRadius, bounds.getY() + 1.0f, bounds.getRight() - cornerRadius, bounds.getY() + 1.0f, 1.0f);
}

void paintLed(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
{
    juce::ColourGradient grad(juce::Colours::white, bounds.getX() + bounds.getWidth() * 0.35f,
                               bounds.getY() + bounds.getHeight() * 0.3f,
                               colour.darker(0.4f), bounds.getRight(), bounds.getBottom(), true);
    grad.addColour(0.45, colour);
    g.setGradientFill(grad);
    g.fillEllipse(bounds);
    g.setColour(colour);
    g.drawEllipse(bounds.expanded(1.5f), 1.0f);
}

}
