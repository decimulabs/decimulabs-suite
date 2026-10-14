#include "HitScope.h"
#include <algorithm>
#include <cmath>

namespace
{
    juce::Font hudFont(juce::Component& c, float height)
    {
        if (auto* laf = dynamic_cast<DecimuLookAndFeel*>(&c.getLookAndFeel()))
            return laf->monoFont(height);
        return juce::Font(juce::FontOptions(height));
    }
}

// ================================ HitScope ================================

void HitScope::pushFrame(float levelLinear, int velocity)
{
    history.push_back({ levelLinear, velocity });
    while ((int) history.size() > historyFrames)
        history.pop_front();
}

void HitScope::setThresholdDb(float db)
{
    if (db != thresholdDb)
    {
        thresholdDb = db;
        repaint();
    }
}

void HitScope::setMaxLevelDb(float db)
{
    if (db != maxLevelDb)
    {
        maxLevelDb = db;
        repaint();
    }
}

juce::Rectangle<float> HitScope::plotArea() const
{
    return getLocalBounds().toFloat().reduced(10.0f, 10.0f).withTrimmedLeft(30.0f);
}

float HitScope::dbToY(float db) const
{
    auto a = plotArea();
    const float t = std::clamp((db - floorDb) / (0.0f - floorDb), 0.0f, 1.0f);
    return a.getBottom() - t * a.getHeight();
}

float HitScope::yToDb(float y) const
{
    auto a = plotArea();
    const float t = std::clamp((a.getBottom() - y) / a.getHeight(), 0.0f, 1.0f);
    return floorDb + t * (0.0f - floorDb);
}

int HitScope::hitLine(float y) const
{
    const float dt = std::abs(y - dbToY(thresholdDb));
    const float dm = std::abs(y - dbToY(maxLevelDb));
    if (std::min(dt, dm) > 7.0f)
        return -1;
    return dt <= dm ? 0 : 1;
}

void HitScope::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(DecimuColours::trackFill);
    g.fillRoundedRectangle(bounds, 6.0f);

    auto a = plotArea();
    const auto accent = DecimuColours::accentPrimary;

    // ---- Escala en dB ----
    g.setFont(hudFont(*this, 13.0f));
    for (float db = 0.0f; db >= floorDb; db -= 12.0f)
    {
        const float y = dbToY(db);
        g.setColour(DecimuColours::textMuted.withAlpha(0.16f));
        g.drawHorizontalLine((int) y, a.getX(), a.getRight());
        g.setColour(DecimuColours::textMuted);
        g.drawText(juce::String((int) db), juce::Rectangle<float>(bounds.getX() + 2.0f, y - 7.0f, 28.0f, 14.0f),
                   juce::Justification::centredRight);
    }

    // ---- Nivel (cada cuadro, una columna) ----
    const int count = (int) history.size();
    const float colW = a.getWidth() / (float) historyFrames;
    const float x0 = a.getRight() - (float) count * colW;

    for (int i = 0; i < count; ++i)
    {
        const auto& f = history[(size_t) i];
        const float db = 20.0f * std::log10(std::max(f.level, 1.0e-6f));
        const float y = dbToY(db);
        const float x = x0 + (float) i * colW;

        g.setColour(accent.withAlpha(0.55f));
        g.fillRect(x, y, std::max(1.0f, colW), a.getBottom() - y);
    }

    // ---- Golpes ----
    for (int i = 0; i < count; ++i)
    {
        const auto& f = history[(size_t) i];
        if (f.velocity <= 0)
            continue;

        const float x = x0 + (float) i * colW;
        const float h = (float) f.velocity / 127.0f * (a.getHeight() - 20.0f);

        g.setColour(DecimuColours::accentRec);
        g.fillRect(x - 1.0f, a.getBottom() - h, 3.0f, h);
        g.setFont(hudFont(*this, 15.0f));
        g.setColour(DecimuColours::text);
        g.drawText(juce::String(f.velocity), juce::Rectangle<float>(x - 16.0f, a.getBottom() - h - 16.0f, 34.0f, 14.0f),
                   juce::Justification::centred);
    }

    // ---- Líneas de umbral y de nivel máximo ----
    auto drawLine = [&](float db, const juce::String& label, juce::Colour colour, bool active)
    {
        const float y = dbToY(db);
        g.setColour(colour.withAlpha(active ? 1.0f : 0.8f));
        const float dashes[] = { 6.0f, 4.0f };
        g.drawDashedLine(juce::Line<float>(a.getX(), y, a.getRight(), y), dashes, 2, active ? 2.2f : 1.4f);
        g.setFont(hudFont(*this, 14.0f));
        g.drawText(label + " " + juce::String(db, 1) + " dB",
                   juce::Rectangle<float>(a.getX() + 6.0f, y - 17.0f, 150.0f, 14.0f),
                   juce::Justification::centredLeft);
    };
    drawLine(thresholdDb, "THRESHOLD", DecimuColours::accentRec, hoverLine == 0 || dragLine == 0);
    drawLine(maxLevelDb, "MAX LEVEL", DecimuColours::text, hoverLine == 1 || dragLine == 1);

    if (count == 0)
    {
        g.setColour(DecimuColours::textMuted);
        g.setFont(hudFont(*this, 18.0f));
        g.drawText("TOCA LA BATERIA: ACA SE VE EL NIVEL Y CADA GOLPE DETECTADO", a, juce::Justification::centred);
    }
}

