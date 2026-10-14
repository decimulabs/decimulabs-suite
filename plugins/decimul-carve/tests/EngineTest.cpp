// Prueba del motor de Carve sin DAW: senales sinteticas -> medir cuanto baja cada frecuencia.
#include "../Source/CarveEngine.h"
#include <cstdio>
#include <vector>

namespace
{
    constexpr double sr = 48000.0;
    constexpr double pi = 3.14159265358979323846;
    int failures = 0;

    void check(bool ok, const char* what, double value = 0.0)
    {
        std::printf("[%s] %s (%.2f)\n", ok ? " ok " : "FAIL", what, value);
        if (!ok) ++failures;
    }

    // Nivel (dB) de una frecuencia dentro de `x`, por correlacion con seno/coseno.
    double levelDbAt(const std::vector<float>& x, double hz, size_t from)
    {
        double re = 0, im = 0;
        const size_t n = x.size() - from;
        for (size_t i = from; i < x.size(); ++i)
        {
            const double ph = 2.0 * pi * hz * (double) i / sr;
            re += x[i] * std::cos(ph);
            im += x[i] * std::sin(ph);
        }
        const double amp = 2.0 * std::sqrt(re * re + im * im) / (double) n;
        return 20.0 * std::log10(amp + 1e-12);
    }

    struct Result { std::vector<float> out; carve::Engine engine; };

    // Pista principal = 200 Hz + 1 kHz + 5 kHz a -20 dB c/u. Sidechain = seno (o nada).
    Result run(const carve::Params& p, double scHz, double scDb, bool withSidechain)
    {
        Result r;
        r.engine.prepare(sr);
        const int n = (int) (sr * 1.5);
        std::vector<float> l((size_t) n), rr((size_t) n), sc((size_t) n);
        const double amp = std::pow(10.0, -20.0 / 20.0);
        const double scAmp = std::pow(10.0, scDb / 20.0);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            const double v = amp * (std::sin(2 * pi * 200 * t) + std::sin(2 * pi * 1000 * t) + std::sin(2 * pi * 5000 * t));
            l[(size_t) i] = rr[(size_t) i] = (float) v;
            sc[(size_t) i] = (float) (scAmp * std::sin(2 * pi * scHz * t));
        }
        const int block = 256;
        for (int s = 0; s < n; s += block)
        {
            const int c = std::min(block, n - s);
            r.engine.process(l.data() + s, rr.data() + s,
                             withSidechain ? sc.data() + s : nullptr, withSidechain ? sc.data() + s : nullptr, c, p);
        }
        r.out = l;
        return r;
    }
}

