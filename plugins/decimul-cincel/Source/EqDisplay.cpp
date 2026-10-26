#include "EqDisplay.h"
#include "dsp/CincelFit.h"

using cincel::BandParams;
using cincel::Placement;
using cincel::Shape;

namespace
{
    constexpr double fMin = SpectrumAnalyzer::loHz;
    constexpr double fMax = SpectrumAnalyzer::hiHz;

    juce::String tr(bool es, const char* en, const char* spa)
    {
        return juce::String(juce::CharPointer_UTF8(es ? spa : en));
    }

    const char* shapeNameEn[] = { "Bell", "Low Shelf", "Low Cut", "High Shelf", "High Cut", "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt" };
    const char* shapeNameEs[] = { "Campana", "Shelf grave", "Corte grave", "Shelf agudo", "Corte agudo", "Notch", "Pasabanda", "Tilt shelf", "Tilt plano" };
    const char* slopeName[] = { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct", "Brickwall" };
    const char* placeNameEn[] = { "Stereo", "Left", "Right", "Mid", "Side" };
    const char* placeNameEs[] = { "Estéreo", "Izquierda", "Derecha", "Centro (Mid)", "Lados (Side)" };
}

EqDisplay::EqDisplay(DecimulCincelProcessor& p) : proc(p)
{
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    curveValid.fill(false);
}

juce::Colour EqDisplay::bandColour(int index)
{
    // Tonos repartidos con la proporción áurea: dos bandas vecinas nunca se parecen.
    const float hue = std::fmod(0.58f + index * 0.381966f, 1.0f);
    return juce::Colour::fromHSV(hue, 0.62f, 1.0f, 1.0f);
}

juce::String EqDisplay::noteName(double hz)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
    const int n = (int) std::lround(midi);
    const int cents = (int) std::lround((midi - n) * 100.0);
    juce::String s = juce::String(names[((n % 12) + 12) % 12]) + juce::String(n / 12 - 1);
    if (cents != 0)
        s << (cents > 0 ? " +" : " ") << cents;
    return s;
}

juce::String EqDisplay::formatHz(double hz)
{
    if (hz >= 10000.0)
        return juce::String(hz / 1000.0, 1) + " kHz";
    if (hz >= 1000.0)
        return juce::String(hz / 1000.0, 2) + " kHz";
    if (hz >= 100.0)
        return juce::String((int) std::lround(hz)) + " Hz";
    return juce::String(hz, 1) + " Hz";
}

float EqDisplay::eqRangeDb() const
{
    static constexpr float r[] = { 3.0f, 6.0f, 12.0f, 30.0f };
    return r[juce::jlimit(0, 3, (int) proc.uiState().getProperty("range", 2))];
}

float EqDisplay::analyzerRangeDb() const
{
    static constexpr float r[] = { 60.0f, 90.0f, 120.0f };
    return r[juce::jlimit(0, 2, (int) proc.uiState().getProperty("anaRange", 1))];
}

juce::Font EqDisplay::mono(float h) const
{
    if (auto* laf = dynamic_cast<DecimuLookAndFeel*>(&getLookAndFeel()))
        return laf->monoFont(h);
    return juce::Font(juce::FontOptions(h));
}

//==============================================================================
float EqDisplay::xForHz(double hz) const
{
    return plot.getX() + plot.getWidth() * (float) (std::log(hz / fMin) / std::log(fMax / fMin));
}

double EqDisplay::hzForX(float x) const
{
    return fMin * std::pow(fMax / fMin, juce::jlimit(0.0, 1.0, (double) ((x - plot.getX()) / plot.getWidth())));
}

float EqDisplay::yForDb(double db) const
{
    const float half = plot.getHeight() * 0.5f - 10.0f;
    return plot.getCentreY() - (float) (db / eqRangeDb()) * half;
}

double EqDisplay::dbForY(float y) const
{
    const float half = plot.getHeight() * 0.5f - 10.0f;
    return (plot.getCentreY() - y) / half * eqRangeDb();
}

float EqDisplay::yForAnalyzer(double db, double hz) const
{
    static constexpr float tilts[] = { 0.0f, 1.5f, 3.0f, 4.5f, 6.0f };
    const float tilt = tilts[juce::jlimit(0, 4, (int) proc.uiState().getProperty("anaTilt", 3))];
    const double t = db + tilt * std::log2(hz / 1000.0);
    return plot.getY() + (float) (-t / analyzerRangeDb()) * plot.getHeight();
}

double EqDisplay::displayedGain(int i) const
{
    const auto& b = bands[(size_t) i];
    if (!cincel::shapeHasGain(b.shape))
        return 0.0;
    const double g = b.gainDb * gainScale;
    return (b.shape == Shape::TiltShelf || b.shape == Shape::FlatTilt) ? 0.5 * g : g;
}

juce::Point<float> EqDisplay::dotPosition(int i) const
{
    const auto& b = bands[(size_t) i];
    return { xForHz(b.freq), juce::jlimit(plot.getY() + 6.0f, plot.getBottom() - 6.0f, yForDb(displayedGain(i))) };
}

int EqDisplay::hitBand(juce::Point<float> p) const
{
    int best = -1;
    float bestD = 11.0f;
    for (int i = 0; i < cincel::maxBands; ++i)
    {
        if (!bands[(size_t) i].used)
            continue;
        const float d = dotPosition(i).getDistanceFrom(p);
        // Ante empate, gana la elegida.
        const float bias = std::find(selection.begin(), selection.end(), i) != selection.end() ? -2.0f : 0.0f;
        if (d + bias < bestD)
        {
            bestD = d + bias;
            best = i;
        }
    }
    return best;
}

int EqDisplay::hitRangeHandle(juce::Point<float> p) const
{
    for (int i : selection)
    {
        const auto& b = bands[(size_t) i];
        if (!cincel::usesDynamics(b))
            continue;
        const auto dot = dotPosition(i);
        const float y = yForDb(displayedGain(i) + b.rangeDb * gainScale);
        if (juce::Point<float>(dot.x, y).getDistanceFrom(p) < 8.0f)
            return i;
    }
    return -1;
}

int EqDisplay::findSpectrumPeak(juce::Point<float> p) const
{
    if (!(bool) proc.uiState().getProperty("anaPost", true))
        return -1;
    const auto& db = proc.postAnalyzer.db;
    const double hz = hzForX(p.x);
    const int centre = juce::jlimit(0, SpectrumAnalyzer::points - 1,
                                    (int) std::lround(std::log(hz / fMin) / std::log(fMax / fMin) * (SpectrumAnalyzer::points - 1)));
    int best = -1;
    float bestDb = -200.0f;
    for (int i = std::max(1, centre - 8); i <= std::min(SpectrumAnalyzer::points - 2, centre + 8); ++i)
        if (db[(size_t) i] >= db[(size_t) (i - 1)] && db[(size_t) i] >= db[(size_t) (i + 1)] && db[(size_t) i] > bestDb)
        {
            bestDb = db[(size_t) i];
            best = i;
        }
    if (best < 0 || bestDb < -analyzerRangeDb() + 6.0f)
        return -1;
    const float y = yForAnalyzer(bestDb, SpectrumAnalyzer::pointHz(best));
    const float x = xForHz(SpectrumAnalyzer::pointHz(best));
    return juce::Point<float>(x, y).getDistanceFrom(p) < 16.0f ? best : -1;
}

