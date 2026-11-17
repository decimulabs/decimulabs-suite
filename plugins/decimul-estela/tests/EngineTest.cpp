// Prueba del motor de Estela (y del agachado) sin DAW.
#include "../Source/EstelaEngine.h"
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

    std::vector<float> run(estela::Params p, const std::vector<float>& in, std::vector<float>* right = nullptr)
    {
        estela::Engine e;
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
    // Un clic al principio y silencio: el eco aparece a los 375 ms.
    std::vector<float> click((size_t) (sr * 2.0), 0.0f);
    click[0] = 1.0f;
    estela::Params p;
    p.mix = 1.0f; p.duck = 0.0f; p.wobble = 0.0f; p.lowCutHz = 20.0f; p.highCutHz = 20000.0f;
    {
        auto out = run(p, click);
        size_t peak = 1; float best = 0;
        for (size_t i = 1000; i < (size_t) (sr * 0.6); ++i) if (std::fabs(out[i]) > best) { best = std::fabs(out[i]); peak = i; }
        check(std::abs((double) peak - 0.375 * sr) < 30, "el primer eco cae a los 375 ms", peak / sr * 1000.0);
        check(best > 0.5f, "el primer eco tiene nivel", best);
        float second = 0; for (size_t i = (size_t) (0.7 * sr); i < (size_t) (0.8 * sr); ++i) second = std::max(second, std::fabs(out[i]));
        check(second > 0.25f && second < best, "el segundo eco es más bajo (realimentación)", second);
    }
    {
        // Ping-pong: el primer eco a la izquierda, el segundo a la derecha.
        auto q = p; q.pingPong = true;
        std::vector<float> rr;
        auto ll = run(q, click, &rr);
        float l1 = 0, r1 = 0, l2 = 0, r2 = 0;
        for (size_t i = (size_t) (0.3 * sr); i < (size_t) (0.45 * sr); ++i) { l1 = std::max(l1, std::fabs(ll[i])); r1 = std::max(r1, std::fabs(rr[i])); }
        for (size_t i = (size_t) (0.7 * sr); i < (size_t) (0.8 * sr); ++i) { l2 = std::max(l2, std::fabs(ll[i])); r2 = std::max(r2, std::fabs(rr[i])); }
        check(l1 > 3 * r1 && r2 > 3 * l2, "ping-pong: izquierda y después derecha", r2);
    }
    {
        // Agachado: con la entrada sonando fuerte los ecos bajan; en el silencio vuelven.
        std::vector<float> in((size_t) (sr * 3.0), 0.0f);
        for (size_t i = 0; i < (size_t) (sr * 1.5); ++i) in[i] = 0.5f * (float) std::sin(2 * 3.14159265 * 440 * i / sr);
        auto dry = in;
        auto q = p; q.mix = 1.0f; q.feedback = 0.6f; q.duck = 1.0f; q.thresholdDb = -40.0f; q.releaseMs = 150.0f;
        auto noDuck = q; noDuck.duck = 0.0f;
        auto a = run(q, in), b = run(noDuck, in);
        for (size_t i = 0; i < a.size(); ++i) { a[i] -= dry[i]; b[i] -= dry[i]; }      // sólo los ecos
        check(rms(a, 0.8, 1.4) < 0.1 * rms(b, 0.8, 1.4), "mientras suena, los ecos bajan más de 20 dB", rms(a, 0.8, 1.4) / rms(b, 0.8, 1.4));
        check(rms(a, 2.0, 2.4) > 0.5 * rms(b, 2.0, 2.4), "en el silencio los ecos vuelven", rms(a, 2.0, 2.4) / rms(b, 2.0, 2.4));
    }
    {
        auto q = p; q.mix = 0.0f;
        auto out = run(q, click);
        check(std::fabs(out[0] - 1.0f) < 1e-6 && rms(out, 0.1, 1.9) < 1e-7, "mix 0: pasa la señal sin ecos", rms(out, 0.1, 1.9));
    }
    {
        auto q = p; q.feedback = 0.95f; q.wobble = 1.0f;
        std::vector<float> noise((size_t) (sr * 2), 0.0f);
        for (size_t i = 0; i < noise.size(); ++i) noise[i] = (float) ((i * 2654435761u % 1000) / 1000.0 - 0.5);
        auto out = run(q, noise);
        bool finite = true; float pk = 0; for (float v : out) { finite = finite && std::isfinite(v); pk = std::max(pk, std::fabs(v)); }
        check(finite && pk < 20.0f, "realimentación al máximo y wobble: estable", pk);
    }
    std::printf("\n%s\n", failures == 0 ? "TODO OK" : "HAY FALLAS");
    return failures ? 1 : 0;
}
