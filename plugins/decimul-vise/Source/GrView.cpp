#include "GrView.h"

void ViseView::pushFrames(const float* inDb, const float* grDb, int count)
{
    for (int i = 0; i < count; ++i)
    {
        levels[(size_t) head] = inDb[i];
        reductions[(size_t) head] = grDb[i];
        head = (head + 1) % historySize;
        filled = std::min(filled + 1, historySize);
        lastGr = grDb[i];
    }
}

void ViseView::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(DecimuColours::background);
    g.fillRoundedRectangle(area, 6.0f);

    constexpr float labelW = 38.0f;
    auto plot = area.reduced(8.0f, 8.0f);
    plot.removeFromLeft(labelW);
    auto grArea = plot.removeFromTop(plot.getHeight() * 0.40f);
    plot.removeFromTop(16.0f);
    auto levelArea = plot;

    g.setFont(juce::Font(juce::FontOptions(10.0f)));

    // ---- Reducción (cuelga desde arriba, 0 a 24 dB) ----
    constexpr float maxGr = 24.0f;
    for (float db : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f })
    {
        const float y = grArea.getY() + grArea.getHeight() * (db / maxGr);
        g.setColour(DecimuColours::textMuted.withAlpha(db == 0.0f ? 0.5f : 0.18f));
        g.drawHorizontalLine((int) y, grArea.getX(), grArea.getRight());
        g.setColour(DecimuColours::textMuted);
        g.drawText(db == 0.0f ? "0" : "-" + juce::String((int) db),
                   juce::Rectangle<float>(area.getX() + 2.0f, y - 6.0f, labelW, 12.0f), juce::Justification::centredRight);
    }
    g.setColour(DecimuColours::textMuted);
    g.drawText("REDUCCION dB", juce::Rectangle<float>(grArea.getX() + 4.0f, grArea.getBottom() + 2.0f, 140.0f, 12.0f),
               juce::Justification::centredLeft);

    // ---- Nivel de entrada ----
    constexpr float minDb = -60.0f;
    for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f })
    {
        const float y = levelArea.getY() + levelArea.getHeight() * (db / minDb);
        g.setColour(DecimuColours::textMuted.withAlpha(0.18f));
        g.drawHorizontalLine((int) y, levelArea.getX(), levelArea.getRight());
        g.setColour(DecimuColours::textMuted);
        g.drawText(juce::String((int) db), juce::Rectangle<float>(area.getX() + 2.0f, y - 6.0f, labelW, 12.0f),
                   juce::Justification::centredRight);
    }
    g.drawText("ENTRADA dBFS", juce::Rectangle<float>(levelArea.getX() + 4.0f, levelArea.getY() + 1.0f, 140.0f, 12.0f),
               juce::Justification::centredLeft);

    if (filled >= 2)
    {
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

        const auto cut = juce::Colour(0xffE5533B);
        for (int k = 1; k < filled; ++k)
        {
            const float gr = std::clamp(reductions[(size_t) indexOf(k)], 0.0f, maxGr);
            if (gr < 0.05f)
                continue;
            g.setColour(cut.withAlpha(0.88f));
            g.fillRect(juce::Rectangle<float>(xAt(k) - step, grArea.getY(), step + 0.6f, std::max(1.0f, grArea.getHeight() * gr / maxGr)));
        }
    }

    // umbral
    const float ty = levelArea.getY() + levelArea.getHeight() * (std::clamp(threshold, minDb, 0.0f) / minDb);
    g.setColour(DecimuColours::accentPrimary);
    g.drawHorizontalLine((int) ty, levelArea.getX(), levelArea.getRight());
    g.fillRect(juce::Rectangle<float>(levelArea.getX() - 6.0f, ty - 3.0f, 6.0f, 6.0f));

    // reducción actual en números
    g.setColour(DecimuColours::text);
    g.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    g.drawText((lastGr < 0.05f ? juce::String("0.0") : "-" + juce::String(lastGr, 1)) + " dB",
               grArea.reduced(8.0f, 4.0f), juce::Justification::topRight);
}