//==============================================================================
void EqDisplay::refresh()
{
    proc.readAllBands(bands);
    gainScale = proc.globalValue(CincelIDs::gainScale) * 0.01;
    gainQ = proc.globalValue(CincelIDs::gainQ) > 0.5f;
    linearPhase = proc.globalValue(CincelIDs::mode) > 1.5f;
    const auto& tel = proc.dynamicsTelemetry();

    mainCurve.fill(0.0f);
    leftCurve.fill(0.0f);
    rightCurve.fill(0.0f);
    sideCurve.fill(0.0f);
    anyLR = anySide = false;

    for (int i = 0; i < cincel::maxBands; ++i)
    {
        const auto& b = bands[(size_t) i];
        dynOffset[(size_t) i] = cincel::usesDynamics(b) ? tel.offsetDb[(size_t) i].load() : 0.0f;
        if (!b.used)
            continue;
        const auto& k = curveKey[(size_t) i];
        const bool same = curveValid[(size_t) i] && k.freq == b.freq && k.gainDb == b.gainDb && k.q == b.q && k.shape == b.shape
                          && k.slope == b.slope && curveDynKey[(size_t) i] == (float) gainScale;
        if (!same)
        {
            curves::band(b, b.gainDb * gainScale, gainQ, linearPhase, staticCurves[(size_t) i]);
            curveKey[(size_t) i] = b;
            curveDynKey[(size_t) i] = (float) gainScale;
            curveValid[(size_t) i] = true;
        }
        if (std::abs(dynOffset[(size_t) i]) > 0.01f)
            curves::band(b, b.gainDb * gainScale + dynOffset[(size_t) i], gainQ, linearPhase, bandCurves[(size_t) i]);
        else
            bandCurves[(size_t) i] = staticCurves[(size_t) i];

        if (!b.enabled)
            continue;
        const auto& c = bandCurves[(size_t) i];
        for (int p = 0; p < SpectrumAnalyzer::points; ++p)
        {
            const float v = c[(size_t) p];
            switch (b.place)
            {
                case Placement::Stereo: mainCurve[(size_t) p] += v; sideCurve[(size_t) p] += v; break;
                case Placement::Mid: mainCurve[(size_t) p] += v; break;
                case Placement::Left: leftCurve[(size_t) p] += v; break;
                case Placement::Right: rightCurve[(size_t) p] += v; break;
                case Placement::Side: sideCurve[(size_t) p] += v; break;
            }
        }
        anyLR = anyLR || b.place == Placement::Left || b.place == Placement::Right;
        anySide = anySide || b.place == Placement::Side || b.place == Placement::Mid;
    }
    for (int p = 0; p < SpectrumAnalyzer::points; ++p)
    {
        leftCurve[(size_t) p] += mainCurve[(size_t) p];
        rightCurve[(size_t) p] += mainCurve[(size_t) p];
    }

    overlayValid = overlayInstance != 0 && InstanceRegistry::get().find(overlayInstance, overlay);

    // ¿Cambió algo que se vea? Si no, no se redibuja (con varias instancias abiertas se nota).
    {
        juce::MemoryOutputStream sig;
        for (const auto& b : bands)
        {
            sig.writeBool(b.used); sig.writeBool(b.enabled); sig.writeFloat(b.freq); sig.writeFloat(b.gainDb); sig.writeFloat(b.q);
            sig.writeInt((int) b.shape); sig.writeInt(b.slope); sig.writeInt((int) b.place); sig.writeBool(b.dynamic);
            sig.writeFloat(b.rangeDb); sig.writeBool(b.spectral);
        }
        for (float v : dynOffset)
            sig.writeFloat(std::round(v * 20.0f));
        sig.writeInt((int) (proc.preAnalyzer.version + proc.postAnalyzer.version * 7u + proc.scAnalyzer.version * 13u));
        sig.writeDouble(gainScale);
        sig.writeBool(gainQ);
        sig.writeInt(proc.soloBand.load());
        sig.writeInt(overlayValid ? (int) overlay.updatedMs : 0);
        sig.writeInt((int) proc.uiState().getNumProperties());
        for (int i = 0; i < proc.uiState().getNumProperties(); ++i)
            sig.writeString(proc.uiState().getProperty(proc.uiState().getPropertyName(i)).toString());
        const auto hash = juce::String::toHexString(sig.getData(), (int) sig.getDataSize()).hashCode64();
        if (hash == lastSignature && !forceRepaint)
            return;
        lastSignature = hash;
        forceRepaint = false;
    }

    // La selección no puede apuntar a bandas borradas.
    selection.erase(std::remove_if(selection.begin(), selection.end(), [this](int i) { return !bands[(size_t) i].used; }), selection.end());
    if (primary >= 0 && !bands[(size_t) primary].used)
    {
        primary = selection.empty() ? -1 : selection.front();
        if (onSelectionChanged)
            onSelectionChanged();
    }
    repaint();
}

void EqDisplay::resized()
{
    auto r = getLocalBounds().toFloat();
    r.removeFromLeft(34.0f);
    r.removeFromRight(36.0f);
    r.removeFromTop(6.0f);
    r.removeFromBottom(18.0f);
    if ((bool) proc.uiState().getProperty("piano", false))
        pianoArea = r.removeFromBottom(16.0f);
    else
        pianoArea = {};
    plot = r;
}

