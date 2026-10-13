#include "VersusComponents.h"
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

    juce::Font textFont(juce::Component& c, float height, bool bold = false)
    {
        if (auto* laf = dynamic_cast<DecimuLookAndFeel*>(&c.getLookAndFeel()))
            return laf->displayFont(height, bold);
        return juce::Font(juce::FontOptions(height));
    }

    juce::String fmt(float v, int decimals = 1, bool signedValue = false)
    {
        juce::String s = juce::String(v, decimals);
        if (signedValue && v > 0.0f)
            s = "+" + s;
        return s;
    }
}

// ================================ Slot ================================

void VersusSlotButton::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onContextMenu)
            onContextMenu();
        return;
    }
    juce::Button::mouseDown(e);
}

void VersusSlotButton::paintButton(juce::Graphics& g, bool isMouseOver, bool)
{
    auto b = getLocalBounds().toFloat().reduced(1.5f);
    const auto accent = DecimuColours::accentPrimary;

    g.setColour(state.selected ? accent.withAlpha(0.14f)
                               : (isMouseOver ? DecimuColours::panel.brighter(0.15f) : DecimuColours::panel));
    g.fillRoundedRectangle(b, 6.0f);

    g.setColour(state.selected ? accent
                               : (state.hasError ? DecimuColours::accentRec
                                                 : DecimuColours::textMuted.withAlpha(0.5f)));
    g.drawRoundedRectangle(b, 6.0f, state.selected ? 1.8f : 1.0f);

    auto numberArea = b.removeFromLeft(36.0f);
    g.setColour(state.selected ? accent : DecimuColours::text);
    g.setFont(hudFont(*this, 28.0f));
    g.drawText(juce::String(index + 1), numberArea, juce::Justification::centred);

    juce::String line = state.hasError ? "ERROR AL CARGAR"
                      : state.loading  ? "CARGANDO..."
                      : state.loaded   ? state.name
                                       : "vacio - click para cargar";

    g.setColour(state.loaded ? DecimuColours::text : DecimuColours::textMuted);
    g.setFont(textFont(*this, 13.0f));
    g.drawFittedText(line, b.reduced(2.0f, 6.0f).toNearestInt(), juce::Justification::centredLeft, 2);
}

// ============================== Waveform ==============================

void VersusWaveformView::setTrack(std::shared_ptr<const ReferenceTrack> t)
{
    if (track != t)
    {
        track = std::move(t);
        repaint();
    }
}

void VersusWaveformView::setPlayhead(double seconds)
{
    if (std::abs(seconds - playhead) > 0.01)
    {
        playhead = seconds;
        repaint();
    }
}

void VersusWaveformView::setLoop(double startSeconds, double endSeconds, bool enabled)
{
    if (startSeconds != loopStart || endSeconds != loopEnd || enabled != loopOn)
    {
        loopStart = startSeconds;
        loopEnd = endSeconds;
        loopOn = enabled;
        repaint();
    }
}

double VersusWaveformView::xToSeconds(float x) const
{
    if (track == nullptr || getWidth() <= 0)
        return 0.0;
    return juce::jlimit(0.0, track->lengthSeconds, (double) x / (double) getWidth() * track->lengthSeconds);
}

float VersusWaveformView::secondsToX(double seconds) const
{
    if (track == nullptr || track->lengthSeconds <= 0.0)
        return 0.0f;
    return (float) (seconds / track->lengthSeconds * (double) getWidth());
}

