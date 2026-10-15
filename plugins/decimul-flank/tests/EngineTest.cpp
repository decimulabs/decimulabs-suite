// Test de integración de Flank (nombre de trabajo): señales sintéticas por processBlock,
// verificando la desafinación (en centésimos), los retardos, el pan de las copias, el
// nivel, los cortes, el ambiente, el chequeo en mono, el bypass y que no explote con
// parámetros extremos.
//
// Compilar con: cmake -DDECIMU_FLANK_TESTS=ON y correr FlankEngineTest.exe.
// Con `--screenshot ruta.png` arma una escena de demostración de la UI y la guarda.

#include "../Source/PluginProcessor.h"
#include <chrono>
#include <cmath>
#include <iostream>

namespace
{
    int failures = 0;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;
    constexpr double pi = juce::MathConstants<double>::pi;

    void check(bool ok, const juce::String& what, const juce::String& detail = {})
    {
        std::cout << (ok ? "[ OK ] " : "[FAIL] ") << what.toRawUTF8()
                  << (detail.isNotEmpty() ? (juce::String("  -> ") + detail).toRawUTF8() : "") << std::endl;
        if (!ok)
            ++failures;
    }

    struct Out
    {
        std::vector<float> l, r;
    };

    using Params = std::vector<std::pair<juce::String, float>>;

    void setParam(DecimulFlankProcessor& p, const juce::String& id, float plain)
    {
        auto* param = p.apvts.getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    }

    // Procesador con los parámetros ya puestos antes de preparar (así los suavizadores
    // arrancan en el valor final y no hay rampas en la medición).
    std::unique_ptr<DecimulFlankProcessor> make(const Params& params = {})
    {
        auto p = std::make_unique<DecimulFlankProcessor>();
        p->setRateAndBufferSizeDetails(sr, blockSize);
        setParam(*p, ParamIDs::mix, 100.0f);
        setParam(*p, ParamIDs::space, 0.0f);
        setParam(*p, ParamIDs::lowCut, 20.0f);
        setParam(*p, ParamIDs::highCut, 20000.0f);
        for (auto& [id, value] : params)
            setParam(*p, id, value);
        p->prepareToPlay(sr, blockSize);
        return p;
    }

    Out run(DecimulFlankProcessor& p, const std::vector<float>& inL, const std::vector<float>* inR = nullptr)
    {
        Out out;
        const int total = (int) inL.size();
        for (int start = 0; start < total; start += blockSize)
        {
            const int n = std::min(blockSize, total - start);
            juce::AudioBuffer<float> block(2, n);
            for (int i = 0; i < n; ++i)
            {
                block.setSample(0, i, inL[(size_t) (start + i)]);
                block.setSample(1, i, inR != nullptr ? (*inR)[(size_t) (start + i)] : inL[(size_t) (start + i)]);
            }
            juce::MidiBuffer midi;
            p.processBlock(block, midi);
            for (int i = 0; i < n; ++i)
            {
                out.l.push_back(block.getSample(0, i));
                out.r.push_back(block.getSample(1, i));
            }
        }
        return out;
    }

    std::vector<float> sine(double freq, float amp, double seconds)
    {
        std::vector<float> x((size_t) (seconds * sr));
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = amp * (float) std::sin(2.0 * pi * freq * (double) i / sr);
        return x;
    }

    std::vector<float> noise(float amp, double seconds, int seed = 7)
    {
        juce::Random rng(seed);
        std::vector<float> x((size_t) (seconds * sr));
        for (auto& s : x)
            s = amp * (rng.nextFloat() * 2.0f - 1.0f);
        return x;
    }

    double rms(const std::vector<float>& x, int start, int end)
    {
        double s = 0.0;
        end = std::min(end, (int) x.size());
        for (int i = std::max(0, start); i < end; ++i)
            s += (double) x[(size_t) i] * (double) x[(size_t) i];
        return std::sqrt(s / std::max(1, end - start));
    }

