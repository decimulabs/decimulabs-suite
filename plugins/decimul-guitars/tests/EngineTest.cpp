// Test del procesador de Guitars v0.5: Drive calibrado, gate ligado, Tone/Cab suavizados,
// Space/Echo/Modulation propios. Compilar con: cmake -DDECIMU_GUITARS_TESTS=ON y correr
// GuitarsEngineTest.exe. Al final imprime "TODO OK" si no falló nada.

#include "../Source/PluginProcessor.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>

namespace
{
    int failures = 0;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;

    void check(bool ok, const juce::String& what, const juce::String& detail = {})
    {
        std::cout << (ok ? "[ OK ] " : "[FAIL] ") << what.toRawUTF8()
                  << (detail.isNotEmpty() ? juce::String("  -> ") + detail : juce::String()).toRawUTF8() << std::endl;
        if (!ok)
            ++failures;
    }

    double dbOf(double lin) { return 20.0 * std::log10(std::max(lin, 1.0e-12)); }

    double rmsOf(const std::vector<float>& v, size_t from = 0)
    {
        double s = 0.0;
        for (size_t i = from; i < v.size(); ++i) s += (double) v[i] * v[i];
        return std::sqrt(s / (double) std::max<size_t>(1, v.size() - from));
    }

    std::vector<float> whiteNoise(int n, float amp, unsigned seed)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> d(-1.0f, 1.0f);
        std::vector<float> v((size_t) n);
        for (auto& x : v) x = d(rng) * amp;
        return v;
    }

    std::vector<float> sine(int n, double freq, float amp)
    {
        std::vector<float> v((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = amp * (float) std::sin(2.0 * juce::MathConstants<double>::pi * freq * i / sr);
        return v;
    }

    bool allFinite(const std::vector<float>& v, float limit = 50.0f)
    {
        for (float x : v)
            if (!std::isfinite(x) || std::abs(x) > limit)
                return false;
        return true;
    }

    void setParam(DecimulGuitarsProcessor& p, const juce::String& id, float plain)
    {
        auto* param = p.apvts.getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    }

    void runStereo(DecimulGuitarsProcessor& p, std::vector<float>& l, std::vector<float>& r)
    {
        juce::MidiBuffer midi;
        for (size_t start = 0; start < l.size(); start += blockSize)
        {
            const int n = (int) std::min<size_t>(blockSize, l.size() - start);
            juce::AudioBuffer<float> b(2, n);
            for (int i = 0; i < n; ++i)
            {
                b.setSample(0, i, l[start + (size_t) i]);
                b.setSample(1, i, r[start + (size_t) i]);
            }
            p.processBlock(b, midi);
            for (int i = 0; i < n; ++i)
            {
                l[start + (size_t) i] = b.getSample(0, i);
                r[start + (size_t) i] = b.getSample(1, i);
            }
        }
    }

    // Todo apagado salvo lo que se prenda a mano.
    std::unique_ptr<DecimulGuitarsProcessor> makeProcessor()
    {
        auto p = std::make_unique<DecimulGuitarsProcessor>();
        p->setRateAndBufferSizeDetails(sr, blockSize);
        p->prepareToPlay(sr, blockSize);
        setParam(*p, ParamIDs::toneEnabled, 0.0f);
        return p;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // ---- Drive calibrado: misma sonoridad (RMS) que la entrada con una guitarra típica ----
    {
        double worst = 0.0;
        juce::String detail;
        for (int mode = 0; mode < 3; ++mode)
            for (float amount : { 10.0f, 35.0f, 60.0f, 85.0f, 100.0f })
            {
                auto p = makeProcessor();
                setParam(*p, ParamIDs::driveMode, (float) mode);
                setParam(*p, ParamIDs::driveAmount, amount);
                auto l = sine((int) sr, 1000.0, 0.178f); // -15 dBFS de pico
                auto r = l;
                runStereo(*p, l, r);
                // el Drive agrega latencia (oversampling): se mide al final, ya en régimen
                const double diff = dbOf(rmsOf(l, 24000)) - dbOf(0.178 / std::sqrt(2.0));
                if (std::abs(diff) > std::abs(worst)) { worst = diff; detail = "modo " + juce::String(mode) + ", " + juce::String(amount, 0) + " %"; }
            }
        check(std::abs(worst) < 1.0, "Drive: el nivel queda igual que la entrada en los 3 modos de 10 a 100 %",
              "peor caso " + juce::String(worst, 2) + " dB (" + detail + ")");
    }
    {
        // Antes (1/sqrt(pre)) al 100 % la salida quedaba unos 18 dB abajo: ya no.
        auto p = makeProcessor();
        setParam(*p, ParamIDs::driveAmount, 100.0f);
        auto l = sine((int) sr, 1000.0, 0.05f);
        auto r = l;
        runStereo(*p, l, r);
        check(rmsOf(l, 24000) > 0.08 && rmsOf(l, 24000) < 1.0, "Drive 100 %: con una señal suave igual hay sonido fuerte y sin pasarse (sustain)",
              juce::String(dbOf(rmsOf(l, 24000)), 1) + " dBFS RMS");
    }
    {
        // Mover Drive de golpe no pega escalones (ganancias rampadas entre bloques).
        auto p = makeProcessor();
        auto l = sine((int) (sr * 2), 440.0, 0.1f);
        auto r = l;
        std::vector<float> l1(l.begin(), l.begin() + 48000), r1 = l1, l2(l.begin() + 48000, l.end()), r2 = l2;
        setParam(*p, ParamIDs::driveAmount, 20.0f);
        runStereo(*p, l1, r1);
        setParam(*p, ParamIDs::driveAmount, 90.0f);
        runStereo(*p, l2, r2);
        double maxStep = 0.0;
        for (size_t i = 1; i < l2.size(); ++i) maxStep = std::max(maxStep, (double) std::abs(l2[i] - l2[i - 1]));
        check(maxStep < 0.5, "Drive: subir de 20 a 90 % de golpe no pega escalones", juce::String(maxStep, 3));
    }

    // ---- Gate ligado ----
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::gateAmount, 60.0f);
        // L = ráfagas fuertes, R = soplido constante bajo: el gate se abre por L y R pasa igual.
        auto l = std::vector<float>((size_t) sr * 2, 0.0f);
        for (size_t i = 0; i < l.size(); ++i)
            if ((i / 12000) % 2 == 0) l[i] = 0.4f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 300.0 * (double) i / sr);
        auto r = sine((int) (sr * 2), 500.0, 0.002f); // -54 dBFS: por sí solo no abre el gate (umbral -44 dB)
        const auto r0 = r;
        runStereo(*p, l, r);
        // en los tramos con L fuerte (abierto) R sale como entró; en los silencios de L, los dos se cierran
        const double rOpen = rmsOf(std::vector<float>(r.begin() + 6000, r.begin() + 11000)) / rmsOf(std::vector<float>(r0.begin() + 6000, r0.begin() + 11000));
        const double rClosed = rmsOf(std::vector<float>(r.begin() + 22000, r.begin() + 23500)) / rmsOf(std::vector<float>(r0.begin() + 22000, r0.begin() + 23500));
        check(rOpen > 0.9 && rClosed < 0.1, "Gate ligado: R abre y cierra junto con L (la imagen no se mueve)",
              "abierto " + juce::String(rOpen, 2) + ", cerrado " + juce::String(rClosed, 3));
    }

    // ---- Tone / Cab suavizados y transparentes en cero ----
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::toneEnabled, 1.0f);
        setParam(*p, ParamIDs::toneMode, 1.0f);
        // Tone en plano igual agrega el peak fijo de +4 dB del mid-sweep: se compara contra el cálculo de JUCE.
        auto l = sine((int) sr, 800.0, 0.1f);
        auto r = l;
        runStereo(*p, l, r);
        const double gain = dbOf(rmsOf(l, 24000)) - dbOf(0.1 / std::sqrt(2.0));
        check(std::abs(gain - 4.0) < 0.3, "Tone: el peak del mid-sweep sube 4 dB en su frecuencia", juce::String(gain, 2) + " dB");
    }
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::cabEnabled, 1.0f);
        setParam(*p, ParamIDs::cabMode, 0.0f);
        auto n1 = whiteNoise((int) sr * 2, 0.3f, 31);
        auto n2 = n1;
        runStereo(*p, n1, n2);
        check(allFinite(n1, 3.0f) && rmsOf(n1, 24000) < 0.3 * 0.58, "Cab: recorta graves y agudos (menos energía que el ruido blanco)");
    }

    // ---- Modulación ----
    {
        // Chorus estéreo: una guitarra mono sale con L y R distintos.
        auto p = makeProcessor();
        setParam(*p, ParamIDs::modAmount, 100.0f);
        setParam(*p, ParamIDs::modMode, 0.0f);
        auto l = whiteNoise((int) sr * 3, 0.2f, 41);
        auto r = l;
        runStereo(*p, l, r);
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (size_t i = 24000; i < l.size(); ++i) { lr += (double) l[i] * r[i]; ll += (double) l[i] * l[i]; rr += (double) r[i] * r[i]; }
        const double corr = lr / std::sqrt(ll * rr);
        check(corr < 0.97 && allFinite(l, 3.0f), "Chorus: con entrada mono, L y R salen distintos", juce::String(corr, 3));
        for (int mode : { 1, 2 })
        {
            auto q = makeProcessor();
            setParam(*q, ParamIDs::modAmount, 100.0f);
            setParam(*q, ParamIDs::modMode, (float) mode);
            auto a = whiteNoise((int) sr * 3, 0.2f, 42);
            auto b = a;
            runStereo(*q, a, b);
            check(allFinite(a, 5.0f) && allFinite(b, 5.0f), mode == 1 ? "Flanger: estable" : "Phaser: estable");
        }
    }

    // ---- Space / Echo ----
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::spaceAmount, 70.0f);
        setParam(*p, ParamIDs::spaceMode, 2.0f);
        setParam(*p, ParamIDs::echoAmount, 50.0f);
        setParam(*p, ParamIDs::echoDivision, 2.0f);
        setParam(*p, ParamIDs::echoFeedback, 100.0f);
        setParam(*p, ParamIDs::modAmount, 60.0f);
        setParam(*p, ParamIDs::driveAmount, 50.0f);
        setParam(*p, ParamIDs::cabEnabled, 1.0f);
        setParam(*p, ParamIDs::toneEnabled, 1.0f);
        auto l = whiteNoise((int) (sr * 10), 0.3f, 51);
        auto r = whiteNoise((int) (sr * 10), 0.3f, 52);
        const auto t0 = std::chrono::steady_clock::now();
        runStereo(*p, l, r);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(allFinite(l, 10.0f) && allFinite(r, 10.0f), "Guitars: cadena completa con feedback 100 %, sin NaN ni explosión");
        check(10.0 / secs > 15.0, "Guitars: rendimiento", juce::String(10.0 / secs, 1) + "x tiempo real");
    }
    {
        // Apagar el Space (LED) y volver a prenderlo: no vuelve la cola vieja.
        auto p = makeProcessor();
        setParam(*p, ParamIDs::spaceAmount, 100.0f);
        setParam(*p, ParamIDs::spaceMode, 2.0f);
        auto burst = whiteNoise(24000, 0.5f, 61);
        auto burstR = burst;
        runStereo(*p, burst, burstR);
        const double tailPeak = rmsOf(burst, 12000);
        setParam(*p, ParamIDs::spaceEnabled, 0.0f);
        std::vector<float> silL((size_t) sr * 2, 0.0f), silR = silL;
        runStereo(*p, silL, silR);
        setParam(*p, ParamIDs::spaceEnabled, 1.0f);
        std::vector<float> aL((size_t) sr, 0.0f), aR = aL;
        runStereo(*p, aL, aR);
        check(dbOf(rmsOf(aL) / tailPeak) < -60.0, "Space: apagar y prender no devuelve audio viejo", juce::String(dbOf(rmsOf(aL) / tailPeak), 1) + " dB");
    }
    {
        // Latencia informada = la del oversampling del Drive, constante.
        auto p = makeProcessor();
        check(p->getLatencySamples() > 0 && p->getLatencySamples() < 100, "Guitars: latencia informada por el oversampling del Drive", juce::String(p->getLatencySamples()) + " muestras");
    }

    std::cout << (failures == 0 ? "\nTODO OK" : "\nHAY FALLAS: ") << (failures == 0 ? "" : std::to_string(failures).c_str()) << std::endl;
    return failures == 0 ? 0 : 1;
}