//==============================================================================
void EqDisplay::paint(juce::Graphics& g)
{
    resized();   // por si cambió el piano
    auto full = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff07080e));
    g.fillRoundedRectangle(full, 6.0f);

    paintGrid(g);
    {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(plot.toNearestInt());
        paintSpectra(g);
        paintCollisions(g);
        paintCurves(g);
        paintSoloShade(g);
    }
    paintDots(g);
    if (!pianoArea.isEmpty())
        paintPiano(g);

    if (drag == Drag::Lasso)
    {
        g.setColour(DecimuColours::accentPrimary.withAlpha(0.12f));
        g.fillRect(lasso);
        g.setColour(DecimuColours::accentPrimary.withAlpha(0.6f));
        g.drawRect(lasso, 1.0f);
    }
    if (drag == Drag::Sketch || sketchMode)
    {
        if (sketch.size() > 1)
        {
            juce::Path p;
            p.startNewSubPath(sketch.front());
            for (size_t i = 1; i < sketch.size(); ++i)
                p.lineTo(sketch[i]);
            g.setColour(DecimuColours::accentRec);
            g.strokePath(p, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour(DecimuColours::accentRec.withAlpha(0.8f));
        g.setFont(mono(15.0f));
        g.drawText(tr(spanish, "SKETCH: draw the curve you want with the mouse", "SKETCH: dibujá con el mouse la curva que querés"),
                   plot.reduced(10.0f).removeFromTop(20.0f), juce::Justification::centredLeft);
    }

    bool anyBand = false;
    for (const auto& b : bands)
        anyBand = anyBand || b.used;
    if (!anyBand && !sketchMode)
    {
        g.setColour(DecimuColours::textMuted.withAlpha(0.7f));
        g.setFont(mono(17.0f));
        g.drawText(tr(spanish, "Double-click to create a band", "Doble clic para crear una banda"), plot, juce::Justification::centred);
    }

    const int readout = drag == Drag::Bands ? dragBand : (hovered >= 0 ? hovered : -1);
    if (readout >= 0)
        paintReadout(g, readout);

    if (grabPeak >= 0 && hovered < 0 && drag == Drag::None)
    {
        const double hz = SpectrumAnalyzer::pointHz(grabPeak);
        const juce::Point<float> c(xForHz(hz), yForAnalyzer(proc.postAnalyzer.db[(size_t) grabPeak], hz));
        g.setColour(DecimuColours::accentPrimary);
        g.drawEllipse(juce::Rectangle<float>(14.0f, 14.0f).withCentre(c), 1.5f);
        g.setFont(mono(14.0f));
        g.drawText(formatHz(hz) + "  " + noteName(hz), juce::Rectangle<float>(c.x - 60.0f, c.y - 26.0f, 120.0f, 14.0f),
                   juce::Justification::centred);
    }
}

void EqDisplay::paintGrid(juce::Graphics& g)
{
    g.setFont(mono(13.0f));
    // Frecuencias
    const double lines[] = { 20, 30, 40, 50, 60, 80, 100, 200, 300, 400, 500, 600, 800, 1000, 2000, 3000, 4000, 5000, 6000, 8000, 10000, 20000 };
    for (double hz : lines)
    {
        const float x = xForHz(hz);
        const bool major = hz == 100 || hz == 1000 || hz == 10000;
        g.setColour(juce::Colours::white.withAlpha(major ? 0.10f : 0.045f));
        g.drawVerticalLine((int) x, plot.getY(), plot.getBottom());
    }
    const std::pair<double, const char*> labels[] = { { 20, "20" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" },
                                                      { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" } };
    g.setColour(DecimuColours::textMuted);
    const float labelY = getHeight() - 17.0f;
    for (const auto& l : labels)
        g.drawText(l.second, juce::Rectangle<float>(xForHz(l.first) - 20.0f, labelY, 40.0f, 15.0f), juce::Justification::centred);

    // dB del EQ (izquierda) y del analizador (derecha)
    const float range = eqRangeDb();
    const float step = range <= 3.0f ? 1.0f : (range <= 6.0f ? 2.0f : (range <= 12.0f ? 3.0f : 6.0f));
    for (float d = -range; d <= range + 0.01f; d += step)
    {
        const float y = yForDb(d);
        g.setColour(juce::Colours::white.withAlpha(std::abs(d) < 0.01f ? 0.16f : 0.05f));
        g.drawHorizontalLine((int) y, plot.getX(), plot.getRight());
        g.setColour(std::abs(d) < 0.01f ? DecimuColours::text : DecimuColours::textMuted);
        g.drawText((d > 0 ? "+" : "") + juce::String((int) d), juce::Rectangle<float>(0.0f, y - 7.0f, 30.0f, 14.0f),
                   juce::Justification::centredRight);
    }
    const float ar = analyzerRangeDb();
    g.setColour(DecimuColours::textMuted.withAlpha(0.65f));
    for (float d = 0.0f; d >= -ar; d -= (ar > 90.0f ? 20.0f : 10.0f))
    {
        const float y = plot.getY() + (-d / ar) * plot.getHeight();
        g.drawText(juce::String((int) d), juce::Rectangle<float>(plot.getRight() + 3.0f, y - 7.0f, 32.0f, 14.0f), juce::Justification::centredLeft);
    }
}

juce::Path EqDisplay::curvePath(const curves::Curve& c) const
{
    juce::Path p;
    for (int i = 0; i < SpectrumAnalyzer::points; ++i)
    {
        const float x = xForHz(SpectrumAnalyzer::pointHz(i));
        const float y = juce::jlimit(plot.getY() - 40.0f, plot.getBottom() + 40.0f, yForDb(c[(size_t) i]));
        if (i == 0)
            p.startNewSubPath(x, y);
        else
            p.lineTo(x, y);
    }
    return p;
}

void EqDisplay::paintSpectra(juce::Graphics& g)
{
    auto ui = proc.uiState();
    auto spectrumPath = [this](const std::array<float, SpectrumAnalyzer::points>& db, bool close)
    {
        juce::Path p;
        for (int i = 0; i < SpectrumAnalyzer::points; ++i)
        {
            const double hz = SpectrumAnalyzer::pointHz(i);
            const float x = xForHz(hz);
            const float y = juce::jlimit(plot.getY(), plot.getBottom() + 2.0f, yForAnalyzer(db[(size_t) i], hz));
            if (i == 0)
                p.startNewSubPath(x, y);
            else
                p.lineTo(x, y);
        }
        if (close)
        {
            p.lineTo(plot.getRight(), plot.getBottom() + 2.0f);
            p.lineTo(plot.getX(), plot.getBottom() + 2.0f);
            p.closeSubPath();
        }
        return p;
    };

    if ((bool) ui.getProperty("anaPre", true))
    {
        g.setColour(juce::Colour(0xff5a6480).withAlpha(0.28f));
        g.fillPath(spectrumPath(proc.preAnalyzer.db, true));
    }
    if ((bool) ui.getProperty("anaPost", true))
    {
        const auto accent = DecimuColours::accentSecondary;
        auto fill = spectrumPath(proc.postAnalyzer.db, true);
        g.setGradientFill(juce::ColourGradient(accent.withAlpha(0.35f), 0.0f, plot.getY(), accent.withAlpha(0.06f), 0.0f, plot.getBottom(), false));
        g.fillPath(fill);
        g.setColour(DecimuColours::accentSecondarySoft.withAlpha(0.75f));
        g.strokePath(spectrumPath(proc.postAnalyzer.db, false), juce::PathStrokeType(1.1f));
        if ((bool) ui.getProperty("anaFreeze", false) || (bool) ui.getProperty("anaPeaks", false))
        {
            g.setColour(DecimuColours::accentPrimary.withAlpha(0.45f));
            g.strokePath(spectrumPath(proc.postAnalyzer.peak, false), juce::PathStrokeType(0.8f));
        }
    }
    if ((bool) ui.getProperty("anaSc", true) && proc.sidechainConnected.load() && proc.scAnalyzer.level > -110.0f)
    {
        g.setColour(juce::Colour(0xffffb347).withAlpha(0.75f));
        g.strokePath(spectrumPath(proc.scAnalyzer.db, false), juce::PathStrokeType(1.1f));
    }
    if (overlayValid && overlay.hasSpectrum)
    {
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.strokePath(spectrumPath(overlay.spectrum, false), juce::PathStrokeType(1.0f));
    }
}

void EqDisplay::paintCollisions(juce::Graphics& g)
{
    if (!overlayValid || !overlay.hasSpectrum || !(bool) proc.uiState().getProperty("collisions", true))
        return;
    // Choque: las dos pistas fuertes en la misma zona (y parecidas en nivel).
    const auto& mine = proc.postAnalyzer.db;
    const float T = -analyzerRangeDb() * 0.55f;
    const float w = plot.getWidth() / (float) SpectrumAnalyzer::points + 1.0f;
    for (int i = 0; i < SpectrumAnalyzer::points; ++i)
    {
        const double hz = SpectrumAnalyzer::pointHz(i);
        static constexpr float tilts[] = { 0.0f, 1.5f, 3.0f, 4.5f, 6.0f };
        const float tilt = tilts[juce::jlimit(0, 4, (int) proc.uiState().getProperty("anaTilt", 3))];
        const float a = mine[(size_t) i] + tilt * (float) std::log2(hz / 1000.0);
        const float b = overlay.spectrum[(size_t) i] + tilt * (float) std::log2(hz / 1000.0);
        if (a < T || b < T)
            continue;
        const float strength = juce::jlimit(0.0f, 1.0f, (std::min(a, b) - T) / 24.0f) * juce::jlimit(0.0f, 1.0f, 1.0f - std::abs(a - b) / 18.0f);
        if (strength <= 0.02f)
            continue;
        const float x = xForHz(hz);
        const float top = yForAnalyzer(std::min(mine[(size_t) i], overlay.spectrum[(size_t) i]), hz);
        g.setColour(DecimuColours::accentRec.withAlpha(0.55f * strength));
        g.fillRect(x - w * 0.5f, top, w, plot.getBottom() - top);
    }
}

void EqDisplay::paintCurves(juce::Graphics& g)
{
    const float zeroY = yForDb(0.0);
    for (int i = 0; i < cincel::maxBands; ++i)
    {
        const auto& b = bands[(size_t) i];
        if (!b.used)
            continue;
        const bool sel = std::find(selection.begin(), selection.end(), i) != selection.end();
        const bool hov = i == hovered;
        const auto col = b.enabled ? bandColour(i) : DecimuColours::textMuted;
        auto path = curvePath(bandCurves[(size_t) i]);
        juce::Path fill(path);
        fill.lineTo(plot.getRight(), zeroY);
        fill.lineTo(plot.getX(), zeroY);
        fill.closeSubPath();
        g.setColour(col.withAlpha(sel ? 0.22f : (hov ? 0.16f : 0.08f)));
        g.fillPath(fill);
        if (sel || hov)
        {
            g.setColour(col.withAlpha(0.8f));
            g.strokePath(path, juce::PathStrokeType(1.2f));
        }
        if (cincel::usesDynamics(b) && std::abs(dynOffset[(size_t) i]) > 0.01f)
        {
            // La curva quieta, punteada, para ver cuánto se movió la dinámica.
            juce::Path dashed;
            const float dashes[] = { 3.0f, 3.0f };
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, curvePath(staticCurves[(size_t) i]), dashes, 2);
            g.setColour(col.withAlpha(0.6f));
            g.fillPath(dashed);
        }
    }

    auto strokeDashed = [&](const curves::Curve& c, juce::Colour col)
    {
        juce::Path dashed;
        const float dashes[] = { 6.0f, 4.0f };
        juce::PathStrokeType(1.6f).createDashedStroke(dashed, curvePath(c), dashes, 2);
        g.setColour(col);
        g.fillPath(dashed);
    };
    if (anySide)
        strokeDashed(sideCurve, juce::Colour(0xff9fe8c0).withAlpha(0.8f));
    if (anyLR)
    {
        strokeDashed(leftCurve, juce::Colour(0xff8fc8ff).withAlpha(0.8f));
        strokeDashed(rightCurve, juce::Colour(0xffffa08f).withAlpha(0.8f));
    }

    const auto mainPath = curvePath(mainCurve);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.strokePath(mainPath, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colour(0xfff2f5ff));
    g.strokePath(mainPath, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (overlayValid)
    {
        juce::Path p = curvePath(overlay.curve);
        juce::Path dashed;
        const float dashes[] = { 2.0f, 3.0f };
        juce::PathStrokeType(1.4f).createDashedStroke(dashed, p, dashes, 2);
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.fillPath(dashed);
    }
}

void EqDisplay::paintSoloShade(juce::Graphics& g)
{
    // Con el solo prendido se oscurece todo lo que no suena: queda iluminada solo la franja de
    // la banda (la misma forma del filtro con que se escucha).
    const int sb = proc.soloBand.load();
    if (sb < 0 || sb >= cincel::maxBands || !cincel::isActive(bands[(size_t) sb]))
        return;
    const auto& b = bands[(size_t) sb];
    const auto a = cincel::designAudition(b.shape, b.freq, b.q);
    if (a.isIdentity())
        return;
    const float w = plot.getWidth() / (float) (SpectrumAnalyzer::points - 1);
    for (int i = 0; i < SpectrumAnalyzer::points; ++i)
    {
        const double hz = SpectrumAnalyzer::pointHz(i);
        const double dbv = 20.0 * std::log10(std::max(1.0e-9, a.magnitude(hz)));
        const float shade = (float) juce::jlimit(0.0, 1.0, -dbv / 24.0);
        if (shade < 0.01f)
            continue;
        g.setColour(juce::Colours::black.withAlpha(0.78f * shade));
        const float x = xForHz(hz);
        g.fillRect(x - w * 0.5f, plot.getY(), w + 0.6f, plot.getHeight());
    }
    // Bordes de la franja (donde cae 3 dB) y un cartel.
    const auto col = bandColour(sb);
    double lo = -1.0, hi = -1.0;
    for (int i = 0; i < SpectrumAnalyzer::points; ++i)
    {
        const double hz = SpectrumAnalyzer::pointHz(i);
        const bool in = 20.0 * std::log10(std::max(1.0e-9, a.magnitude(hz))) > -3.0;
        if (in && lo < 0.0) lo = hz;
        if (in) hi = hz;
    }
    g.setColour(col.withAlpha(0.7f));
    if (lo > SpectrumAnalyzer::loHz * 1.01)
        g.drawVerticalLine((int) xForHz(lo), plot.getY(), plot.getBottom());
    if (hi > 0.0 && hi < SpectrumAnalyzer::hiHz * 0.99)
        g.drawVerticalLine((int) xForHz(hi), plot.getY(), plot.getBottom());
    g.setColour(col);
    g.setFont(mono(15.0f));
    const juce::String text = "SOLO " + juce::String(sb + 1) + "   " + formatHz(lo > 0 ? lo : b.freq) + " - " + formatHz(hi > 0 ? hi : b.freq);
    g.drawText(text, plot.reduced(10.0f, 8.0f).removeFromTop(18.0f), juce::Justification::centred);
}

void EqDisplay::paintDots(juce::Graphics& g)
{
    for (int i = 0; i < cincel::maxBands; ++i)
    {
        const auto& b = bands[(size_t) i];
        if (!b.used)
            continue;
        const bool sel = std::find(selection.begin(), selection.end(), i) != selection.end();
        const auto col = b.enabled ? bandColour(i) : DecimuColours::textMuted;
        const auto c = dotPosition(i);

        // Rango dinámico: barra desde la ganancia hasta donde puede llegar, con su manija.
        if (cincel::usesDynamics(b))
        {
            const float yEnd = yForDb(displayedGain(i) + b.rangeDb * gainScale);
            const float yNow = yForDb(displayedGain(i) + dynOffset[(size_t) i]);
            g.setColour(col.withAlpha(0.35f));
            g.fillRoundedRectangle(juce::Rectangle<float>(c.x - 2.0f, std::min(c.y, yEnd), 4.0f, std::abs(yEnd - c.y)), 2.0f);
            g.setColour(col);
            g.fillRoundedRectangle(juce::Rectangle<float>(c.x - 2.0f, std::min(c.y, yNow), 4.0f, std::abs(yNow - c.y)), 2.0f);
            if (sel)
            {
                g.setColour(col);
                g.drawEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre({ c.x, yEnd }), 1.5f);
            }
            if (b.spectral)
            {
                g.setFont(mono(11.0f));
                g.drawText("SPEC", juce::Rectangle<float>(c.x + 9.0f, c.y + 6.0f, 32.0f, 11.0f), juce::Justification::centredLeft);
            }
        }

        const float r = sel ? 9.0f : 7.5f;
        if (sel)
        {
            g.setColour(col.withAlpha(0.25f));
            g.fillEllipse(juce::Rectangle<float>(r * 2.0f + 8.0f, r * 2.0f + 8.0f).withCentre(c));
        }
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillEllipse(juce::Rectangle<float>(r * 2.0f + 2.0f, r * 2.0f + 2.0f).withCentre(c));
        if (b.enabled)
        {
            g.setColour(col);
            g.fillEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(c));
        }
        g.setColour(b.enabled ? juce::Colours::white.withAlpha(sel ? 0.95f : 0.4f) : col);
        g.drawEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(c), sel ? 1.8f : 1.2f);
        g.setColour(b.enabled ? juce::Colours::black.withAlpha(0.85f) : col);
        g.setFont(mono(sel ? 15.0f : 13.0f));
        g.drawText(juce::String(i + 1), juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(c), juce::Justification::centred);

        // Letra de la ubicación (L, R, M, S) arriba del punto.
        if (b.place != Placement::Stereo)
        {
            static const char* tags[] = { "", "L", "R", "M", "S" };
            g.setColour(col);
            g.setFont(mono(12.0f));
            g.drawText(tags[(int) b.place], juce::Rectangle<float>(c.x - 8.0f, c.y - r - 15.0f, 16.0f, 12.0f), juce::Justification::centred);
        }
        if (proc.soloBand.load() == i)
        {
            g.setColour(DecimuColours::accentRec);
            g.drawEllipse(juce::Rectangle<float>(r * 2.0f + 10.0f, r * 2.0f + 10.0f).withCentre(c), 1.5f);
        }
    }
}

void EqDisplay::paintReadout(juce::Graphics& g, int i)
{
    const auto& b = bands[(size_t) i];
    const auto c = dotPosition(i);
    juce::StringArray lines;
    lines.add(juce::String(i + 1) + "  " + juce::String(juce::CharPointer_UTF8(spanish ? shapeNameEs[(int) b.shape] : shapeNameEn[(int) b.shape]))
              + (cincel::shapeHasSlope(b.shape) ? juce::String("  ") + slopeName[b.slope] : juce::String()));
    lines.add(formatHz(b.freq) + "   " + noteName(b.freq));
    juce::String third;
    if (cincel::shapeHasGain(b.shape))
        third << (b.gainDb > 0 ? "+" : "") << juce::String(b.gainDb * gainScale, 1) << " dB   ";
    if (cincel::shapeHasQ(b.shape))
        third << "Q " << juce::String(b.q, 2);
    lines.add(third);
    if (cincel::usesDynamics(b))
        lines.add(tr(spanish, "Dyn ", "Din ") + juce::String(dynOffset[(size_t) i], 1) + " / " + juce::String(b.rangeDb * gainScale, 1) + " dB");
    if (!b.enabled)
        lines.add(tr(spanish, "(off)", "(apagada)"));

    juce::ignoreUnused(c);
    const float w = 190.0f, h = 8.0f + 15.0f * lines.size();
    // Fija en la esquina de arriba a la izquierda: no sigue al punto, así nunca tapa otro.
    juce::Rectangle<float> box(plot.getX() + 8.0f, plot.getY() + 8.0f, w, h);
    g.setColour(juce::Colour(0xee0b0d16));
    g.fillRoundedRectangle(box, 5.0f);
    g.setColour(bandColour(i).withAlpha(0.8f));
    g.drawRoundedRectangle(box, 5.0f, 1.0f);
    g.setFont(mono(14.0f));
    for (int k = 0; k < lines.size(); ++k)
    {
        g.setColour(k == 0 ? bandColour(i) : DecimuColours::text);
        g.drawText(lines[k], box.reduced(8.0f, 4.0f).withHeight(15.0f).translated(0.0f, 15.0f * k), juce::Justification::centredLeft);
    }
}

void EqDisplay::paintPiano(juce::Graphics& g)
{
    auto area = pianoArea;
    g.setColour(juce::Colour(0xff10121b));
    g.fillRect(area);
    auto noteHz = [](double n) { return 440.0 * std::pow(2.0, (n - 69.0) / 12.0); };
    const int highlightNote = primary >= 0 ? (int) std::lround(69.0 + 12.0 * std::log2(bands[(size_t) primary].freq / 440.0)) : -1;
    for (int pass = 0; pass < 2; ++pass)
        for (int n = 12; n <= 135; ++n)
        {
            const int pc = n % 12;
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            if ((pass == 0) == black)
                continue;
            const float x0 = xForHz(noteHz(n - 0.5)), x1 = xForHz(noteHz(n + 0.5));
            if (x1 < area.getX() || x0 > area.getRight())
                continue;
            juce::Rectangle<float> key(x0, area.getY(), x1 - x0, black ? area.getHeight() * 0.6f : area.getHeight());
            if (n == highlightNote)
                g.setColour(primary >= 0 ? bandColour(primary) : DecimuColours::accentPrimary);
            else
                g.setColour(black ? juce::Colour(0xff1c1f2b) : juce::Colour(0xffb8bfd0));
            g.fillRect(key.reduced(black ? 0.0f : 0.3f, 0.0f));
            if (pc == 0 && !black && x1 - x0 > 4.0f)
            {
                g.setColour(juce::Colours::black.withAlpha(0.7f));
                g.setFont(mono(10.0f));
                g.drawText("C" + juce::String(n / 12 - 1), key.withTrimmedTop(key.getHeight() * 0.45f), juce::Justification::centred);
            }
        }
}

//==============================================================================
void EqDisplay::selectOnly(int band)
{
    selection.clear();
    if (band >= 0)
        selection.push_back(band);
    primary = band;
    if (onSelectionChanged)
        onSelectionChanged();
}

void EqDisplay::setSolo(int band)
{
    proc.soloBand.store(band);
}

void EqDisplay::mouseMove(const juce::MouseEvent& e)
{
    mousePos = e.position;
    mouseInside = true;
    const int h = hitBand(e.position);
    hovered = h;
    grabPeak = (h < 0 && hitRangeHandle(e.position) < 0 && !sketchMode) ? findSpectrumPeak(e.position) : -1;
    setMouseCursor(h >= 0 ? juce::MouseCursor::DraggingHandCursor
                          : (grabPeak >= 0 ? juce::MouseCursor::PointingHandCursor
                                           : (sketchMode ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor)));
    repaint();
}

void EqDisplay::mouseExit(const juce::MouseEvent&)
{
    mouseInside = false;
    hovered = -1;
    grabPeak = -1;
    repaint();
}

void EqDisplay::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    proc.undoManager.beginNewTransaction();
    dragStart = e.position;
    drag = Drag::None;

    if (sketchMode && !e.mods.isPopupMenu())
    {
        sketch.clear();
        sketch.push_back(e.position);
        drag = Drag::Sketch;
        return;
    }

    const int hit = hitBand(e.position);
    if (hit >= 0)
    {
        if (e.mods.isPopupMenu())
        {
            if (std::find(selection.begin(), selection.end(), hit) == selection.end())
                selectOnly(hit);
            showBandMenu(hit);
            return;
        }
        const bool inSel = std::find(selection.begin(), selection.end(), hit) != selection.end();
        if (e.mods.isShiftDown())
        {
            if (inSel)
                selection.erase(std::remove(selection.begin(), selection.end(), hit), selection.end());
            else
                selection.push_back(hit);
            primary = selection.empty() ? -1 : hit;
            if (onSelectionChanged)
                onSelectionChanged();
        }
        else if (!inSel)
            selectOnly(hit);
        else
        {
            primary = hit;
            if (onSelectionChanged)
                onSelectionChanged();
        }
        drag = Drag::Bands;
        dragBand = hit;
        starts.clear();
        for (int i : selection)
            starts.push_back({ i, bands[(size_t) i].freq, bands[(size_t) i].gainDb, bands[(size_t) i].q, bands[(size_t) i].rangeDb });
        if (e.mods.isAltDown())
        {
            setSolo(hit);
            soloFromDrag = true;
        }
        return;
    }

    const int handle = hitRangeHandle(e.position);
    if (handle >= 0)
    {
        drag = Drag::Range;
        dragBand = handle;
        starts = { { handle, bands[(size_t) handle].freq, bands[(size_t) handle].gainDb, bands[(size_t) handle].q, bands[(size_t) handle].rangeDb } };
        return;
    }

    if (e.mods.isPopupMenu())
    {
        showBackgroundMenu();
        return;
    }

    if (grabPeak >= 0)
    {
        createBandAt(e.position, true, SpectrumAnalyzer::pointHz(grabPeak));
        grabPeak = -1;
        if (primary >= 0)
        {
            drag = Drag::Bands;
            dragBand = primary;
            starts = { { primary, bands[(size_t) primary].freq, bands[(size_t) primary].gainDb, bands[(size_t) primary].q, bands[(size_t) primary].rangeDb } };
            dragStart = dotPosition(primary);
        }
        return;
    }

    drag = Drag::Lasso;
    lasso = { e.position, e.position };
    if (!e.mods.isShiftDown())
        selectOnly(-1);
}

