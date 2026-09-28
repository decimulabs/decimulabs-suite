#include "DecimuLedStackSelector.h"

void DecimuLedStackSelector::setOptions(const juce::StringArray& items)
{
    options = items;
    repaint();
}

void DecimuLedStackSelector::setSelectedIndex(int index)
{
    if (index != selected)
    {
        selected = index;
        repaint();
    }
}

void DecimuLedStackSelector::paint(juce::Graphics& g)
{
    const int n = options.size();
    if (n == 0)
        return;

    const int rowH = getHeight() / n;
    for (int i = 0; i < n; ++i)
    {
        auto row = juce::Rectangle<int>(0, i * rowH, getWidth(), rowH);
        const bool isSel = (i == selected);

        auto dotArea = row.removeFromLeft(rowH).toFloat().reduced((float) rowH * 0.3f);
        g.setColour(isSel ? DecimuColours::accentPrimary : DecimuColours::textMuted.withAlpha(0.4f));
        g.fillEllipse(dotArea);

        g.setColour(isSel ? DecimuColours::text : DecimuColours::textMuted);
        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        g.drawText(options[i], row.reduced(2, 0), juce::Justification::centredLeft);
    }
}

void DecimuLedStackSelector::mouseUp(const juce::MouseEvent& e)
{
    const int n = options.size();
    if (n == 0)
        return;

    const int rowH = juce::jmax(1, getHeight() / n);
    const int idx = juce::jlimit(0, n - 1, e.y / rowH);
    setSelectedIndex(idx);
    if (onChange != nullptr)
        onChange(idx);
}
