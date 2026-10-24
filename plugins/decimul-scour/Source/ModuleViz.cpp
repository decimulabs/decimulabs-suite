#include "ModuleViz.h"
#include <cmath>

namespace
{
    // número pseudoaleatorio fijo (0..1) a partir de un índice: así el dibujo del reverb no parpadea
    float hash01(int i)
    {
        uint32_t x = (uint32_t) i * 2654435761u + 12345u;
        x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
        return (float) (x & 0xffff) / 65535.0f;
    }
}

ModuleViz::ModuleViz(Kind k, juce::AudioProcessorValueTreeState& state) : kind(k), apvts(state)
{
    decimate = (k == Kind::wobble || k == Kind::magnetic) ? 8 : 1;
    setInterceptsMouseClicks(true, false);
}

void ModuleViz::pushFrame(const DecimulScourProcessor::Frame& f)
{
    accMin = std::min(accMin, f.noiseMin);
    accMax = std::max(accMax, f.noiseMax);
    accGain = std::min(accGain, f.magGain);
    accDelay = f.wobDelayMs;
    if (++decimateCount < decimate)
        return;
    decimateCount = 0;
    if (kind == Kind::noise) { histA[(size_t) head] = accMin; histB[(size_t) head] = accMax; }
    else if (kind == Kind::wobble) { histA[(size_t) head] = accDelay; histB[(size_t) head] = 0.0f; }
    else if (kind == Kind::magnetic) { histA[(size_t) head] = accGain; histB[(size_t) head] = 0.0f; }
    else return;
    head = (head + 1) % historySize;
    filled = std::min(filled + 1, historySize);
    accMin = accMax = 0.0f;
    accGain = 1.0f;
}

float ModuleViz::effectiveAmount(const juce::String& amountID, const juce::String& onID) const
{
    const float on = param(onID) > 0.5f ? 1.0f : 0.0f;
    return std::clamp(param(amountID) * 0.01f * param(ParamIDs::magnitude) * 0.01f, 0.0f, 1.0f) * on;
}

void ModuleViz::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(DecimuColours::background);
    g.fillRoundedRectangle(area, 5.0f);
    g.setColour(DecimuColours::textMuted.withAlpha(0.18f));
    g.drawRoundedRectangle(area.reduced(0.5f), 5.0f, 1.0f);

    const juce::String onIDs[] = { ParamIDs::noiseOn, ParamIDs::wobOn, ParamIDs::disOn, ParamIDs::digOn, ParamIDs::spaceOn, ParamIDs::magOn };
    const bool on = param(onIDs[(int) kind]) > 0.5f;
    const auto colour = on ? DecimuColours::accentPrimary : DecimuColours::textMuted.withAlpha(0.45f);
    auto plot = area.reduced(6.0f, 5.0f);

    switch (kind)
    {
        case Kind::noise:    paintNoise(g, plot, colour); break;
        case Kind::wobble:   paintWobble(g, plot, colour); break;
        case Kind::distort:  paintDistort(g, plot, colour); break;
        case Kind::digital:  paintDigital(g, plot, colour); break;
        case Kind::space:    paintSpace(g, plot, colour); break;
        case Kind::magnetic: paintMagnetic(g, plot, colour); break;
    }
}

void ModuleViz::paintNoise(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) const
{
    const float midY = r.getCentreY();
    g.setColour(DecimuColours::textMuted.withAlpha(0.22f));
    g.drawHorizontalLine((int) midY, r.getX(), r.getRight());
    if (filled < 2)
        return;

    // escala automática: sigue el pico reciente y baja despacio, así se ve a cualquier nivel
    float peak = 1.0e-5f;
    for (int k = 0; k < filled; ++k)
        peak = std::max({ peak, std::abs(histA[(size_t) k]), std::abs(histB[(size_t) k]) });
    noiseScale += (peak - noiseScale) * (peak > noiseScale ? 0.5f : 0.03f);

    const float step = r.getWidth() / (float) (historySize - 1);
    g.setColour(c);
    for (int k = 0; k < filled; ++k)
    {
        const int idx = (head - filled + k + historySize * 2) % historySize;
        const float x = r.getRight() - (float) (filled - 1 - k) * step;
        const float top = midY - std::clamp(histB[(size_t) idx] / noiseScale, -1.0f, 1.0f) * r.getHeight() * 0.48f;
        const float bottom = midY - std::clamp(histA[(size_t) idx] / noiseScale, -1.0f, 1.0f) * r.getHeight() * 0.48f;
        g.fillRect(juce::Rectangle<float>(x - step, top, step + 0.6f, std::max(1.0f, bottom - top)));
    }
}

