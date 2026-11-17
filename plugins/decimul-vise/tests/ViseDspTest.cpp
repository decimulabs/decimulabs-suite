// Test del motor de Vise (compresor de bus), solo biblioteca estándar, sin JUCE.
// Se compila suelto con tests/build_dsp.bat o con cmake -DDECIMU_VISE_TESTS=ON (objetivo ViseDspTest).
// Todo debe decir "TODO OK".

#include "ViseDsp.h"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <xmmintrin.h>

using namespace vise;

namespace
{
    int failures = 0;
    double sr = 48000.0;
    const double pi = 3.14159265358979323846;

    void check(bool ok, const std::string& what, const std::string& detail = {})
    {
        std::cout << (ok ? "[ OK ] " : "[FAIL] ") << what << (detail.empty() ? "" : "  -> " + detail) << std::endl;
        if (!ok)
            ++failures;
    }

    std::string num(double v, int prec = 2)
    {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.*f", prec, v);
        return buf;
    }

    double dbOf(double lin) { return 20.0 * std::log10(std::max(lin, 1.0e-12)); }
    int ms(double t) { return (int) std::lround(t * 0.001 * sr); }

    std::vector<float> tone(int n, double f, float db, double phase = 0.0)
    {
        std::vector<float> v((size_t) n);
        const float a = std::pow(10.0f, db / 20.0f);
        for (int i = 0; i < n; ++i) v[(size_t) i] = a * (float) std::sin(2.0 * pi * f * i / sr + phase);
        return v;
    }

    double rms(const std::vector<float>& v, int from, int to)
    {
        double s = 0.0;
        for (int i = from; i < to; ++i) s += (double) v[(size_t) i] * v[(size_t) i];
        return std::sqrt(s / (double) std::max(1, to - from));
    }

    struct Result { std::vector<float> l, r; std::vector<float> gr; };

    // Corre el compresor y registra la reducción (dB) muestra a muestra.
    Result run(const std::vector<float>& inL, const std::vector<float>& inR, const Params& p)
    {
        BusCompressor c;
        c.setParams(p);
        c.prepare(sr);
        Result o { inL, inR, std::vector<float>(inL.size()) };
        for (size_t i = 0; i < inL.size(); ++i)
        {
            c.process(&o.l[i], &o.r[i], 1);
            o.gr[i] = c.currentGr();
        }
        return o;
    }
}

