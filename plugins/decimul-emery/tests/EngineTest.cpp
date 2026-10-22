// Test del motor de Emery (de-esser + de-popper) y del procesador, con señales sintéticas.
// Compilar con: cmake -DDECIMU_EMERY_TESTS=ON y correr EmeryEngineTest.exe.
// Con `--screenshot ruta.png` guarda una captura de la UI con una voz de ejemplo (eses y golpes).

#include "../Source/PluginProcessor.h"
#include <cmath>
#include <iostream>
#include <random>

namespace
{
    int failures = 0;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;
    constexpr double pi = 3.14159265358979323846;

    void check(bool ok, const juce::String& what, const juce::String& detail = {})
    {
        std::cout << (ok ? "[ OK ] " : "[FAIL] ") << what.toRawUTF8()
                  << (detail.isNotEmpty() ? juce::String("  -> ") + detail : juce::String()).toRawUTF8() << std::endl;
        if (!ok)
            ++failures;
    }

    double dbOf(double lin) { return 20.0 * std::log10(std::max(lin, 1.0e-12)); }
    int ms(double t) { return (int) std::lround(t * 0.001 * sr); }

    // Amplitud de la componente de frecuencia f entre las muestras [from, to) (Goertzel).
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

    std::vector<float> tone(int n, double f, float db, int from = 0, int to = -1)
    {
        std::vector<float> v((size_t) n, 0.0f);
        const float a = std::pow(10.0f, db / 20.0f);
        for (int i = from; i < (to < 0 ? n : to); ++i)
            v[(size_t) i] = a * (float) std::sin(2.0 * pi * f * i / sr);
        return v;
    }

    void add(std::vector<float>& a, const std::vector<float>& b)
    {
        for (size_t i = 0; i < a.size(); ++i) a[i] += b[i];
    }

    void setParam(DecimulEmeryProcessor& p, const juce::String& id, float plain)
    {
        auto* param = p.apvts.getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    }

    void runStereo(DecimulEmeryProcessor& p, std::vector<float>& l, std::vector<float>& r)
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

    std::unique_ptr<DecimulEmeryProcessor> makeProcessor(double rate = sr)
    {
        auto p = std::make_unique<DecimulEmeryProcessor>();
        p->setRateAndBufferSizeDetails(rate, blockSize);
        p->prepareToPlay(rate, blockSize);
        return p;
    }

    // Procesa un par (l, r) con los parámetros dados (id -> valor) y devuelve el resultado.
    struct Out { std::vector<float> l, r; };
    Out run(std::vector<float> l, std::vector<float> r, const std::vector<std::pair<juce::String, float>>& params)
    {
        auto p = makeProcessor();
        for (auto& kv : params)
            setParam(*p, kv.first, kv.second);
        // un bloque corto de silencio deja que los parámetros se asienten
        runStereo(*p, l, r);
        return { l, r };
    }
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // ---- Captura de la UI ----
    if (argc >= 3 && juce::String(argv[1]) == "--screenshot")
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        if (const auto shotSize = juce::SystemStats::getEnvironmentVariable("DECIMU_SHOT_SIZE", {}); shotSize.contains("x"))
            editor->setSize(shotSize.upToFirstOccurrenceOf("x", false, false).getIntValue(),
                            shotSize.fromFirstOccurrenceOf("x", false, false).getIntValue());

