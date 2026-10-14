#include "CurveEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float pointRadius = 6.5f;
    constexpr float hitRadius = 10.0f;
}

void GapCurveEditor::setCurve(const GapCurve& newCurve)
{
    curve = newCurve;
    curve.sanitize();
    repaint();
}

void GapCurveEditor::setPlayState(float newPhase, float newGain)
{
    if (std::abs(newPhase - phase) > 0.0005f || std::abs(newGain - gain) > 0.0005f)
    {
        phase = newPhase;
        gain = newGain;
        repaint();
    }
}

void GapCurveEditor::setDepth(float newDepth)
{
    if (newDepth != depth)
    {
        depth = newDepth;
        repaint();
    }
}

void GapCurveEditor::setGridDivisions(int divisions)
{
    if (divisions != gridDivisions)
    {
        gridDivisions = std::max(1, divisions);
        repaint();
    }
}

void GapCurveEditor::setAccentActive(bool) {}

juce::Rectangle<float> GapCurveEditor::plotArea() const
{
    return getLocalBounds().toFloat().reduced(18.0f, 16.0f);
}

juce::Point<float> GapCurveEditor::toPixel(float x, float y) const
{
    auto a = plotArea();
    return { a.getX() + x * a.getWidth(), a.getY() + (1.0f - y) * a.getHeight() };
}

float GapCurveEditor::xFromPixel(float px) const
{
    auto a = plotArea();
    return std::clamp((px - a.getX()) / a.getWidth(), 0.0f, 1.0f);
}

float GapCurveEditor::yFromPixel(float py) const
{
    auto a = plotArea();
    return std::clamp(1.0f - (py - a.getY()) / a.getHeight(), 0.0f, 1.0f);
}

juce::Point<float> GapCurveEditor::bendHandlePosition(int segment) const
{
    const auto& a = curve.points[(size_t) segment];
    const auto& b = curve.points[(size_t) segment + 1];
    const float mx = 0.5f * (a.x + b.x);
    return toPixel(mx, curve.evaluate(mx));
}

int GapCurveEditor::hitPoint(juce::Point<float> p) const
{
    int best = -1;
    float bestDist = hitRadius;
    for (size_t i = 0; i < curve.points.size(); ++i)
    {
        const float d = toPixel(curve.points[i].x, curve.points[i].y).getDistanceFrom(p);
        if (d < bestDist)
        {
            bestDist = d;
            best = (int) i;
        }
    }
    return best;
}

int GapCurveEditor::hitBendHandle(juce::Point<float> p) const
{
    for (size_t i = 0; i + 1 < curve.points.size(); ++i)
    {
        const auto& a = curve.points[i];
        const auto& b = curve.points[i + 1];
        if (b.x - a.x < 0.03f || std::abs(b.y - a.y) < 0.03f)
            continue;   // tramo demasiado corto o plano: no tiene nada que curvar
        if (bendHandlePosition((int) i).getDistanceFrom(p) < hitRadius)
            return (int) i;
    }
    return -1;
}

void GapCurveEditor::notifyChanged()
{
    if (onCurveChanged)
        onCurveChanged(curve);
}

void GapCurveEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(DecimuColours::trackFill);
    g.fillRoundedRectangle(bounds, 6.0f);

    auto a = plotArea();
    const auto accent = DecimuColours::accentPrimary;

    // ---- Grilla ----
    for (int i = 0; i <= gridDivisions; ++i)
    {
        const float x = a.getX() + a.getWidth() * (float) i / (float) gridDivisions;
        g.setColour(DecimuColours::textMuted.withAlpha(i == 0 || i == gridDivisions ? 0.35f : 0.16f));
        g.drawVerticalLine((int) x, a.getY(), a.getBottom());
    }
    for (float v : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        const float y = a.getY() + (1.0f - v) * a.getHeight();
        g.setColour(DecimuColours::textMuted.withAlpha(v == 0.0f || v == 1.0f ? 0.35f : 0.14f));
        g.drawHorizontalLine((int) y, a.getX(), a.getRight());
    }

    // ---- Curva (con relleno) ----
    const int steps = 240;
    juce::Path line;
    for (int i = 0; i <= steps; ++i)
    {
        const float x = (float) i / (float) steps;
        const auto p = toPixel(x, curve.evaluate(x));
        if (i == 0) line.startNewSubPath(p);
        else        line.lineTo(p);
    }

    juce::Path fill(line);
    fill.lineTo(toPixel(1.0f, 0.0f));
    fill.lineTo(toPixel(0.0f, 0.0f));
    fill.closeSubPath();
    g.setColour(accent.withAlpha(0.14f));
    g.fillPath(fill);

    // Con depth < 100 %, la curva que realmente se aplica es más suave: se dibuja tenue.
    if (depth < 0.995f)
    {
        juce::Path eff;
        for (int i = 0; i <= steps; ++i)
        {
            const float x = (float) i / (float) steps;
            const auto p = toPixel(x, 1.0f - depth * (1.0f - curve.evaluate(x)));
            if (i == 0) eff.startNewSubPath(p);
            else        eff.lineTo(p);
        }
        g.setColour(DecimuColours::text.withAlpha(0.55f));
        const float dashes[] = { 5.0f, 4.0f };
        juce::Path dashed;
        juce::PathStrokeType(1.4f).createDashedStroke(dashed, eff, dashes, 2);
        g.fillPath(dashed);
    }

    g.setColour(accent);
    g.strokePath(line, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved));

    // ---- Cabezal ----
    {
        const float x = a.getX() + phase * a.getWidth();
        g.setColour(DecimuColours::text.withAlpha(0.75f));
        g.drawVerticalLine((int) x, a.getY(), a.getBottom());
        const auto dot = toPixel(phase, gain);
        g.setColour(DecimuColours::accentRec);
        g.fillEllipse(dot.x - 5.0f, dot.y - 5.0f, 10.0f, 10.0f);
    }

    // ---- Manijas de curvatura y puntos ----
    for (size_t i = 0; i + 1 < curve.points.size(); ++i)
    {
        const auto& p0 = curve.points[i];
        const auto& p1 = curve.points[i + 1];
        if (p1.x - p0.x < 0.03f || std::abs(p1.y - p0.y) < 0.03f)
            continue;

        const auto h = bendHandlePosition((int) i);
        const bool active = (int) i == hoverBend || (int) i == dragBend;
        juce::Path diamond;
        diamond.addPolygon(h, 4, active ? 6.0f : 4.5f, juce::MathConstants<float>::pi / 4.0f);
        g.setColour(active ? DecimuColours::text : DecimuColours::textMuted);
        g.fillPath(diamond);
    }

    for (size_t i = 0; i < curve.points.size(); ++i)
    {
        const auto p = toPixel(curve.points[i].x, curve.points[i].y);
        const bool active = (int) i == hoverPoint || (int) i == dragPoint;

        g.setColour(DecimuColours::background);
        g.fillEllipse(p.x - pointRadius - 1.5f, p.y - pointRadius - 1.5f, (pointRadius + 1.5f) * 2.0f, (pointRadius + 1.5f) * 2.0f);
        g.setColour(active ? DecimuColours::text : accent);
        g.fillEllipse(p.x - pointRadius, p.y - pointRadius, pointRadius * 2.0f, pointRadius * 2.0f);
    }
}