void EqDisplay::mouseDrag(const juce::MouseEvent& e)
{
    mousePos = e.position;
    if (drag == Drag::Sketch)
    {
        if (sketch.empty() || std::abs(e.position.x - sketch.back().x) >= 2.0f)
            sketch.push_back({ juce::jlimit(plot.getX(), plot.getRight(), e.position.x), juce::jlimit(plot.getY(), plot.getBottom(), e.position.y) });
        repaint();
        return;
    }
    if (drag == Drag::Lasso)
    {
        lasso = juce::Rectangle<float>(dragStart, e.position);
        std::vector<int> inside;
        for (int i = 0; i < cincel::maxBands; ++i)
            if (bands[(size_t) i].used && lasso.contains(dotPosition(i)))
                inside.push_back(i);
        if (inside != selection)
        {
            selection = inside;
            primary = selection.empty() ? -1 : selection.front();
            if (onSelectionChanged)
                onSelectionChanged();
        }
        repaint();
        return;
    }

    const float fine = e.mods.isCtrlDown() || e.mods.isCommandDown() ? 0.1f : 1.0f;
    const double pxPerOct = plot.getWidth() / std::log2(fMax / fMin);
    const double dOct = (e.position.x - dragStart.x) / pxPerOct * fine;
    const double dDb = (dbForY(e.position.y) - dbForY(dragStart.y)) * fine;
    const double scale = std::abs(gainScale) > 0.05 ? gainScale : 1.0;

    if (drag == Drag::Range)
    {
        auto b = bands[(size_t) dragBand];
        b.rangeDb = (float) juce::jlimit(-30.0, 30.0, starts[0].range + dDb / scale);
        proc.writeBand(dragBand, b);
        bands[(size_t) dragBand] = b;
        repaint();
        return;
    }

    if (drag == Drag::Bands)
    {
        for (const auto& s : starts)
        {
            auto b = bands[(size_t) s.band];
            b.freq = (float) juce::jlimit(10.0, 30000.0, s.freq * std::pow(2.0, dOct));
            if (cincel::shapeHasGain(b.shape))
            {
                const double k = (b.shape == Shape::TiltShelf || b.shape == Shape::FlatTilt) ? 2.0 : 1.0;
                b.gainDb = (float) juce::jlimit(-30.0, 30.0, s.gain + k * dDb / scale);
            }
            else
                b.q = (float) juce::jlimit(0.025, 40.0, s.q * std::pow(2.0, -(e.position.y - dragStart.y) / 70.0 * fine));
            proc.writeBand(s.band, b);
            bands[(size_t) s.band] = b;
        }
        repaint();
    }
}

