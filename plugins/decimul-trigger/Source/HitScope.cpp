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

void HitScope::setCalibration(float progress)
{
    if (std::abs(progress - calibration) > 0.004f)
    {
        calibration = progress;
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

    // ---- Golpes (rojos) y rechazados (grises) ----
    for (int i = 0; i < count; ++i)
    {
        const auto& f = history[(size_t) i];
        if (f.velocity == 0)
            continue;

        const bool rejected = f.velocity < 0;
        const int velocity = std::abs(f.velocity);
        const float x = x0 + (float) i * colW;
        const float h = (float) velocity / 127.0f * (a.getHeight() - 20.0f);

        g.setColour(rejected ? DecimuColours::textMuted : DecimuColours::accentRec);
        g.fillRect(x - 1.0f, a.getBottom() - h, 3.0f, h);
        g.setFont(hudFont(*this, 15.0f));
        g.setColour(rejected ? DecimuColours::textMuted : DecimuColours::text);
        g.drawText(rejected ? juce::String("x") : juce::String(velocity),
                   juce::Rectangle<float>(x - 16.0f, a.getBottom() - h - 16.0f, 34.0f, 14.0f),
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

    if (calibration >= 0.0f)
    {
        auto banner = a.withSizeKeepingCentre(std::min(a.getWidth() - 40.0f, 560.0f), 46.0f).withY(a.getY() + 8.0f);
        g.setColour(DecimuColours::background.withAlpha(0.88f));
        g.fillRoundedRectangle(banner, 6.0f);
        g.setColour(accent);
        g.drawRoundedRectangle(banner, 6.0f, 1.4f);

        g.setFont(hudFont(*this, 17.0f));
        g.drawText("CALIBRANDO - TOCA GOLPES DE SUAVE A FUERTE", banner.withTrimmedBottom(14.0f), juce::Justification::centred);

        auto inner = banner.reduced(12.0f, 0.0f);
        auto bar = inner.removeFromBottom(14.0f).reduced(0.0f, 4.0f);
        g.setColour(DecimuColours::trackFill);
        g.fillRoundedRectangle(bar, 2.0f);
        g.setColour(accent);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * std::clamp(calibration, 0.0f, 1.0f)), 2.0f);
    }
    else if (count == 0)
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

void HitReadout::setState(const State& s)
{
    if (s.velocity != state.velocity || s.noteName != state.noteName || s.hits != state.hits
        || s.rejected != state.rejected || std::abs(s.similarity - state.similarity) > 0.004f
        || s.fingerprintOn != state.fingerprintOn || std::abs(s.flash - state.flash) > 0.02f)
    {
        state = s;
        repaint();
    }
}

void HitReadout::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(DecimuColours::trackFill);
    g.fillRoundedRectangle(b, 6.0f);

    if (state.flash > 0.0f)
    {
        g.setColour(DecimuColours::accentRec.withAlpha(0.35f * state.flash));
        g.fillRoundedRectangle(b, 6.0f);
    }

    auto area = b.reduced(12.0f, 8.0f);
    g.setColour(DecimuColours::textMuted);
    g.setFont(hudFont(*this, 16.0f));
    g.drawText("ULTIMO GOLPE", area.removeFromTop(18.0f), juce::Justification::centred);

    g.setColour(state.velocity > 0 ? DecimuColours::accentPrimary : DecimuColours::textMuted);
    g.setFont(hudFont(*this, 66.0f));
    g.drawText(state.velocity > 0 ? juce::String(state.velocity) : juce::String("--"), area.removeFromTop(66.0f),
               juce::Justification::centred);

    g.setColour(DecimuColours::textMuted);
    g.setFont(hudFont(*this, 16.0f));
    g.drawText("VELOCITY  /  " + state.noteName, area.removeFromTop(18.0f), juce::Justification::centred);

    auto bar = area.removeFromTop(12.0f).reduced(6.0f, 0.0f);
    g.setColour(DecimuColours::background);
    g.fillRoundedRectangle(bar, 3.0f);
    g.setColour(DecimuColours::accentPrimary);
    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * (float) state.velocity / 127.0f), 3.0f);

    area.removeFromTop(6.0f);
    g.setColour(DecimuColours::textMuted);
    g.drawText("GOLPES: " + juce::String(state.hits), area.removeFromTop(16.0f), juce::Justification::centred);

    if (state.fingerprintOn)
    {
        const juce::String sim = state.similarity >= 0.0f ? juce::String(state.similarity, 2) : juce::String("--");
        g.drawText("PARECIDO " + sim + "   RECHAZ. " + juce::String(state.rejected),
                   area.removeFromTop(16.0f), juce::Justification::centred);
    }
}

// ============================ FingerprintView ============================

void FingerprintView::setData(const FingerprintBank::Vector& templateVector, bool templateValid,
                              const FingerprintBank::Vector& lastVector, bool lastValid, bool learning)
{
    tpl = templateVector;
    tplValid = templateValid;
    last = lastVector;
    lastOk = lastValid;
    isLearning = learning;
    repaint();
}

void FingerprintView::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(DecimuColours::trackFill);
    g.fillRoundedRectangle(b, 6.0f);

    auto area = b.reduced(8.0f, 6.0f);
    area.removeFromBottom(14.0f);

    if (!tplValid && !isLearning && !lastOk)
    {
        g.setColour(DecimuColours::textMuted);
        g.setFont(hudFont(*this, 14.0f));
        g.drawFittedText("SIN HUELLA: toca solo el instrumento y apreta APRENDER", b.reduced(8.0f).toNearestInt(),
                         juce::Justification::centred, 3);
        return;
    }

    const int n = FingerprintBank::numBands;
    float maxV = 0.0001f;
    for (int k = 0; k < n; ++k)
    {
        maxV = std::max(maxV, tpl[(size_t) k]);
        if (lastOk)
            maxV = std::max(maxV, last[(size_t) k]);
    }

    const float slot = area.getWidth() / (float) n;
    for (int k = 0; k < n; ++k)
    {
        const float x = area.getX() + (float) k * slot;

        if (tplValid)
        {
            const float h = tpl[(size_t) k] / maxV * area.getHeight();
            g.setColour(DecimuColours::accentPrimary.withAlpha(0.75f));
            g.fillRect(x + 2.0f, area.getBottom() - h, slot - 4.0f, h);
        }
        if (lastOk)
        {
            const float y = area.getBottom() - last[(size_t) k] / maxV * area.getHeight();
            g.setColour(DecimuColours::text);
            g.fillRect(x + 1.0f, y - 1.5f, slot - 2.0f, 3.0f);
        }
    }

    g.setColour(DecimuColours::textMuted);
    g.setFont(hudFont(*this, 12.0f));
    auto labels = b.reduced(8.0f, 0.0f).removeFromBottom(14.0f);
    g.drawText("90 Hz", labels, juce::Justification::centredLeft);
    g.drawText("11 kHz", labels, juce::Justification::centredRight);
    if (isLearning)
        g.drawText("APRENDIENDO", labels, juce::Justification::centred);
}
