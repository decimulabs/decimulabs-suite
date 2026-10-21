#include "TransientView.h"

void AtaqueView::pushFrames(const float* levelsDb, const float* gainsDb, int count)
{
    for (int i = 0; i < count; ++i)
    {
        levels[(size_t) head] = levelsDb[i];
        gains[(size_t) head] = gainsDb[i];
        head = (head + 1) % historySize;
        filled = std::min(filled + 1, historySize);
    }
}

void AtaqueView::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(DecimuColours::background);
    g.fillRoundedRectangle(area, 6.0f);

    constexpr float labelW = 38.0f;
    auto plot = area.reduced(8.0f, 8.0f);
    plot.removeFromLeft(labelW);
    auto levelArea = plot.removeFromTop(plot.getHeight() * 0.58f);
    plot.removeFromTop(16.0f);
    auto gainArea = plot;

    g.setFont(juce::Font(juce::FontOptions(10.0f)));

    // ---- Nivel ----
    constexpr float minDb = -72.0f;
    for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f })
    {
        const float y = levelArea.getY() + levelArea.getHeight() * (db / minDb);
        g.setColour(DecimuColours::textMuted.withAlpha(0.18f));
        g.drawHorizontalLine((int) y, levelArea.getX(), levelArea.getRight());
        g.setColour(DecimuColours::textMuted);
        g.drawText(juce::String((int) db), juce::Rectangle<float>(area.getX() + 2.0f, y - 6.0f, labelW, 12.0f),
                   juce::Justification::centredRight);
    }
    g.setColour(DecimuColours::textMuted);
    g.drawText("NIVEL dBFS", juce::Rectangle<float>(levelArea.getX() + 4.0f, levelArea.getY() + 1.0f, 120.0f, 12.0f),
               juce::Justification::centredLeft);

    // ---- Ganancia ----
    constexpr float range = 12.0f;
    const float zeroY = gainArea.getCentreY();
    for (float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f })
    {
        const float y = zeroY - gainArea.getHeight() * 0.5f * (db / range);
        g.setColour(DecimuColours::textMuted.withAlpha(db == 0.0f ? 0.5f : 0.18f));
        g.drawHorizontalLine((int) y, gainArea.getX(), gainArea.getRight());
        g.setColour(DecimuColours::textMuted);
        g.drawText((db > 0.0f ? "+" : "") + juce::String((int) db),
                   juce::Rectangle<float>(area.getX() + 2.0f, y - 6.0f, labelW, 12.0f), juce::Justification::centredRight);
    }
    g.setColour(DecimuColours::textMuted);
    g.drawText("GANANCIA APLICADA dB", juce::Rectangle<float>(gainArea.getX() + 4.0f, gainArea.getY() - 15.0f, 200.0f, 12.0f),
               juce::Justification::centredLeft);

    if (filled < 2)
        return;

    const float step = levelArea.getWidth() / (float) (historySize - 1);
    auto xAt = [&](int k) { return levelArea.getRight() - (float) (filled - 1 - k) * step; };   // k = 0 el más viejo
    auto indexOf = [&](int k) { return (head - filled + k + historySize * 2) % historySize; };

    juce::Path levelPath;
    levelPath.startNewSubPath(xAt(0), levelArea.getBottom());
    for (int k = 0; k < filled; ++k)
    {
        const float db = std::clamp(levels[(size_t) indexOf(k)], minDb, 0.0f);
        levelPath.lineTo(xAt(k), levelArea.getY() + levelArea.getHeight() * (db / minDb));
    }
    levelPath.lineTo(xAt(filled - 1), levelArea.getBottom());
    levelPath.closeSubPath();
    g.setColour(DecimuColours::textMuted.withAlpha(0.35f));
    g.fillPath(levelPath);

    // Ganancia: relleno entre el cero y el valor (acento hacia arriba, rojo hacia abajo)
    const auto cut = juce::Colour(0xffE5533B);
    for (int k = 1; k < filled; ++k)
    {
        const float g1 = std::clamp(gains[(size_t) indexOf(k)], -range, range);
        const float y1 = zeroY - gainArea.getHeight() * 0.5f * (g1 / range);
        g.setColour((g1 >= 0.0f ? DecimuColours::accentPrimary : cut).withAlpha(0.85f));
        const float x = xAt(k);
        g.fillRect(juce::Rectangle<float>(x - step, std::min(y1, zeroY), step + 0.6f, std::max(1.0f, std::abs(y1 - zeroY))));
    }
}
