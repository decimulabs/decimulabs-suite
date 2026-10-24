// Test del motor de Scour (solo biblioteca estándar, sin JUCE): se compila suelto con
//   cl /std:c++20 /O2 /EHsc /utf-8 /I ..\..\..\shared\Source /I ..\Source ScourDspTest.cpp
// o con cmake -DDECIMU_SCOUR_TESTS=ON (objetivo ScourDspTest). Todo debe decir "TODO OK".
// Con `--noise` imprime el RMS de cada tipo de ruido (para calibrar kNorm).

#include "ScourDsp.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <xmmintrin.h>

using namespace scour;

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

    // Un motor con todo apagado/neutro: se prende lo que cada test necesita.
    Params neutral()
    {
        Params p;
        p.noiseOn = p.wobOn = p.disOn = p.digOn = p.spaceOn = p.magOn = true;
        p.noiseAmt = p.wobAmt = p.disAmt = p.digAmt = p.spaceAmt = p.magAmt = 0.0f;
        p.eqOn = false;
        return p;
    }

    struct Out { std::vector<float> l, r; };

    Out run(const std::vector<float>& inL, const std::vector<float>& inR, const Params& p, int block = 480)
    {
        ScourEngine e;
        e.prepare(sr);
        e.setParams(p);
        Out o { inL, inR };
        for (size_t s = 0; s < o.l.size(); s += (size_t) block)
        {
            const int n = (int) std::min<size_t>((size_t) block, o.l.size() - s);
            e.process(o.l.data() + s, o.r.data() + s, n);
        }
        return o;
    }

    std::vector<float> tone(int n, double f, float db)
    {
        std::vector<float> v((size_t) n);
        const float a = std::pow(10.0f, db / 20.0f);
        for (int i = 0; i < n; ++i) v[(size_t) i] = a * (float) std::sin(2.0 * pi * f * i / sr);
        return v;
    }

    double rms(const std::vector<float>& v, int from, int to)
    {
        double s = 0.0;
        for (int i = from; i < to; ++i) s += (double) v[(size_t) i] * v[(size_t) i];
        return std::sqrt(s / (double) std::max(1, to - from));
    }

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

    bool allFinite(const Out& o, float limit = 30.0f)
    {
        for (size_t i = 0; i < o.l.size(); ++i)
            if (!std::isfinite(o.l[i]) || !std::isfinite(o.r[i]) || std::abs(o.l[i]) > limit || std::abs(o.r[i]) > limit)
                return false;
        return true;
    }
}

