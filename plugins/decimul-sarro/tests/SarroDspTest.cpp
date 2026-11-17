// Test del motor de Sarro (supresor de resonancias), solo biblioteca estándar, sin JUCE.
// Se compila suelto con tests/build_dsp.bat o con cmake -DDECIMU_SARRO_TESTS=ON (objetivo SarroDspTest).
// Todo debe decir "TODO OK".

#include "SarroDsp.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <xmmintrin.h>

using namespace sarro;

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

    struct Rng
    {
        uint32_t s = 12345;
        float next() { s = s * 1664525u + 1013904223u; return (float) (int32_t) s * (1.0f / 2147483648.0f); }
    };

    std::vector<float> noise(int n, float amp, uint32_t seed = 1)
    {
        Rng r; r.s = seed;
        std::vector<float> v((size_t) n);
        for (auto& x : v) x = amp * r.next();
        return v;
    }

    std::vector<float> tone(int n, double f, float db)
    {
        std::vector<float> v((size_t) n);
        const float a = std::pow(10.0f, db / 20.0f);
        for (int i = 0; i < n; ++i) v[(size_t) i] = a * (float) std::sin(2.0 * pi * f * i / sr);
        return v;
    }

    std::vector<float> sum(const std::vector<float>& a, const std::vector<float>& b)
    {
        std::vector<float> o(a.size());
        for (size_t i = 0; i < a.size(); ++i) o[i] = a[i] + b[i];
        return o;
    }

    // Amplitud de la componente de frecuencia f entre [from, to) (Goertzel).
    double toneAmp(const std::vector<float>& v, double f, int from, int to)
    {
        double re = 0.0, im = 0.0;
        for (int i = from; i < to; ++i)
        {
            const double w = 2.0 * pi * f * i / sr;
            re += v[(size_t) i] * std::cos(w);
            im += v[(size_t) i] * std::sin(w);
        }
        return 2.0 * std::sqrt(re * re + im * im) / (double) (to - from);
    }

    // Energía en una banda (suma de Goertzel en varias frecuencias).
    double bandRms(const std::vector<float>& v, double f0, double f1, int from, int to)
    {
        double s = 0.0; int cnt = 0;
        for (double f = f0; f <= f1; f += 37.0, ++cnt)
        {
            const double a = toneAmp(v, f, from, to);
            s += a * a;
        }
        return std::sqrt(s / std::max(cnt, 1));
    }

    std::vector<float> run(const std::vector<float>& l, const std::vector<float>& r, const Params& p, std::vector<float>* outR = nullptr, int block = 480)
    {
        ResonanceSuppressor e;
        e.prepare(sr);
        e.setParams(p);
        auto ol = l, orr = r;
        for (size_t s = 0; s < ol.size(); s += (size_t) block)
        {
            const int n = (int) std::min<size_t>((size_t) block, ol.size() - s);
            e.process(ol.data() + s, orr.data() + s, n);
        }
        if (outR) *outR = orr;
        return ol;
    }
}