int main()
{
    const size_t settle = (size_t) (sr * 1.0);

    {
        carve::Params p;
        p.amount = 1.0f;
        auto r = run(p, 1000.0, -6.0, true);
        const double d200 = levelDbAt(r.out, 200, settle) + 20.0;
        const double d1k = levelDbAt(r.out, 1000, settle) + 20.0;
        const double d5k = levelDbAt(r.out, 5000, settle) + 20.0;
        check(d1k < -10.0, "sidechain 1 kHz baja el 1 kHz de la pista (> 10 dB)", d1k);
        check(std::fabs(d200) < 1.5, "el 200 Hz casi no se toca", d200);
        check(std::fabs(d5k) < 1.5, "el 5 kHz casi no se toca", d5k);
    }
    {
        carve::Params p;
        p.amount = 0.0f;
        auto r = run(p, 1000.0, -6.0, true);
        check(std::fabs(levelDbAt(r.out, 1000, settle) + 20.0) < 0.1, "Amount 0 = no hace nada", levelDbAt(r.out, 1000, settle) + 20.0);
    }
    {
        carve::Params p;
        p.amount = 1.0f;
        auto r = run(p, 1000.0, -6.0, false);
        check(std::fabs(levelDbAt(r.out, 1000, settle) + 20.0) < 0.1, "sin sidechain no recorta", levelDbAt(r.out, 1000, settle) + 20.0);
    }
    {
        carve::Params p;
        p.amount = 1.0f;
        p.lowHz = 2000.0f;   // el 1 kHz queda fuera del rango
        auto r = run(p, 1000.0, -6.0, true);
        check(std::fabs(levelDbAt(r.out, 1000, settle) + 20.0) < 1.0, "el rango Low/High protege lo que queda afuera", levelDbAt(r.out, 1000, settle) + 20.0);
    }
    {
        carve::Params p;
        p.amount = 1.0f;
        p.mix = 0.5f;
        auto full = run(carve::Params { 1.0f, 0, 8, 150, 20, 20000, 0.25f, 1.0f }, 1000.0, -6.0, true);
        auto half = run(p, 1000.0, -6.0, true);
        const double a = levelDbAt(full.out, 1000, settle) + 20.0;
        const double b = levelDbAt(half.out, 1000, settle) + 20.0;
        check(b > a + 3.0, "Mix 50 % recorta menos que Mix 100 %", b - a);
    }
    {
        carve::Params p;
        p.amount = 1.0f;
        p.scGainDb = 24.0f;
        p.attackMs = 0.1f;
        p.releaseMs = 5.0f;
        p.smooth = 1.0f;
        auto r = run(p, 60.0, 0.0, true);
        bool finite = true;
        float peak = 0.0f;
        for (float v : r.out) { finite = finite && std::isfinite(v); peak = std::max(peak, std::fabs(v)); }
        check(finite && peak < 2.0f, "parametros extremos: estable y sin NaN", peak);
    }
    {
        // Recuperacion: al callarse el sidechain, el nivel vuelve (release).
        carve::Params p;
        p.amount = 1.0f;
        p.releaseMs = 50.0f;
        carve::Engine e;
        e.prepare(sr);
        const int n = (int) (sr * 0.5);
        std::vector<float> l((size_t) n), r((size_t) n), sc((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            l[(size_t) i] = r[(size_t) i] = (float) (0.1 * std::sin(2 * pi * 1000 * t));
            sc[(size_t) i] = i < n / 5 ? (float) (0.5 * std::sin(2 * pi * 1000 * t)) : 0.0f;
        }
        for (int s = 0; s < n; s += 256)
        {
            const int c = std::min(256, n - s);
            e.process(l.data() + s, r.data() + s, sc.data() + s, sc.data() + s, c, p);
        }
        std::vector<float> tail(l.end() - (long) (sr * 0.1), l.end());
        const double rec = levelDbAt(tail, 1000, 0) ;
        check(std::fabs(rec - (-20.0)) < 1.0, "al callarse el sidechain el 1 kHz vuelve a su nivel", rec);
    }

    {
        // Info: sidechain de banda ancha (un seno por banda) -> recorte medido en 1 kHz.
        for (double scDb : { -40.0, -30.0, -20.0 })
        {
            carve::Engine e;
            e.prepare(sr);
            const int n = (int) (sr * 1.5);
            std::vector<float> l((size_t) n), r((size_t) n), sc((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double t = (double) i / sr;
                double v = 0;
                for (int b = 0; b < carve::numBands; ++b)
                    v += std::sin(2 * pi * carve::bandCentreHz(b) * t + b * 1.7);
                // Cada seno queda a scDb (en amplitud) dentro del sidechain.
                sc[(size_t) i] = (float) (v * std::pow(10.0, scDb / 20.0));
                l[(size_t) i] = r[(size_t) i] = (float) (0.1 * std::sin(2 * pi * 1000 * t));
            }
            carve::Params p;
            p.amount = 1.0f;
            p.smooth = 0.0f;
            for (int s = 0; s < n; s += 256)
            {
                const int c = std::min(256, n - s);
                e.process(l.data() + s, r.data() + s, sc.data() + s, sc.data() + s, c, p);
            }
            std::printf("info: banda ancha, cada banda a %.0f dB: recorte por banda %.2f dB, recorte medido en 1 kHz %.2f dB\n",
                        scDb, e.cutsDb()[16], -(levelDbAt(l, 1000, (size_t) (sr * 1.0)) + 20.0));
        }
    }

    std::printf("\n%s\n", failures == 0 ? "TODO OK" : "HAY FALLAS");
    return failures == 0 ? 0 : 1;
}
