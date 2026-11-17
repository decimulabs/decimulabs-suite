#include "SpectrumView.h"

void SpectrumView::update(const float* specDb, const float* redDb)
{
    for (int i = 0; i < kPoints; ++i)
    {
        // el espectro cae despacio (se ve mejor) y sube de golpe; la reducción igual
        spec[(size_t) i] = specDb[i] > spec[(size_t) i] ? specDb[i] : spec[(size_t) i] + (specDb[i] - spec[(size_t) i]) * 0.25f;
        red[(size_t) i] = redDb[i] > red[(size_t) i] ? redDb[i] : red[(size_t) i] + (redDb[i] - red[(size_t) i]) * 0.25f;
    }
    hasData = true;
}

float SpectrumView::xOfFreq(juce::Rectangle<float> plot, float hz) const
{
    const float t = std::log(std::clamp(hz, 20.0f, 20000.0f) / 20.0f) / std::log(1000.0f);
    return plot.getX() + t * plot.getWidth();
}

void SpectrumView::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(DecimuColours::background);
    g.fillRoundedRectangle(area, 6.0f);

    constexpr float labelW = 34.0f, bottomH = 16.0f;
    auto plot = area.reduced(8.0f, 8.0f);
    plot.removeFromLeft(labelW);
    plot.removeFromBottom(bottomH);

    constexpr float minDb = -100.0f, maxDb = 0.0f, maxRed = 24.0f;
    auto yOfDb = [&](float db) { return plot.getY() + plot.getHeight() * (maxDb - std::clamp(db, minDb, maxDb)) / (maxDb - minDb); };

    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    for (float db : { 0.0f, -20.0f, -40.0f, -60.0f, -80.0f })
    {
        const float y = yOfDb(db);
        g.setColour(DecimuColours::textMuted.withAlpha(0.18f));
        g.drawHorizontalLine((int) y, plot.getX(), plot.getRight());
        g.setColour(DecimuColours::textMuted);
        g.drawText(juce::String((int) db), juce::Rectangle<float>(area.getX() + 2.0f, y - 6.0f, labelW, 12.0f), juce::Justification::centredRight);
    }
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xOfFreq(plot, hz);
        g.setColour(DecimuColours::textMuted.withAlpha(0.18f));
        g.drawVerticalLine((int) x, plot.getY(), plot.getBottom());
        g.setColour(DecimuColours::textMuted);
        g.drawText(hz >= 1000.0f ? juce::String((int) (hz / 1000.0f)) + "k" : juce::String((int) hz),
                   juce::Rectangle<float>(x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 12.0f), juce::Justification::centred);
    }

    // zona fuera del foco: más oscura
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRect(juce::Rectangle<float>(plot.getX(), plot.getY(), xOfFreq(plot, focusLow) - plot.getX(), plot.getHeight()));
    g.fillRect(juce::Rectangle<float>(xOfFreq(plot, focusHigh), plot.getY(), plot.getRight() - xOfFreq(plot, focusHigh), plot.getHeight()));

    if (!hasData)
        return;

    auto xOfPoint = [&](int i) { return plot.getX() + plot.getWidth() * ((float) i + 0.5f) / (float) kPoints; };

    // espectro de entrada
    juce::Path inPath;
    inPath.startNewSubPath(plot.getX(), plot.getBottom());
    for (int i = 0; i < kPoints; ++i)
        inPath.lineTo(xOfPoint(i), yOfDb(spec[(size_t) i]));
    inPath.lineTo(plot.getRight(), plot.getBottom());
    inPath.closeSubPath();
    g.setColour(DecimuColours::textMuted.withAlpha(0.35f));
    g.fillPath(inPath);

    // espectro después de bajar los picos
    juce::Path outPath;
    for (int i = 0; i < kPoints; ++i)
    {
        const float x = xOfPoint(i), y = yOfDb(spec[(size_t) i] - red[(size_t) i]);
        if (i == 0) outPath.startNewSubPath(x, y); else outPath.lineTo(x, y);
    }
    g.setColour(DecimuColours::accentPrimary);
    g.strokePath(outPath, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved));

    // reducción colgando desde arriba
    const auto cut = juce::Colour(0xffE5533B);
    const float step = plot.getWidth() / (float) kPoints;
    for (int i = 0; i < kPoints; ++i)
    {
        const float r = std::clamp(red[(size_t) i], 0.0f, maxRed);
        if (r < 0.2f)
            continue;
        g.setColour(cut.withAlpha(0.85f));
        g.fillRect(juce::Rectangle<float>(xOfPoint(i) - step * 0.5f, plot.getY(), step + 0.5f, plot.getHeight() * 0.30f * r / maxRed));
    }
    g.setColour(DecimuColours::textMuted);
    g.drawText("REDUCCION (hasta 24 dB)", juce::Rectangle<float>(plot.getX() + 6.0f, plot.getY() + plot.getHeight() * 0.30f + 2.0f, 200.0f, 12.0f), juce::Justification::centredLeft);
}