void VersusWaveformView::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(DecimuColours::trackFill);
    g.fillRoundedRectangle(b, 6.0f);

    if (track == nullptr)
    {
        g.setColour(DecimuColours::textMuted);
        g.setFont(hudFont(*this, 20.0f));
        g.drawText(status.isNotEmpty() ? status : juce::String("ARRASTRA UN ARCHIVO ACA, O CLICK EN UN SLOT"),
                   b, juce::Justification::centred);
        return;
    }

    const float midY = b.getCentreY();
    const float halfH = b.getHeight() * 0.5f - 6.0f;
    const float playX = secondsToX(playhead);
    const int width = getWidth();
    const auto& peaks = track->wavePeaks;

    for (int x = 0; x < width; ++x)
    {
        const int col = std::clamp((int) ((juce::int64) x * ReferenceTrack::waveColumns / std::max(1, width)),
                                   0, ReferenceTrack::waveColumns - 1);
        const float lo = peaks[(size_t) col * 2];
        const float hi = peaks[(size_t) col * 2 + 1];

        g.setColour((float) x < playX ? DecimuColours::accentPrimary.withAlpha(0.85f)
                                      : DecimuColours::textMuted.withAlpha(0.65f));
        g.drawVerticalLine(x, midY - hi * halfH, midY - lo * halfH + 1.0f);
    }

    if (loopEnd > loopStart)
    {
        const float x0 = secondsToX(loopStart), x1 = secondsToX(loopEnd);
        g.setColour(DecimuColours::accentSecondary.withAlpha(loopOn ? 0.28f : 0.12f));
        g.fillRect(x0, 0.0f, x1 - x0, b.getHeight());
        g.setColour(DecimuColours::accentSecondary.withAlpha(loopOn ? 1.0f : 0.5f));
        g.drawVerticalLine((int) x0, 0.0f, b.getHeight());
        g.drawVerticalLine((int) x1, 0.0f, b.getHeight());
    }

    if (dragging)
    {
        const float x0 = std::min(dragStartX, dragCurrentX), x1 = std::max(dragStartX, dragCurrentX);
        g.setColour(DecimuColours::accentPrimary.withAlpha(0.25f));
        g.fillRect(x0, 0.0f, x1 - x0, b.getHeight());
    }

    g.setColour(DecimuColours::text);
    g.drawVerticalLine((int) playX, 0.0f, b.getHeight());

    // Aviso superpuesto (p. ej. "recargando el audio" de un slot que se había soltado).
    if (status.isNotEmpty())
    {
        auto banner = b.withSizeKeepingCentre(std::min(b.getWidth(), 360.0f), 26.0f);
        g.setColour(DecimuColours::background.withAlpha(0.85f));
        g.fillRoundedRectangle(banner, 5.0f);
        g.setColour(DecimuColours::accentPrimary);
        g.setFont(hudFont(*this, 17.0f));
        g.drawText(status, banner, juce::Justification::centred);
    }
}

void VersusWaveformView::mouseDown(const juce::MouseEvent& e)
{
    dragStartX = dragCurrentX = (float) e.x;
    dragging = false;
}

void VersusWaveformView::mouseDrag(const juce::MouseEvent& e)
{
    dragCurrentX = (float) e.x;
    if (!dragging && std::abs(dragCurrentX - dragStartX) > 6.0f)
        dragging = true;
    if (dragging)
        repaint();
}

void VersusWaveformView::mouseUp(const juce::MouseEvent& e)
{
    if (track == nullptr)
        return;

    if (dragging)
    {
        dragging = false;
        const double a = xToSeconds(std::min(dragStartX, (float) e.x));
        const double b = xToSeconds(std::max(dragStartX, (float) e.x));
        if (onLoop)
            onLoop(a, b);
        repaint();
    }
    else if (onSeek)
    {
        onSeek(xToSeconds((float) e.x));
    }
}

void VersusWaveformView::mouseDoubleClick(const juce::MouseEvent&)
{
    if (onLoop)
        onLoop(0.0, 0.0);
}

// ============================== Spectrum ==============================