void EqDisplay::mouseUp(const juce::MouseEvent&)
{
    if (soloFromDrag)
    {
        setSolo(-1);
        soloFromDrag = false;
    }
    if (drag == Drag::Sketch)
        finishSketch();
    drag = Drag::None;
    repaint();
}

void EqDisplay::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (sketchMode)
        return;
    // Solo el segundo clic cuenta como doble clic. Si no, el clic extra que se hace para agarrar
    // una banda recién creada se contaba como otro doble clic y la apagaba.
    if (e.getNumberOfClicks() != 2)
        return;
    const int hit = hitBand(e.position);
    if (hit >= 0)
    {
        if (hit == lastCreated && juce::Time::getMillisecondCounter() - lastCreatedMs < 1500)
            return;
        auto b = bands[(size_t) hit];
        b.enabled = !b.enabled;
        proc.writeBand(hit, b);
        return;
    }
    if (plot.contains(e.position))
        createBandAt(e.position, false, 0.0);
}

void EqDisplay::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    std::vector<int> targets;
    const int h = hitBand(e.position);
    if (h >= 0)
    {
        if (std::find(selection.begin(), selection.end(), h) != selection.end())
            targets = selection;
        else
            targets = { h };
    }
    else
        targets = selection;
    if (targets.empty())
        return;
    proc.undoManager.beginNewTransaction();
    const float delta = (w.isReversed ? -w.deltaY : w.deltaY);
    for (int i : targets)
    {
        auto b = bands[(size_t) i];
        if (e.mods.isAltDown())
            b.rangeDb = juce::jlimit(-30.0f, 30.0f, b.rangeDb + delta * 6.0f);
        else
            b.q = (float) juce::jlimit(0.025, 40.0, b.q * std::pow(2.0, delta * 1.5));
        proc.writeBand(i, b);
        bands[(size_t) i] = b;
    }
    repaint();
}