int main(int argc, char** argv)
{
    _mm_setcsr(_mm_getcsr() | 0x8040);   // como ScopedNoDenormals en el plugin
    if (argc >= 2 && std::strcmp(argv[1], "--noise") == 0)
    {
        const char* names[] = { "vinilo", "cinta", "zumbido", "estatica", "vhs", "polvo" };
        for (int t = 0; t < NoiseGen::kTypes; ++t)
        {
            NoiseGen g;
            g.prepare(sr);
            g.setTone(0.6f);
            double s = 0.0, peak = 0.0;
            const int n = (int) (20 * sr);
            for (int i = 0; i < n; ++i)
            {
                float l, r;
                g.next(t, l, r);
                s += (double) l * l;
                peak = std::max(peak, (double) std::abs(l));
            }
            std::cout << names[t] << ": rms " << num(std::sqrt(s / n), 4) << "  pico " << num(peak, 3) << std::endl;
        }
        return 0;
    }

    if (argc >= 2 && std::strcmp(argv[1], "--bench") == 0)
    {
        const char* names[] = { "ruido", "wobble", "distort", "digital", "space", "magnetic", "todo apagado" };
        for (int k = 0; k < 7; ++k)
        {
            Params p = neutral();
            float* amts[] = { &p.noiseAmt, &p.wobAmt, &p.disAmt, &p.digAmt, &p.spaceAmt, &p.magAmt };
            if (k < 6) *amts[k] = 60.0f;
            const int m = (int) (sr * 20.0);
            std::vector<float> l((size_t) m), r((size_t) m);
            for (int i = 0; i < m; ++i) { l[(size_t) i] = 0.2f * (float) std::sin(2 * pi * 220.0 * i / sr); r[(size_t) i] = l[(size_t) i]; }
            ScourEngine e;
            e.prepare(sr);
            e.setParams(p);
            const auto t0 = std::chrono::steady_clock::now();
            for (size_t s = 0; s < (size_t) m; s += 480) e.process(l.data() + s, r.data() + s, 480);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::cout << names[k] << ": " << num(20.0 / secs, 0) << "x tiempo real" << std::endl;
        }
        return 0;
    }

    const int n = ms(1500.0);
    const std::vector<float> zeros((size_t) n, 0.0f);

    // ---------- Neutro ----------
    {
        auto in = tone(n, 440.0, -12.0f);
        auto o = run(in, in, neutral());
        double maxDiff = 0.0;
        for (size_t i = 0; i < in.size(); ++i) maxDiff = std::max(maxDiff, (double) std::abs(o.l[i] - in[i]));
        check(maxDiff < 2.0e-6, "Con las cantidades en 0 la señal pasa intacta", num(maxDiff, 9));
    }
    {
        auto in = tone(n, 440.0, -12.0f);
        Params p = neutral();
        p.inGainDb = 6.0f; p.outGainDb = -6.0f;
        auto o = run(in, in, p);
        const double d = dbOf(toneAmp(o.l, 440.0, ms(500.0), n)) - dbOf(toneAmp(in, 440.0, ms(500.0), n));
        check(std::abs(d) < 0.05, "Ganancias de entrada y salida opuestas se cancelan", num(d, 3) + " dB");
    }

    // ---------- Ruido ----------
    {
        const char* names[] = { "vinilo", "cinta", "zumbido", "estática", "VHS", "polvo" };
        for (int t = 0; t < NoiseGen::kTypes; ++t)
        {
            Params p = neutral();
            p.noiseAmt = 100.0f; p.noiseType = t; p.noiseTone = 60.0f;
            auto o = run(zeros, zeros, p);
            const double level = dbOf(rms(o.l, ms(300.0), n));
            double pk = 0.0;
            for (float v : o.l) pk = std::max(pk, (double) std::abs(v));
            const double target = NoiseGen::kTarget[t];
            check(std::abs(level - target) < 2.5 && pk < 0.9, std::string("Ruido ") + names[t] + " a 100 %: nivel previsto y sin pasarse de 0 dBFS",
                  num(level, 1) + " dBFS (previsto " + num(target, 0) + "), pico " + num(dbOf(pk), 1) + " dBFS");
        }
        Params p = neutral();
        p.noiseAmt = 100.0f; p.noiseType = 1;
        auto high = run(zeros, zeros, p);
        p.noiseAmt = 50.0f;
        auto mid = run(zeros, zeros, p);
        const double d = dbOf(rms(high.l, ms(300.0), n)) - dbOf(rms(mid.l, ms(300.0), n));
        check(d > 20.0 && d < 30.0, "El ruido sube unos 25 dB entre 50 % y 100 %", num(d, 1) + " dB");
        p.noiseAmt = 0.0f;
        auto off = run(zeros, zeros, p);
        check(rms(off.l, 0, n) == 0.0, "Ruido con cantidad 0: silencio");
        p.noiseAmt = 100.0f; p.noiseTone = 0.0f;
        auto dark = run(zeros, zeros, p);
        p.noiseTone = 100.0f;
        auto bright = run(zeros, zeros, p);
        auto hf = [&](const Out& o) { double s = 0; for (int i = ms(300.0) + 1; i < n; ++i) { double d2 = o.l[(size_t) i] - o.l[(size_t) i - 1]; s += d2 * d2; } return s; };
        check(hf(bright) > 6.0 * hf(dark), "TONE: más brillante tiene más agudos que oscuro");
        // L y R distintos (ruido estéreo), salvo el zumbido
        p.noiseType = 1; p.noiseTone = 60.0f;
        auto st = run(zeros, zeros, p);
        double dif = 0.0;
        for (int i = ms(300.0); i < n; ++i) dif += std::abs(st.l[(size_t) i] - st.r[(size_t) i]);
        check(dif > 0.1, "El ruido de cinta es distinto en L y en R");
        // FOLLOW: sin señal, el ruido casi desaparece; con señal sube
        p.noiseType = 1; p.noiseFollow = 100.0f;
        auto silentF = run(zeros, zeros, p);
        auto loud = tone(n, 300.0, -6.0f);
        auto withSig = run(loud, loud, p);
        std::vector<float> noiseOnly((size_t) n);
        for (int i = 0; i < n; ++i) noiseOnly[(size_t) i] = withSig.l[(size_t) i] - loud[(size_t) i];
        check(dbOf(rms(silentF.l, ms(300.0), n)) < -90.0 && dbOf(rms(noiseOnly, ms(600.0), n)) > -40.0,
              "FOLLOW: el ruido solo aparece cuando hay señal");
        // DUCK: con señal el ruido baja
        p.noiseFollow = 0.0f; p.noiseDuck = 100.0f;
        auto ducked = run(loud, loud, p);
        for (int i = 0; i < n; ++i) noiseOnly[(size_t) i] = ducked.l[(size_t) i] - loud[(size_t) i];
        auto silentD = run(zeros, zeros, p);
        check(dbOf(rms(noiseOnly, ms(600.0), n)) < dbOf(rms(silentD.l, ms(600.0), n)) - 12.0, "DUCK: con señal el ruido baja");
        // PRE y POST: con distorsión fuerte el ruido PRE sale distinto de POST
        p = neutral(); p.noiseAmt = 100.0f; p.noiseType = 1; p.disAmt = 100.0f;
        auto pre = run(loud, loud, p);
        p.noisePost = true;
        auto post = run(loud, loud, p);
        double dd = 0.0;
        for (int i = ms(300.0); i < n; ++i) dd += std::abs(pre.l[(size_t) i] - post.l[(size_t) i]);
        check(dd > 1.0, "ROUTING: ruido antes y después de la cadena no suena igual");
    }

    // ---------- Distorsión ----------
    {
        const char* names[] = { "tubo", "cinta", "diodo", "pliegue" };
        for (int t = 0; t < Distortion::kTypes; ++t)
        {
            auto in = tone(n, 1000.0, -12.0f);
            Params p = neutral();
            p.disAmt = 100.0f; p.disType = t;
            auto o = run(in, in, p);
            const double fund = toneAmp(o.l, 1000.0, ms(500.0), n);
            const double e = rms(o.l, ms(500.0), n) * std::sqrt(2.0);
            const double thd = std::sqrt(std::max(0.0, e * e - fund * fund)) / std::max(fund, 1.0e-9);
            const double dlevel = dbOf(rms(o.l, ms(500.0), n)) - dbOf(rms(in, ms(500.0), n));
            check(thd > 0.15 && dlevel > -9.0 && dlevel < 9.0, std::string("Distorsión ") + names[t] + ": agrega armónicos y mantiene el nivel",
                  "THD " + num(thd * 100, 0) + " %, nivel " + num(dlevel, 1) + " dB");
        }
        // foco: con foco 2 kHz, un tono de 200 Hz casi no se distorsiona
        auto in = tone(n, 200.0, -12.0f);
        Params p = neutral(); p.disAmt = 100.0f; p.disFocus = 3000.0f;
        auto o = run(in, in, p);
        double maxDiff = 0.0;
        for (int i = ms(500.0); i < n; ++i) maxDiff = std::max(maxDiff, (double) std::abs(o.l[(size_t) i] - in[(size_t) i]));
        p.disFocus = 20.0f;
        auto oFull = run(in, in, p);
        double fullDiff = 0.0;
        for (int i = ms(500.0); i < n; ++i) fullDiff = std::max(fullDiff, (double) std::abs(oFull.l[(size_t) i] - in[(size_t) i]));
        check(maxDiff < 0.45 * fullDiff, "FOCUS: lo grave por debajo del foco se distorsiona mucho menos", num(maxDiff, 4) + " contra " + num(fullDiff, 4));
        // mix 0
        p = neutral(); p.disAmt = 100.0f; p.disMix = 0.0f;
        auto o2 = run(in, in, p);
        maxDiff = 0.0;
        for (size_t i = 0; i < in.size(); ++i) maxDiff = std::max(maxDiff, (double) std::abs(o2.l[i] - in[i]));
        check(maxDiff < 2.0e-6, "Distort con MIX 0: no cambia nada");
        // sin DC
        auto q = tone(n, 300.0, -6.0f);
        p = neutral(); p.disAmt = 80.0f; p.disType = 0;
        auto o3 = run(q, q, p);
        double mean = 0.0;
        for (int i = ms(800.0); i < n; ++i) mean += o3.l[(size_t) i];
        mean /= (n - ms(800.0));
        check(std::abs(mean) < 0.01, "Distort asimétrico: sin DC", num(mean, 4));
    }

    // ---------- Digital ----------
    {
        auto in = tone(n, 10000.0, -12.0f);
        Params p = neutral(); p.digAmt = 100.0f; p.digBalance = 0.0f; p.digSmooth = 0.0f;
        auto o = run(in, in, p);
        const double d = dbOf(toneAmp(o.l, 10000.0, ms(300.0), n)) - dbOf(toneAmp(in, 10000.0, ms(300.0), n));
        check(d < -15.0, "Digital, solo frecuencia al 100 %: el tono de 10 kHz se pierde", num(d, 1) + " dB");

        auto in2 = tone(n, 300.0, -6.0f);
        p.digBalance = 100.0f;
        auto o2 = run(in2, in2, p);
        std::vector<float> err((size_t) n);
        for (int i = 0; i < n; ++i) err[(size_t) i] = o2.l[(size_t) i] - in2[(size_t) i];
        const double snr = dbOf(rms(in2, ms(300.0), n)) - dbOf(rms(err, ms(300.0), n));
        check(snr < 30.0 && snr > 5.0, "Digital, solo bits al 100 % (4 bits): ruido de cuantización marcado", "SNR " + num(snr, 1) + " dB");

        // bits al 0 %: casi transparente
        p.digAmt = 5.0f;
        auto o3 = run(in2, in2, p);
        for (int i = 0; i < n; ++i) err[(size_t) i] = o3.l[(size_t) i] - in2[(size_t) i];
        check(dbOf(rms(in2, ms(300.0), n)) - dbOf(rms(err, ms(300.0), n)) > 40.0, "Digital al 5 %: casi transparente");

        // SMOOTH apaga agudos
        p = neutral(); p.digAmt = 100.0f; p.digBalance = 50.0f; p.digSmooth = 100.0f;
        std::vector<float> noise((size_t) n);
        Rng r(5);
        for (auto& v : noise) v = 0.2f * r.next();
        auto o4 = run(noise, noise, p);
        p.digSmooth = 0.0f;
        auto o5 = run(noise, noise, p);
        auto hf = [&](const Out& o) { double s = 0; for (int i = ms(300.0) + 1; i < n; ++i) { double d2 = o.l[(size_t) i] - o.l[(size_t) i - 1]; s += d2 * d2; } return s; };
        check(hf(o4) < 0.5 * hf(o5), "SMOOTH: suaviza los agudos del bitcrusher");
    }

    // ---------- Wobble ----------
    {
        // tono de 1 kHz con wow al 100 %: la desviación de tono tiene que andar cerca de +/-45 centésimos
        const int m = ms(8000.0);
        auto in = tone(m, 1000.0, -12.0f);
        Params p = neutral(); p.wobAmt = 100.0f; p.wobBalance = 0.0f; p.wowRate = 1.0f; p.wobStereo = 0.0f; p.wobMix = 100.0f;
        auto o = run(in, in, p);
        // frecuencia instantánea por cruces por cero ascendentes
        std::vector<double> t;
        for (int i = ms(1000.0) + 1; i < m; ++i)
            if (o.l[(size_t) i - 1] < 0.0f && o.l[(size_t) i] >= 0.0f)
                t.push_back(i - 1 + (-o.l[(size_t) i - 1]) / (o.l[(size_t) i] - o.l[(size_t) i - 1]));
        double maxC = -1e9, minC = 1e9;
        for (size_t k = 8; k + 8 < t.size(); ++k)
        {
            const double f = 8.0 * sr / (t[k + 8] - t[k]);        // promedio de 8 ciclos
            const double cents = 1200.0 * std::log2(f / 1000.0);
            maxC = std::max(maxC, cents); minC = std::min(minC, cents);
        }
        check(maxC > 28.0 && maxC < 62.0 && minC < -28.0 && minC > -62.0, "Wobble, wow al 100 %: tono +/-45 centésimos",
              num(minC, 1) + " a " + num(maxC, 1) + " centésimos");

        p.wobAmt = 100.0f; p.wobBalance = 100.0f; p.flutRate = 10.0f;
        auto o2 = run(in, in, p);
        // el flutter es más pequeño: la modulación de la envolvente de fase es de ~+/-14 centésimos
        std::vector<double> t2;
        for (int i = ms(1000.0) + 1; i < m; ++i)
            if (o2.l[(size_t) i - 1] < 0.0f && o2.l[(size_t) i] >= 0.0f)
                t2.push_back(i - 1 + (-o2.l[(size_t) i - 1]) / (o2.l[(size_t) i] - o2.l[(size_t) i - 1]));
        double mx = -1e9, mn = 1e9;
        for (size_t k = 2; k + 2 < t2.size(); ++k)
        {
            const double f = 2.0 * sr / (t2[k + 2] - t2[k]);
            const double cents = 1200.0 * std::log2(f / 1000.0);
            mx = std::max(mx, cents); mn = std::min(mn, cents);
        }
        check(mx > 6.0 && mx < 30.0 && mn < -6.0 && mn > -30.0, "Wobble, flutter al 100 %: tono +/-14 centésimos",
              num(mn, 1) + " a " + num(mx, 1) + " centésimos");

        // stereo: con 0 los dos canales iguales; con 100 distintos
        p.wobBalance = 0.0f; p.wobStereo = 0.0f;
        auto o3 = run(in, in, p);
        double dif = 0.0;
        for (int i = ms(500.0); i < m; ++i) dif += std::abs(o3.l[(size_t) i] - o3.r[(size_t) i]);
        p.wobStereo = 100.0f;
        auto o4 = run(in, in, p);
        double dif2 = 0.0;
        for (int i = ms(500.0); i < m; ++i) dif2 += std::abs(o4.l[(size_t) i] - o4.r[(size_t) i]);
        check(dif < 1.0e-3 && dif2 > 100.0 * std::max(dif, 1.0e-3), "Wobble STEREO: 0 = canales iguales, 100 = distintos");

        // mix 0: pasa la señal
        p.wobMix = 0.0f;
        auto o5 = run(in, in, p);
        double maxDiff = 0.0;
        for (int i = 0; i < m; ++i) maxDiff = std::max(maxDiff, (double) std::abs(o5.l[(size_t) i] - in[(size_t) i]));
        check(maxDiff < 2.0e-6, "Wobble con MIX 0: no cambia nada");
        check(allFinite(o) && allFinite(o2), "Wobble: salida finita");
    }

    // ---------- Magnetic ----------
    {
        const int m = ms(6000.0);
        auto hi = tone(m, 10000.0, -12.0f), lo = tone(m, 100.0, -12.0f);
        Params p = neutral(); p.magAmt = 100.0f; p.magBalance = 0.0f; p.magDropouts = 0.0f; p.magStereo = 0.0f;
        auto oh = run(hi, hi, p);
        auto ol = run(lo, lo, p);
        const double dHi = dbOf(rms(oh.l, ms(1000.0), m)) - dbOf(rms(hi, ms(1000.0), m));
        const double dLo = dbOf(rms(ol.l, ms(1000.0), m)) - dbOf(rms(lo, ms(1000.0), m));
        check(dHi < -9.0 && std::abs(dLo) < 4.5, "Magnetic, wear al 100 %: se pierden los agudos, los graves se mantienen",
              "10 kHz " + num(dHi, 1) + " dB, 100 Hz " + num(dLo, 1) + " dB");

        // dropouts
        p = neutral(); p.magAmt = 100.0f; p.magBalance = 100.0f; p.magDropouts = 100.0f; p.magRate = 6.0f;
        // con flutter 100 % sin caídas el nivel igual oscila: medimos las caídas con wear 0 y flutter 0 (amount alto, balance 50 y dropouts)
        p.magBalance = 50.0f;
        auto mid = tone(m, 1000.0, -12.0f);
        auto od = run(mid, mid, p);
        double minLvl = 1e9;
        for (int i = ms(500.0); i + 480 < m; i += 480)
            minLvl = std::min(minLvl, dbOf(rms(od.l, i, i + 480)) - dbOf(rms(mid, i, i + 480)));
        p.magDropouts = 0.0f;
        auto od0 = run(mid, mid, p);
        double minLvl0 = 1e9;
        for (int i = ms(500.0); i + 480 < m; i += 480)
            minLvl0 = std::min(minLvl0, dbOf(rms(od0.l, i, i + 480)) - dbOf(rms(mid, i, i + 480)));
        check(minLvl < -8.0 && minLvl0 > -8.0, "Magnetic DROPOUTS: aparecen caídas de volumen solo si se piden",
              "con " + num(minLvl, 1) + " dB, sin " + num(minLvl0, 1) + " dB");

        // stereo
        p.magDropouts = 100.0f; p.magStereo = 0.0f;
        auto o1 = run(mid, mid, p);
        double dif = 0.0;
        for (int i = ms(500.0); i < m; ++i) dif += std::abs(o1.l[(size_t) i] - o1.r[(size_t) i]);
        p.magStereo = 100.0f;
        auto o2 = run(mid, mid, p);
        double dif2 = 0.0;
        for (int i = ms(500.0); i < m; ++i) dif2 += std::abs(o2.l[(size_t) i] - o2.r[(size_t) i]);
        check(dif2 > 5.0 * std::max(dif, 1.0e-3), "Magnetic STEREO: 100 = los canales se mueven distinto", num(dif, 3) + " vs " + num(dif2, 3));
        check(allFinite(od) && allFinite(o2), "Magnetic: salida finita");
    }

    // ---------- Space ----------
    {
        std::vector<float> imp((size_t) ms(4000.0), 0.0f);
        imp[(size_t) ms(500.0)] = 0.5f;
        Params p = neutral(); p.spaceAmt = 100.0f; p.spaceDecay = 30.0f; p.spacePre = 0.0f;
        auto o = run(imp, imp, p);
        const int tailFrom = ms(750.0), tailTo = ms(1100.0);
        const double tail = dbOf(rms(o.l, tailFrom, tailTo));
        check(tail > -80.0 && tail < -25.0, "Space: cola de reverb presente", num(tail, 1) + " dBFS");
        p.spaceDecay = 80.0f;
        auto o2 = run(imp, imp, p);
        const double lateShort = dbOf(rms(o.l, ms(2500.0), ms(3500.0)));
        const double lateLong = dbOf(rms(o2.l, ms(2500.0), ms(3500.0)));
        const double longTail = dbOf(rms(o2.l, ms(750.0), ms(1100.0)));
        check(lateLong > lateShort + 15.0 && longTail > -75.0, "Space DECAY: más decay = cola más larga", num(lateShort, 1) + " vs " + num(lateLong, 1) + " dBFS");
        p.spaceDecay = 30.0f; p.spacePre = 100.0f;
        auto o3 = run(imp, imp, p);
        double early = 0.0, late = 0.0;
        for (int i = ms(500.0) + 50; i < ms(560.0); ++i) early += std::abs(o3.l[(size_t) i]);
        for (int i = ms(610.0); i < ms(800.0); ++i) late += std::abs(o3.l[(size_t) i]);
        check(early < 0.02 * late, "Space PREDELAY: el reverb arranca después del retardo");
        p.spacePre = 0.0f; p.spaceAmt = 0.0f;
        auto o4 = run(imp, imp, p);
        double maxDiff = 0.0;
        for (size_t i = 0; i < imp.size(); ++i) maxDiff = std::max(maxDiff, (double) std::abs(o4.l[i] - imp[i]));
        check(maxDiff < 2.0e-6, "Space con cantidad 0: no cambia nada");
        // apagar el módulo: la cola desaparece
        p.spaceAmt = 100.0f; p.spaceOn = false;
        auto o5 = run(imp, imp, p);
        double mx = 0.0;
        for (int i = ms(800.0); i < ms(2500.0); ++i) mx = std::max(mx, (double) std::abs(o5.l[(size_t) i]));
        check(mx < 1.0e-6, "Space apagado: sin cola");
        // stereo 0: wet igual en los dos canales
        p.spaceOn = true; p.spaceStereo = 0.0f;
        auto o6 = run(imp, imp, p);
        double d6 = 0.0, s6 = 0.0;
        for (int i = ms(700.0); i < ms(2000.0); ++i) { d6 += std::abs(o6.l[(size_t) i] - o6.r[(size_t) i]); s6 += std::abs(o6.l[(size_t) i]); }
        p.spaceStereo = 100.0f;
        auto o7 = run(imp, imp, p);
        double d7 = 0.0;
        for (int i = ms(700.0); i < ms(2000.0); ++i) d7 += std::abs(o7.l[(size_t) i] - o7.r[(size_t) i]);
        check(d7 > 3.0 * d6, "Space STEREO: más estéreo = más diferencia entre canales", num(d6 / s6, 3) + " vs " + num(d7 / s6, 3));
        check(allFinite(o) && allFinite(o2) && allFinite(o3), "Space: salida finita");
    }

    // ---------- EQ, ancho, mix ----------
    {
        auto lo = tone(n, 100.0, -12.0f), hi = tone(n, 8000.0, -12.0f);
        Params p = neutral(); p.eqOn = true; p.eqLow = 400.0f;
        auto o = run(lo, lo, p);
        const double d = dbOf(toneAmp(o.l, 100.0, ms(500.0), n)) - dbOf(toneAmp(lo, 100.0, ms(500.0), n));
        check(d < -10.0, "EQ: el corte de graves baja 100 Hz", num(d, 1) + " dB");
        p = neutral(); p.eqOn = true; p.eqHigh = 2000.0f;
        auto o2 = run(hi, hi, p);
        const double d2 = dbOf(toneAmp(o2.l, 8000.0, ms(500.0), n)) - dbOf(toneAmp(hi, 8000.0, ms(500.0), n));
        check(d2 < -10.0, "EQ: el corte de agudos baja 8 kHz", num(d2, 1) + " dB");
        p = neutral(); p.eqOn = true; p.eqTone = 100.0f;
        auto o3 = run(hi, hi, p);
        auto o3l = run(lo, lo, p);
        const double up = dbOf(toneAmp(o3.l, 8000.0, ms(500.0), n)) - dbOf(toneAmp(hi, 8000.0, ms(500.0), n));
        const double dn = dbOf(toneAmp(o3l.l, 100.0, ms(500.0), n)) - dbOf(toneAmp(lo, 100.0, ms(500.0), n));
        check(up > 3.0 && dn < -3.0, "EQ TONE +100: sube agudos y baja graves", num(up, 1) + " / " + num(dn, 1) + " dB");
        p.eqOn = true; p.eqTone = 0.0f;
        auto oN = run(lo, lo, p);
        double neutralDiff = 0.0;
        for (size_t i = 0; i < lo.size(); ++i) neutralDiff = std::max(neutralDiff, (double) std::abs(oN.l[i] - lo[i]));
        check(neutralDiff < 2.0e-6, "EQ encendido con las perillas en neutro: la señal pasa intacta", num(neutralDiff, 8));
        p.eqOn = false;
        auto o4 = run(hi, hi, p);
        const double off = dbOf(toneAmp(o4.l, 8000.0, ms(500.0), n)) - dbOf(toneAmp(hi, 8000.0, ms(500.0), n));
        check(std::abs(off) < 0.05, "EQ apagado: no cambia nada", num(off, 3) + " dB");

        // ancho
        std::vector<float> a((size_t) n), b((size_t) n);
        for (int i = 0; i < n; ++i) { a[(size_t) i] = 0.3f * (float) std::sin(2 * pi * 300.0 * i / sr); b[(size_t) i] = 0.3f * (float) std::sin(2 * pi * 450.0 * i / sr); }
        p = neutral(); p.width = 0.0f;
        auto w0 = run(a, b, p);
        double dd = 0.0;
        for (int i = ms(100.0); i < n; ++i) dd += std::abs(w0.l[(size_t) i] - w0.r[(size_t) i]);
        p.width = 200.0f;
        auto w2 = run(a, b, p);
        double d200 = 0.0, d100 = 0.0;
        for (int i = ms(100.0); i < n; ++i) { d200 += std::abs(w2.l[(size_t) i] - w2.r[(size_t) i]); d100 += std::abs(a[(size_t) i] - b[(size_t) i]); }
        check(dd < 1.0e-3 && d200 > 1.8 * d100, "Ancho: 0 = mono, 200 = el doble de lados", num(dd, 5) + " / x" + num(d200 / d100, 2));

        // mix global 0: pasa la entrada (con las ganancias)
        p = neutral(); p.mix = 0.0f; p.disAmt = 100.0f; p.noiseAmt = 100.0f; p.wobAmt = 100.0f; p.spaceAmt = 100.0f;
        auto m0 = run(a, b, p);
        double maxDiff = 0.0;
        for (int i = ms(100.0); i < n; ++i) maxDiff = std::max(maxDiff, (double) std::abs(m0.l[(size_t) i] - a[(size_t) i]));
        check(maxDiff < 1.0e-5, "MIX 0: sale la entrada", num(maxDiff, 7));
    }

    // ---------- Magnitude y Flux ----------
    {
        auto in = tone(n, 1000.0, -12.0f);
        Params p = neutral(); p.disAmt = 100.0f; p.magnitude = 0.0f;
        auto o = run(in, in, p);
        double maxDiff = 0.0;
        for (size_t i = 0; i < in.size(); ++i) maxDiff = std::max(maxDiff, (double) std::abs(o.l[i] - in[i]));
        check(maxDiff < 2.0e-6, "MAGNITUDE 0: ningún módulo actúa", num(maxDiff, 7));
        // flux: la cantidad efectiva se mueve; con flux 0 queda fija
        auto sweep = [&](float fluxPct)
        {
            ScourEngine e;
            e.prepare(sr);
            Params q = neutral();
            q.digAmt = 50.0f; q.digFlux = fluxPct;
            e.setParams(q);
            std::vector<float> l((size_t) 480), r((size_t) 480);
            double mn = 1e9, mx = -1e9;
            for (int blk = 0; blk < 2000; ++blk)       // 20 s
            {
                std::fill(l.begin(), l.end(), 0.1f); std::fill(r.begin(), r.end(), 0.1f);
                e.process(l.data(), r.data(), 480);
                if (blk > 100)
                {
                    mn = std::min(mn, (double) e.effectiveAmount(ScourEngine::mDigital));
                    mx = std::max(mx, (double) e.effectiveAmount(ScourEngine::mDigital));
                }
            }
            return mx - mn;
        };
        const double s0 = sweep(0.0f), s100 = sweep(100.0f);
        check(s0 < 1.0e-4 && s100 > 0.12, "FLUX: con 0 la cantidad no se mueve, con 100 deriva", num(s0, 4) + " vs " + num(s100, 3));
    }

    // ---------- Robustez y velocidad ----------
    {
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            sr = rate;
            const int m = (int) (rate * 8.0);
            Rng rng(77);
            std::vector<float> l((size_t) m), r((size_t) m);
            for (int i = 0; i < m; ++i)
            {
                const double tt = (double) i / rate;
                l[(size_t) i] = 0.3f * (float) std::sin(2 * pi * 220.0 * tt) + 0.2f * rng.next();
                r[(size_t) i] = 0.3f * (float) std::sin(2 * pi * 330.0 * tt) + 0.2f * rng.next();
            }
            ScourEngine e;
            e.prepare(rate);
            Params p;
            p.disAmt = p.digAmt = p.wobAmt = p.magAmt = p.spaceAmt = p.noiseAmt = 100.0f;
            p.noiseFlux = p.wobFlux = p.disFlux = p.digFlux = p.spaceFlux = p.magFlux = 100.0f;
            p.noiseFollow = 50.0f; p.noiseDuck = 50.0f; p.spaceDecay = 100.0f; p.magDropouts = 100.0f; p.width = 200.0f; p.eqOn = true; p.eqTone = 50.0f;
            // mover parámetros mientras suena
            bool finite = true; float peak = 0.0f;
            Rng prng(3);
            for (size_t s = 0; s < (size_t) m; s += 441)
            {
                p.disType = (int) (prng.uni() * 3.99f); p.noiseType = (int) (prng.uni() * 5.99f);
                p.disFocus = 20.0f + prng.uni() * 5000.0f; p.digFocus = 20.0f + prng.uni() * 5000.0f;
                p.wowRate = 0.1f + prng.uni() * 2.9f; p.flutRate = 3.0f + prng.uni() * 17.0f;
                p.spacePre = prng.uni() * 200.0f; p.spaceFocus = prng.next() * 100.0f;
                p.eqLow = 20.0f + prng.uni() * 600.0f; p.eqHigh = 3000.0f + prng.uni() * 17000.0f;
                p.noisePost = prng.uni() > 0.5f; p.digBalance = prng.uni() * 100.0f; p.wobBalance = prng.uni() * 100.0f;
                p.noiseOn = prng.uni() > 0.2f; p.spaceOn = prng.uni() > 0.2f; p.magOn = prng.uni() > 0.2f;
                e.setParams(p);
                const int len = (int) std::min<size_t>(441, (size_t) m - s);
                e.process(l.data() + s, r.data() + s, len);
                for (int i = 0; i < len; ++i)
                {
                    const float a = l[s + (size_t) i], b = r[s + (size_t) i];
                    if (!std::isfinite(a) || !std::isfinite(b)) finite = false;
                    peak = std::max(peak, std::max(std::abs(a), std::abs(b)));
                }
            }
            check(finite && peak < 25.0f, "Todo al máximo con parámetros cambiando a " + num(rate, 0) + " Hz: salida finita y acotada", "pico " + num(peak, 2));
        }
        sr = 48000.0;

        const int m = (int) (sr * 20.0);
        std::vector<float> l((size_t) m, 0.1f), r((size_t) m, 0.1f);
        for (int i = 0; i < m; ++i) { l[(size_t) i] = 0.2f * (float) std::sin(2 * pi * 220.0 * i / sr); r[(size_t) i] = l[(size_t) i]; }
        ScourEngine e;
        e.prepare(sr);
        Params p;
        p.disAmt = p.digAmt = p.wobAmt = p.magAmt = p.spaceAmt = p.noiseAmt = 50.0f;
        e.setParams(p);
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t s = 0; s < (size_t) m; s += 480) e.process(l.data() + s, r.data() + s, 480);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(20.0 / secs > 15.0, "Velocidad: más de 15 veces tiempo real con todo prendido", num(20.0 / secs, 0) + "x");
    }

    std::cout << (failures == 0 ? "\nTODO OK" : "\nHAY FALLAS: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