void HitScope::mouseMove(const juce::MouseEvent& e)
{
    const int line = hitLine(e.position.y);
    if (line != hoverLine)
    {
        hoverLine = line;
        setMouseCursor(line >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void HitScope::mouseDown(const juce::MouseEvent& e)
{
    dragLine = hitLine(e.position.y);
}

void HitScope::mouseDrag(const juce::MouseEvent& e)
{
    if (dragLine < 0)
        return;

    const float db = yToDb(e.position.y);
    if (dragLine == 0 && onThresholdDragged)
        onThresholdDragged(std::clamp(db, -60.0f, 0.0f));
    else if (dragLine == 1 && onMaxLevelDragged)
        onMaxLevelDragged(std::clamp(db, -40.0f, 0.0f));
}

void HitScope::mouseUp(const juce::MouseEvent&)
{
    dragLine = -1;
    repaint();
}

// ============================== HitReadout ==============================

void HitReadout::setState(int newVelocity, const juce::String& newNote, int newHits, float newFlash)
{
    if (newVelocity != velocity || newNote != noteName || newHits != hits || std::abs(newFlash - flash) > 0.02f)
    {
        velocity = newVelocity;
        noteName = newNote;
        hits = newHits;
        flash = newFlash;
        repaint();
    }
}

void HitReadout::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(DecimuColours::trackFill);
    g.fillRoundedRectangle(b, 6.0f);

    if (flash > 0.0f)
    {
        g.setColour(DecimuColours::accentRec.withAlpha(0.35f * flash));
        g.fillRoundedRectangle(b, 6.0f);
    }

    auto area = b.reduced(12.0f, 10.0f);
    g.setColour(DecimuColours::textMuted);
    g.setFont(hudFont(*this, 16.0f));
    g.drawText("ULTIMO GOLPE", area.removeFromTop(18.0f), juce::Justification::centred);

    g.setColour(velocity > 0 ? DecimuColours::accentPrimary : DecimuColours::textMuted);
    g.setFont(hudFont(*this, 74.0f));
    g.drawText(velocity > 0 ? juce::String(velocity) : juce::String("--"), area.removeFromTop(78.0f),
               juce::Justification::centred);

    g.setColour(DecimuColours::textMuted);
    g.setFont(hudFont(*this, 16.0f));
    g.drawText("VELOCITY  /  " + noteName, area.removeFromTop(20.0f), juce::Justification::centred);

    auto bar = area.removeFromTop(14.0f).reduced(6.0f, 0.0f);
    g.setColour(DecimuColours::background);
    g.fillRoundedRectangle(bar, 3.0f);
    g.setColour(DecimuColours::accentPrimary);
    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * (float) velocity / 127.0f), 3.0f);

    area.removeFromTop(8.0f);
    g.setColour(DecimuColours::textMuted);
    g.drawText("GOLPES: " + juce::String(hits), area.removeFromTop(18.0f), juce::Justification::centred);
}