bool EqDisplay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelection();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        selectOnly(-1);
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'A')
    {
        selection.clear();
        for (int i = 0; i < cincel::maxBands; ++i)
            if (bands[(size_t) i].used)
                selection.push_back(i);
        primary = selection.empty() ? -1 : selection.front();
        if (onSelectionChanged)
            onSelectionChanged();
        return true;
    }
    return false;
}

//==============================================================================
void EqDisplay::createBandAt(juce::Point<float> p, bool fromPeak, double peakHz)
{
    const int i = proc.firstFreeBand();
    if (i < 0)
        return;
    proc.undoManager.beginNewTransaction("New band");
    BandParams b;
    b.used = true;
    b.enabled = true;
    b.freq = (float) juce::jlimit(10.0, 30000.0, fromPeak ? peakHz : hzForX(p.x));
    const float rel = (p.x - plot.getX()) / plot.getWidth();
    if (!fromPeak && rel < 0.04f)
    {
        b.shape = Shape::LowCut;
        b.slope = 3;
        b.q = 0.71f;
    }
    else if (!fromPeak && rel > 0.96f)
    {
        b.shape = Shape::HighCut;
        b.slope = 3;
        b.q = 0.71f;
    }
    else
    {
        b.shape = Shape::Bell;
        const double scale = std::abs(gainScale) > 0.05 ? gainScale : 1.0;
        b.gainDb = fromPeak ? 0.0f : (float) juce::jlimit(-30.0, 30.0, dbForY(p.y) / scale);
        b.q = 1.0f;
    }
    proc.writeBand(i, b);
    bands[(size_t) i] = b;
    curveValid[(size_t) i] = false;
    lastCreated = i;
    lastCreatedMs = juce::Time::getMillisecondCounter();
    selectOnly(i);
}

