#include "DecimuLadderMeter.h"

void DecimuLadderMeter::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    constexpr int numSegments = 16;
    constexpr float minDb = -48.0f, maxDb = 0.0f;
    const float norm = juce::jlimit(0.0f, 1.0f, (levelDb - minDb) / (maxDb - minDb));
    const int litSegments = juce::roundToInt(norm * (float) numSegments);

    auto bounds = getLocalBounds();
    constexpr int segGap = 1;
    const int segHeight = juce::jmax(1, (bounds.getHeight() - segGap * (numSegments - 1)) / numSegments);

    for (int i = 0; i < numSegments; ++i)
    {
        // i = 0 es el segmento de más abajo.
        auto segRect = juce::Rectangle<int>(bounds.getX(),
                                             bounds.getBottom() - (i + 1) * (segHeight + segGap) + segGap,
                                             bounds.getWidth(), segHeight);
        juce::Colour c = DecimuColours::accentPrimary;
        if (i >= numSegments - 2)      c = DecimuColours::accentRec;   // últimos 2: zona de clip
        else if (i >= numSegments - 5) c = juce::Colours::yellow;      // siguientes 3

        g.setColour(i < litSegments ? c : DecimuColours::textMuted.withAlpha(0.15f));
        g.fillRect(segRect);
    }
}
