#include "SpectrumDisplay.h"
#include <DecimuLookAndFeel.h>
#include <cmath>

void CarveSpectrumDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff0a0c10));
    g.fillRoundedRectangle(bounds, 6.0f);

    auto area = bounds.reduced(10.0f, 10.0f);
    area.removeFromBottom(16.0f);                 // lugar para las etiquetas de frecuencia

    const float colW = area.getWidth() / (float) carve::numBands;
    const auto accent = DecimuColours::accentPrimary;

    // Rejilla de dB del recorte (0, 6, 12, 18, 24 desde arriba).
    g.setColour(DecimuColours::textMuted.withAlpha(0.15f));
    for (int db = 0; db <= 24; db += 6)
    {
        const float y = area.getY() + area.getHeight() * 0.5f * ((float) db / 24.0f);
        g.drawHorizontalLine((int) y, area.getX(), area.getRight());
    }

    // La mitad de arriba es el recorte (0..24 dB hacia abajo), la de abajo el nivel del sidechain.
    const float cutTop = area.getY();
    const float cutHeight = area.getHeight() * 0.5f;
    const float lvlBottom = area.getBottom();
    const float lvlHeight = area.getHeight() * 0.5f;

    for (int b = 0; b < carve::numBands; ++b)
    {
        const float hz = carve::bandCentreHz(b);
        const bool inside = hz >= rangeLow && hz <= rangeHigh;
        auto col = juce::Rectangle<float>(area.getX() + (float) b * colW + 1.0f, area.getY(), colW - 2.0f, area.getHeight());

        if (!inside)
        {
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.fillRect(col);
        }

        const float lvlNorm = juce::jlimit(0.0f, 1.0f, (level[(size_t) b] - carve::floorDb) / (0.0f - carve::floorDb));
        g.setColour((inside ? DecimuColours::textMuted : DecimuColours::textMuted.withAlpha(0.4f)).withAlpha(0.75f));
        g.fillRect(col.getX(), lvlBottom - lvlHeight * lvlNorm, col.getWidth(), lvlHeight * lvlNorm);

        const float cutNorm = juce::jlimit(0.0f, 1.0f, cut[(size_t) b] / carve::maxCutDb);
        g.setColour(accent.withAlpha(0.85f));
        g.fillRect(col.getX(), cutTop, col.getWidth(), cutHeight * cutNorm);
    }

    // Línea que separa las dos mitades.
    g.setColour(DecimuColours::textMuted.withAlpha(0.5f));
    g.drawHorizontalLine((int) (area.getY() + cutHeight), area.getX(), area.getRight());

    // Etiquetas de frecuencia.
    g.setFont(monoFont);
    g.setColour(DecimuColours::textMuted);
    const float lo = std::log10(carve::minBandHz), hi = std::log10(carve::maxBandHz);
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float t = (std::log10(hz) - lo) / (hi - lo);
        const float x = area.getX() + colW * 0.5f + t * (area.getWidth() - colW);
        const juce::String text = hz >= 1000.0f ? juce::String((int) (hz / 1000.0f)) + "k" : juce::String((int) hz);
        g.drawText(text, juce::Rectangle<float>(x - 20.0f, area.getBottom() + 1.0f, 40.0f, 14.0f), juce::Justification::centred);
    }

    g.setFont(monoFont);
    g.setColour(accent.withAlpha(0.9f));
    g.drawText("RECORTE", juce::Rectangle<float>(area.getX() + 6.0f, cutTop + 2.0f, 120.0f, 14.0f), juce::Justification::left);
    g.setColour(DecimuColours::textMuted);
    g.drawText("SIDECHAIN", juce::Rectangle<float>(area.getX() + 6.0f, lvlBottom - 16.0f, 120.0f, 14.0f), juce::Justification::left);

    if (!scConnected)
    {
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillRect(area);
        g.setColour(DecimuColours::text);
        g.setFont(monoFont.withHeight(16.0f));
        g.drawFittedText(juce::String::fromUTF8("SIN SIDECHAIN\nruteá una pista al sidechain de Carve en tu DAW"),
                         area.toNearestInt().reduced(20), juce::Justification::centred, 3);
    }
}