void VersusSpectrumView::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(DecimuColours::trackFill);
    g.fillRoundedRectangle(b, 6.0f);

    auto plot = b.reduced(8.0f);
    plot.removeFromLeft(30.0f);
    plot.removeFromBottom(18.0f);
    plot.removeFromTop(16.0f);

    constexpr int nb = VersusBands::numBands;
    auto bandX = [&](int i) { return plot.getX() + (i + 0.5f) * plot.getWidth() / (float) nb; };

    g.setFont(hudFont(*this, 14.0f));

    // ---- Etiquetas de frecuencia ----
    static const struct { int band; const char* label; } marks[] = {
        { 1, "31" }, { 4, "63" }, { 7, "125" }, { 10, "250" }, { 13, "500" },
        { 16, "1k" }, { 19, "2k" }, { 22, "4k" }, { 25, "8k" }, { 28, "16k" }
    };
    for (auto& m : marks)
    {
        const float x = bandX(m.band);
        g.setColour(DecimuColours::textMuted.withAlpha(0.18f));
        g.drawVerticalLine((int) x, plot.getY(), plot.getBottom());
        g.setColour(DecimuColours::textMuted);
        g.drawText(m.label, juce::Rectangle<float>(x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 14.0f),
                   juce::Justification::centred);
    }

    if (!data.mixValid && !data.refValid)
    {
        g.setColour(DecimuColours::textMuted);
        g.setFont(hudFont(*this, 18.0f));
        g.drawText(data.emptyHint.isNotEmpty() ? data.emptyHint
                                               : juce::String("REPRODUCI LA MEZCLA Y/O UNA REFERENCIA PARA VER EL ESPECTRO"),
                   plot, juce::Justification::centred);
        return;
    }

    // ---- Modo diferencia: mezcla - (referencia con volumen igualado), por banda ----
    if (showDiff)
    {
        g.setColour(DecimuColours::textMuted);
        g.setFont(hudFont(*this, 14.0f));
        g.drawText("DIFERENCIA POR BANDA (MEZCLA - REF)   arriba = la mezcla tiene mas",
                   b.reduced(10.0f, 2.0f).removeFromTop(16.0f), juce::Justification::centredLeft);

        if (!(data.mixValid && data.refValid))
        {
            g.setFont(hudFont(*this, 18.0f));
            g.drawText("HACEN FALTA LAS DOS CURVAS: MEZCLA Y REFERENCIA", plot, juce::Justification::centred);
            return;
        }

        constexpr float range = 12.0f;
        const float zeroY = plot.getCentreY();
        const float scale = (plot.getHeight() * 0.5f) / range;

        g.setColour(DecimuColours::textMuted.withAlpha(0.5f));
        g.drawHorizontalLine((int) zeroY, plot.getX(), plot.getRight());
        for (float db : { -12.0f, -6.0f, 6.0f, 12.0f })
        {
            g.setColour(DecimuColours::textMuted.withAlpha(0.15f));
            g.drawHorizontalLine((int) (zeroY - db * scale), plot.getX(), plot.getRight());
            g.setColour(DecimuColours::textMuted);
            g.drawText(fmt(db, 0, true), juce::Rectangle<float>(b.getX() + 4.0f, zeroY - db * scale - 7.0f, 28.0f, 14.0f),
                       juce::Justification::centredRight);
        }

        const float barW = plot.getWidth() / (float) nb * 0.7f;
        for (int i = 0; i < nb; ++i)
        {
            const float diff = juce::jlimit(-range, range,
                                            (data.mix[(size_t) i] + data.mixGainDb)
                                                - (data.ref[(size_t) i] + data.refGainDb));
            g.setColour(diff >= 0.0f ? DecimuColours::accentRec : DecimuColours::accentSecondarySoft);
            const float h = std::abs(diff) * scale;
            g.fillRect(bandX(i) - barW * 0.5f, diff >= 0.0f ? zeroY - h : zeroY, barW, std::max(h, 1.0f));
        }
        return;
    }

    // ---- Curvas superpuestas ----
    float maxV = -200.0f;
    for (int i = 0; i < nb; ++i)
    {
        if (data.mixValid) maxV = std::max(maxV, data.mix[(size_t) i] + data.mixGainDb);
        if (data.refValid) maxV = std::max(maxV, data.ref[(size_t) i] + data.refGainDb);
    }
    const float top = std::ceil(maxV / 10.0f) * 10.0f + 5.0f;
    const float span = 60.0f;
    auto yOf = [&](float db) { return plot.getY() + (top - db) / span * plot.getHeight(); };

    g.setFont(hudFont(*this, 14.0f));
    for (float db = top; db > top - span; db -= 10.0f)
    {
        g.setColour(DecimuColours::textMuted.withAlpha(0.15f));
        g.drawHorizontalLine((int) yOf(db), plot.getX(), plot.getRight());
        g.setColour(DecimuColours::textMuted);
        g.drawText(fmt(db, 0), juce::Rectangle<float>(b.getX() + 2.0f, yOf(db) - 7.0f, 30.0f, 14.0f),
                   juce::Justification::centredRight);
    }

    auto makePath = [&](const std::array<float, nb>& values, float offsetDb)
    {
        juce::Path p;
        for (int i = 0; i < nb; ++i)
        {
            const float y = juce::jlimit(plot.getY(), plot.getBottom(), yOf(values[(size_t) i] + offsetDb));
            if (i == 0) p.startNewSubPath(bandX(i), y);
            else        p.lineTo(bandX(i), y);
        }
        return p;
    };

    if (data.refValid)
    {
        auto p = makePath(data.ref, data.refGainDb);
        juce::Path fill(p);
        fill.lineTo(bandX(nb - 1), plot.getBottom());
        fill.lineTo(bandX(0), plot.getBottom());
        fill.closeSubPath();
        g.setColour(DecimuColours::text.withAlpha(0.10f));
        g.fillPath(fill);
        g.setColour(DecimuColours::text.withAlpha(0.9f));
        g.strokePath(p.createPathWithRoundedCorners(6.0f), juce::PathStrokeType(2.0f));
    }

    if (data.mixValid)
    {
        auto p = makePath(data.mix, data.mixGainDb);
        g.setColour(DecimuColours::accentPrimary);
        g.strokePath(p.createPathWithRoundedCorners(6.0f), juce::PathStrokeType(2.6f));
    }

    // Leyenda
    g.setFont(hudFont(*this, 15.0f));
    auto legend = b.reduced(10.0f, 2.0f).removeFromTop(16.0f);
    g.setColour(DecimuColours::accentPrimary);
    g.drawText(data.mixName, legend.removeFromLeft(150.0f), juce::Justification::centredLeft);
    g.setColour(DecimuColours::text);
    g.drawText(data.refName, legend.removeFromLeft(320.0f), juce::Justification::centredLeft);
}