    // Magnitud de una sola frecuencia con ventana Hann (Goertzel complejo).
    double binMagnitude(const std::vector<float>& x, int start, int len, double freq)
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos(2.0 * pi * (double) i / (double) (len - 1));
            const double ph = 2.0 * pi * freq * (double) i / sr;
            const double v = (double) x[(size_t) (start + i)] * w;
            re += v * std::cos(ph);
            im -= v * std::sin(ph);
        }
        return std::sqrt(re * re + im * im) / (0.5 * (double) len) * 2.0;
    }

    // Frecuencia del pico en [lo, hi], con refinado parabólico.
    double peakFreq(const std::vector<float>& x, int start, int len, double lo, double hi, double* magnitude = nullptr)
    {
        const double step = 0.25;
        double bestF = lo, bestM = -1.0;
        std::vector<double> mags;
        for (double f = lo; f <= hi + 1e-9; f += step)
            mags.push_back(binMagnitude(x, start, len, f));
        size_t bi = 0;
        for (size_t i = 0; i < mags.size(); ++i)
            if (mags[i] > bestM)
            {
                bestM = mags[i];
                bi = i;
            }
        bestF = lo + (double) bi * step;
        if (bi > 0 && bi + 1 < mags.size())
        {
            const double a = mags[bi - 1], b = mags[bi], c = mags[bi + 1];
            const double denom = a - 2.0 * b + c;
            if (std::abs(denom) > 1e-12)
                bestF += 0.5 * (a - c) / denom * step;
        }
        if (magnitude != nullptr)
            *magnitude = bestM;
        return bestF;
    }

    int peakIndex(const std::vector<float>& x, int from, int to)
    {
        int best = from;
        float m = -1.0f;
        for (int i = from; i < to && i < (int) x.size(); ++i)
            if (std::abs(x[(size_t) i]) > m)
            {
                m = std::abs(x[(size_t) i]);
                best = i;
            }
        return best;
    }

    double cents(double f, double ref) { return 1200.0 * std::log2(f / ref); }

    // "Voz" sintética para la captura de la UI: armónicos de ~190 Hz con vibrato lento,
    // una envolvente por sílabas y un poco de aire.
    std::vector<float> fakeVoice(double seconds)
    {
        std::vector<float> x((size_t) (seconds * sr));
        juce::Random rng(11);
        double phase = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double) i / sr;
            const double f0 = 190.0 * (1.0 + 0.012 * std::sin(2.0 * pi * 5.2 * t));
            phase += 2.0 * pi * f0 / sr;
            double v = 0.0;
            for (int k = 1; k <= 14; ++k)
            {
                const double formant = 1.0 / (1.0 + std::pow((k * f0 - 700.0) / 500.0, 2.0))
                                     + 0.6 / (1.0 + std::pow((k * f0 - 1900.0) / 700.0, 2.0));
                v += formant * std::sin((double) k * phase) / (double) k;
            }
            const double env = 0.5 + 0.5 * std::sin(2.0 * pi * 1.6 * t - 0.8);
            x[i] = (float) (0.55 * v * env * env) + 0.004f * (rng.nextFloat() - 0.5f);
        }
        return x;
    }
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // ---- Modo captura de la UI ----
    if (argc >= 3 && juce::String(argv[1]) == "--screenshot")
    {
        auto p = make({ { ParamIDs::mix, 55.0f }, { ParamIDs::space, 25.0f }, { ParamIDs::lowCut, 150.0f },
                        { ParamIDs::highCut, 10000.0f }, { ParamIDs::detune, 12.0f }, { ParamIDs::spread, 60.0f },
                        { ParamIDs::delay, 14.0f }, { ParamIDs::offset, 5.0f } });

        // El editor se abre antes de procesar: al construirse descarta lo que había en la cola.
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        editor->setSize(1000, 560);

        auto voice = fakeVoice(2.2);
        run(*p, voice);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(700);

        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        const juce::File out(argv[2]);
        out.deleteFile();
        juce::PNGImageFormat png;
        juce::FileOutputStream stream(out);
        return (stream.openedOk() && png.writeImageToStream(image, stream)) ? 0 : 1;
    }

    // ================= Sin efecto =================
    {
        auto p = make({ { ParamIDs::mix, 0.0f } });
        const auto x = noise(0.3f, 1.0);
        const auto o = run(*p, x);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < x.size(); ++i)
            maxDiff = std::max(maxDiff, std::max(std::abs(o.l[i] - x[i]), std::abs(o.r[i] - x[i])));
        check(maxDiff < 1.0e-6f, "Mix 0 y Space 0: la salida es la entrada sin tocar", juce::String(maxDiff, 8));
        check(p->getLatencySamples() == 0, "latencia cero");
    }
    {
        auto p = make({ { ParamIDs::bypass, 1.0f } });
        const auto x = noise(0.3f, 0.6);
        const auto o = run(*p, x);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < x.size(); ++i)
            maxDiff = std::max(maxDiff, std::abs(o.l[i] - x[i]));
        check(maxDiff < 1.0e-6f, "bypass: el audio pasa intacto", juce::String(maxDiff, 8));
    }

    // ================= Desafinación =================
    {
        // Static: la copia izquierda baja y la derecha sube lo que dice Detune.
        auto p = make({ { ParamIDs::detune, 20.0f }, { ParamIDs::spread, 100.0f }, { ParamIDs::offset, 0.0f },
                        { ParamIDs::mode, 0.0f }, { ParamIDs::delay, 14.0f } });
        const auto o = run(*p, sine(1000.0, 0.3f, 3.0));
        const int start = (int) (0.6 * sr), len = 65536;

        double magL = 0.0, magR = 0.0;
        const double fL = peakFreq(o.l, start, len, 980.0, 996.0, &magL);
        const double fR = peakFreq(o.r, start, len, 1004.0, 1020.0, &magR);
        check(std::abs(cents(fL, 1000.0) + 20.0) < 0.6, "Static: la copia izquierda baja 20 centésimos",
              juce::String(cents(fL, 1000.0), 2) + " c");
        check(std::abs(cents(fR, 1000.0) - 20.0) < 0.6, "Static: la copia derecha sube 20 centésimos",
              juce::String(cents(fR, 1000.0), 2) + " c");
        check(magL > 0.06 && magR > 0.06, "Static: las copias suenan a nivel razonable",
              juce::String(magL, 3) + " / " + juce::String(magR, 3));
    }
    {
        // Detune 0: las copias son el original retrasado exactamente `delay`.
        for (int mode = 0; mode < 2; ++mode)
        {
            auto p = make({ { ParamIDs::detune, 0.0f }, { ParamIDs::spread, 100.0f }, { ParamIDs::offset, 5.0f },
                            { ParamIDs::delay, 10.0f }, { ParamIDs::mode, (float) mode } });
            std::vector<float> x((size_t) (0.4 * sr), 0.0f);
            x[2000] = 1.0f;
            const auto o = run(*p, x);

            // la seca (con la ganancia de Mix 100 %: 0.707) y las copias a 10 y 15 ms
            const int copyL = peakIndex(o.l, 2100, 4000);
            const int copyR = peakIndex(o.r, 2100, 4000);
            check(std::abs(copyL - (2000 + 480)) <= 2, juce::String(mode == 0 ? "Static" : "Drift") + ": la copia L llega a los 10 ms",
                  juce::String(copyL - 2000));
            check(std::abs(copyR - (2000 + 720)) <= 2, juce::String(mode == 0 ? "Static" : "Drift") + ": la copia R llega a los 15 ms (10 + Offset 5)",
                  juce::String(copyR - 2000));
            check(std::abs(o.l[2000]) > 0.5f && std::abs(o.r[2000]) > 0.5f, "la voz seca sale en su lugar, sin retardo",
                  juce::String(o.l[2000], 3));
        }
    }
    {
        // Spread: 100 % = cada copia solo en su lado; 0 = las dos al centro.
        auto build = [](float spread)
        {
            auto p = make({ { ParamIDs::detune, 0.0f }, { ParamIDs::spread, spread }, { ParamIDs::offset, 8.0f },
                            { ParamIDs::delay, 10.0f }, { ParamIDs::mode, 0.0f } });
            std::vector<float> x((size_t) (0.3 * sr), 0.0f);
            x[2000] = 1.0f;
            return run(*p, x);
        };
        const auto wide = build(100.0f);
        const float wideLonR = std::abs(wide.r[2000 + 480]), wideLonL = std::abs(wide.l[2000 + 480]);
        check(wideLonL > 0.3f && wideLonR < 0.02f, "Spread 100 %: la copia L va solo a la izquierda",
              juce::String(wideLonL, 3) + " / " + juce::String(wideLonR, 3));

        const auto mid = build(0.0f);
        check(std::abs(std::abs(mid.l[2000 + 480]) - std::abs(mid.r[2000 + 480])) < 0.01f
                  && std::abs(mid.l[2000 + 480]) > 0.1f,
              "Spread 0 %: las dos copias quedan en el centro (L = R)",
              juce::String(mid.l[2000 + 480], 3) + " / " + juce::String(mid.r[2000 + 480], 3));
    }

    // ================= Nivel =================
    {
        auto p = make({ { ParamIDs::mix, 100.0f }, { ParamIDs::detune, 15.0f } });
        const auto x = noise(0.3f, 2.0);
        const auto o = run(*p, x);
        const double rin = rms(x, (int) (0.5 * sr), (int) (2.0 * sr));
        const double rout = 0.5 * (rms(o.l, (int) (0.5 * sr), (int) (2.0 * sr)) + rms(o.r, (int) (0.5 * sr), (int) (2.0 * sr)));
        check(std::abs(20.0 * std::log10(rout / rin)) < 1.0, "Mix 100 %: el volumen total casi no cambia",
              juce::String(20.0 * std::log10(rout / rin), 2) + " dB");
    }
    {
        auto p = make({ { ParamIDs::outputTrim, -6.0f }, { ParamIDs::mix, 0.0f } });
        const auto o = run(*p, sine(1000.0, 0.5f, 0.5));
        const double r = rms(o.l, (int) (0.2 * sr), (int) (0.5 * sr));
        check(std::abs(r - 0.5 / std::sqrt(2.0) * std::pow(10.0, -6.0 / 20.0)) < 0.004, "Output -6 dB baja a la mitad",
              juce::String(r, 4));
    }

    // ================= Cortes =================
    {
        // Low cut: los graves no entran a las copias; la salida a 80 Hz es solo la seca.
        auto p = make({ { ParamIDs::lowCut, 600.0f }, { ParamIDs::detune, 0.0f }, { ParamIDs::delay, 14.0f } });
        const auto o = run(*p, sine(80.0, 0.4f, 2.0));
        const double amp = binMagnitude(o.l, (int) (0.5 * sr), 32768, 80.0);
        check(amp < 0.4 * 0.7071 * 1.12 && amp > 0.4 * 0.7071 * 0.9, "Low cut 600 Hz: a 80 Hz queda solo la seca (sin copias)",
              juce::String(amp, 4) + " (seca = " + juce::String(0.4 * 0.7071, 4) + ")");
    }
    {
        auto p = make({ { ParamIDs::highCut, 2000.0f }, { ParamIDs::detune, 0.0f }, { ParamIDs::delay, 14.0f } });
        const auto o = run(*p, sine(10000.0, 0.4f, 2.0));
        const double amp = binMagnitude(o.l, (int) (0.5 * sr), 32768, 10000.0);
        check(amp < 0.4 * 0.7071 * 1.12 && amp > 0.4 * 0.7071 * 0.9, "High cut 2 kHz: a 10 kHz queda solo la seca (sin copias)",
              juce::String(amp, 4));
    }

    // ================= Ambiente =================
    {
        std::vector<float> x((size_t) (0.6 * sr), 0.0f);
        x[2000] = 1.0f;

        auto dry = make({ { ParamIDs::mix, 0.0f }, { ParamIDs::space, 0.0f } });
        const auto o0 = run(*dry, x);
        auto wet = make({ { ParamIDs::mix, 0.0f }, { ParamIDs::space, 100.0f } });
        const auto o1 = run(*wet, x);

        const double tail0 = rms(o0.l, 2000 + 240, 2000 + 2400);
        const double tail1 = rms(o1.l, 2000 + 240, 2000 + 2400);
        const double late = rms(o1.l, 2000 + (int) (0.3 * sr), 2000 + (int) (0.55 * sr));
        check(tail0 < 1.0e-6 && tail1 > 0.005, "Space: aparecen reflexiones después de la voz",
              juce::String(tail0, 6) + " -> " + juce::String(tail1, 4));
        check(late < 1.0e-5, "Space: el ambiente se apaga (no hay cola larga)", juce::String(late, 8));
        check(rms(o1.l, 2000 + 240, 2000 + 2400) > 0.0 && rms(o1.r, 2000 + 240, 2000 + 2400) > 0.0
                  && std::abs(peakIndex(o1.l, 2100, 4400) - peakIndex(o1.r, 2100, 4400)) > 0,
              "Space: el ambiente es distinto en cada lado");
    }

    // ================= Mono =================
    {
        auto p = make({ { ParamIDs::monoCheck, 1.0f }, { ParamIDs::detune, 25.0f }, { ParamIDs::spread, 100.0f } });
        const auto o = run(*p, noise(0.3f, 1.0));
        float maxDiff = 0.0f;
        for (size_t i = (size_t) (0.2 * sr); i < o.l.size(); ++i)
            maxDiff = std::max(maxDiff, std::abs(o.l[i] - o.r[i]));
        check(maxDiff < 1.0e-6f, "Mono check: L y R salen iguales", juce::String(maxDiff, 8));
    }
    {
        auto mono = make({ { ParamIDs::mix, 0.0f } });
        run(*mono, noise(0.3f, 1.0));
        check(mono->correlation.load() > 0.99f && std::abs(mono->monoLossDb.load()) < 0.1f,
              "correlación 1 y sin pérdida en mono si no hay copias",
              juce::String(mono->correlation.load(), 3) + " / " + juce::String(mono->monoLossDb.load(), 2) + " dB");

        auto wide = make({ { ParamIDs::mix, 100.0f }, { ParamIDs::spread, 100.0f }, { ParamIDs::detune, 30.0f },
                           { ParamIDs::delay, 25.0f }, { ParamIDs::offset, 12.0f } });
        run(*wide, noise(0.3f, 2.0));
        check(wide->correlation.load() < 0.8f && wide->monoLossDb.load() < -0.5f,
              "con las copias abiertas baja la correlación y se nota la pérdida en mono",
              juce::String(wide->correlation.load(), 3) + " / " + juce::String(wide->monoLossDb.load(), 2) + " dB");
    }
    {
        // Entrada estéreo con contenido distinto en cada lado: la seca lo conserva.
        auto p = make({ { ParamIDs::mix, 0.0f } });
        const auto xl = sine(300.0, 0.3f, 0.5), xr = sine(900.0, 0.3f, 0.5);
        const auto o = run(*p, xl, &xr);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < xl.size(); ++i)
            maxDiff = std::max(maxDiff, std::max(std::abs(o.l[i] - xl[i]), std::abs(o.r[i] - xr[i])));
        check(maxDiff < 1.0e-6f, "entrada estéreo: la seca conserva cada canal", juce::String(maxDiff, 8));
    }
    {
        // Layouts
        DecimulFlankProcessor p;
        auto layout = [](juce::AudioChannelSet in, juce::AudioChannelSet out)
        {
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add(in);
            l.outputBuses.add(out);
            return l;
        };
        check(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo()))
                  && p.isBusesLayoutSupported(layout(juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo()))
                  && !p.isBusesLayoutSupported(layout(juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono())),
              "layouts: mono o estéreo hacia estéreo; no salida mono");
    }

    // ================= Modo móvil =================
    {
        // El promedio de la copia queda en el tono original (va y viene alrededor).
        auto p = make({ { ParamIDs::mode, 1.0f }, { ParamIDs::detune, 20.0f }, { ParamIDs::spread, 100.0f },
                        { ParamIDs::offset, 0.0f }, { ParamIDs::delay, 14.0f } });
        const auto x = sine(1000.0, 0.3f, 6.0);
        const auto o = run(*p, x);

        // Copia = salida - seca (la seca sale a norm = 0.7071 de la entrada con Mix 100 %)
        std::vector<float> copy(o.l.size());
        for (size_t i = 0; i < copy.size(); ++i)
            copy[i] = o.l[i] - 0.70710678f * x[i];
        const double e = rms(copy, (int) (1.0 * sr), (int) (6.0 * sr));
        check(e > 0.05, "Drift: la copia suena", juce::String(e, 4));

        // Frecuencia instantánea media por cruces por cero de la copia
        int crossings = 0;
        const int a = (int) (1.0 * sr), b = (int) (6.0 * sr);
        for (int i = a + 1; i < b; ++i)
            if (copy[(size_t) (i - 1)] <= 0.0f && copy[(size_t) i] > 0.0f)
                ++crossings;
        const double meanFreq = (double) crossings / ((double) (b - a) / sr);
        check(std::abs(cents(meanFreq, 1000.0)) < 3.0, "Drift: en promedio la copia queda en el tono original",
              juce::String(meanFreq, 2) + " Hz");
    }
    {
        // Pasar de Static a Drift en caliente no hace saltos.
        auto p = make({ { ParamIDs::mode, 0.0f }, { ParamIDs::detune, 15.0f } });
        const auto x = sine(200.0, 0.3f, 1.0);
        auto first = run(*p, std::vector<float>(x.begin(), x.begin() + 24000));
        setParam(*p, ParamIDs::mode, 1.0f);
        auto second = run(*p, std::vector<float>(x.begin() + 24000, x.end()));
        first.l.insert(first.l.end(), second.l.begin(), second.l.end());

        float maxStep = 0.0f;
        for (size_t i = 1; i < first.l.size(); ++i)
            maxStep = std::max(maxStep, std::abs(first.l[i] - first.l[i - 1]));
        check(maxStep < 0.06f, "cambiar de modo en caliente no hace clicks", juce::String(maxStep, 4));
    }

    // ================= Robustez =================
    {
        // Parámetros en los extremos: salida finita y acotada.
        bool finite = true;
        float peak = 0.0f;
        const float detunes[] = { 0.0f, 50.0f };
        const float delays[] = { 3.0f, 40.0f };
        const float offsets[] = { 0.0f, 25.0f };
        for (float d : detunes)
            for (float dl : delays)
                for (float of : offsets)
                    for (int mode = 0; mode < 2; ++mode)
                    {
                        auto p = make({ { ParamIDs::detune, d }, { ParamIDs::delay, dl }, { ParamIDs::offset, of },
                                        { ParamIDs::mode, (float) mode }, { ParamIDs::spread, 100.0f },
                                        { ParamIDs::space, 100.0f }, { ParamIDs::mix, 100.0f },
                                        { ParamIDs::lowCut, 20.0f }, { ParamIDs::highCut, 20000.0f } });
                        const auto o = run(*p, noise(0.5f, 0.7));
                        for (size_t i = 0; i < o.l.size(); ++i)
                        {
                            finite = finite && std::isfinite(o.l[i]) && std::isfinite(o.r[i]);
                            peak = std::max(peak, std::max(std::abs(o.l[i]), std::abs(o.r[i])));
                        }
                    }
        check(finite && peak < 3.0f, "parámetros extremos: la salida es finita y acotada", juce::String(peak, 2));
    }
    {
        // Cambios bruscos de parámetros mientras suena
        auto p = make();
        const auto x = noise(0.3f, 2.0);
        bool finite = true;
        juce::Random rng(5);
        for (int start = 0; start < (int) x.size(); start += blockSize)
        {
            if ((start / blockSize) % 5 == 0)
            {
                setParam(*p, ParamIDs::detune, rng.nextFloat() * 50.0f);
                setParam(*p, ParamIDs::delay, 3.0f + rng.nextFloat() * 37.0f);
                setParam(*p, ParamIDs::offset, rng.nextFloat() * 25.0f);
                setParam(*p, ParamIDs::lowCut, 20.0f + rng.nextFloat() * 580.0f);
            }
            const int n = std::min(blockSize, (int) x.size() - start);
            juce::AudioBuffer<float> block(2, n);
            for (int i = 0; i < n; ++i)
            {
                block.setSample(0, i, x[(size_t) (start + i)]);
                block.setSample(1, i, x[(size_t) (start + i)]);
            }
            juce::MidiBuffer midi;
            p->processBlock(block, midi);
            for (int i = 0; i < n; ++i)
                finite = finite && std::isfinite(block.getSample(0, i)) && std::abs(block.getSample(0, i)) < 3.0f;
        }
        check(finite, "mover los knobs a lo loco mientras suena: sin NaN ni explosiones");
    }
    {
        // Silencio después del audio: se apaga sola (sin ruido residual).
        auto p = make({ { ParamIDs::space, 100.0f } });
        auto x = noise(0.3f, 0.5);
        x.resize((size_t) (3.0 * sr), 0.0f);
        const auto o = run(*p, x);
        const double tail = rms(o.l, (int) (2.0 * sr), (int) (3.0 * sr));
        check(tail < 1.0e-6, "después del audio no queda nada sonando", juce::String(tail, 9));
    }

    // ================= Estado y rendimiento =================
    {
        auto p = make({ { ParamIDs::detune, 33.0f }, { ParamIDs::delay, 22.5f }, { ParamIDs::mode, 0.0f },
                        { ParamIDs::spread, 80.0f }, { ParamIDs::monoCheck, 1.0f } });
        juce::MemoryBlock state;
        p->getStateInformation(state);

        auto q = std::make_unique<DecimulFlankProcessor>();
        q->setStateInformation(state.getData(), (int) state.getSize());
        auto get = [&](const juce::String& id) { return q->apvts.getRawParameterValue(id)->load(); };
        check(std::abs(get(ParamIDs::detune) - 33.0f) < 0.01f && std::abs(get(ParamIDs::delay) - 22.5f) < 0.01f
                  && (int) get(ParamIDs::mode) == 0 && std::abs(get(ParamIDs::spread) - 80.0f) < 0.01f
                  && get(ParamIDs::monoCheck) > 0.5f,
              "estado: los parámetros se guardan y se restauran");
    }
    {
        auto p = make({ { ParamIDs::space, 50.0f }, { ParamIDs::mode, 1.0f } });
        const auto x = noise(0.3f, 10.0);
        const auto t0 = std::chrono::steady_clock::now();
        run(*p, x);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(secs < 3.0, "rendimiento: 10 s de audio estéreo en poco tiempo",
              juce::String(secs, 2) + " s (" + juce::String(10.0 / secs, 0) + "x tiempo real)");
    }

    std::cout << (failures == 0 ? "\nTODO OK" : (juce::String("\nHAY FALLAS: ") + juce::String(failures)).toRawUTF8()) << std::endl;
    return failures == 0 ? 0 : 1;
}
