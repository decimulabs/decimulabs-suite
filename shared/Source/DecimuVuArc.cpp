#include "DecimuVuArc.h"
#include <cmath>

void DecimuVuArc::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Ventanita tipo vidrio (fondo hundido, sombra interior).
    g.setColour(juce::Colour(0xff030304));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(juce::Colours::black);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    auto arcArea = bounds.reduced(8.0f, 6.0f);
    arcArea.removeFromBottom(arcArea.getHeight() * 0.15f);
    const float radius = juce::jmin(arcArea.getWidth() * 0.5f, arcArea.getHeight());
    const juce::Point<float> centre(arcArea.getCentreX(), arcArea.getBottom());

    constexpr float startAngle = -juce::MathConstants<float>::halfPi;
    constexpr float endAngle = juce::MathConstants<float>::halfPi;

    // Aro de fondo.
    juce::Path bg;
    bg.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(juce::Colour(0xff232635));
    g.strokePath(bg, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Tramo cian (0-70%) + tramo rojo (70-100%), como el gauge de la referencia.
    juce::Path cyanArc, redArc;
    const float splitAngle = startAngle + (endAngle - startAngle) * 0.7f;
    cyanArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, splitAngle, true);
    redArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, splitAngle, endAngle, true);
    g.setColour(DecimuColours::accentPrimary);
    g.strokePath(cyanArc, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(DecimuColours::accentRec);
    g.strokePath(redArc, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Aguja.
    constexpr float minDb = -36.0f, maxDb = 3.0f;
    const float norm = juce::jlimit(0.0f, 1.0f, (levelDb - minDb) / (maxDb - minDb));
    const float angle = startAngle + norm * (endAngle - startAngle);
    juce::Point<float> tip(centre.x + radius * 0.92f * std::sin(angle), centre.y - radius * 0.92f * std::cos(angle));
    g.setColour(DecimuColours::text);
    g.drawLine({ centre, tip }, 2.0f);
    g.fillEllipse(juce::Rectangle<float>(4.0f, 4.0f).withCentre(centre));

    g.setColour(DecimuColours::textMuted);
    g.setFont(juce::Font(juce::FontOptions(11.0f)).withExtraKerningFactor(0.1f));
    g.drawText("VU", bounds.removeFromBottom(14.0f), juce::Justification::centred);
}