// ============================== Metrics ==============================

void VersusMetricsView::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(DecimuColours::trackFill);
    g.fillRoundedRectangle(b, 6.0f);

    auto area = b.reduced(10.0f, 8.0f);
    const float labelW = area.getWidth() * 0.34f;
    const float colW = (area.getWidth() - labelW) / 3.0f;
    const float rowH = 22.0f;

    g.setFont(hudFont(*this, 18.0f));

    auto row = area.removeFromTop(rowH);
    row.removeFromLeft(labelW);
    g.setColour(DecimuColours::accentPrimary);
    g.drawText(data.leftName, row.removeFromLeft(colW), juce::Justification::centredRight);
    g.setColour(DecimuColours::text);
    g.drawText(data.rightName, row.removeFromLeft(colW), juce::Justification::centredRight);
    g.setColour(DecimuColours::textMuted);
    g.drawText("DIFF", row.removeFromLeft(colW), juce::Justification::centredRight);

    auto addRow = [&](const juce::String& label, bool mixOk, float mixV, bool refOk, float refV,
                      int decimals, bool diffSigned = true)
    {
        auto r = area.removeFromTop(rowH);
        g.setColour(DecimuColours::textMuted);
        g.drawText(label, r.removeFromLeft(labelW), juce::Justification::centredLeft);

        g.setColour(DecimuColours::accentPrimary);
        g.drawText(mixOk ? fmt(mixV, decimals) : "--", r.removeFromLeft(colW), juce::Justification::centredRight);
        g.setColour(DecimuColours::text);
        g.drawText(refOk ? fmt(refV, decimals) : "--", r.removeFromLeft(colW), juce::Justification::centredRight);
        g.setColour(DecimuColours::textMuted);
        g.drawText((mixOk && refOk) ? fmt(mixV - refV, decimals, diffSigned) : "--",
                   r.removeFromLeft(colW), juce::Justification::centredRight);
    };

    const bool mI = data.mixValid && data.mixLufsI > -99.0f;
    const bool rI = data.refValid && data.refLufsI > -99.0f;
    const bool mS = data.mixValid && data.mixLufsS > -99.0f;
    const bool rS = data.refValid && data.refLufsS > -99.0f;

    addRow("LUFS-I", mI, data.mixLufsI, rI, data.refLufsI, 1);
    addRow("LUFS-S", mS, data.mixLufsS, rS, data.refLufsS, 1);
    addRow("TRUE PK", data.mixValid && data.mixTruePeak > -99.0f, data.mixTruePeak,
           data.refValid && data.refTruePeak > -99.0f, data.refTruePeak, 1);
    addRow("PLR dB", mI && data.mixTruePeak > -99.0f, data.mixTruePeak - data.mixLufsI,
           rI && data.refTruePeak > -99.0f, data.refTruePeak - data.refLufsI, 1);
    addRow("LRA LU", mI, data.mixLra, rI, data.refLra, 1);
    addRow("WIDTH %", data.mixValid, data.mixWidth, data.refValid, data.refWidth, 0);
    addRow("CORR", data.mixValid, data.mixCorr, data.refValid, data.refCorr, 2);

    area.removeFromTop(6.0f);
    g.setFont(hudFont(*this, 16.0f));
    g.setColour(DecimuColours::textMuted);

    juce::String gainLine;
    if (!data.refValid)
        gainLine = "SIN REFERENCIA CARGADA";
    else if (!data.matchAuto)
        gainLine = "IGUALADO APAGADO  (trim " + fmt(data.gainAppliedDb, 1, true) + " dB)";
    else
        gainLine = data.rightName + " " + fmt(data.gainAppliedDb, 1, true) + " dB PARA IGUALAR VOLUMEN";

    g.drawFittedText(gainLine, area.removeFromTop(36.0f).toNearestInt(), juce::Justification::topLeft, 2);

    if (data.refValid && data.gainLimited)
    {
        g.setColour(DecimuColours::accentRec);
        g.drawText("LIMITADO POR PICO (no pasa de 0 dBFS)", area.removeFromTop(18.0f),
                   juce::Justification::centredLeft);
    }
}
