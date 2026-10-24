#pragma once

// Ajuste automático de bandas a una curva objetivo. Lo usan:
//   - EQ Match: la curva es "cómo suena la referencia" menos "cómo suena esto", suavizada;
//   - EQ Sketch: la curva la dibuja el usuario con el mouse.
// Primero se ubican bandas una por una donde más falta (bell, o shelf si el desvío está pegado a
// un extremo) y después se ajustan todas juntas (Levenberg-Marquardt sobre frecuencia, ganancia
// y Q) para que la suma siga a la curva lo mejor posible.

#include "PrismaFilters.h"
#include <vector>

namespace prisma
{
struct FitGrid
{
    std::vector<double> hz, target, weight;

    /** Grilla logarítmica de `points` puntos entre `lo` y `hi`. */
    void build(int points, double lo = 20.0, double hi = 20000.0)
    {
        hz.resize((size_t) points);
        target.assign((size_t) points, 0.0);
        weight.assign((size_t) points, 1.0);
        for (int i = 0; i < points; ++i)
            hz[(size_t) i] = lo * std::pow(hi / lo, i / (double) (points - 1));
    }

    int size() const { return (int) hz.size(); }
};

namespace fitdetail
{
    struct Band
    {
        Shape shape = Shape::Bell;
        double lf = std::log(1000.0), gain = 0.0, lq = 0.0;
    };

    inline double bandDb(const Band& b, double hz)
    {
        const auto a = designBand(b.shape, 1, std::exp(b.lf), b.gain, std::exp(b.lq));
        return 20.0 * std::log10(std::max(1.0e-9, a.magnitude(hz)));
    }

    inline void clampBand(Band& b)
    {
        b.lf = std::clamp(b.lf, std::log(20.0), std::log(20000.0));
        b.gain = std::clamp(b.gain, -24.0, 24.0);
        b.lq = std::clamp(b.lq, std::log(0.2), std::log(16.0));
    }

