// Prueba del motor de Navaja sin DAW.
#include "../Source/NavajaEngine.h"
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
    constexpr double sr = 48000.0, pi = 3.14159265358979;

    struct Out { std::vector<float> l, r; float corr; };
    Out run(navaja::Params p, double fl, double fr, double ampL = 0.5, double ampR = 0.5, double dc = 0.0, double phaseR = 0.0)
    {
        navaja::Engine e;
        e.prepare(sr);
        const int n = (int) sr;
        Out o { std::vector<float>((size_t) n), std::vector<float>((size_t) n), 0 };
        for (int i = 0; i < n; ++i)
        {
            o.l[(size_t) i] = (float) (ampL * std::sin(2 * pi * fl * i / sr) + dc);
            o.r[(size_t) i] = (float) (ampR * std::sin(2 * pi * fr * i / sr + phaseR) + dc);
        }
        for (int s = 0; s < n; s += 256)
            e.process(o.l.data() + s, o.r.data() + s, std::min(256, n - s), p);
        o.corr = e.correlation();
        return o;
    }
    double rms(const std::vector<float>& x, size_t from)
    {
        double e = 0;
        for (size_t i = from; i < x.size(); ++i) e += (double) x[i] * x[i];
        return std::sqrt(e / (double) (x.size() - from));
    }
}

int main()
{
    const size_t tail = (size_t) (sr * 0.5);
    navaja::Params p;
    { auto o = run(p, 440, 440); check(std::fabs(rms(o.l, tail) - 0.3536) < 0.002, "por defecto no cambia nada", rms(o.l, tail)); check(o.corr > 0.99f, "señal mono: correlación +1", o.corr); }
    { auto q = p; q.gainDb = 6.0f; auto o = run(q, 440, 440); check(std::fabs(rms(o.l, tail) / 0.3536 - 1.995) < 0.02, "+6 dB duplica el nivel", rms(o.l, tail) / 0.3536); }
    { auto q = p; q.pan = -1.0f; auto o = run(q, 440, 440); check(rms(o.r, tail) < 1e-6, "paneo a la izquierda: derecha en silencio", rms(o.r, tail)); }
    { auto q = p; q.invertR = true; auto o = run(q, 440, 440); check(o.corr < -0.99f, "invertir la fase de R: correlación -1", o.corr); }
    { auto q = p; q.mono = true; auto o = run(q, 440, 440, 0.5, 0.0); check(std::fabs(rms(o.l, tail) - rms(o.r, tail)) < 1e-4 && o.corr > 0.99f, "mono: L y R iguales", rms(o.r, tail)); }
    { auto q = p; q.swap = true; auto o = run(q, 440, 440, 0.5, 0.0); check(rms(o.l, tail) < 1e-6 && rms(o.r, tail) > 0.3, "intercambiar L/R", rms(o.r, tail)); }
    { auto q = p; q.width = 0.0f; auto o = run(q, 440, 440, 0.5, 0.0); check(std::fabs(rms(o.l, tail) - rms(o.r, tail)) < 1e-4, "ancho 0 = mono", rms(o.l, tail)); }
    { auto q = p; q.width = 2.0f; auto o = run(q, 440, 440, 0.5, 0.5, 0.0, 1.0); auto ref = run(p, 440, 440, 0.5, 0.5, 0.0, 1.0); check(o.corr < ref.corr - 0.2f, "ancho 200 % abre la imagen (baja la correlación)", o.corr); }
    {
        // Graves en mono: un grave en fase invertida (puro "side") desaparece; un agudo igual queda.
        auto q = p; q.bassMono = true; q.bassMonoHz = 150.0f;
        auto low = run(q, 50, 50, 0.5, 0.5, 0.0, pi);
        auto high = run(q, 3000, 3000, 0.5, 0.5, 0.0, pi);
        check(rms(low.l, tail) < 0.06, "graves en mono: el side de 50 Hz se va", rms(low.l, tail));
        check(rms(high.l, tail) > 0.3, "graves en mono: el side de 3 kHz queda", rms(high.l, tail));
    }
    {
        auto q = p; q.dcFilter = true;
        auto o = run(q, 440, 440, 0.3, 0.3, 0.4);
        double mean = 0; for (size_t i = tail; i < o.l.size(); ++i) mean += o.l[i]; mean /= (double) (o.l.size() - tail);
        check(std::fabs(mean) < 0.005, "el filtro DC saca el corrimiento", mean);
    }
    std::printf("\n%s\n", failures == 0 ? "TODO OK" : "HAY FALLAS");
    return failures ? 1 : 0;
}