int main()
{
    _mm_setcsr(_mm_getcsr() | 0x8040);   // como ScopedNoDenormals en el plugin

    ResonanceSuppressor probe;
    probe.prepare(sr);
    const int lat = probe.latencySamples();
    const int n = ms(4000.0);
    const int from = ms(1500.0), to = ms(3900.0);      // ventana asentada

    // ---------- Reconstrucción y latencia ----------
    {
        auto in = noise(n, 0.3f, 7);
        Params p; p.depth = 0.0f;
        auto o = run(in, in, p);
        double maxErr = 0.0;
        for (int i = lat + 100; i < n; ++i) maxErr = std::max(maxErr, (double) std::abs(o[(size_t) i] - in[(size_t) (i - lat)]));
        check(maxErr < 2.0e-4, "Con DEPTH 0 la salida es la entrada retrasada " + std::to_string(lat) + " muestras", num(maxErr, 7));
        std::vector<float> imp((size_t) 12000, 0.0f);
        imp[3000] = 1.0f;
        auto oi = run(imp, imp, p);
        int peak = 0; float pv = 0.0f;
        for (int i = 0; i < (int) oi.size(); ++i) if (std::abs(oi[(size_t) i]) > pv) { pv = std::abs(oi[(size_t) i]); peak = i; }
        check(peak == 3000 + lat && pv > 0.99f, "El impulso sale justo en la latencia informada", "muestra " + std::to_string(peak) + ", amplitud " + num(pv, 3));
    }

    // ---------- Un pico estrecho se baja, el resto no ----------
    {
        auto bed = noise(n, 0.08f, 3);
        auto in = sum(bed, tone(n, 3000.0, -16.0f));
        Params p; p.depth = 100.0f; p.sharpness = 60.0f; p.sensitivity = 70.0f; p.attackMs = 5.0f; p.lowHz = 50.0f; p.highHz = 18000.0f;
        auto o = run(in, in, p);
        const double d = dbOf(toneAmp(o, 3000.0, from + lat, to)) - dbOf(toneAmp(in, 3000.0, from, to - lat));
        check(d < -9.0, "Pico de 3 kHz: baja más de 9 dB", num(d, 1) + " dB");
        const double dBand = dbOf(bandRms(o, 700.0, 1500.0, from + lat, to)) - dbOf(bandRms(in, 700.0, 1500.0, from, to - lat));
        check(std::abs(dBand) < 1.5, "El ruido de alrededor casi no se toca", num(dBand, 2) + " dB");
    }

    // ---------- Ruido solo: casi no actúa ----------
    {
        auto in = noise(n, 0.2f, 9);
        Params p; p.depth = 60.0f; p.sharpness = 50.0f; p.sensitivity = 30.0f;
        auto o = run(in, in, p);
        double e1 = 0, e2 = 0;
        for (int i = from; i < to - lat; ++i) { e1 += (double) in[(size_t) i] * in[(size_t) i]; e2 += (double) o[(size_t) (i + lat)] * o[(size_t) (i + lat)]; }
        const double d = 10.0 * std::log10(e2 / e1);
        check(d > -1.5, "Ruido blanco sin picos: pierde menos de 1.5 dB con sensibilidad baja", num(d, 2) + " dB");
    }

    // ---------- DEPTH escala la reducción ----------
    {
        auto in = sum(noise(n, 0.08f, 5), tone(n, 3000.0, -14.0f));
        Params p; p.sharpness = 60.0f; p.sensitivity = 60.0f; p.attackMs = 5.0f;
        p.depth = 100.0f;
        auto o100 = run(in, in, p);
        p.depth = 50.0f;
        auto o50 = run(in, in, p);
        p.depth = 0.0f;
        auto o0 = run(in, in, p);
        const double r100 = dbOf(toneAmp(o0, 3000.0, from + lat, to)) - dbOf(toneAmp(o100, 3000.0, from + lat, to));
        const double r50 = dbOf(toneAmp(o0, 3000.0, from + lat, to)) - dbOf(toneAmp(o50, 3000.0, from + lat, to));
        check(r100 > 8.0 && r50 > 3.0 && r50 < 0.75 * r100, "DEPTH 50 baja bastante menos que DEPTH 100", num(r50, 1) + " dB contra " + num(r100, 1) + " dB");
    }

    // ---------- Zona de trabajo ----------
    {
        auto in = sum(noise(n, 0.08f, 11), sum(tone(n, 300.0, -14.0f), tone(n, 5000.0, -14.0f)));
        Params p; p.depth = 100.0f; p.sharpness = 60.0f; p.sensitivity = 70.0f; p.attackMs = 5.0f;
        p.lowHz = 2000.0f; p.highHz = 18000.0f;
        auto o = run(in, in, p);
        const double d300 = dbOf(toneAmp(o, 300.0, from + lat, to)) - dbOf(toneAmp(in, 300.0, from, to - lat));
        const double d5k = dbOf(toneAmp(o, 5000.0, from + lat, to)) - dbOf(toneAmp(in, 5000.0, from, to - lat));
        check(d300 > -1.0 && d5k < -8.0, "LOW FOCUS 2 kHz: el pico de 300 Hz queda, el de 5 kHz se baja", num(d300, 1) + " dB / " + num(d5k, 1) + " dB");
        p.lowHz = 20.0f; p.highHz = 1000.0f;
        auto o2 = run(in, in, p);
        const double e5k = dbOf(toneAmp(o2, 5000.0, from + lat, to)) - dbOf(toneAmp(in, 5000.0, from, to - lat));
        check(e5k > -1.0, "HIGH FOCUS 1 kHz: el pico de 5 kHz queda", num(e5k, 1) + " dB");
    }

    // ---------- Nitidez: pico angosto contra grupo ancho ----------
    {
        // un grupo de 7 tonos separados 100 Hz (un bulto de unos 600 Hz de ancho) y un pico aislado de 7 kHz
        auto in = noise(n, 0.05f, 13);
        for (int k = 0; k < 7; ++k)
            in = sum(in, tone(n, 2700.0 + 100.0 * k, -26.0f));
        in = sum(in, tone(n, 7000.0, -14.0f));
        Params p; p.depth = 100.0f; p.sensitivity = 60.0f; p.attackMs = 5.0f;
        p.sharpness = 95.0f;
        auto sharp = run(in, in, p);
        p.sharpness = 5.0f;
        auto soft = run(in, in, p);
        const double bumpSharp = dbOf(toneAmp(sharp, 3000.0, from + lat, to)) - dbOf(toneAmp(in, 3000.0, from, to - lat));
        const double bumpSoft = dbOf(toneAmp(soft, 3000.0, from + lat, to)) - dbOf(toneAmp(in, 3000.0, from, to - lat));
        const double peakSharp = dbOf(toneAmp(sharp, 7000.0, from + lat, to)) - dbOf(toneAmp(in, 7000.0, from, to - lat));
        check(bumpSharp > bumpSoft + 2.0 && peakSharp < -6.0, "SHARPNESS alto trata el pico aislado y deja más el grupo ancho",
              "grupo " + num(bumpSharp, 1) + " dB (agudo) contra " + num(bumpSoft, 1) + " dB (suave); pico " + num(peakSharp, 1) + " dB");
    }

    // ---------- Ataque y caída ----------
    {
        // un tono de 3 kHz que aparece a los 1.5 s y se va a los 2.5 s sobre un colchón de ruido
        auto in = noise(n, 0.08f, 21);
        auto t = tone(n, 3000.0, -14.0f);
        for (int i = ms(1500.0); i < ms(2500.0); ++i) in[(size_t) i] += t[(size_t) i];
        auto timeToRecover = [&](float releaseMs)
        {
            Params p; p.depth = 100.0f; p.sharpness = 60.0f; p.sensitivity = 70.0f; p.attackMs = 5.0f; p.releaseMs = releaseMs;
            ResonanceSuppressor e; e.prepare(sr); e.setParams(p);
            auto l = in, r = in;
            float peakRed = 0.0f;
            std::vector<float> red;
            for (size_t s = 0; s < l.size(); s += 128)
            {
                e.process(l.data() + s, r.data() + s, 128);
                red.push_back(e.lastMaxReduction());
                if ((int) s < ms(2500.0)) peakRed = std::max(peakRed, e.lastMaxReduction());
            }
            for (size_t k = (size_t) ms(2500.0) / 128; k < red.size(); ++k)
                if (red[k] < 0.5f * peakRed) return (double) (k * 128 - (size_t) ms(2500.0)) / sr * 1000.0;
            return 9999.0;
        };
        const double fast = timeToRecover(60.0f), slow = timeToRecover(800.0f);
        check(slow > 2.5 * fast && slow > 200.0, "RELEASE largo suelta mucho más lento que RELEASE corto", num(fast, 0) + " ms contra " + num(slow, 0) + " ms");
    }

    // ---------- Mix, delta y bypass ----------
    {
        auto in = sum(noise(n, 0.08f, 23), tone(n, 3000.0, -14.0f));
        Params p; p.depth = 100.0f; p.sharpness = 60.0f; p.sensitivity = 70.0f; p.attackMs = 5.0f;
        auto wet = run(in, in, p);
        p.mix = 0.0f;
        auto dry = run(in, in, p);
        double maxDiff = 0.0;
        for (int i = ms(500.0); i < n; ++i) maxDiff = std::max(maxDiff, (double) std::abs(dry[(size_t) i] - in[(size_t) (i - lat)]));
        check(maxDiff < 1.0e-5, "MIX 0: sale la original con el mismo retardo", num(maxDiff, 8));
        p.mix = 100.0f; p.delta = true;
        auto delta = run(in, in, p);
        double maxErr = 0.0;
        for (int i = ms(500.0); i < n; ++i) maxErr = std::max(maxErr, (double) std::abs(delta[(size_t) i] + wet[(size_t) i] - in[(size_t) (i - lat)]));
        check(maxErr < 1.0e-5, "DELTA + lo que queda = la entrada", num(maxErr, 8));
        p.delta = false; p.bypass = true;
        auto byp = run(in, in, p);
        maxDiff = 0.0;
        for (int i = ms(500.0); i < n; ++i) maxDiff = std::max(maxDiff, (double) std::abs(byp[(size_t) i] - in[(size_t) (i - lat)]));
        check(maxDiff < 1.0e-4, "BYPASS: sale la original con el mismo retardo", num(maxDiff, 8));
        p.bypass = false; p.trimDb = -6.0f;
        auto trim = run(in, in, p);
        const double dt = dbOf(toneAmp(trim, 3000.0, from + lat, to)) - dbOf(toneAmp(wet, 3000.0, from + lat, to));
        check(std::abs(dt + 6.0) < 0.2, "TRIM -6 dB baja la salida 6 dB", num(dt, 2) + " dB");
    }

    // ---------- Enlace estéreo ----------
    {
        auto bedL = noise(n, 0.08f, 31), bedR = noise(n, 0.08f, 32);
        auto inL = sum(bedL, tone(n, 3000.0, -14.0f));                // el pico solo en L
        Params p; p.depth = 100.0f; p.sharpness = 60.0f; p.sensitivity = 70.0f; p.attackMs = 5.0f;
        // R lleva un tono más tranquilo en 3 kHz, que solo se baja si hay enlace
        auto inR = sum(bedR, tone(n, 3000.0, -40.0f));
        std::vector<float> outR;
        p.link = 100.0f;
        run(inL, inR, p, &outR);
        const double linked = dbOf(toneAmp(outR, 3000.0, from + lat, to)) - dbOf(toneAmp(inR, 3000.0, from, to - lat));
        p.link = 0.0f;
        run(inL, inR, p, &outR);
        const double indep = dbOf(toneAmp(outR, 3000.0, from + lat, to)) - dbOf(toneAmp(inR, 3000.0, from, to - lat));
        check(linked < indep - 3.0, "Enlace 100 %: el canal R baja también donde L tiene el pico", num(linked, 1) + " dB contra " + num(indep, 1) + " dB");
    }

    // ---------- Pantalla ----------
    {
        auto in = sum(noise(n, 0.05f, 41), tone(n, 3000.0, -12.0f));
        Params p; p.depth = 100.0f; p.sharpness = 60.0f; p.sensitivity = 70.0f; p.attackMs = 5.0f;
        ResonanceSuppressor e; e.prepare(sr); e.setParams(p);
        auto l = in, r = in;
        for (size_t s = 0; s < l.size(); s += 480) e.process(l.data() + s, r.data() + s, 480);
        std::vector<float> spec(ResonanceSuppressor::kDisplay), red(ResonanceSuppressor::kDisplay);
        e.getDisplay(spec.data(), red.data());
        int sp = 0, rp = 0;
        for (int d = 0; d < ResonanceSuppressor::kDisplay; ++d)
        {
            if (spec[(size_t) d] > spec[(size_t) sp]) sp = d;
            if (red[(size_t) d] > red[(size_t) rp]) rp = d;
        }
        const double fSpec = 20.0 * std::pow(1000.0, (sp + 0.5) / ResonanceSuppressor::kDisplay);
        const double fRed = 20.0 * std::pow(1000.0, (rp + 0.5) / ResonanceSuppressor::kDisplay);
        check(std::abs(std::log2(fSpec / 3000.0)) < 0.2 && std::abs(std::log2(fRed / 3000.0)) < 0.3 && red[(size_t) rp] > 6.0f,
              "La pantalla marca el pico en 3 kHz y la reducción ahí", num(fSpec, 0) + " Hz, reducción en " + num(fRed, 0) + " Hz (" + num(red[(size_t) rp], 1) + " dB)");
        check(spec[(size_t) sp] > -30.0f && spec[(size_t) sp] < 0.0f, "El espectro está calibrado en dBFS", num(spec[(size_t) sp], 1) + " dB");
    }

    // ---------- Robustez ----------
    {
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            sr = rate;
            const int m = (int) (rate * 5.0);
            std::vector<float> l = noise(m, 0.5f, 51), r = noise(m, 0.5f, 52);
            for (int i = 0; i < m; ++i) l[(size_t) i] += 0.4f * (float) std::sin(2 * pi * 2500.0 * i / rate);
            ResonanceSuppressor e;
            e.prepare(rate);
            Params p; p.depth = 100.0f; p.sharpness = 100.0f; p.sensitivity = 100.0f; p.attackMs = 1.0f; p.releaseMs = 20.0f; p.lowHz = 20.0f; p.highHz = 20000.0f;
            bool finite = true; float peak = 0.0f;
            for (int i = 0; i < m; i += 441)
            {
                p.delta = (i / 441) % 7 == 0; p.mix = (float) ((i / 441) % 11) * 10.0f; p.link = (float) ((i / 441) % 5) * 25.0f;
                p.depth = 20.0f + (float) ((i / 441) % 9) * 10.0f; p.bypass = (i / 441) % 13 == 0;
                e.setParams(p);
                const int len = std::min(441, m - i);
                e.process(l.data() + i, r.data() + i, len);
                for (int j = 0; j < len; ++j)
                {
                    const float a = l[(size_t) (i + j)], b = r[(size_t) (i + j)];
                    if (!std::isfinite(a) || !std::isfinite(b)) finite = false;
                    peak = std::max({ peak, std::abs(a), std::abs(b) });
                }
            }
            check(finite && peak < 4.0f, "Todo al máximo y cambiando a " + num(rate, 0) + " Hz: salida finita y acotada", "pico " + num(peak, 2));
        }
        sr = 48000.0;

        std::vector<float> zero((size_t) 48000, 0.0f);
        auto o = run(zero, zero, Params {});
        bool clean = true;
        for (float v : o) if (std::abs(v) > 1.0e-9f) clean = false;
        check(clean, "Silencio entra, silencio sale");

        // mono
        ResonanceSuppressor e;
        e.prepare(sr);
        auto in = sum(noise(n, 0.08f, 61), tone(n, 3000.0, -14.0f));
        Params p; p.depth = 100.0f; p.sharpness = 60.0f; p.sensitivity = 70.0f; p.attackMs = 5.0f;
        e.setParams(p);
        for (size_t s = 0; s < in.size(); s += 480) e.process(in.data() + s, nullptr, (int) std::min<size_t>(480, in.size() - s));
        check(e.lastMaxReduction() > 5.0f, "Mono: actúa con un solo canal", num(e.lastMaxReduction(), 1) + " dB");

        const int m = (int) (sr * 20.0);
        std::vector<float> l = noise(m, 0.3f, 71), r = noise(m, 0.3f, 72);
        ResonanceSuppressor e2;
        e2.prepare(sr);
        e2.setParams(Params {});
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t s = 0; s < (size_t) m; s += 480) e2.process(l.data() + s, r.data() + s, 480);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(20.0 / secs > 15.0, "Velocidad: más de 15 veces tiempo real", num(20.0 / secs, 0) + "x");
    }

    std::cout << (failures == 0 ? "\nTODO OK" : "\nHAY FALLAS: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