        // Voz de ejemplo: vocal sostenido (200 Hz + armónicos), "eses" (ruido agudo) y "golpes" (80 Hz).
        const int n = ms(2600.0);
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        std::vector<float> l((size_t) n, 0.0f);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            const double syl = 0.55 + 0.45 * std::sin(2.0 * pi * 1.6 * t);
            l[(size_t) i] = (float) (syl * (0.07 * std::sin(2.0 * pi * 200.0 * t) + 0.04 * std::sin(2.0 * pi * 400.0 * t)
                                           + 0.025 * std::sin(2.0 * pi * 1100.0 * t)));
        }
        float prev = 0.0f, prev2 = 0.0f;
        for (double t0 : { 350.0, 1050.0, 1800.0 })               // eses: ruido pasado por un pasaaltos
            for (int i = ms(t0); i < ms(t0 + 160.0); ++i)
            {
                const float x = u(rng);
                const float hp = x - 2.0f * prev + prev2;           // segunda diferencia: sube hacia los agudos
                prev2 = prev; prev = x;
                const double env = std::sin(pi * (i - ms(t0)) / (double) ms(160.0));
                l[(size_t) i] += 0.10f * hp * (float) env;
            }
        for (double t0 : { 700.0, 1500.0, 2250.0 })                 // golpes de p
            for (int i = ms(t0); i < ms(t0 + 90.0); ++i)
                l[(size_t) i] += 0.45f * (float) (std::exp(-(double) (i - ms(t0)) / ms(25.0))
                                                  * std::sin(2.0 * pi * 75.0 * (i - ms(t0)) / sr));
        auto r = l;
        runStereo(*p, l, r);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(600);

        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        const juce::File out(argv[2]);
        out.deleteFile();
        juce::PNGImageFormat png;
        juce::FileOutputStream stream(out);
        return (stream.openedOk() && png.writeImageToStream(image, stream)) ? 0 : 1;
    }

    const int n = ms(900.0);
    const int a = ms(500.0), b = ms(880.0);            // ventana de medición (ya asentado)

    // ---------- Sin acción por debajo del umbral ----------
    {
        std::mt19937 rng(3);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        std::vector<float> in((size_t) n);
        for (auto& s : in) s = 0.003f * u(rng);
        auto o = run(in, in, {});
        double maxDiff = 0.0;
        for (size_t i = 0; i < in.size(); ++i) maxDiff = std::max(maxDiff, (double) std::abs(o.l[i] - in[i]));
        check(maxDiff < 1.0e-7, "Bajo el umbral no se toca nada (bit a bit)", juce::String(maxDiff));
    }

    // ---------- Sibilancia, modo BAND ----------
    {
        auto in = tone(n, 300.0, -20.0f);
        add(in, tone(n, 7000.0, -12.0f));                                    // ese fuerte, umbral -30 -> baja
        auto o = run(in, in, { { ParamIDs::popOn, 0.0f }, { ParamIDs::essFreq, 7000.0f } });
        const double dEss = dbOf(toneAmp(o.l, 7000.0, a, b)) - dbOf(toneAmp(in, 7000.0, a, b));
        const double dVoice = dbOf(toneAmp(o.l, 300.0, a, b)) - dbOf(toneAmp(in, 300.0, a, b));
        check(dEss < -8.5 && dEss > -11.0, "Ese a 7 kHz y -12 dBFS: baja el máximo (10 dB)", juce::String(dEss, 2) + " dB");
        check(std::abs(dVoice) < 0.4, "Ese: la voz de 300 Hz casi no se toca", juce::String(dVoice, 2) + " dB");
    }
    {
        // Reducción proporcional: la banda queda en el umbral (±1.5 dB) si la cantidad lo permite
        auto in = tone(n, 7000.0, -22.0f);
        auto o = run(in, in, { { ParamIDs::popOn, 0.0f }, { ParamIDs::essFreq, 7000.0f }, { ParamIDs::essAmount, 24.0f } });
        const double level = dbOf(toneAmp(o.l, 7000.0, a, b));
        check(std::abs(level + 30.0) < 1.5, "La banda se sostiene en el umbral (-30 dB)", juce::String(level, 2) + " dB");
    }
    {
        auto in = tone(n, 7000.0, -36.0f);
        auto o = run(in, in, { { ParamIDs::popOn, 0.0f } });
        const double d = dbOf(toneAmp(o.l, 7000.0, a, b)) - dbOf(toneAmp(in, 7000.0, a, b));
        check(std::abs(d) < 0.05, "Ese por debajo del umbral: sin cambio", juce::String(d, 3) + " dB");
    }
    {
        // El efecto sigue a la frecuencia elegida: con 4 kHz una ese de 7 kHz casi no se toca
        auto in = tone(n, 7000.0, -12.0f);
        auto o = run(in, in, { { ParamIDs::popOn, 0.0f }, { ParamIDs::essFreq, 3000.0f } });
        const double d = dbOf(toneAmp(o.l, 7000.0, a, b)) - dbOf(toneAmp(in, 7000.0, a, b));
        auto o2 = run(in, in, { { ParamIDs::popOn, 0.0f }, { ParamIDs::essFreq, 7000.0f } });
        const double d2 = dbOf(toneAmp(o2.l, 7000.0, a, b)) - dbOf(toneAmp(in, 7000.0, a, b));
        check(d > d2 + 4.0, "La banda sigue la frecuencia elegida", juce::String(d, 1) + " dB con 3 kHz, " + juce::String(d2, 1) + " dB con 7 kHz");
    }
    {
        // Cantidad 0: no baja nada
        auto in = tone(n, 7000.0, -12.0f);
        auto o = run(in, in, { { ParamIDs::popOn, 0.0f }, { ParamIDs::essAmount, 0.0f } });
        const double d = dbOf(toneAmp(o.l, 7000.0, a, b)) - dbOf(toneAmp(in, 7000.0, a, b));
        check(std::abs(d) < 0.05, "Amount 0: sin reducción", juce::String(d, 3) + " dB");
        // Sección apagada
        auto o2 = run(in, in, { { ParamIDs::popOn, 0.0f }, { ParamIDs::essOn, 0.0f } });
        const double d2 = dbOf(toneAmp(o2.l, 7000.0, a, b)) - dbOf(toneAmp(in, 7000.0, a, b));
        check(std::abs(d2) < 0.05, "Sección apagada: sin reducción", juce::String(d2, 3) + " dB");
    }

    // ---------- Sibilancia, modo WIDE ----------
    {
        auto in = tone(n, 300.0, -20.0f);
        add(in, tone(n, 7000.0, -12.0f));
        auto o = run(in, in, { { ParamIDs::popOn, 0.0f }, { ParamIDs::essWide, 1.0f } });
        const double dVoice = dbOf(toneAmp(o.l, 300.0, a, b)) - dbOf(toneAmp(in, 300.0, a, b));
        check(dVoice < -8.5, "WIDE: baja también la voz mientras pasa la ese", juce::String(dVoice, 2) + " dB");
    }

    // ---------- Golpes ----------
    {
        auto in = tone(n, 1000.0, -22.0f);
        add(in, tone(n, 80.0, -8.0f));                                       // golpe grave fuerte
        auto o = run(in, in, { { ParamIDs::essOn, 0.0f } });
        const double dPop = dbOf(toneAmp(o.l, 80.0, a, b)) - dbOf(toneAmp(in, 80.0, a, b));
        const double dVoice = dbOf(toneAmp(o.l, 1000.0, a, b)) - dbOf(toneAmp(in, 1000.0, a, b));
        check(dPop < -8.0 && dPop > -13.0, "Golpe de 80 Hz y -8 dBFS: baja casi el máximo (12 dB)", juce::String(dPop, 2) + " dB");
        check(std::abs(dVoice) < 0.3, "Golpe: la voz de 1 kHz no se toca", juce::String(dVoice, 2) + " dB");
    }
    {
        auto in = tone(n, 150.0, -40.0f);
        auto o = run(in, in, { { ParamIDs::essOn, 0.0f } });
        const double d = dbOf(toneAmp(o.l, 150.0, a, b)) - dbOf(toneAmp(in, 150.0, a, b));
        check(std::abs(d) < 0.05, "Graves por debajo del umbral: sin cambio", juce::String(d, 3) + " dB");
    }
    {
        // Un golpe corto (ráfaga) pasa por el detector y se atenúa en su comienzo
        auto in = tone(n, 1000.0, -22.0f);
        auto thump = tone(n, 70.0, -4.0f, ms(300.0), ms(380.0));
        add(in, thump);
        auto o = run(in, in, { { ParamIDs::essOn, 0.0f } });
        const double before = dbOf(toneAmp(in, 70.0, ms(320.0), ms(370.0)));
        const double after = dbOf(toneAmp(o.l, 70.0, ms(320.0), ms(370.0)));
        check(before - after > 8.0, "Un golpe corto de 70 Hz se atenúa", juce::String(before - after, 1) + " dB");
    }

    // ---------- Escuchar lo que se quita ----------
    {
        std::mt19937 rng(9);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        std::vector<float> in((size_t) n);
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = 0.25f * u(rng) + 0.2f * (float) std::sin(2.0 * pi * 7000.0 * (double) i / sr);
        auto normal = run(in, in, { { ParamIDs::popOn, 0.0f } });
        auto listen = run(in, in, { { ParamIDs::popOn, 0.0f }, { ParamIDs::essListen, 1.0f } });
        double maxDiff = 0.0;
        for (size_t i = 0; i < in.size(); ++i)
            maxDiff = std::max(maxDiff, (double) std::abs(normal.l[i] + listen.l[i] - in[i]));
        check(maxDiff < 2.0e-5, "LISTEN: lo que se oye + lo que queda = la entrada", juce::String(maxDiff));
    }
    {
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        std::vector<float> in((size_t) n);
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = 0.1f * u(rng) + 0.5f * (float) std::sin(2.0 * pi * 80.0 * (double) i / sr);
        auto normal = run(in, in, { { ParamIDs::essOn, 0.0f } });
        auto listen = run(in, in, { { ParamIDs::essOn, 0.0f }, { ParamIDs::popListen, 1.0f } });
        double maxDiff = 0.0;
        for (size_t i = 0; i < in.size(); ++i)
            maxDiff = std::max(maxDiff, (double) std::abs(normal.l[i] + listen.l[i] - in[i]));
        check(maxDiff < 2.0e-5, "LISTEN golpes: lo que se oye + lo que queda = la entrada", juce::String(maxDiff));
    }

    // ---------- Canales ligados ----------
    {
        auto lIn = tone(n, 7000.0, -12.0f);                  // la ese solo en L
        auto rIn = tone(n, 300.0, -20.0f);                   // voz en R
        auto o = run(lIn, rIn, { { ParamIDs::popOn, 0.0f }, { ParamIDs::essWide, 1.0f } });
        const double dR = dbOf(toneAmp(o.r, 300.0, a, b)) - dbOf(toneAmp(rIn, 300.0, a, b));
        check(dR < -8.5, "Canales ligados: la ese de L baja también R", juce::String(dR, 2) + " dB");
    }

    // ---------- Frecuencias y tasas de muestreo ----------
    {
        for (double rate : { 44100.0, 96000.0 })
        {
            auto p = makeProcessor(rate);
            const int m = (int) (0.9 * rate);
            std::vector<float> l((size_t) m);
            for (int i = 0; i < m; ++i)
                l[(size_t) i] = 0.25f * (float) std::sin(2.0 * pi * 7000.0 * i / rate) + 0.1f * (float) std::sin(2.0 * pi * 300.0 * i / rate);
            auto r = l;
            runStereo(*p, l, r);
            bool finite = true;
            for (float v : l) if (!std::isfinite(v)) finite = false;
            double e = 0.0;
            for (int i = (int) (0.5 * rate); i < m; ++i) e += (double) l[(size_t) i] * l[(size_t) i];
            check(finite && e > 0.0, juce::String("Tasa ") + juce::String((int) rate) + " Hz: salida finita y con señal");
        }
    }
    {
        // Barrer la frecuencia mientras suena no hace saltos ni valores raros
        auto p = makeProcessor();
        const int m = ms(1500.0);
        auto l = tone(m, 5000.0, -10.0f);
        auto r = l;
        juce::MidiBuffer midi;
        double worstJump = 0.0;
        float prev = 0.0f;
        bool finite = true;
        for (int start = 0; start < m; start += blockSize)
        {
            const int len = std::min(blockSize, m - start);
            const float t = (float) start / (float) m;
            setParam(*p, ParamIDs::essFreq, 2500.0f + 9000.0f * t);
            setParam(*p, ParamIDs::popFreq, 60.0f + 200.0f * t);
            juce::AudioBuffer<float> buf(2, len);
            for (int i = 0; i < len; ++i) { buf.setSample(0, i, l[(size_t) (start + i)]); buf.setSample(1, i, r[(size_t) (start + i)]); }
            p->processBlock(buf, midi);
            for (int i = 0; i < len; ++i)
            {
                const float v = buf.getSample(0, i);
                if (!std::isfinite(v)) finite = false;
                if (start > 0 || i > 0) worstJump = std::max(worstJump, (double) std::abs(v - prev));
                prev = v;
            }
        }
        check(finite && worstJump < 0.4, "Barrer las frecuencias: sin saltos ni valores raros", juce::String(worstJump, 3));
    }

    // ---------- Procesador ----------
    {
        auto p = makeProcessor();
        check(p->getLatencySamples() == 0, "Procesador: latencia cero");

        // bypass: sale igual a la entrada
        setParam(*p, ParamIDs::bypass, 1.0f);
        std::vector<float> l = tone(n, 7000.0, -6.0f), r = l;
        const auto orig = l;
        runStereo(*p, l, r);
        double maxDiff = 0.0;
        for (int i = ms(100.0); i < n; ++i) maxDiff = std::max(maxDiff, (double) std::abs(l[(size_t) i] - orig[(size_t) i]));
        check(maxDiff < 1.0e-6, "Procesador: bypass deja pasar la entrada intacta", juce::String(maxDiff));

        // mono
        auto m = std::make_unique<DecimulEmeryProcessor>();
        m->setPlayConfigDetails(1, 1, sr, blockSize);
        m->prepareToPlay(sr, blockSize);
        juce::AudioBuffer<float> mb(1, blockSize);
        mb.clear();
        juce::MidiBuffer midi;
        m->processBlock(mb, midi);
        check(m->getTotalNumInputChannels() == 1, "Procesador: acepta pistas mono");
        {
            auto ml = tone(ms(900.0), 7000.0, -12.0f);
            juce::AudioBuffer<float> big(1, (int) ml.size());
            for (int i = 0; i < big.getNumSamples(); ++i) big.setSample(0, i, ml[(size_t) i]);
            auto mm = std::make_unique<DecimulEmeryProcessor>();
            mm->setPlayConfigDetails(1, 1, sr, blockSize);
            mm->prepareToPlay(sr, blockSize);
            setParam(*mm, ParamIDs::popOn, 0.0f);
            setParam(*mm, ParamIDs::essFreq, 7000.0f);
            for (int s = 0; s < big.getNumSamples(); s += blockSize)
            {
                const int len = std::min(blockSize, big.getNumSamples() - s);
                juce::AudioBuffer<float> blk(1, len);
                blk.copyFrom(0, 0, big, 0, s, len);
                mm->processBlock(blk, midi);
                big.copyFrom(0, s, blk, 0, 0, len);
            }
            std::vector<float> outv((size_t) big.getNumSamples());
            for (int i = 0; i < big.getNumSamples(); ++i) outv[(size_t) i] = big.getSample(0, i);
            const double d = dbOf(toneAmp(outv, 7000.0, a, b)) - dbOf(toneAmp(ml, 7000.0, a, b));
            check(d < -8.5, "Procesador: mono también reduce", juce::String(d, 2) + " dB");
        }

        // estado
        setParam(*p, ParamIDs::essThresh, -41.5f);
        setParam(*p, ParamIDs::popWide, 1.0f);
        setParam(*p, ParamIDs::essFreq, 4400.0f);
        juce::MemoryBlock state;
        p->getStateInformation(state);
        auto p2 = makeProcessor();
        p2->setStateInformation(state.getData(), (int) state.getSize());
        check(std::abs(p2->apvts.getRawParameterValue(ParamIDs::essThresh)->load() + 41.5f) < 0.2f
                  && p2->apvts.getRawParameterValue(ParamIDs::popWide)->load() > 0.5f
                  && std::abs(p2->apvts.getRawParameterValue(ParamIDs::essFreq)->load() - 4400.0f) < 5.0f,
              "Procesador: el estado se guarda y se restaura");

        // cuadros para la pantalla
        DecimulEmeryProcessor::Frame frames[512];
        const int got = p->readFrames(frames, 512);
        check(got > 100, "Procesador: entrega cuadros a la pantalla", juce::String(got) + " cuadros");
    }

    std::cout << (failures == 0 ? "\nTODO OK" : (juce::String("\nHAY FALLAS: ") + juce::String(failures)).toRawUTF8()) << std::endl;
    return failures == 0 ? 0 : 1;
}