    /** Resuelve A x = b (n x n) por eliminación con pivoteo. */
    inline bool solve(std::vector<double>& A, std::vector<double>& b, int n)
    {
        for (int c = 0; c < n; ++c)
        {
            int piv = c;
            for (int r = c + 1; r < n; ++r)
                if (std::abs(A[(size_t) (r * n + c)]) > std::abs(A[(size_t) (piv * n + c)]))
                    piv = r;
            if (std::abs(A[(size_t) (piv * n + c)]) < 1.0e-14)
                return false;
            if (piv != c)
            {
                for (int k = 0; k < n; ++k)
                    std::swap(A[(size_t) (c * n + k)], A[(size_t) (piv * n + k)]);
                std::swap(b[(size_t) c], b[(size_t) piv]);
            }
            for (int r = c + 1; r < n; ++r)
            {
                const double f = A[(size_t) (r * n + c)] / A[(size_t) (c * n + c)];
                if (f == 0.0)
                    continue;
                for (int k = c; k < n; ++k)
                    A[(size_t) (r * n + k)] -= f * A[(size_t) (c * n + k)];
                b[(size_t) r] -= f * b[(size_t) c];
            }
        }
        for (int r = n - 1; r >= 0; --r)
        {
            double s = b[(size_t) r];
            for (int k = r + 1; k < n; ++k)
                s -= A[(size_t) (r * n + k)] * b[(size_t) k];
            b[(size_t) r] = s / A[(size_t) (r * n + r)];
        }
        return true;
    }
}

/** Devuelve hasta `bandLimit` bandas (bell / low shelf / high shelf) cuya suma sigue a la curva
    `grid.target` (dB). Para antes de llegar al máximo si lo que falta es menos de `stopDb`. */
inline std::vector<BandParams> fitBandsToCurve(const FitGrid& grid, int bandLimit, double stopDb = 0.4)
{
    using namespace fitdetail;
    const int G = grid.size();
    std::vector<double> resid(grid.target);
    std::vector<Band> bands;

    for (int k = 0; k < bandLimit; ++k)
    {
        int idx = -1;
        double best = 0.0;
        for (int i = 0; i < G; ++i)
        {
            const double v = std::abs(resid[(size_t) i]) * grid.weight[(size_t) i];
            if (v > best)
            {
                best = v;
                idx = i;
            }
        }
        if (idx < 0 || best < stopDb)
            break;

        const double g0 = resid[(size_t) idx];
        int lo = idx, hi = idx;
        while (lo > 0 && resid[(size_t) (lo - 1)] * g0 > 0.0 && std::abs(resid[(size_t) (lo - 1)]) > 0.5 * std::abs(g0))
            --lo;
        while (hi < G - 1 && resid[(size_t) (hi + 1)] * g0 > 0.0 && std::abs(resid[(size_t) (hi + 1)]) > 0.5 * std::abs(g0))
            ++hi;

        Band b;
        b.gain = g0;
        const double f0 = grid.hz[(size_t) idx];
        if (lo == 0 && f0 < 200.0)
        {
            b.shape = Shape::LowShelf;
            b.lf = std::log(grid.hz[(size_t) hi]);
            b.lq = std::log(0.71);
        }
        else if (hi == G - 1 && f0 > 5000.0)
        {
            b.shape = Shape::HighShelf;
            b.lf = std::log(grid.hz[(size_t) lo]);
            b.lq = std::log(0.71);
        }
        else
        {
            const double fl = grid.hz[(size_t) lo], fh = grid.hz[(size_t) hi];
            b.lf = std::log(f0);
            b.lq = std::log(std::clamp(std::sqrt(fl * fh) / std::max(1.0, fh - fl), 0.3, 12.0));
        }
        clampBand(b);
        bands.push_back(b);
        for (int i = 0; i < G; ++i)
            resid[(size_t) i] -= bandDb(b, grid.hz[(size_t) i]);
    }

    // Ajuste conjunto (Levenberg-Marquardt) de frecuencia, ganancia y Q de todas las bandas.
    const int B = (int) bands.size();
    if (B > 0)
    {
        const int P = 3 * B;
        std::vector<std::vector<double>> contrib((size_t) B, std::vector<double>((size_t) G));
        auto evalBand = [&](int j)
        {
            for (int i = 0; i < G; ++i)
                contrib[(size_t) j][(size_t) i] = bandDb(bands[(size_t) j], grid.hz[(size_t) i]);
        };
        auto cost = [&]()
        {
            double c = 0.0;
            for (int i = 0; i < G; ++i)
            {
                double m = 0.0;
                for (int j = 0; j < B; ++j)
                    m += contrib[(size_t) j][(size_t) i];
                const double e = grid.target[(size_t) i] - m;
                c += grid.weight[(size_t) i] * e * e;
            }
            return c;
        };
        for (int j = 0; j < B; ++j)
            evalBand(j);

        double lambda = 1.0e-2;
        double current = cost();
        std::vector<double> J((size_t) (G * P)), JtJ((size_t) (P * P)), Jtr((size_t) P), r((size_t) G);
        for (int iter = 0; iter < 40; ++iter)
        {
            for (int i = 0; i < G; ++i)
            {
                double m = 0.0;
                for (int j = 0; j < B; ++j)
                    m += contrib[(size_t) j][(size_t) i];
                r[(size_t) i] = (grid.target[(size_t) i] - m) * std::sqrt(grid.weight[(size_t) i]);
            }
            for (int j = 0; j < B; ++j)
                for (int p = 0; p < 3; ++p)
                {
                    Band t = bands[(size_t) j];
                    const double h = p == 1 ? 0.05 : 0.01;
                    (p == 0 ? t.lf : (p == 1 ? t.gain : t.lq)) += h;
                    for (int i = 0; i < G; ++i)
                        J[(size_t) (i * P + 3 * j + p)] = (bandDb(t, grid.hz[(size_t) i]) - contrib[(size_t) j][(size_t) i]) / h
                                                          * std::sqrt(grid.weight[(size_t) i]);
                }
            std::fill(JtJ.begin(), JtJ.end(), 0.0);
            std::fill(Jtr.begin(), Jtr.end(), 0.0);
            for (int i = 0; i < G; ++i)
                for (int a = 0; a < P; ++a)
                {
                    const double ja = J[(size_t) (i * P + a)];
                    if (ja == 0.0)
                        continue;
                    Jtr[(size_t) a] += ja * r[(size_t) i];
                    for (int b = 0; b < P; ++b)
                        JtJ[(size_t) (a * P + b)] += ja * J[(size_t) (i * P + b)];
                }

            bool improved = false;
            for (int tries = 0; tries < 6 && !improved; ++tries)
            {
                auto A = JtJ;
                auto delta = Jtr;
                for (int a = 0; a < P; ++a)
                    A[(size_t) (a * P + a)] += lambda * (JtJ[(size_t) (a * P + a)] + 1.0e-6);
                if (!solve(A, delta, P))
                {
                    lambda *= 10.0;
                    continue;
                }
                const auto saved = bands;
                const auto savedContrib = contrib;
                for (int j = 0; j < B; ++j)
                {
                    bands[(size_t) j].lf += delta[(size_t) (3 * j)];
                    bands[(size_t) j].gain += delta[(size_t) (3 * j + 1)];
                    bands[(size_t) j].lq += delta[(size_t) (3 * j + 2)];
                    clampBand(bands[(size_t) j]);
                    evalBand(j);
                }
                const double c = cost();
                if (c < current)
                {
                    current = c;
                    lambda = std::max(1.0e-6, lambda / 3.0);
                    improved = true;
                }
                else
                {
                    bands = saved;
                    contrib = savedContrib;
                    lambda *= 4.0;
                }
            }
            if (!improved)
                break;
        }
    }

    std::vector<BandParams> out;
    for (const auto& b : bands)
    {
        if (std::abs(b.gain) < 0.1)
            continue;
        BandParams p;
        p.used = true;
        p.enabled = true;
        p.shape = b.shape;
        p.freq = (float) std::exp(b.lf);
        p.gainDb = (float) b.gain;
        p.q = (float) std::exp(b.lq);
        p.slope = 1;
        out.push_back(p);
    }
    return out;
}

/** Suavizado en fracciones de octava (dB sobre la grilla logarítmica). */
inline std::vector<double> smoothOctave(const std::vector<double>& hz, const std::vector<double>& db, double fraction)
{
    std::vector<double> out(db.size(), 0.0);
    const double half = 0.5 * fraction;
    for (size_t i = 0; i < db.size(); ++i)
    {
        double s = 0.0;
        int n = 0;
        for (size_t k = 0; k < db.size(); ++k)
            if (std::abs(std::log2(hz[k] / hz[i])) <= half)
            {
                s += db[k];
                ++n;
            }
        out[i] = n > 0 ? s / n : db[i];
    }
    return out;
}
} // namespace prisma