void GapCurveEditor::mouseMove(const juce::MouseEvent& e)
{
    const auto p = e.position;
    const int hp = hitPoint(p);
    const int hb = hp >= 0 ? -1 : hitBendHandle(p);

    if (hp != hoverPoint || hb != hoverBend)
    {
        hoverPoint = hp;
        hoverBend = hb;
        setMouseCursor(hp >= 0 ? juce::MouseCursor::DraggingHandCursor
                                : (hb >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor));
        repaint();
    }
}

void GapCurveEditor::mouseDown(const juce::MouseEvent& e)
{
    const int hp = hitPoint(e.position);

    if (e.mods.isPopupMenu())
    {
        // Clic derecho sobre un punto del medio: lo borra.
        if (hp > 0 && hp < (int) curve.points.size() - 1)
        {
            curve.points.erase(curve.points.begin() + hp);
            hoverPoint = -1;
            notifyChanged();
            repaint();
        }
        return;
    }

    dragPoint = hp;
    dragBend = hp >= 0 ? -1 : hitBendHandle(e.position);
}

void GapCurveEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (dragPoint >= 0)
    {
        auto& pt = curve.points[(size_t) dragPoint];
        float x = xFromPixel(e.position.x);
        float y = yFromPixel(e.position.y);

        if (e.mods.isShiftDown())
        {
            x = std::round(x * 16.0f) / 16.0f;
            y = std::round(y * 20.0f) / 20.0f;
        }

        const bool isFirst = dragPoint == 0;
        const bool isLast = dragPoint == (int) curve.points.size() - 1;
        if (!isFirst && !isLast)
        {
            const float lo = curve.points[(size_t) dragPoint - 1].x + GapCurve::minSpacing;
            const float hi = curve.points[(size_t) dragPoint + 1].x - GapCurve::minSpacing;
            pt.x = std::clamp(x, lo, hi);
        }
        pt.y = y;

        notifyChanged();
        repaint();
    }
    else if (dragBend >= 0)
    {
        auto& a = curve.points[(size_t) dragBend];
        const auto& b = curve.points[(size_t) dragBend + 1];

        // Qué valor tiene que tener el tramo en su punto medio para seguir al mouse.
        const float target = yFromPixel(e.position.y);
        const float s = std::clamp((target - a.y) / (b.y - a.y), 0.02f, 0.98f);

        float bend;
        if (s <= 0.5f)
            bend = (std::log(s) / std::log(0.5f) - 1.0f) / 3.0f;          // s = 0.5^(1+3b)
        else
            bend = (1.0f - std::log(1.0f - s) / std::log(0.5f)) / 3.0f;   // s = 1 - 0.5^(1-3b)

        a.bend = std::clamp(bend, -1.0f, 1.0f);
        notifyChanged();
        repaint();
    }
}

void GapCurveEditor::mouseUp(const juce::MouseEvent&)
{
    dragPoint = dragBend = -1;
    repaint();
}

void GapCurveEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int hp = hitPoint(e.position);

    if (hp > 0 && hp < (int) curve.points.size() - 1)
    {
        curve.points.erase(curve.points.begin() + hp);
        hoverPoint = -1;
        notifyChanged();
        repaint();
        return;
    }

    if (hp < 0 && hitBendHandle(e.position) < 0 && (int) curve.points.size() < maxPoints)
    {
        const float x = xFromPixel(e.position.x);
        CurvePoint np;
        np.x = x;
        np.y = curve.evaluate(x);   // arranca sobre la curva: no la cambia hasta que lo muevas
        np.bend = 0.0f;
        curve.points.push_back(np);
        curve.sanitize();
        notifyChanged();
        repaint();
    }
}
