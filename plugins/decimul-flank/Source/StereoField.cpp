#include "StereoField.h"
#include <algorithm>
#include <cmath>

namespace
{
    juce::Font hudFont(juce::Component& c, float height)
    {
        if (auto* laf = dynamic_cast<DecimuLookAndFeel*>(&c.getLookAndFeel()))
            return laf->monoFont(height);
        return juce::Font(juce::FontOptions(height));
    }
}

void FlankStereoField::pushPoints(const float* interleaved, int pairs)
{
    for (int i = 0; i < pairs; ++i)
    {
        points[(size_t) pointHead * 2] = interleaved[i * 2];
        points[(size_t) pointHead * 2 + 1] = interleaved[i * 2 + 1];
        pointHead = (pointHead + 1) % maxPoints;
        pointCount = std::min(pointCount + 1, maxPoints);
    }
}

void FlankStereoField::setState(const State& s)
{
    state = s;
}

void FlankStereoField::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(DecimuColours::trackFill);
    g.fillRoundedRectangle(bounds, 6.0f);

    const auto accent = DecimuColours::accentPrimary;
    auto area = bounds.reduced(14.0f, 10.0f);

    // ---- Abajo: correlación ----
    auto bottom = area.removeFromBottom(54.0f);
    area.removeFromBottom(6.0f);

    // ---- En el medio: posiciones ----
    auto lineArea = area.removeFromBottom(58.0f);
    area.removeFromBottom(6.0f);

    // ---- Arriba: goniómetro ----
    const float side = std::min(area.getWidth(), area.getHeight());
    auto gonio = area.withSizeKeepingCentre(side, side);
    const auto centre = gonio.getCentre();
    const float radius = side * 0.5f - 4.0f;

    g.setColour(DecimuColours::background);
    g.fillEllipse(gonio.reduced(2.0f));
    g.setColour(DecimuColours::textMuted.withAlpha(0.35f));
    g.drawEllipse(gonio.reduced(2.0f), 1.0f);
    g.setColour(DecimuColours::textMuted.withAlpha(0.22f));
    g.drawLine(centre.x, gonio.getY() + 4.0f, centre.x, gonio.getBottom() - 4.0f);          // mono
    g.drawLine(gonio.getX() + 4.0f, centre.y, gonio.getRight() - 4.0f, centre.y);           // lados
    const float diag = radius * 0.7071f;
    g.drawLine(centre.x - diag, centre.y + diag, centre.x + diag, centre.y - diag);         // solo R
    g.drawLine(centre.x - diag, centre.y - diag, centre.x + diag, centre.y + diag);         // solo L

    g.setFont(hudFont(*this, 13.0f));
    g.setColour(DecimuColours::textMuted);
    g.drawText("L", juce::Rectangle<float>(centre.x - diag - 14.0f, centre.y - diag - 16.0f, 16.0f, 14.0f), juce::Justification::centred);
    g.drawText("R", juce::Rectangle<float>(centre.x + diag - 2.0f, centre.y - diag - 16.0f, 16.0f, 14.0f), juce::Justification::centred);
    g.drawText("M", juce::Rectangle<float>(centre.x - 8.0f, gonio.getY() - 2.0f, 16.0f, 14.0f), juce::Justification::centred);

    // Puntos: el más nuevo, más fuerte.
    constexpr float gain = 2.4f;
    for (int i = 0; i < pointCount; ++i)
    {
        const int idx = (pointHead - pointCount + i + maxPoints * 2) % maxPoints;
        const float l = points[(size_t) idx * 2], r = points[(size_t) idx * 2 + 1];
        float x = (r - l) * 0.7071f * gain;
        float y = (l + r) * 0.7071f * gain;
        const float len = std::sqrt(x * x + y * y);
        if (len > 1.0f)
        {
            x /= len;
            y /= len;
        }
        const float age = (float) i / (float) std::max(1, pointCount - 1);
        g.setColour(accent.withAlpha(0.06f + 0.55f * age * age));
        g.fillRect(centre.x + x * radius - 1.0f, centre.y - y * radius - 1.0f, 2.2f, 2.2f);
    }

    // ---- Posiciones: izquierda <-> derecha ----
    {
        auto strip = lineArea.reduced(6.0f, 0.0f);
        const float y = strip.getY() + 22.0f;
        const float x0 = strip.getX() + 10.0f, x1 = strip.getRight() - 10.0f;
        const float cx = 0.5f * (x0 + x1);

        g.setColour(DecimuColours::textMuted.withAlpha(0.55f));
        g.drawLine(x0, y, x1, y, 1.5f);
        g.setFont(hudFont(*this, 13.0f));
        g.drawText("L", juce::Rectangle<float>(x0 - 10.0f, y + 6.0f, 20.0f, 14.0f), juce::Justification::centred);
        g.drawText("R", juce::Rectangle<float>(x1 - 10.0f, y + 6.0f, 20.0f, 14.0f), juce::Justification::centred);

        // Ambiente: una banda suave detrás
        if (state.space > 0.01f)
        {
            g.setColour(accent.withAlpha(0.10f + 0.20f * state.space));
            g.fillRoundedRectangle(x0, y - 12.0f, x1 - x0, 24.0f, 12.0f);
        }

        // Posición de las copias: spread 0 = al centro, 1 = en los extremos
        const float half = 0.5f * (x1 - x0);
        const float copyX = half * state.spread;

        g.setColour(accent);
        g.fillEllipse(cx - copyX - 7.0f, y - 7.0f, 14.0f, 14.0f);
        g.fillEllipse(cx + copyX - 7.0f, y - 7.0f, 14.0f, 14.0f);
        g.setColour(DecimuColours::text);
        g.fillEllipse(cx - 9.0f, y - 9.0f, 18.0f, 18.0f);
        g.setColour(DecimuColours::background);
        g.setFont(hudFont(*this, 12.0f));
        g.drawText("V", juce::Rectangle<float>(cx - 9.0f, y - 8.0f, 18.0f, 16.0f), juce::Justification::centred);

        g.setColour(DecimuColours::textMuted);
        g.setFont(hudFont(*this, 13.0f));
        g.drawText("VOICE + 2 COPIES   OFFSET " + juce::String(state.offsetMs, 1) + " ms", strip.removeFromBottom(16.0f),
                   juce::Justification::centred);
    }

    // ---- Correlación ----
    {
        auto row = bottom;
        g.setFont(hudFont(*this, 14.0f));
        g.setColour(DecimuColours::textMuted);
        g.drawText("CORRELATION", row.removeFromTop(16.0f), juce::Justification::centredLeft);

        auto bar = row.removeFromTop(14.0f).reduced(2.0f, 3.0f);
        g.setColour(DecimuColours::background);
        g.fillRoundedRectangle(bar, 3.0f);

        const float zero = bar.getX() + bar.getWidth() * 0.5f;
        g.setColour(DecimuColours::textMuted.withAlpha(0.5f));
        g.drawLine(zero, bar.getY() - 2.0f, zero, bar.getBottom() + 2.0f, 1.0f);

        const float c = std::clamp(state.correlation, -1.0f, 1.0f);
        const float markX = bar.getX() + (c * 0.5f + 0.5f) * bar.getWidth();
        const bool risky = c < 0.2f;
        g.setColour(risky ? DecimuColours::accentRec : accent);
        g.fillRoundedRectangle(juce::Rectangle<float>(std::min(zero, markX), bar.getY(), std::abs(markX - zero), bar.getHeight()), 3.0f);
        g.setColour(DecimuColours::text);
        g.fillRect(markX - 1.5f, bar.getY() - 3.0f, 3.0f, bar.getHeight() + 6.0f);

        g.setFont(hudFont(*this, 14.0f));
        g.setColour(risky ? DecimuColours::accentRec : DecimuColours::text);
        const juce::String text = (c >= 0.0f ? "+" : "") + juce::String(c, 2) + "     MONO SUM "
                                  + juce::String(state.monoLossDb, 1) + " dB"
                                  + (state.monoCheck ? "     [ MONO CHECK ]" : "");
        g.drawText(text, row, juce::Justification::centredLeft);
    }
}
