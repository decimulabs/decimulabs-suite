#include "ScopeView.h"

void ScourScope::pushFrames(const float* mins, const float* maxs, int count)
{
    for (int i = 0; i < count; ++i)
    {
        lo[(size_t) head] = mins[i];
        hi[(size_t) head] = maxs[i];
        head = (head + 1) % historySize;
        filled = std::min(filled + 1, historySize);
    }
}

void ScourScope::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(DecimuColours::background);
    g.fillRoundedRectangle(area, 5.0f);

    auto plot = area.reduced(6.0f, 5.0f);
    const float midY = plot.getCentreY();
    g.setColour(DecimuColours::textMuted.withAlpha(0.25f));
    g.drawHorizontalLine((int) midY, plot.getX(), plot.getRight());
    for (float v : { 0.5f, -0.5f })
        g.drawHorizontalLine((int) (midY - v * plot.getHeight() * 0.5f), plot.getX(), plot.getRight());

    if (filled < 2)
        return;

    const float step = plot.getWidth() / (float) (historySize - 1);
    g.setColour(DecimuColours::accentPrimary.withAlpha(0.9f));
    for (int k = 0; k < filled; ++k)
    {
        const int idx = (head - filled + k + historySize * 2) % historySize;
        const float x = plot.getRight() - (float) (filled - 1 - k) * step;
        const float top = midY - std::clamp(hi[(size_t) idx], -1.0f, 1.0f) * plot.getHeight() * 0.5f;
        const float bottom = midY - std::clamp(lo[(size_t) idx], -1.0f, 1.0f) * plot.getHeight() * 0.5f;
        g.fillRect(juce::Rectangle<float>(x - step, top, step + 0.6f, std::max(1.0f, bottom - top)));
    }
}