int main()
{
    _mm_setcsr(_mm_getcsr() | 0x8040);   // como ScopedNoDenormals en el plugin

    const int n = ms(2000.0);
    const int a = ms(1000.0);              // desde acá ya está asentado

    // ---------- Por debajo del umbral no toca nada ----------
    {
        auto in = tone(n, 440.0, -30.0f);
        Params p; p.thresholdDb = -18.0f;
        auto o = run(in, in, p);
        double maxDiff = 0.0;
        for (size_t i = 0; i < in.size(); ++i) maxDiff = std::max(maxDiff, (double) std::abs(o.l[i] - in[i]));
        check(maxDiff < 1.0e-6, "Bajo el umbral la señal pasa intacta", num(maxDiff, 9));
    }

    // ---------- Curva estática ----------
    {
        auto in = tone(n, 1000.0, -6.0f);                // 12 dB sobre el umbral de -18
        Params p; p.thresholdDb = -18.0f; p.ratio = 4.0f; p.kneeDb = 0.0f; p.attackMs = 1.0f;
        auto o = run(in, in, p);
        const double gr = o.gr[(size_t) (n - 100)];
        check(std::abs(gr - 9.0) < 1.0, "Ratio 4:1, 12 dB sobre el umbral: baja unos 9 dB", num(gr, 2) + " dB");
        p.ratio = 10.0f;
        auto o2 = run(in, in, p);
        const double gr2 = o2.gr[(size_t) (n - 100)];
        check(std::abs(gr2 - 10.8) < 1.0, "Ratio 10:1: baja unos 10.8 dB", num(gr2, 2) + " dB");
        p.ratio = 1.1f;
        auto o3 = run(in, in, p);
        check(o3.gr[(size_t) (n - 100)] < 1.6, "Ratio 1.1:1: casi no comprime", num(o3.gr[(size_t) (n - 100)], 2) + " dB");
        // la salida medida coincide con la reducción
        p.ratio = 4.0f;
        auto o4 = run(in, in, p);
        const double measured = dbOf(rms(in, a, n)) - dbOf(rms(o4.l, a, n));
        check(std::abs(measured - 9.0) < 1.2, "El nivel de salida baja lo mismo que marca la reducción", num(measured, 2) + " dB");
    }

    // ---------- Rodilla ----------
    {
        // justo en el umbral, con rodilla de 12 dB y 4:1 la reducción teórica es ~1.1 dB
        auto in = tone(n, 1000.0, -18.0f);
        Params p; p.thresholdDb = -18.0f; p.ratio = 4.0f; p.kneeDb = 12.0f; p.attackMs = 1.0f;
        auto o = run(in, in, p);
        const double gr = o.gr[(size_t) (n - 100)];
        check(gr > 0.5 && gr < 2.2, "Rodilla de 12 dB: en el umbral comprime suave (~1 dB)", num(gr, 2) + " dB");
        p.kneeDb = 0.0f;
        auto o2 = run(in, in, p);
        check(o2.gr[(size_t) (n - 100)] < gr, "Rodilla 0: en el umbral comprime menos que con rodilla ancha");
    }

    // ---------- Ataque ----------
    {
        // paso de -40 a -6 dBFS: la reducción llega al 63 % del final en ~ el tiempo de ataque
        auto atkTime = [&](float attackMs)
        {
            const int total = ms(600.0);
            auto in = tone(total, 1000.0, -6.0f);
            for (int i = 0; i < ms(100.0); ++i) in[(size_t) i] *= 0.01f;
            Params p; p.thresholdDb = -18.0f; p.ratio = 4.0f; p.kneeDb = 0.0f; p.attackMs = attackMs; p.releaseMs = 1500.0f;
            auto o = run(in, in, p);
            const double finalGr = o.gr[(size_t) (total - 10)];
            for (int i = ms(100.0); i < total; ++i)
                if (o.gr[(size_t) i] >= 0.63 * finalGr)
                    return (double) (i - ms(100.0)) / sr * 1000.0;
            return 999.0;
        };
        const double t10 = atkTime(10.0f), t1 = atkTime(1.0f), t30 = atkTime(30.0f);
        check(t10 > 5.0 && t10 < 16.0, "Ataque de 10 ms: la reducción llega al 63 % en unos 10 ms", num(t10, 1) + " ms");
        check(t1 < t10 && t10 < t30, "Más ataque = reacciona más lento", num(t1, 1) + " < " + num(t10, 1) + " < " + num(t30, 1) + " ms");
    }

    // ---------- Caída ----------
    {
        auto relTime = [&](float releaseMs, bool autoRel, double burstDb)
        {
            const int total = ms(4000.0);
            auto in = tone(total, 1000.0, (float) burstDb);
            for (int i = ms(1500.0); i < total; ++i) in[(size_t) i] *= 0.01f;      // el golpe fuerte termina a los 1.5 s
            Params p; p.thresholdDb = -30.0f; p.ratio = 4.0f; p.kneeDb = 0.0f; p.attackMs = 1.0f;
            p.releaseMs = releaseMs; p.autoRelease = autoRel;
            auto o = run(in, in, p);
            const double start = o.gr[(size_t) (ms(1500.0) - 20)];
            for (int i = ms(1500.0); i < total; ++i)
                if (o.gr[(size_t) i] <= 0.5 * start)
                    return (double) (i - ms(1500.0)) / sr * 1000.0;
            return 9999.0;
        };
        const double r100 = relTime(100.0f, false, -6.0), r500 = relTime(500.0f, false, -6.0);
        check(r500 > 3.0 * r100 && r500 > 150.0 && r500 < 600.0, "Caída: 500 ms es bastante más lenta que 100 ms",
              num(r100, 0) + " ms contra " + num(r500, 0) + " ms (a la mitad)");
        const double aShallow = relTime(300.0f, true, -22.0), aDeep = relTime(300.0f, true, -3.0);
        check(aDeep > 1.4 * aShallow, "Caída automática: comprimir mucho suelta más lento que comprimir poco",
              num(aShallow, 0) + " ms contra " + num(aDeep, 0) + " ms");
    }

    // ---------- Makeup ----------
    {
        auto in = tone(n, 1000.0, -30.0f);
        Params p; p.thresholdDb = -18.0f; p.makeupDb = 6.0f;
        auto o = run(in, in, p);
        const double d = dbOf(rms(o.l, a, n)) - dbOf(rms(in, a, n));
        check(std::abs(d - 6.0) < 0.1, "Makeup +6 dB sube la salida 6 dB", num(d, 2) + " dB");
        p.makeupDb = 0.0f; p.autoMakeup = true; p.thresholdDb = -24.0f; p.ratio = 4.0f;
        auto o2 = run(in, in, p);
        const double expected = BusCompressor::autoMakeupDb(-24.0f, 4.0f);
        const double d2 = dbOf(rms(o2.l, a, n)) - dbOf(rms(in, a, n));
        // -30 dBFS está bajo el umbral de -24, así que solo se nota el makeup
        check(expected > 8.0 && std::abs(d2 - expected) < 0.2, "Makeup automático según umbral y ratio", num(d2, 2) + " dB (previsto " + num(expected, 2) + ")");
    }

    // ---------- Mezcla paralela ----------
    {
        auto in = tone(n, 1000.0, -6.0f);
        Params p; p.thresholdDb = -24.0f; p.ratio = 10.0f; p.kneeDb = 0.0f; p.attackMs = 1.0f;
        p.mix = 0.0f;
        auto o0 = run(in, in, p);
        double maxDiff = 0.0;
        for (size_t i = 0; i < in.size(); ++i) maxDiff = std::max(maxDiff, (double) std::abs(o0.l[i] - in[i]));
        check(maxDiff < 1.0e-5, "Mix 0: sale la señal original", num(maxDiff, 8));
        p.mix = 100.0f;
        auto o100 = run(in, in, p);
        p.mix = 50.0f;
        auto o50 = run(in, in, p);
        const double l0 = dbOf(rms(o0.l, a, n)), l50 = dbOf(rms(o50.l, a, n)), l100 = dbOf(rms(o100.l, a, n));
        check(l0 > l50 && l50 > l100 && l0 - l100 > 10.0, "Mix 50: queda entre la original y la comprimida (compresión paralela)",
              num(l0, 1) + " / " + num(l50, 1) + " / " + num(l100, 1) + " dBFS");
    }

    // ---------- Pasa-altos del sidechain ----------
    {
        // un grave fuerte y una voz tranquila: sin filtro el grave aprieta a la voz, con filtro no
        auto lo = tone(n, 50.0, -8.0f), hi = tone(n, 2000.0, -26.0f);
        std::vector<float> in(lo.size());
        for (size_t i = 0; i < in.size(); ++i) in[i] = lo[i] + hi[i];
        Params p; p.thresholdDb = -20.0f; p.ratio = 4.0f; p.kneeDb = 0.0f; p.attackMs = 1.0f; p.releaseMs = 200.0f;
        auto off = run(in, in, p);
        p.scHpHz = 300.0f;
        auto on = run(in, in, p);
        auto grAvg = [&](const Result& r) { double s = 0; for (int i = a; i < n; ++i) s += r.gr[(size_t) i]; return s / (n - a); };
        check(grAvg(on) < 0.5 * grAvg(off), "Pasa-altos del sidechain: el grave deja de comprimir", num(grAvg(off), 1) + " dB sin filtro, " + num(grAvg(on), 1) + " dB con filtro");
    }

    // ---------- Enlace estéreo ----------
    {
        auto loud = tone(n, 1000.0, -6.0f), quiet = tone(n, 1000.0, -40.0f);
        Params p; p.thresholdDb = -18.0f; p.ratio = 4.0f; p.kneeDb = 0.0f; p.attackMs = 1.0f;
        p.link = 100.0f;
        auto linked = run(loud, quiet, p);
        const double dRlinked = dbOf(rms(quiet, a, n)) - dbOf(rms(linked.r, a, n));
        p.link = 0.0f;
        auto indep = run(loud, quiet, p);
        const double dRindep = dbOf(rms(quiet, a, n)) - dbOf(rms(indep.r, a, n));
        check(dRlinked > 7.0 && std::abs(dRindep) < 0.2, "Enlace 100 %: el canal bajo también se comprime; 0 %: no",
              num(dRlinked, 1) + " dB y " + num(dRindep, 2) + " dB");
    }

    // ---------- Pico contra RMS ----------
    {
        // un golpe corto muy fuerte: el detector de pico lo aprieta más que el RMS
        std::vector<float> in((size_t) n, 0.0f);
        const auto burst = tone(n, 1000.0, -2.0f);
        for (int rep = 0; rep < 8; ++rep)
            for (int i = 0; i < ms(1.5); ++i) in[(size_t) (ms(200.0) + rep * ms(250.0) + i)] = burst[(size_t) i];
        Params p; p.thresholdDb = -20.0f; p.ratio = 8.0f; p.kneeDb = 0.0f; p.attackMs = 0.1f; p.releaseMs = 100.0f;
        auto pk = run(in, in, p);
        p.rms = true;
        auto rm = run(in, in, p);
        double maxPk = 0, maxRm = 0;
        for (int i = 0; i < n; ++i) { maxPk = std::max(maxPk, (double) pk.gr[(size_t) i]); maxRm = std::max(maxRm, (double) rm.gr[(size_t) i]); }
        check(maxPk > maxRm + 3.0, "Detector de pico aprieta más los golpes cortos que el RMS", num(maxPk, 1) + " dB contra " + num(maxRm, 1) + " dB");
    }

    // ---------- Latencia cero ----------
    {
        std::vector<float> imp((size_t) 2000, 0.0f);
        imp[500] = 0.01f;                                  // bajo el umbral: pasa intacto
        Params p;
        auto o = run(imp, imp, p);
        int first = -1;
        for (int i = 0; i < 2000; ++i) if (std::abs(o.l[(size_t) i]) > 1.0e-6f) { first = i; break; }
        check(first == 500, "Latencia cero: el impulso sale en la misma muestra", "primera muestra " + std::to_string(first));
    }

    // ---------- Robustez ----------
    {
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            sr = rate;
            const int m = (int) (rate * 6.0);
            std::vector<float> l((size_t) m), r((size_t) m);
            unsigned s = 17;
            for (int i = 0; i < m; ++i)
            {
                s = s * 1664525u + 1013904223u;
                const float noise = ((float) (s >> 8) / 8388608.0f - 1.0f);
                l[(size_t) i] = 0.6f * (float) std::sin(2 * pi * 90.0 * i / rate) + 0.3f * noise;
                r[(size_t) i] = 0.5f * (float) std::sin(2 * pi * 1300.0 * i / rate) + 0.3f * noise;
            }
            BusCompressor c;
            Params p;
            p.thresholdDb = -40.0f; p.ratio = 20.0f; p.attackMs = 0.1f; p.releaseMs = 20.0f; p.kneeDb = 12.0f;
            p.makeupDb = 24.0f; p.scHpHz = 400.0f; p.rms = true;
            c.setParams(p);
            c.prepare(rate);
            bool finite = true; float peak = 0.0f;
            for (int i = 0; i < m; ++i)
            {
                if (i % 5000 == 0)
                {
                    p.autoRelease = (i / 5000) % 2 == 0; p.rms = !p.rms; p.thresholdDb = -40.0f + (float) ((i / 5000) % 40); p.link = (float) ((i / 5000) % 11) * 10.0f;
                    c.setParams(p);
                }
                c.process(&l[(size_t) i], &r[(size_t) i], 1);
                if (!std::isfinite(l[(size_t) i]) || !std::isfinite(r[(size_t) i])) finite = false;
                peak = std::max({ peak, std::abs(l[(size_t) i]), std::abs(r[(size_t) i]) });
            }
            check(finite && peak < 60.0f, "Parámetros extremos y cambiando a " + num(rate, 0) + " Hz: salida finita", "pico " + num(peak, 2));
        }
        sr = 48000.0;

        std::vector<float> zero((size_t) 48000, 0.0f);
        Params p;
        auto o = run(zero, zero, p);
        bool clean = true;
        for (float v : o.l) if (v != 0.0f) clean = false;
        check(clean, "Silencio entra, silencio sale");

        // mono
        BusCompressor c;
        c.setParams(Params {});
        c.prepare(sr);
        auto in = tone(ms(500.0), 1000.0, -6.0f);
        for (size_t i = 0; i < in.size(); ++i) c.process(&in[i], nullptr, 1);
        check(c.currentGr() > 3.0f, "Mono: comprime con un solo canal", num(c.currentGr(), 1) + " dB");

        const int m = (int) (sr * 20.0);
        std::vector<float> l((size_t) m), r((size_t) m);
        for (int i = 0; i < m; ++i) { l[(size_t) i] = 0.4f * (float) std::sin(2 * pi * 220.0 * i / sr); r[(size_t) i] = l[(size_t) i]; }
        BusCompressor c2;
        c2.setParams(Params {});
        c2.prepare(sr);
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t s = 0; s < (size_t) m; s += 480) c2.process(l.data() + s, r.data() + s, 480);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(20.0 / secs > 100.0, "Velocidad: más de 100 veces tiempo real", num(20.0 / secs, 0) + "x");
    }

    std::cout << (failures == 0 ? "\nTODO OK" : "\nHAY FALLAS: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
