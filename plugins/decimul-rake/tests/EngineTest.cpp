// Prueba del motor de Rake sin DAW: ruido blanco por el peine -> ¿resuena en la nota y sus armónicos?
#include "../Source/RakeEngine.h"
#include <cstdio>
#include <vector>

namespace
{
    int failures = 0;
    void check(bool ok, const char* what, double v = 0)
    {
        std::printf("[%s] %s (%.3f)\n", ok ? " ok " : "FAIL", what, v);
        if (!ok) ++failures;
    }
    constexpr double sr = 48000.0;

    std::vector<float> noise(int n)
    {
        std::vector<float> x((size_t) n);
        unsigned s = 12345;
        for (auto& v : x) { s = s * 1664525u + 1013904223u; v = ((s >> 9) / 8388608.0f - 0.5f) * 0.5f; }
        return x;
    }
    std::vector<float> run(rake::Params p, std::vector<float> in)
    {
        rake::Engine e;
        e.prepare(sr);
        std::vector<float> l = in, r = in;
        for (size_t s = 0; s < l.size(); s += 256)
            e.process(l.data() + s, r.data() + s, (int) std::min<size_t>(256, l.size() - s), p);
        return l;
    }
    double band(const std::vector<float>& x, double hz)
    {
        // Energía alrededor de una frecuencia (Goertzel promediado en ventanas).
        double total = 0;
        const size_t win = 4800;
        for (size_t s = x.size() / 2; s + win < x.size(); s += win)
        {
            double re = 0, im = 0;
            for (size_t i = 0; i < win; ++i) { const double ph = 2 * rake::pi * hz * i / sr; re += x[s + i] * std::cos(ph); im += x[s + i] * std::sin(ph); }
            total += re * re + im * im;
        }
        return total;
    }
}

int main()
{
    const auto in = noise((int) sr * 2);
    rake::Params p;
    p.note = 57.0f;           // 220 Hz
    p.mix = 1.0f; p.feedback = 0.9f; p.damping = 0.0f;
    {
        auto out = run(p, in);
        const double onNote = band(out, 220.0) + band(out, 440.0) + band(out, 660.0);
        const double between = band(out, 330.0) + band(out, 550.0) + band(out, 770.0);
        check(onNote > 20.0 * between, "resuena en la nota y sus armónicos (220, 440, 660 Hz)", onNote / between);
    }
    {
        auto q = p; q.feedback = -0.9f;
        auto out = run(q, in);
        // Con feedback negativo la resonancia cae en los armónicos impares de la mitad (110, 330, 550...).
        check(band(out, 330.0) > 10.0 * band(out, 440.0), "feedback negativo: suena hueco, en los impares", band(out, 330.0) / band(out, 440.0));
    }
    {
        auto q = p; q.chord = 1;   // quinta: 220 y 330 Hz
        auto out = run(q, in);
        check(band(out, 330.0) > 5.0 * band(out, 275.0), "acorde de quinta: también resuena la quinta", band(out, 330.0) / band(out, 275.0));
    }
    {
        auto q = p; q.mix = 0.0f;
        auto out = run(q, in);
        double d = 0; for (size_t i = 0; i < out.size(); ++i) d += std::fabs(out[i] - in[i]);
        check(d < 1e-3, "mix 0: pasa la señal tal cual", d);
    }
    {
        auto q = p; q.feedback = 0.99f; q.lfoDepth = 1.0f; q.lfoRateHz = 5.0f; q.chord = 5;
        std::vector<float> loud = in; for (auto& v : loud) v *= 4.0f;
        auto out = run(q, loud);
        bool finite = true; float pk = 0; for (float v : out) { finite = finite && std::isfinite(v); pk = std::max(pk, std::fabs(v)); }
        check(finite && pk < 3.0f, "feedback al máximo, LFO y acorde: estable y sin picos enormes", pk);
    }
    {
        auto bright = p; bright.damping = 0.0f;
        auto dark = p; dark.damping = 1.0f;
        auto a = run(bright, in), b = run(dark, in);
        check(band(b, 3080.0) < 0.5 * band(a, 3080.0), "DAMPING apaga los armónicos agudos", band(b, 3080.0) / band(a, 3080.0));
    }
    std::printf("\n%s\n", failures == 0 ? "TODO OK" : "HAY FALLAS");
    return failures ? 1 : 0;
}
