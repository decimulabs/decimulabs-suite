#include "PumpCurve.h"
#include <algorithm>
#include <cmath>

namespace
{
    struct PresetDef
    {
        const char* name;
        std::vector<CurvePoint> points;
    };

    // x, y, bend (del tramo que sale de ese punto)
    const std::vector<PresetDef>& presets()
    {
        static const std::vector<PresetDef> defs = {
            { "Classic Pump",  { { 0.00f, 0.00f, -0.55f }, { 0.55f, 1.00f, 0.0f }, { 1.00f, 1.00f, 0.0f } } },
            { "Hard Pump",     { { 0.00f, 0.00f, -0.30f }, { 0.78f, 1.00f, 0.0f }, { 1.00f, 1.00f, 0.0f } } },
            { "Soft Pump",     { { 0.00f, 0.35f, -0.60f }, { 0.50f, 1.00f, 0.0f }, { 1.00f, 1.00f, 0.0f } } },
            { "Tight Kick",    { { 0.00f, 0.00f, -0.50f }, { 0.25f, 1.00f, 0.0f }, { 1.00f, 1.00f, 0.0f } } },
            { "Long Swell",    { { 0.00f, 0.00f,  0.35f }, { 0.90f, 1.00f, 0.0f }, { 1.00f, 1.00f, 0.0f } } },
            { "Bounce",        { { 0.00f, 0.00f,  0.00f }, { 0.20f, 0.85f, 0.0f }, { 0.30f, 0.45f, -0.4f },
                                 { 0.65f, 1.00f, 0.0f }, { 1.00f, 1.00f, 0.0f } } },
            { "Smooth Sine",   { { 0.00f, 1.00f,  0.30f }, { 0.25f, 0.50f, -0.30f }, { 0.50f, 0.05f, 0.30f },
                                 { 0.75f, 0.50f, -0.30f }, { 1.00f, 1.00f, 0.0f } } },
            { "Trance Gate",   { { 0.00f, 0.00f,  0.00f }, { 0.12f, 0.00f, 0.0f }, { 0.16f, 1.00f, 0.0f },
                                 { 0.50f, 1.00f, 0.0f }, { 0.54f, 0.00f, 0.0f }, { 0.62f, 0.00f, 0.0f },
                                 { 0.66f, 1.00f, 0.0f }, { 1.00f, 1.00f, 0.0f } } },
            { "Offbeat Duck",  { { 0.00f, 1.00f,  0.00f }, { 0.40f, 1.00f, 0.0f }, { 0.50f, 0.00f, -0.5f },
                                 { 0.90f, 1.00f, 0.0f }, { 1.00f, 1.00f, 0.0f } } },
            { "Reverse Pump",  { { 0.00f, 1.00f,  0.55f }, { 0.88f, 0.00f, 0.0f }, { 1.00f, 1.00f, 0.0f } } },
        };
        return defs;
    }
}

PumpCurve::PumpCurve()
{
    points = presets().front().points;
}

float PumpCurve::evaluate(float x) const
{
    if (points.size() < 2)
        return points.empty() ? 1.0f : points.front().y;

    x = std::clamp(x, 0.0f, 1.0f);

    size_t i = 0;
    while (i + 2 < points.size() && x > points[i + 1].x)
        ++i;

    const auto& a = points[i];
    const auto& b = points[i + 1];
    const float span = b.x - a.x;
    if (span <= 1.0e-6f)
        return b.y;

    const float t = std::clamp((x - a.x) / span, 0.0f, 1.0f);
    const float bend = std::clamp(a.bend, -1.0f, 1.0f);
    const float shaped = bend >= 0.0f ? std::pow(t, 1.0f + 3.0f * bend)
                                      : 1.0f - std::pow(1.0f - t, 1.0f - 3.0f * bend);
    return a.y + (b.y - a.y) * shaped;
}

void PumpCurve::fillTable(float* table) const
{
    for (int i = 0; i <= tableSize; ++i)
        table[i] = std::clamp(evaluate((float) i / (float) tableSize), 0.0f, 1.0f);
}

void PumpCurve::sanitize()
{
    if (points.size() < 2)
        points = presets().front().points;

    std::stable_sort(points.begin(), points.end(),
                     [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });

    for (auto& p : points)
    {
        p.x = std::clamp(p.x, 0.0f, 1.0f);
        p.y = std::clamp(p.y, 0.0f, 1.0f);
        p.bend = std::clamp(p.bend, -1.0f, 1.0f);
    }

    points.front().x = 0.0f;
    points.back().x = 1.0f;

    // Separación mínima para que ningún tramo tenga ancho cero.
    for (size_t i = 1; i + 1 < points.size(); ++i)
        points[i].x = std::clamp(points[i].x, points[i - 1].x + minSpacing, 1.0f - minSpacing);
}

std::unique_ptr<juce::XmlElement> PumpCurve::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement>("CURVE");
    for (auto& p : points)
    {
        auto* e = xml->createNewChildElement("P");
        e->setAttribute("x", (double) p.x);
        e->setAttribute("y", (double) p.y);
        e->setAttribute("b", (double) p.bend);
    }
    return xml;
}

PumpCurve PumpCurve::fromXml(const juce::XmlElement& xml)
{
    PumpCurve curve;
    curve.points.clear();

    for (auto* e : xml.getChildWithTagNameIterator("P"))
    {
        CurvePoint p;
        p.x = (float) e->getDoubleAttribute("x", 0.0);
        p.y = (float) e->getDoubleAttribute("y", 1.0);
        p.bend = (float) e->getDoubleAttribute("b", 0.0);
        curve.points.push_back(p);
    }

    curve.sanitize();
    return curve;
}

int PumpCurve::numPresets() { return (int) presets().size(); }

juce::String PumpCurve::presetName(int index)
{
    return presets()[(size_t) std::clamp(index, 0, numPresets() - 1)].name;
}

PumpCurve PumpCurve::preset(int index)
{
    PumpCurve curve;
    curve.points = presets()[(size_t) std::clamp(index, 0, numPresets() - 1)].points;
    curve.sanitize();
    return curve;
}
