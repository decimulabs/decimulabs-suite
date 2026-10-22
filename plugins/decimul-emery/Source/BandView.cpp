#include "BandView.h"

void EmeryView::pushFrames(const float* essLevel, const float* essRed, const float* popLevel, const float* popRed, int count)
{
    for (int i = 0; i < count; ++i)
    {
        sections[0].level[(size_t) head] = essLevel[i];
        sections[0].reduction[(size_t) head] = essRed[i];
        sections[1].level[(size_t) head] = popLevel[i];
        sections[1].reduction[(size_t) head] = popRed[i];
        head = (head + 1) % historySize;
        filled = std::min(filled + 1, historySize);
    }
}

void EmeryView::setSection(bool pop, float thresholdDb, float maxReductionDb, bool on)
{
    auto& s = sections[pop ? 1 : 0];
    s.thresholdDb = thresholdDb;
    s.maxReductionDb = maxReductionDb;
    s.on = on;
}

void EmeryView::setTitles(const juce::String& ess, const juce::String& pop)
{
    sections[0].title = ess;
    sections[1].title = pop;
}

void EmeryView::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(DecimuColours::background);
    g.fillRoundedRectangle(area, 6.0f);

    auto inner = area.reduced(8.0f, 8.0f);
    auto top = inner.removeFromTop((inner.getHeight() - 10.0f) * 0.5f);
    inner.removeFromTop(10.0f);
    paintSection(g, top, sections[0], DecimuColours::accentPrimary);
    paintSection(g, inner, sections[1], DecimuColours::accentPrimary);
}

void EmeryView::paintSection(juce::Graphics& g, juce::Rectangle<float> area, const Section& s, juce::Colour accent) const
{
    constexpr float labelW = 34.0f;
    constexpr float minDb = -72.0f;

    g.setColour(DecimuColours::panel.withAlpha(0.55f));
    g.fillRoundedRectangle(area, 4.0f);

    auto plot = area.reduced(4.0f, 14.0f);
    plot.removeFromLeft(labelW);

    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f })
    {
        const float y = plot.getY() + plot.getHeight() * (db / minDb);
        g.setColour(DecimuColours::textMuted.withAlpha(0.16f));
        g.drawHorizontalLine((int) y, plot.getX(), plot.getRight());
        g.setColour(DecimuColours::textMuted);
        g.drawText(juce::String((int) db), juce::Rectangle<float>(area.getX() + 2.0f, y - 6.0f, labelW, 12.0f),
                   juce::Justification::centredRight);
    }

    g.setColour(s.on ? accent : DecimuColours::textMuted);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(s.title + (s.on ? "" : "  (OFF)"),
               juce::Rectangle<float>(plot.getX() + 2.0f, area.getY() + 1.0f, 340.0f, 13.0f), juce::Justification::centredLeft);

    if (filled >= 2)
    {
        const float step = plot.getWidth() / (float) (historySize - 1);
        auto xAt = [&](int k) { return plot.getRight() - (float) (filled - 1 - k) * step; };
        auto indexOf = [&](int k) { return (head - filled + k + historySize * 2) % historySize; };

        juce::Path levelPath;
        levelPath.startNewSubPath(xAt(0), plot.getBottom());
        for (int k = 0; k < filled; ++k)
        {
            const float db = std::clamp(s.level[(size_t) indexOf(k)], minDb, 0.0f);
            levelPath.lineTo(xAt(k), plot.getY() + plot.getHeight() * (db / minDb));
        }
        levelPath.lineTo(xAt(filled - 1), plot.getBottom());
        levelPath.closeSubPath();
        g.setColour(DecimuColours::textMuted.withAlpha(0.35f));
        g.fillPath(levelPath);

        // Reducción: cuelga desde arriba; el alto completo de la mitad superior = 24 dB.
        const auto cut = juce::Colour(0xffE5533B);
        const float redScale = plot.getHeight() * 0.5f / 24.0f;
        for (int k = 1; k < filled; ++k)
        {
            const float red = std::clamp(s.reduction[(size_t) indexOf(k)], 0.0f, 24.0f);
            if (red < 0.05f)
                continue;
            g.setColour(cut.withAlpha(0.85f));
            g.fillRect(juce::Rectangle<float>(xAt(k) - step, plot.getY(), step + 0.6f, std::max(1.0f, red * redScale)));
        }
    }

    // Umbral
    const float ty = plot.getY() + plot.getHeight() * (std::clamp(s.thresholdDb, minDb, 0.0f) / minDb);
    g.setColour(accent.withAlpha(s.on ? 0.95f : 0.35f));
    g.drawHorizontalLine((int) ty, plot.getX(), plot.getRight());
    g.fillRect(juce::Rectangle<float>(plot.getX() - 6.0f, ty - 3.0f, 6.0f, 6.0f));
}
