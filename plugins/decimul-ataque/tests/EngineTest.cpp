// Test del motor de Ataque (modulador de transientes) y del procesador, con señales sintéticas.
// Compilar con: cmake -DDECIMU_ATAQUE_TESTS=ON y correr AtaqueEngineTest.exe.
// Con `--screenshot ruta.png` guarda una captura de la UI con una frase de golpes de ejemplo.

#include "../Source/PluginProcessor.h"
#include <cmath>
#include <iostream>
#include <random>

using ataque::TransientShaper;

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

    double rms(const std::vector<float>& v, int from, int to)
    {
        double s = 0.0;
        for (int i = from; i < to; ++i) s += (double) v[(size_t) i] * v[(size_t) i];
        return std::sqrt(s / (double) std::max(1, to - from));
    }

    int ms(double t) { return (int) std::lround(t * 0.001 * sr); }

    // Golpes: ruido (kind 0) o seno de 180 Hz (kind 1) con caída exponencial.
    std::vector<float> makeHits(int n, const std::vector<double>& timesMs, float peakDb, double tauMs, int kind, unsigned seed = 1)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        std::vector<float> v((size_t) n, 0.0f);
        const float amp = std::pow(10.0f, peakDb / 20.0f);
        for (double t : timesMs)
        {
            const int start = ms(t);
            for (int i = start; i < n; ++i)
            {
                const double env = std::exp(-(double) (i - start) / (tauMs * 0.001 * sr));
                if (env < 1.0e-4) break;
                const float s = kind == 0 ? u(rng) : (float) std::sin(2.0 * juce::MathConstants<double>::pi * 180.0 * (i - start) / sr);
                v[(size_t) i] += amp * (float) env * s;
            }
        }
        return v;
    }

    struct Result { double attackDb, tailDb; };

    // Cuánto cambia (en dB) el RMS de la ventana de ataque (2 a 25 ms) y de la cola (80 a 200 ms) del golpe en t0.
    Result effect(float attack, float sustain, float speed, float peakDb, int kind, double tauMs = 60.0)
    {
        const int n = ms(700.0);
        auto in = makeHits(n, { 100.0 }, peakDb, tauMs, kind);
        auto l = in, r = in;
        TransientShaper s;
        s.prepare(sr);
        s.set(attack, sustain, speed);
        s.process(l.data(), r.data(), n);
        const int t0 = ms(100.0);
        auto win = [&](double a, double b) { return dbOf(rms(l, t0 + ms(a), t0 + ms(b))) - dbOf(rms(in, t0 + ms(a), t0 + ms(b))); };
        return { win(2.0, 25.0), win(80.0, 200.0) };
    }

    void setParam(DecimulAtaqueProcessor& p, const juce::String& id, float plain)
    {
        auto* param = p.apvts.getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    }

    void runStereo(DecimulAtaqueProcessor& p, std::vector<float>& l, std::vector<float>& r)
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

    std::unique_ptr<DecimulAtaqueProcessor> makeProcessor()
    {
        auto p = std::make_unique<DecimulAtaqueProcessor>();
        p->setRateAndBufferSizeDetails(sr, blockSize);
        p->prepareToPlay(sr, blockSize);
        return p;
    }
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // ---- Captura de la UI ----
    if (argc >= 3 && juce::String(argv[1]) == "--screenshot")
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::attack, 55.0f);
        setParam(*p, ParamIDs::sustain, -35.0f);
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        if (const auto shotSize = juce::SystemStats::getEnvironmentVariable("DECIMU_SHOT_SIZE", {}); shotSize.contains("x"))
            editor->setSize(shotSize.upToFirstOccurrenceOf("x", false, false).getIntValue(),
                            shotSize.fromFirstOccurrenceOf("x", false, false).getIntValue());

        const int n = ms(2600.0);
        auto l = makeHits(n, { 100, 520, 940, 1360, 1780, 2200 }, -8.0f, 55.0, 0);
        auto kick = makeHits(n, { 100, 940, 1780 }, -6.0f, 120.0, 1, 5);
        for (size_t i = 0; i < l.size(); ++i) l[i] += kick[i];
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

    // Afinación de tiempos (desarrollo): --tune
    if (argc >= 2 && juce::String(argv[1]) == "--tune")
    {
        const float sets[][5] = { { 0.5f, 8, 25, 180, 0.8f }, { 0.5f, 16, 25, 180, 0.8f }, { 0.5f, 25, 25, 180, 1.0f },
                                  { 0.5f, 35, 25, 220, 1.5f }, { 0.5f, 50, 25, 250, 2.0f }, { 0.3f, 30, 20, 220, 1.0f } };
        for (const auto& t : sets)
        {
            double worstRipple = 0.0;
            for (double f : { 40.0, 60.0, 100.0, 200.0 })
            {
                const int n = ms(1500.0);
                std::vector<float> l((size_t) n), r;
                for (int i = 0; i < n; ++i) l[(size_t) i] = 0.25f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * f * i / sr);
                r = l;
                TransientShaper s;
                s.prepare(sr);
                s.setTimes(t[0], t[1], t[2], t[3], t[4]);
                s.set(1.0f, 1.0f, 0.5f);
                float lo = 1.0e9f, hi = -1.0e9f;
                for (int i = 0; i < n; i += 16)
                {
                    s.process(l.data() + i, r.data() + i, std::min(16, n - i));
                    if (i > ms(500.0)) { lo = std::min(lo, s.getGainDb()); hi = std::max(hi, s.getGainDb()); }
                }
                worstRipple = std::max(worstRipple, (double) (hi - lo));
            }
            // efecto en un golpe de ruido
            const int n = ms(700.0);
            auto in = makeHits(n, { 100.0 }, -6.0f, 60.0, 0);
            double res[4] = {};
            for (int k = 0; k < 4; ++k)
            {
                auto l = in, r = in;
                TransientShaper s;
                s.prepare(sr);
                s.setTimes(t[0], t[1], t[2], t[3], t[4]);
                s.set(k == 0 ? 1.0f : k == 1 ? -1.0f : 0.0f, k == 2 ? 1.0f : k == 3 ? -1.0f : 0.0f, 0.5f);
                s.process(l.data(), r.data(), n);
                const int t0 = ms(100.0);
                const bool att = k < 2;
                const int a0 = att ? ms(2.0) : ms(80.0), b0 = att ? ms(25.0) : ms(200.0);
                res[k] = dbOf(rms(l, t0 + a0, t0 + b0)) - dbOf(rms(in, t0 + a0, t0 + b0));
            }
            std::cout << "tiempos " << t[0] << "/" << t[1] << "/" << t[2] << "/" << t[3] << "/" << t[4] << "  rizado peor " << worstRipple
                      << " dB   ataque +" << res[0] << " / " << res[1] << "   cola " << res[2] << " / " << res[3] << std::endl;
        }
        return 0;
    }

    // ---- Neutro: bit a bit igual ----
    {
        const int n = ms(500.0);
        std::mt19937 rng(3);
        std::uniform_real_distribution<float> u(-0.5f, 0.5f);
        std::vector<float> l((size_t) n), r((size_t) n);
        for (int i = 0; i < n; ++i) { l[(size_t) i] = u(rng); r[(size_t) i] = u(rng); }
        const auto l0 = l, r0 = r;
        TransientShaper s;
        s.prepare(sr);
        s.set(0.0f, 0.0f, 0.5f);
        s.process(l.data(), r.data(), n);
        check(l == l0 && r == r0, "Neutro (Attack 0, Sustain 0): la señal sale idéntica, bit a bit");
    }

    // ---- Cada perilla hace lo que dice (ruido y seno, golpes a -6 dBFS) ----
    for (int kind : { 0, 1 })
    {
        const juce::String name = kind == 0 ? "golpe de ruido" : "golpe de 180 Hz";
        const auto aUp = effect(1.0f, 0.0f, 0.5f, -6.0f, kind), aDn = effect(-1.0f, 0.0f, 0.5f, -6.0f, kind);
        const auto sUp = effect(0.0f, 1.0f, 0.5f, -6.0f, kind), sDn = effect(0.0f, -1.0f, 0.5f, -6.0f, kind);
        std::cout << "       " << name.toRawUTF8() << ": Attack +100 -> ataque " << aUp.attackDb << " / cola " << aUp.tailDb
                  << " ; Attack -100 -> " << aDn.attackDb << " / " << aDn.tailDb << " ; Sustain +100 -> " << sUp.attackDb << " / "
                  << sUp.tailDb << " ; Sustain -100 -> " << sDn.attackDb << " / " << sDn.tailDb << std::endl;
        check(aUp.attackDb >= 3.0 && std::abs(aUp.tailDb) < 2.0, name + ": Attack +100 % sube el ataque y no toca la cola");
        check(aDn.attackDb <= -3.0 && std::abs(aDn.tailDb) < 2.0, name + ": Attack -100 % baja el ataque y no toca la cola");
        check(sUp.tailDb >= 3.0 && std::abs(sUp.attackDb) < 2.0, name + ": Sustain +100 % sube la cola y no toca el ataque");
        check(sDn.tailDb <= -3.0 && std::abs(sDn.attackDb) < 2.0, name + ": Sustain -100 % baja la cola y no toca el ataque");
    }

    // ---- No depende del nivel ----
    {
        const auto loud = effect(1.0f, 0.0f, 0.5f, -6.0f, 0), soft = effect(1.0f, 0.0f, 0.5f, -30.0f, 0);
        const auto loudS = effect(0.0f, 1.0f, 0.5f, -6.0f, 0), softS = effect(0.0f, 1.0f, 0.5f, -30.0f, 0);
        check(std::abs(loud.attackDb - soft.attackDb) < 1.5 && std::abs(loudS.tailDb - softS.tailDb) < 1.5,
              "El efecto es el mismo con golpes suaves (-30 dBFS) y fuertes (-6 dBFS)",
              "ataque " + juce::String(loud.attackDb, 2) + " / " + juce::String(soft.attackDb, 2)
                  + ", cola " + juce::String(loudS.tailDb, 2) + " / " + juce::String(softS.tailDb, 2));
    }

    // ---- Speed cambia la detección ----
    {
        const auto fast = effect(1.0f, 0.0f, 1.0f, -6.0f, 0), slow = effect(1.0f, 0.0f, 0.0f, -6.0f, 0);
        check(std::abs(fast.attackDb - slow.attackDb) > 0.5, "Speed: con 0 % y 100 % el ataque se trata distinto",
              juce::String(slow.attackDb, 2) + " dB (lento) / " + juce::String(fast.attackDb, 2) + " dB (rápido)");
    }

    // ---- Canales ligados ----
    {
        const int n = ms(700.0);
        auto l = makeHits(n, { 100.0 }, -6.0f, 60.0, 0);
        std::vector<float> r((size_t) n);
        for (int i = 0; i < n; ++i) r[(size_t) i] = 0.01f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 700.0 * i / sr); // -40 dBFS
        const auto l0 = l, r0 = r;
        TransientShaper s;
        s.prepare(sr);
        s.set(1.0f, 0.0f, 0.5f);
        s.process(l.data(), r.data(), n);
        const int a = ms(102.0), b = ms(125.0);
        const double gl = dbOf(rms(l, a, b)) - dbOf(rms(l0, a, b)), gr = dbOf(rms(r, a, b)) - dbOf(rms(r0, a, b));
        check(gl > 3.0 && gr > 3.0 && std::abs(gl - gr) < 1.0, "Canales ligados: el canal suave (que solo no se tocaría) recibe la ganancia del que pega",
              "L " + juce::String(gl, 2) + " dB, R " + juce::String(gr, 2) + " dB");
    }

    // ---- Sin rizado en tonos continuos (graves incluidos) ----
    {
        double worst = 0.0;
        double worstFreq = 0.0, worstFast = 0.0;
        for (double f : { 40.0, 60.0, 100.0, 200.0, 1000.0 })
            for (float speed : { 0.0f, 0.5f, 1.0f })
            {
                const int n = ms(1500.0);
                std::vector<float> l((size_t) n), r;
                for (int i = 0; i < n; ++i) l[(size_t) i] = 0.25f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * f * i / sr);
                r = l;
                TransientShaper s;
                s.prepare(sr);
                s.set(1.0f, 1.0f, speed);
                float lo = 1.0e9f, hi = -1.0e9f;
                for (int i = 0; i < n; i += 64)
                {
                    s.process(l.data() + i, r.data() + i, std::min(64, n - i));
                    if (i > ms(500.0)) { lo = std::min(lo, s.getGainDb()); hi = std::max(hi, s.getGainDb()); }
                }
                if (speed <= 0.5f && hi - lo > worst) { worst = hi - lo; worstFreq = f; }
                if (speed > 0.5f) worstFast = std::max(worstFast, (double) (hi - lo));
            }
        check(worst < 1.0 && worstFast < 2.0, "Tono continuo con Attack y Sustain al 100 %: la ganancia casi no se mueve (rizado de graves)",
              juce::String(worst, 2) + " dB pico a pico con Speed hasta 50 % (peor a " + juce::String(worstFreq, 0) + " Hz), " + juce::String(worstFast, 2) + " dB con Speed al 100 %");
    }

    // ---- Ruido de fondo: no se levanta ----
    {
        const int n = ms(1000.0);
        std::mt19937 rng(9);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        std::vector<float> l((size_t) n);
        for (auto& x : l) x = 3.0e-5f * u(rng);   // ~ -90 dBFS
        auto r = l;
        const auto l0 = l;
        TransientShaper s;
        s.prepare(sr);
        s.set(1.0f, 1.0f, 0.5f);
        s.process(l.data(), r.data(), n);
        check(std::abs(dbOf(rms(l, 0, n)) - dbOf(rms(l0, 0, n))) < 0.15, "Debajo de -75 dBFS (ruido de fondo) no se levanta nada");
    }

    // ---- Robustez: parámetros al azar, señal fuerte ----
    {
        const int n = ms(8000.0);
        std::mt19937 rng(5);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        std::vector<float> l((size_t) n), r((size_t) n);
        for (int i = 0; i < n; ++i) { l[(size_t) i] = 0.9f * u(rng); r[(size_t) i] = 0.9f * u(rng); }
        const auto l0 = l;
        TransientShaper s;
        s.prepare(sr);
        float worstGain = 0.0f;
        bool finite = true;
        for (int i = 0; i < n; i += 480)
        {
            s.set(u(rng), u(rng), 0.5f + 0.5f * u(rng));
            s.process(l.data() + i, r.data() + i, std::min(480, n - i));
            worstGain = std::max(worstGain, std::abs(s.getGainDb()));
        }
        for (int i = 0; i < n; ++i)
            if (!std::isfinite(l[(size_t) i]) || std::abs(l[(size_t) i]) > std::abs(l0[(size_t) i]) * 4.2f + 1.0e-6f) finite = false;
        check(finite && worstGain <= TransientShaper::maxGainDb + 0.01f, "8 s de ruido fuerte con parámetros al azar: sin NaN y la ganancia nunca pasa de +12 dB",
              juce::String(worstGain, 2) + " dB");
    }

    // ---- Procesador ----
    {
        auto p = makeProcessor();
        check(p->getLatencySamples() == 0, "Procesador: sin latencia");
        auto l = makeHits(ms(700.0), { 100.0 }, -6.0f, 60.0, 0);
        auto r = l;
        const auto l0 = l;
        setParam(*p, ParamIDs::attack, 100.0f);
        runStereo(*p, l, r);
        const int a = ms(102.0), b = ms(125.0);
        check(dbOf(rms(l, a, b)) - dbOf(rms(l0, a, b)) > 3.0, "Procesador: Attack al 100 % sube el ataque a través de processBlock");

        auto q = makeProcessor();
        setParam(*q, ParamIDs::attack, 100.0f);
        setParam(*q, ParamIDs::bypass, 1.0f);
        auto l2 = makeHits(ms(700.0), { 100.0 }, -6.0f, 60.0, 0);
        auto r2 = l2;
        const auto l20 = l2;
        runStereo(*q, l2, r2);
        double d = 0.0;
        for (size_t i = (size_t) ms(100.0); i < l2.size(); ++i) d = std::max(d, (double) std::abs(l2[i] - l20[i]));
        check(d < 1.0e-6, "Procesador: en bypass la señal pasa intacta", juce::String(d, 8));

        auto t = makeProcessor();
        setParam(*t, ParamIDs::outputTrim, -6.0f);
        auto l3 = makeHits(ms(300.0), { 20.0 }, -10.0f, 60.0, 0);
        auto r3 = l3;
        const auto l30 = l3;
        runStereo(*t, l3, r3);
        const double trim = dbOf(rms(l3, ms(100.0), ms(250.0))) - dbOf(rms(l30, ms(100.0), ms(250.0)));
        check(std::abs(trim + 6.0) < 0.3, "Procesador: Output -6 dB baja 6 dB", juce::String(trim, 2));

        // mono
        auto m = std::make_unique<DecimulAtaqueProcessor>();
        m->setPlayConfigDetails(1, 1, sr, blockSize);
        m->prepareToPlay(sr, blockSize);
        juce::AudioBuffer<float> mb(1, blockSize);
        mb.clear();
        juce::MidiBuffer midi;
        m->processBlock(mb, midi);
        check(m->getTotalNumInputChannels() == 1, "Procesador: acepta pistas mono");

        // estado
        setParam(*p, ParamIDs::sustain, -40.0f);
        juce::MemoryBlock state;
        p->getStateInformation(state);
        auto p2 = makeProcessor();
        p2->setStateInformation(state.getData(), (int) state.getSize());
        check(std::abs(p2->apvts.getRawParameterValue(ParamIDs::attack)->load() - 100.0f) < 0.5f
                  && std::abs(p2->apvts.getRawParameterValue(ParamIDs::sustain)->load() + 40.0f) < 0.5f,
              "Procesador: el estado se guarda y se restaura");

        // cuadros para la pantalla
        DecimulAtaqueProcessor::Frame frames[512];
        const int got = p->readFrames(frames, 512);
        check(got > 100, "Procesador: entrega cuadros de nivel y ganancia a la pantalla", juce::String(got) + " cuadros");
    }

    std::cout << (failures == 0 ? "\nTODO OK" : (juce::String("\nHAY FALLAS: ") + juce::String(failures)).toRawUTF8()) << std::endl;
    return failures == 0 ? 0 : 1;
}