void EqDisplay::deleteSelection()
{
    if (selection.empty())
        return;
    proc.undoManager.beginNewTransaction("Delete bands");
    for (int i : selection)
    {
        auto b = bands[(size_t) i];
        b.used = false;
        b.dynamic = false;
        b.spectral = false;
        proc.writeBand(i, b);
        if (proc.soloBand.load() == i)
            setSolo(-1);
    }
    selectOnly(-1);
}

void EqDisplay::finishSketch()
{
    if (sketch.size() < 3)
    {
        sketch.clear();
        return;
    }
    std::sort(sketch.begin(), sketch.end(), [](auto a, auto b) { return a.x < b.x; });
    cincel::FitGrid grid;
    grid.build(200);
    const double hzLo = hzForX(sketch.front().x), hzHi = hzForX(sketch.back().x);
    for (int i = 0; i < grid.size(); ++i)
    {
        const double hz = grid.hz[(size_t) i];
        if (hz < hzLo || hz > hzHi)
        {
            grid.target[(size_t) i] = 0.0;
            grid.weight[(size_t) i] = 0.15;
            continue;
        }
        const float x = xForHz(hz);
        size_t k = 1;
        while (k < sketch.size() - 1 && sketch[k].x < x)
            ++k;
        const auto a = sketch[k - 1], b = sketch[k];
        const float t = b.x > a.x ? juce::jlimit(0.0f, 1.0f, (x - a.x) / (b.x - a.x)) : 0.0f;
        grid.target[(size_t) i] = dbForY(a.y + t * (b.y - a.y));
    }
    grid.target = cincel::smoothOctave(grid.hz, grid.target, 1.0 / 6.0);
    // Un trazo a mano tiene temblores: se ignoran desvíos de menos de 1 dB.
    const auto fitted = cincel::fitBandsToCurve(grid, 10, 1.0);

    proc.undoManager.beginNewTransaction("Sketch");
    proc.resetAll(false);
    const double scale = std::abs(gainScale) > 0.05 ? gainScale : 1.0;
    int idx = 0;
    for (auto b : fitted)
    {
        b.gainDb = (float) juce::jlimit(-30.0, 30.0, b.gainDb / scale);
        proc.writeBand(idx++, b);
    }
    sketch.clear();
    selectOnly(-1);
    if (onSketchFinished)
        onSketchFinished();
}

