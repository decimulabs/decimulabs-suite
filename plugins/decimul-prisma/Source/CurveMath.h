#pragma once

#include "Analyzer.h"
#include "dsp/PrismaFilters.h"

/** Curvas para dibujar (y para compartir con otras instancias), sobre la grilla del analizador. */
namespace curves
{
using Curve = std::array<float, SpectrumAnalyzer::points>;

/** dB de una banda con valores ya efectivos (ganancia con escala y dinámica). */
inline void band(const prisma::BandParams& b, double gainDb, bool gainQ, bool linearPhase, Curve& out)
{
    const double q = prisma::effectiveQ(b.shape, b.q, gainDb, gainQ);
    const auto d = prisma::designBand(b.shape, b.slope, b.freq, gainDb, q, linearPhase);
    for (int i = 0; i < SpectrumAnalyzer::points; ++i)
        out[(size_t) i] = (float) std::max(-120.0, 20.0 * std::log10(std::max(1.0e-9, d.magnitude(SpectrumAnalyzer::pointHz(i)))));
}

/** Curva total (lo que le pasa a un sonido centrado) de todas las bandas activas. */
inline void total(const prisma::BandParams* bands, double gainScale, bool gainQ, bool linearPhase, Curve& out)
{
    out.fill(0.0f);
    Curve tmp;
    for (int i = 0; i < prisma::maxBands; ++i)
    {
        const auto& b = bands[i];
        if (!prisma::isActive(b) || b.place == prisma::Placement::Side)
            continue;
        band(b, b.gainDb * gainScale, gainQ, linearPhase, tmp);
        const float w = (b.place == prisma::Placement::Left || b.place == prisma::Placement::Right) ? 0.5f : 1.0f;
        for (int k = 0; k < SpectrumAnalyzer::points; ++k)
            out[(size_t) k] += w * tmp[(size_t) k];
    }
}
} // namespace curves
