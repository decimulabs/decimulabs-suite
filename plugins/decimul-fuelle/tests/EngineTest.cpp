// Prueba del motor de Fuelle sin DAW.
#include "../Source/FuelleEngine.h"
#include <cstdio>
#include <vector>

namespace
{
    int failures = 0;
    void check(bool ok, const char* what, double v = 0)
    {
        std::printf("[%s] %s (%.4f)\n", ok ? " ok " : "FAIL", what, v);
        if (!ok) ++failures;
    }
    constexpr double sr = 48000.0;

    std::vector<float> run(fuelle::Params p, const std::vector<float>& in, std::vector<float>* right = nullptr)
    {
        fuelle::Engine e;
        e.prepare(sr);
        std::vector<float> l = in, r = in;
        for (size_t s = 0; s < l.size(); s += 256)
            e.process(l.data() + s, r.data() + s, (int) std::min<size_t>(256, l.size() - s), p);
        if (right) *right = r;
        return l;
    }
    double rms(const std::vector<float>& x, double t0, double t1)
    {
        double e = 0; size_t a = (size_t) (t0 * sr), b = (size_t) (t1 * sr);
        for (size_t i = a; i < b; ++i) e += (double) x[i] * x[i];
        return std::sqrt(e / (double) (b - a));
    }
}

int main()
{
    std::vector<float> click((size_t) (sr * 4.0), 0.0f);
    click[0] = 1.0f;
    fuelle::Params p;
    p.mix = 1.0f; p.duck = 0.0f; p.lowCutHz = 20.0f; p.predelayMs = 50.0f;
    {
        auto out = run(p, click);
        size_t first = 0;
        for (size_t i = 10; i < out.size(); ++i) if (std::fabs(out[i]) > 1e-4f) { first = i; break; }
        check(first >= (size_t) (0.050 * sr) && first < (size_t) (0.090 * sr), "la cola arranca después del predelay de 50 ms", first / sr * 1000.0);
        check(rms(out, 0.2, 0.5) > rms(out, 2.0, 2.3) * 3.0, "la cola se va apagando", rms(out, 2.0, 2.3));
    }
    {
        auto big = p; big.size = 1.0f;
        auto small = p; small.size = 0.0f;
        auto a = run(big, click), b = run(small, click);
        check(rms(a, 1.5, 2.0) > 3.0 * rms(b, 1.5, 2.0), "SIZE grande = cola más larga", rms(a, 1.5, 2.0) / std::max(1e-12, rms(b, 1.5, 2.0)));
    }
    {
        std::vector<float> in((size_t) (sr * 4.0), 0.0f);
        for (size_t i = 0; i < (size_t) (sr * 2.0); ++i) in[i] = 0.5f * (float) std::sin(2 * 3.14159265 * 330 * i / sr);
        auto dry = in;
        auto q = p; q.duck = 1.0f; q.thresholdDb = -40.0f; q.releaseMs = 200.0f;
        auto noDuck = q; noDuck.duck = 0.0f;
        auto a = run(q, in), b = run(noDuck, in);
        for (size_t i = 0; i < a.size(); ++i) { a[i] -= dry[i]; b[i] -= dry[i]; }
        check(rms(a, 1.0, 1.9) < 0.1 * rms(b, 1.0, 1.9), "mientras suena, la reverb baja más de 20 dB", rms(a, 1.0, 1.9) / rms(b, 1.0, 1.9));
        check(rms(a, 2.4, 2.9) > 0.4 * rms(b, 2.4, 2.9), "en el silencio la cola vuelve (respira)", rms(a, 2.4, 2.9) / rms(b, 2.4, 2.9));
    }
    {
        auto q = p; q.width = 0.0f;
        std::vector<float> rr;
        auto ll = run(q, click, &rr);
        double diff = 0; for (size_t i = 0; i < ll.size(); ++i) diff += std::fabs(ll[i] - rr[i]);
        check(diff < 1e-3, "ancho 0: la cola sale en mono", diff);
    }
    {
        auto q = p; q.mix = 0.0f;
        auto out = run(q, click);
        check(std::fabs(out[0] - 1.0f) < 1e-6 && rms(out, 0.01, 3.9) < 1e-7, "mix 0: pasa la señal sin reverb", rms(out, 0.01, 3.9));
    }
    {
        auto q = p; q.size = 1.0f; q.damping = 0.0f;
        std::vector<float> noise((size_t) (sr * 3), 0.0f);
        for (size_t i = 0; i < noise.size(); ++i) noise[i] = (float) ((i * 2654435761u % 1000) / 1000.0 - 0.5);
        auto out = run(q, noise);
        bool finite = true; float pk = 0; for (float v : out) { finite = finite && std::isfinite(v); pk = std::max(pk, std::fabs(v)); }
        check(finite && pk < 10.0f, "SIZE al máximo sin amortiguar: estable", pk);
    }
    std::printf("\n%s\n", failures == 0 ? "TODO OK" : "HAY FALLAS");
    return failures ? 1 : 0;
}
