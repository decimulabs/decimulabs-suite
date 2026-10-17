#include "SpectrumDisplay.h"
#include <DecimuLookAndFeel.h>
#include <cmath>

namespace
{
    constexpr float gutterW = 38.0f;       // ancho de cada escala de dB (izquierda y derecha)

    juce::String hzText(float hz)
    {
        return hz >= 1000.0f ? juce::String(hz / 1000.0f, 1) + " kHz" : juce::String((int) std::lround(hz)) + " Hz";
    }

    float textWidth(const juce::Font& font, const juce::String& text)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(font, text, 0.0f, 0.0f);
        return glyphs.getBoundingBox(0, -1, true).getWidth();
    }

    // Texto sobre un fondo oscuro: queda legible aunque pase por encima de las columnas.
    void drawTag(juce::Graphics& g, const juce::Font& font, const juce::String& text, juce::Rectangle<float> anchor,
                 juce::Colour colour, bool alignRight)
    {
        const float w = textWidth(font, text) + 12.0f;
        const juce::Rectangle<float> r(alignRight ? anchor.getRight() - w : anchor.getX(), anchor.getY(), w, anchor.getHeight());
        g.setColour(juce::Colour(0xff0a0c10).withAlpha(0.85f));
        g.fillRoundedRectangle(r, 3.0f);
        g.setFont(font);
        g.setColour(colour);
        g.drawText(text, r, juce::Justification::centred);
    }
}

juce::Rectangle<float> CarveSpectrumDisplay::plotArea() const
{
    auto area = getLocalBounds().toFloat().reduced(10.0f, 10.0f);
    area.removeFromBottom(16.0f);                 // lugar para las etiquetas de frecuencia
    area.removeFromLeft(gutterW);                 // escala del recorte
    area.removeFromRight(gutterW);                // escala del sidechain
    return area;
}

void CarveSpectrumDisplay::mouseMove(const juce::MouseEvent& e)
{
    const auto area = plotArea();
    int band = -1;
    if (area.contains(e.position))
        band = juce::jlimit(0, carve::numBands - 1,
                            (int) ((e.position.x - area.getX()) / (area.getWidth() / (float) carve::numBands)));
    if (band != hoverBand)
    {
        hoverBand = band;
        repaint();
    }
}

void CarveSpectrumDisplay::mouseExit(const juce::MouseEvent&)
{
    if (hoverBand != -1)
    {
        hoverBand = -1;
        repaint();
    }
}

void CarveSpectrumDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff0a0c10));
    g.fillRoundedRectangle(bounds, 6.0f);

    const auto area = plotArea();
    const float colW = area.getWidth() / (float) carve::numBands;
    const auto accent = DecimuColours::accentPrimary;

    // La mitad de arriba es el recorte (0..24 dB hacia abajo), la de abajo el nivel del sidechain.
    const float cutTop = area.getY();
    const float cutHeight = area.getHeight() * 0.5f;
    const float lvlBottom = area.getBottom();
    const float lvlHeight = area.getHeight() * 0.5f;

    // ---- Escala del recorte (izquierda): 0, -6, -12, -18 y -24 dB, de arriba hacia abajo ----
    g.setFont(monoFont);
    for (int db = 0; db <= 24; db += 6)
    {
        const float y = cutTop + cutHeight * ((float) db / carve::maxCutDb);
        g.setColour(DecimuColours::textMuted.withAlpha(0.15f));
        g.drawHorizontalLine((int) y, area.getX(), area.getRight());
        g.setColour(accent.withAlpha(0.9f));
        g.drawText(db == 0 ? juce::String("0") : "-" + juce::String(db),
                   juce::Rectangle<float>(area.getX() - gutterW, y - 7.0f, gutterW - 5.0f, 14.0f),
                   juce::Justification::centredRight);
    }

    // ---- Escala del sidechain (derecha): 0 dBFS en la línea del medio, -60 abajo ----
    for (int db = 0; db >= -60; db -= 12)
    {
        const float norm = ((float) db - carve::floorDb) / (0.0f - carve::floorDb);   // 1 en 0 dB, 0 en -60 dB
        const float y = lvlBottom - lvlHeight * norm;
        if (db != 0 && db != -60)
        {
            g.setColour(DecimuColours::textMuted.withAlpha(0.15f));
            g.drawHorizontalLine((int) y, area.getX(), area.getRight());
        }
        g.setColour(DecimuColours::textMuted);
        g.drawText(juce::String(db), juce::Rectangle<float>(area.getRight() + 5.0f, y - 7.0f, gutterW - 5.0f, 14.0f),
                   juce::Justification::centredLeft);
    }

    // ---- Columnas ----
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

        if (b == hoverBand)
        {
            g.setColour(juce::Colours::white.withAlpha(0.10f));
            g.fillRect(col);
            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.drawRect(col, 1.0f);
        }
    }

    // Línea que separa las dos mitades.
    g.setColour(DecimuColours::textMuted.withAlpha(0.5f));
    g.drawHorizontalLine((int) (area.getY() + cutHeight), area.getX(), area.getRight());

    // ---- Etiquetas de frecuencia ----
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

    drawTag(g, monoFont, "RECORTE dB", juce::Rectangle<float>(area.getX() + 6.0f, cutTop + 3.0f, 120.0f, 15.0f), accent, false);
    drawTag(g, monoFont, "SIDECHAIN dBFS", juce::Rectangle<float>(area.getX() + 6.0f, lvlBottom - 19.0f, 160.0f, 15.0f),
            DecimuColours::textMuted, false);

    // ---- Lectura: valores de la banda bajo el mouse, o el recorte máximo ----
    {
        juce::String text;
        bool hovering = hoverBand >= 0;
        if (hovering)
        {
            const size_t b = (size_t) hoverBand;
            text = hzText(carve::bandCentreHz(hoverBand)) + "   RECORTE -" + juce::String(cut[b], 1)
                   + " dB   SIDECHAIN " + juce::String(std::max(level[b], carve::floorDb), 0) + " dB";
        }
        else
        {
            int best = 0;
            for (int b = 1; b < carve::numBands; ++b)
                if (cut[(size_t) b] > cut[(size_t) best])
                    best = b;
            text = cut[(size_t) best] > 0.05f
                       ? "RECORTE MAX -" + juce::String(cut[(size_t) best], 1) + " dB EN " + hzText(carve::bandCentreHz(best))
                       : juce::String("SIN RECORTE");
        }
        drawTag(g, monoFont.withHeight(monoFont.getHeight() + 1.0f), text,
                juce::Rectangle<float>(area.getRight() - 6.0f - 400.0f, cutTop + 3.0f, 400.0f, 15.0f),
                hovering ? DecimuColours::text : accent, true);
    }

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