void ModuleViz::paintWobble(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) const
{
    const float midY = r.getCentreY();
    g.setColour(DecimuColours::textMuted.withAlpha(0.22f));
    g.drawHorizontalLine((int) midY, r.getX(), r.getRight());
    if (filled < 2)
        return;

    float mean = 0.0f;
    for (int k = 0; k < filled; ++k) mean += histA[(size_t) k];
    mean /= (float) filled;
    float range = 0.4f;
    for (int k = 0; k < filled; ++k) range = std::max(range, std::abs(histA[(size_t) k] - mean));

    const float step = r.getWidth() / (float) (historySize - 1);
    juce::Path path;
    for (int k = 0; k < filled; ++k)
    {
        const int idx = (head - filled + k + historySize * 2) % historySize;
        const float x = r.getRight() - (float) (filled - 1 - k) * step;
        const float y = midY - std::clamp((histA[(size_t) idx] - mean) / range, -1.0f, 1.0f) * r.getHeight() * 0.42f;
        if (k == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.setColour(c);
    g.strokePath(path, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved));
}

void ModuleViz::paintDistort(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) const
{
    const auto sq = r.withSizeKeepingCentre(std::min(r.getWidth(), r.getHeight() * 1.9f), r.getHeight());
    g.setColour(DecimuColours::textMuted.withAlpha(0.22f));
    g.drawHorizontalLine((int) sq.getCentreY(), sq.getX(), sq.getRight());
    g.drawVerticalLine((int) sq.getCentreX(), sq.getY(), sq.getBottom());
    g.setColour(DecimuColours::textMuted.withAlpha(0.28f));
    g.drawLine(sq.getX(), sq.getBottom(), sq.getRight(), sq.getY(), 1.0f);        // sin distorsión: la diagonal

    const float a = effectiveAmount(ParamIDs::disAmt, ParamIDs::disOn);
    const int type = (int) param(ParamIDs::disType);
    float k = 1.0f, comp = 1.0f;
    scour::Distortion::driveFor(a, k, comp);

    juce::Path path;
    constexpr int points = 80;
    for (int i = 0; i < points; ++i)
    {
        const float x = -1.0f + 2.0f * (float) i / (float) (points - 1);
        const float y = a <= 0.0005f ? x : std::clamp(scour::Distortion::shape(type, k * x) * comp, -1.0f, 1.0f);
        const float px = sq.getX() + (x * 0.5f + 0.5f) * sq.getWidth();
        const float py = sq.getBottom() - (y * 0.5f + 0.5f) * sq.getHeight();
        if (i == 0) path.startNewSubPath(px, py); else path.lineTo(px, py);
    }
    g.setColour(c);
    g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
}

void ModuleViz::paintDigital(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) const
{
    const float midY = r.getCentreY();
    g.setColour(DecimuColours::textMuted.withAlpha(0.22f));
    g.drawHorizontalLine((int) midY, r.getX(), r.getRight());

    const float a = effectiveAmount(ParamIDs::digAmt, ParamIDs::digOn);
    const float balance = param(ParamIDs::digBalance) * 0.01f;
    const float wr = balance <= 0.5f ? 1.0f : (1.0f - balance) * 2.0f;
    const float wb = balance >= 0.5f ? 1.0f : balance * 2.0f;
    const float ratio = std::pow(2200.0f / 48000.0f, a * wr);
    const float levels = std::pow(2.0f, 16.0f - 12.0f * a * wb - 1.0f);

    constexpr int n = 200, perCycle = 100;
    const float amp = r.getHeight() * 0.42f;
    auto yOf = [&](float v) { return midY - v * amp; };
    auto xOf = [&](int i) { return r.getX() + (float) i / (float) (n - 1) * r.getWidth(); };

    juce::Path ideal, crushed;
    float phase = 1.0f, held = 0.0f, prevY = midY;
    for (int i = 0; i < n; ++i)
    {
        const float s = std::sin(6.2831853f * (float) i / (float) perCycle);
        phase += ratio;
        if (phase >= 1.0f) { phase -= 1.0f; held = s; }
        const float q = a <= 0.0005f ? s : std::floor(held * levels + 0.5f) / levels;
        const float y = yOf(q);
        if (i == 0) { ideal.startNewSubPath(xOf(i), yOf(s)); crushed.startNewSubPath(xOf(i), y); }
        else { ideal.lineTo(xOf(i), yOf(s)); crushed.lineTo(xOf(i), prevY); crushed.lineTo(xOf(i), y); }
        prevY = y;
    }
    g.setColour(DecimuColours::textMuted.withAlpha(0.35f));
    g.strokePath(ideal, juce::PathStrokeType(1.0f));
    g.setColour(c);
    g.strokePath(crushed, juce::PathStrokeType(1.8f));
}

void ModuleViz::paintSpace(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) const
{
    g.setColour(DecimuColours::textMuted.withAlpha(0.22f));
    g.drawHorizontalLine((int) r.getBottom(), r.getX(), r.getRight());

    const float a = effectiveAmount(ParamIDs::spaceAmt, ParamIDs::spaceOn);
    const float decay01 = param(ParamIDs::spaceDecay) * 0.01f;
    const float rt60 = 0.3f * std::pow(20.0f, decay01);
    const float pre = param(ParamIDs::spacePre) * 0.001f;
    constexpr float span = 3.0f;                           // segundos que muestra el eje

    const float level = a <= 0.0005f ? 0.0f : 0.25f + 0.75f * std::sqrt(a);
    constexpr int bars = 84;
    for (int i = 0; i < bars; ++i)
    {
        const float t = span * (float) i / (float) (bars - 1);
        const float x = r.getX() + t / span * r.getWidth();
        float h = 0.0f;
        if (i == 0)
            h = 1.0f;                                       // el sonido directo
        else if (t > pre)
            h = std::pow(10.0f, -3.0f * (t - pre) / rt60) * (0.45f + 0.55f * hash01(i));   // -60 dB en rt60
        h *= level;
        g.setColour(i == 0 ? DecimuColours::textMuted.withAlpha(0.6f) : c);
        const float len = h * r.getHeight() * 0.95f;
        g.fillRect(juce::Rectangle<float>(x - 1.0f, r.getBottom() - std::max(len, 0.0f), 2.0f, std::max(len, 0.0f)));
    }
}

void ModuleViz::paintMagnetic(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) const
{
    g.setColour(DecimuColours::textMuted.withAlpha(0.22f));
    g.drawHorizontalLine((int) r.getY() + 3, r.getX(), r.getRight());
    if (filled < 2)
        return;

    const float step = r.getWidth() / (float) (historySize - 1);
    const auto cut = juce::Colour(0xffE5533B);
    for (int k = 0; k < filled; ++k)
    {
        const int idx = (head - filled + k + historySize * 2) % historySize;
        const float x = r.getRight() - (float) (filled - 1 - k) * step;
        const float gain = std::clamp(histA[(size_t) idx], 0.0f, 1.0f);
        const float h = gain * (r.getHeight() - 3.0f);
        g.setColour((gain < 0.5f ? cut : c).withAlpha(0.9f));
        g.fillRect(juce::Rectangle<float>(x - step, r.getBottom() - h, step + 0.6f, std::max(1.0f, h)));
    }
}