//==============================================================================
void EqDisplay::showBandMenu(int band)
{
    const auto b = bands[(size_t) band];
    const bool es = spanish;
    juce::PopupMenu m, shapes, slopes, places;
    for (int s = 0; s < cincel::numShapes; ++s)
        shapes.addItem(100 + s, juce::String(juce::CharPointer_UTF8(es ? shapeNameEs[s] : shapeNameEn[s])), true, (int) b.shape == s);
    for (int s = 0; s < cincel::numSlopes; ++s)
        slopes.addItem(200 + s, slopeName[s], true, b.slope == s);
    for (int s = 0; s < cincel::numPlacements; ++s)
        places.addItem(300 + s, juce::String(juce::CharPointer_UTF8(es ? placeNameEs[s] : placeNameEn[s])), true, (int) b.place == s);

    m.addSectionHeader(tr(es, "Band ", "Banda ") + juce::String(band + 1));
    m.addSubMenu(tr(es, "Shape", "Forma"), shapes);
    m.addSubMenu(tr(es, "Slope", "Pendiente"), slopes, cincel::shapeHasSlope(b.shape));
    m.addSubMenu(tr(es, "Stereo placement", "Ubicación estéreo"), places);
    m.addSeparator();
    m.addItem(1, tr(es, "Dynamic EQ", "EQ dinámico"), cincel::shapeHasGain(b.shape), b.dynamic);
    m.addItem(2, tr(es, "Spectral dynamics", "Dinámica espectral"), cincel::shapeHasGain(b.shape) && b.dynamic, b.spectral);
    m.addItem(3, tr(es, "Listen to external sidechain", "Escuchar el sidechain externo"), b.dynamic, b.sidechain);
    m.addSeparator();
    m.addItem(4, tr(es, "Solo (listen to this band)", "Solo (escuchar esta banda)"), true, proc.soloBand.load() == band);
    m.addItem(5, tr(es, "Enabled", "Prendida"), true, b.enabled);
    m.addItem(6, tr(es, "Reset gain to 0 dB", "Ganancia a 0 dB"), cincel::shapeHasGain(b.shape));
    m.addItem(7, tr(es, "Delete", "Borrar"));

    juce::Component::SafePointer<EqDisplay> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [safe, band](int r)
    {
        if (safe == nullptr || r == 0)
            return;
        auto& self = *safe;
        self.proc.undoManager.beginNewTransaction();
        auto b = self.bands[(size_t) band];
        if (r >= 100 && r < 100 + cincel::numShapes) b.shape = (Shape) (r - 100);
        else if (r >= 200 && r < 200 + cincel::numSlopes) b.slope = r - 200;
        else if (r >= 300 && r < 300 + cincel::numPlacements) b.place = (Placement) (r - 300);
        else if (r == 1) b.dynamic = !b.dynamic;
        else if (r == 2) b.spectral = !b.spectral;
        else if (r == 3) b.sidechain = !b.sidechain;
        else if (r == 4) { self.setSolo(self.proc.soloBand.load() == band ? -1 : band); return; }
        else if (r == 5) b.enabled = !b.enabled;
        else if (r == 6) b.gainDb = 0.0f;
        else if (r == 7) { self.deleteSelection(); return; }
        self.proc.writeBand(band, b);
    });
}

void EqDisplay::showBackgroundMenu()
{
    auto ui = proc.uiState();
    const bool es = spanish;
    juce::PopupMenu m, range, res, speed, tilt, arange;
    const char* ranges[] = { "+/- 3 dB", "+/- 6 dB", "+/- 12 dB", "+/- 30 dB" };
    for (int i = 0; i < 4; ++i)
        range.addItem(100 + i, ranges[i], true, (int) ui.getProperty("range", 2) == i);
    const char* resNames[] = { "Low", "Medium", "High", "Maximum" };
    for (int i = 0; i < 4; ++i)
        res.addItem(200 + i, resNames[i], true, (int) ui.getProperty("anaRes", 2) == i);
    const char* speedEn[] = { "Very slow", "Slow", "Medium", "Fast", "Very fast" };
    const char* speedEs[] = { "Muy lento", "Lento", "Medio", "Rápido", "Muy rápido" };
    for (int i = 0; i < 5; ++i)
        speed.addItem(300 + i, juce::String(juce::CharPointer_UTF8(es ? speedEs[i] : speedEn[i])), true, (int) ui.getProperty("anaSpeed", 2) == i);
    const char* tilts[] = { "0 dB/oct", "1.5 dB/oct", "3 dB/oct", "4.5 dB/oct", "6 dB/oct" };
    for (int i = 0; i < 5; ++i)
        tilt.addItem(400 + i, tilts[i], true, (int) ui.getProperty("anaTilt", 3) == i);
    const char* ar[] = { "60 dB", "90 dB", "120 dB" };
    for (int i = 0; i < 3; ++i)
        arange.addItem(500 + i, ar[i], true, (int) ui.getProperty("anaRange", 1) == i);

    m.addSubMenu(tr(es, "Display range", "Rango de la curva"), range);
    m.addSeparator();
    m.addSectionHeader(tr(es, "Analyzer", "Analizador"));
    m.addItem(1, tr(es, "Show input (pre)", "Mostrar la entrada (pre)"), true, (bool) ui.getProperty("anaPre", true));
    m.addItem(2, tr(es, "Show output (post)", "Mostrar la salida (post)"), true, (bool) ui.getProperty("anaPost", true));
    m.addItem(3, tr(es, "Show sidechain", "Mostrar el sidechain"), true, (bool) ui.getProperty("anaSc", true));
    m.addItem(4, tr(es, "Peak hold", "Retener picos"), true, (bool) ui.getProperty("anaPeaks", false));
    m.addItem(5, tr(es, "Freeze", "Congelar"), true, (bool) ui.getProperty("anaFreeze", false));
    m.addSubMenu(tr(es, "Resolution", "Resolución"), res);
    m.addSubMenu(tr(es, "Speed", "Velocidad"), speed);
    m.addSubMenu(tr(es, "Tilt", "Inclinación"), tilt);
    m.addSubMenu(tr(es, "Range", "Rango"), arange);
    m.addSeparator();
    m.addItem(6, tr(es, "Piano keyboard", "Teclado de piano"), true, (bool) ui.getProperty("piano", false));
    m.addSeparator();
    m.addItem(8, tr(es, "Keep spectral latency reserved (SPEC never changes the latency)",
                    "Reservar la latencia espectral (SPEC nunca cambia la latencia)"), true, proc.getReserveSpectral());
    m.addItem(7, tr(es, "Delete all bands", "Borrar todas las bandas"));

    juce::Component::SafePointer<EqDisplay> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [safe](int r)
    {
        if (safe == nullptr || r == 0)
            return;
        auto ui2 = safe->proc.uiState();
        auto toggle = [&](const char* key, bool def) { ui2.setProperty(key, !(bool) ui2.getProperty(key, def), nullptr); };
        if (r >= 100 && r < 104) ui2.setProperty("range", r - 100, nullptr);
        else if (r >= 200 && r < 204) ui2.setProperty("anaRes", r - 200, nullptr);
        else if (r >= 300 && r < 305) ui2.setProperty("anaSpeed", r - 300, nullptr);
        else if (r >= 400 && r < 405) ui2.setProperty("anaTilt", r - 400, nullptr);
        else if (r >= 500 && r < 503) ui2.setProperty("anaRange", r - 500, nullptr);
        else if (r == 1) toggle("anaPre", true);
        else if (r == 2) toggle("anaPost", true);
        else if (r == 3) toggle("anaSc", true);
        else if (r == 4) { toggle("anaPeaks", false); safe->proc.postAnalyzer.clearPeaks(); }
        else if (r == 5) toggle("anaFreeze", false);
        else if (r == 6) toggle("piano", false);
        else if (r == 8) safe->proc.setReserveSpectral(!safe->proc.getReserveSpectral());
        else if (r == 7)
        {
            safe->proc.undoManager.beginNewTransaction("Delete all");
            safe->proc.resetAll(false);
            safe->selectOnly(-1);
        }
        safe->resized();
        safe->repaint();
    });
}
