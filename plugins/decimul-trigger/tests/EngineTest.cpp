// Test de integración del trigger (nombre de trabajo): golpes sintéticos con
// amplitudes conocidas por processBlock, verificando el MIDI que sale (nota, velocity,
// tiempo), el anti-rebote, la alineación y el reproductor de muestras.
//
// Compilar con: cmake -DDECIMU_TRIGGER_TESTS=ON y correr TriggerEngineTest.exe.
// Con `--screenshot ruta.png` arma una escena de demostración de la UI y la guarda.

#include "../Source/PluginProcessor.h"
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

    struct MidiEvent
    {
        int sample = 0;
        bool noteOn = false;
        int note = 0, velocity = 0, channel = 0;
    };

    struct Capture
    {
        std::vector<float> l, r;
        std::vector<MidiEvent> events;

        std::vector<MidiEvent> noteOns() const
        {
            std::vector<MidiEvent> v;
            for (auto& e : events)
                if (e.noteOn)
                    v.push_back(e);
            return v;
        }
    };

    void setParam(DecimulTriggerProcessor& p, const juce::String& id, float plain)
    {
        auto* param = p.apvts.getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    }

    std::unique_ptr<DecimulTriggerProcessor> makeProcessor()
    {
        auto p = std::make_unique<DecimulTriggerProcessor>();
        p->setRateAndBufferSizeDetails(sr, blockSize);
        p->prepareToPlay(sr, blockSize);
        setParam(*p, ParamIDs::threshold, -40.0f);
        return p;
    }

    // Golpe sintético: seno de `freq` con decaimiento exponencial `tau`.
    void addHit(std::vector<float>& x, double atSeconds, float peakDb, double freq = 1000.0,
                double tau = 0.02, double durSeconds = 0.15)
    {
        const int start = (int) (atSeconds * sr);
        const int len = (int) (durSeconds * sr);
        const float amp = juce::Decibels::decibelsToGain(peakDb);
        for (int i = 0; i < len && start + i < (int) x.size(); ++i)
        {
            const double t = (double) i / sr;
            x[(size_t) (start + i)] += amp * (float) (std::exp(-t / tau) * std::sin(2.0 * pi * freq * t));
        }
    }

    Capture run(DecimulTriggerProcessor& p, const std::vector<float>& input)
    {
        Capture cap;
        const int total = (int) input.size();

        for (int start = 0; start < total; start += blockSize)
        {
            const int n = std::min(blockSize, total - start);
            juce::AudioBuffer<float> block(2, n);
            for (int i = 0; i < n; ++i)
            {
                block.setSample(0, i, input[(size_t) (start + i)]);
                block.setSample(1, i, input[(size_t) (start + i)]);
            }

            juce::MidiBuffer midi;
            p.processBlock(block, midi);

            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn() || m.isNoteOff())
                    cap.events.push_back({ start + meta.samplePosition, m.isNoteOn(), m.getNoteNumber(),
                                           m.getVelocity(), m.getChannel() });
            }

            for (int i = 0; i < n; ++i)
            {
                cap.l.push_back(block.getSample(0, i));
                cap.r.push_back(block.getSample(1, i));
            }
        }
        return cap;
    }

    float toneAmplitude(const std::vector<float>& x, int start, int len, double freq)
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < len && start + i < (int) x.size(); ++i)
        {
            const double w = 2.0 * pi * freq * (double) (start + i) / sr;
            re += (double) x[(size_t) (start + i)] * std::cos(w);
            im += (double) x[(size_t) (start + i)] * std::sin(w);
        }
        return (float) (2.0 / (double) len * std::sqrt(re * re + im * im));
    }

    float peakIn(const std::vector<float>& x, int start, int end)
    {
        float m = 0.0f;
        for (int i = std::max(0, start); i < std::min(end, (int) x.size()); ++i)
            m = std::max(m, std::abs(x[(size_t) i]));
        return m;
    }

    juce::File writeTone(const juce::String& name, double freq, float amp, double seconds, double rate = 48000.0)
    {
        auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
        file.deleteFile();

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), rate, 1u, 24, {}, 0));
        stream.release();

        const int n = (int) (seconds * rate);
        juce::AudioBuffer<float> buf(1, n);
        for (int i = 0; i < n; ++i)
            buf.setSample(0, i, amp * (float) std::sin(2.0 * pi * freq * i / rate));
        writer->writeFromAudioSampleBuffer(buf, 0, n);
        return file;
    }

    bool waitForSamples(DecimulTriggerProcessor& p, int expected)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + 15000;
        while (juce::Time::getMillisecondCounter() < deadline)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
            if (p.getResidentSamples() >= expected)
                return true;
        }
        return false;
    }

    std::vector<float> silence(double seconds) { return std::vector<float>((size_t) (seconds * sr), 0.0f); }
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // ---- Modo captura de la UI ----
    if (argc >= 3 && juce::String(argv[1]) == "--screenshot")
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::threshold, -34.0f);
        setParam(*p, ParamIDs::sampler, 1.0f);
        setParam(*p, ParamIDs::note, 38.0f);

        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        struct Item { const char* name; double freq; };
        const Item items[] = { { "Snare soft.wav", 220.0 }, { "Snare med.wav", 260.0 },
                               { "Snare hard A.wav", 300.0 }, { "Snare hard B.wav", 330.0 } };
        for (int i = 0; i < 4; ++i)
        {
            const auto f = writeTone(items[i].name, items[i].freq, 0.5f, 0.2);
            p->loadSample(i, f);
        }
        waitForSamples(*p, 4);
        p->setVelocityRange(0, 1, 50);
        p->setVelocityRange(1, 51, 90);
        p->setVelocityRange(2, 91, 127);
        p->setVelocityRange(3, 91, 127);

        // Una frase de caja: negras con acentos, un par de ghost notes y un flam.
        auto x = silence(3.6);
        const double times[] = { 0.10, 0.50, 0.75, 0.90, 1.30, 1.50, 1.70, 1.90, 2.30, 2.50, 2.60, 3.10 };
        const float levels[] = { -8, -16, -26, -12, -7, -20, -28, -9, -14, -24, -10, -6 };
        for (int i = 0; i < 12; ++i)
            addHit(x, times[i], levels[i], 220.0 + 15.0 * (i % 4), 0.025);

        // El editor se abre antes de tocar: al abrirse descarta lo que había en la cola.
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        editor->setSize(1100, 790);
        run(*p, x);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(600);

        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        const juce::File out(argv[2]);
        out.deleteFile();
        juce::PNGImageFormat png;
        juce::FileOutputStream stream(out);
        return (stream.openedOk() && png.writeImageToStream(image, stream)) ? 0 : 1;
    }

    // ================= Detección y velocity =================
    {
        auto p = makeProcessor();
        auto x = silence(1.6);
        const double times[] = { 0.2, 0.5, 0.8, 1.1 };
        const float levels[] = { -6.0f, -14.0f, -22.0f, -32.0f };
        for (int i = 0; i < 4; ++i)
            addHit(x, times[i], levels[i]);

        const auto cap = run(*p, x);
        const auto ons = cap.noteOns();

        check(ons.size() == 4, "detecta los 4 golpes", juce::String((int) ons.size()));
        if (ons.size() == 4)
        {
            check(ons[0].velocity == 127, "-6 dB = velocity máxima", juce::String(ons[0].velocity));
            check(std::abs(ons[1].velocity - 102) <= 4, "-14 dB ~ 102", juce::String(ons[1].velocity));
            check(std::abs(ons[2].velocity - 77) <= 4, "-22 dB ~ 77", juce::String(ons[2].velocity));
            check(std::abs(ons[3].velocity - 45) <= 5, "-32 dB ~ 45", juce::String(ons[3].velocity));
            check(ons[0].velocity > ons[1].velocity && ons[1].velocity > ons[2].velocity && ons[2].velocity > ons[3].velocity,
                  "la velocity sigue a la intensidad (monótona)");

            const int onset = (int) (0.2 * sr);
            check(ons[0].sample >= onset && ons[0].sample <= onset + (int) (0.006 * sr),
                  "el MIDI sale a los pocos ms del ataque",
                  juce::String((ons[0].sample - onset) * 1000.0 / sr, 2) + " ms");
            check(ons[0].note == 38 && ons[0].channel == 10, "nota 38 en el canal 10 por defecto",
                  juce::String(ons[0].note) + " ch " + juce::String(ons[0].channel));
        }

        int offs = 0;
        for (auto& e : cap.events)
            if (!e.noteOn)
                ++offs;
        check(offs == 4, "cada nota tiene su note-off", juce::String(offs));
    }

    // ================= Umbral =================
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::threshold, -20.0f);
        auto x = silence(1.4);
        addHit(x, 0.2, -10.0f);
        addHit(x, 0.5, -22.0f);
        addHit(x, 0.8, -32.0f);
        const auto ons = run(*p, x).noteOns();
        check(ons.size() == 1, "el umbral deja afuera los golpes más suaves", juce::String((int) ons.size()));
    }

    // ================= Cola larga y rebotes =================
    {
        auto p = makeProcessor();
        auto x = silence(1.2);
        // Redoblante con ring: cola larga y modulada, no tiene que retriguerear.
        const int start = (int) (0.2 * sr);
        const float amp = juce::Decibels::decibelsToGain(-8.0f);
        for (int i = 0; i < (int) (0.6 * sr); ++i)
        {
            const double t = (double) i / sr;
            const double ring = 1.0 + 0.6 * std::sin(2.0 * pi * 35.0 * t);
            x[(size_t) (start + i)] += amp * (float) (std::exp(-t / 0.10) * ring * std::sin(2.0 * pi * 1000.0 * t));
        }
        const auto ons = run(*p, x).noteOns();
        check(ons.size() == 1, "una cola con ring no retriggerea", juce::String((int) ons.size()));
    }
    {
        auto p = makeProcessor();
        auto x = silence(1.2);
        addHit(x, 0.2, -10.0f);
        addHit(x, 0.2 + 0.015, -10.0f);      // a 15 ms: dentro del bloqueo
        addHit(x, 0.6, -10.0f);
        addHit(x, 0.6 + 0.070, -4.0f, 1000.0, 0.02); // a 70 ms y más fuerte: es otro golpe (roll)
        const auto ons = run(*p, x).noteOns();
        check(ons.size() == 3, "un golpe a 15 ms se fusiona, uno a 70 ms y más fuerte cuenta", juce::String((int) ons.size()));
    }

    // ================= Velocity fija, rango y nota =================
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::fixedVel, 1.0f);
        setParam(*p, ParamIDs::maxVel, 100.0f);
        setParam(*p, ParamIDs::note, 36.0f);
        setParam(*p, ParamIDs::channel, 1.0f);
        auto x = silence(1.0);
        addHit(x, 0.2, -6.0f);
        addHit(x, 0.5, -30.0f);
        const auto ons = run(*p, x).noteOns();
        check(ons.size() == 2 && ons[0].velocity == 100 && ons[1].velocity == 100,
              "velocity fija: todos salen con la máxima configurada");
        check(!ons.empty() && ons[0].note == 36 && ons[0].channel == 1, "nota y canal configurables");
    }
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::minVel, 50.0f);
        setParam(*p, ParamIDs::maxVel, 90.0f);
        auto x = silence(1.0);
        addHit(x, 0.2, -6.0f);
        addHit(x, 0.5, -36.0f);
        const auto ons = run(*p, x).noteOns();
        check(ons.size() == 2 && ons[0].velocity == 90 && ons[1].velocity >= 50 && ons[1].velocity < 62,
              "rango de velocity min/max", ons.size() == 2 ? (juce::String(ons[0].velocity) + " / " + juce::String(ons[1].velocity)) : juce::String());
    }

    // ================= Alinear el original =================
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::align, 1.0f);
        setParam(*p, ParamIDs::scan, 4.0f);
        auto x = silence(0.8);
        addHit(x, 0.2, -6.0f);
        const auto cap = run(*p, x);

        // La latencia se informa al host desde el message thread (timer de 100 ms).
        juce::MessageManager::getInstance()->runDispatchLoopUntil(400);
        check(p->getLatencySamples() == (int) std::lround(0.004 * sr), "reporta como latencia el tiempo de medición",
              juce::String(p->getLatencySamples()));

        // El audio sale retrasado por la latencia: el primer pico grande va 192 muestras después.
        auto firstAbove = [](const std::vector<float>& v, float th) {
            for (size_t i = 0; i < v.size(); ++i)
                if (std::abs(v[i]) > th) return (int) i;
            return -1;
        };
        const int inOnset = firstAbove(x, 0.05f);
        const int outOnset = firstAbove(cap.l, 0.05f);
        check(outOnset - inOnset == p->getLatencySamples(), "el audio original sale retrasado esa cantidad",
              juce::String(outOnset - inOnset));
    }

    // ================= Reproductor propio =================
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::sampler, 2.0f);       // Reemplazar
        setParam(*p, ParamIDs::velSens, 100.0f);
        p->loadSample(0, writeTone("trg_test_a.wav", 440.0, 0.5f, 0.3));
        check(waitForSamples(*p, 1), "carga de una muestra");

        auto x = silence(1.0);
        addHit(x, 0.3, -6.0f, 1000.0);               // golpe original a 1 kHz
        const auto cap = run(*p, x);

        const int at = (int) (0.3 * sr) + 800;
        const float sampleTone = toneAmplitude(cap.l, at, 2400, 440.0);
        const float originalTone = toneAmplitude(cap.l, (int) (0.3 * sr), 1200, 1000.0);
        check(std::abs(sampleTone - 0.5f) < 0.05f, "Reemplazar: suena la muestra (velocity 127 = ganancia completa)",
              juce::String(sampleTone, 3));
        check(originalTone < 0.02f, "Reemplazar: el audio original queda silenciado", juce::String(originalTone, 4));
        check(peakIn(cap.l, 0, (int) (0.25 * sr)) < 1e-6f, "sin golpes no suena nada");
    }
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::sampler, 1.0f);       // Reforzar
        setParam(*p, ParamIDs::origLevel, 0.0f);
        setParam(*p, ParamIDs::velSens, 100.0f);
        p->loadSample(0, writeTone("trg_test_a.wav", 440.0, 0.5f, 0.3));
        waitForSamples(*p, 1);

        auto x = silence(1.0);
        addHit(x, 0.3, -6.0f, 1000.0, 0.05);
        const auto cap = run(*p, x);
        const float sampleTone = toneAmplitude(cap.l, (int) (0.3 * sr) + 1200, 2400, 440.0);
        const float originalTone = toneAmplitude(cap.l, (int) (0.3 * sr) + 200, 1200, 1000.0);
        check(sampleTone > 0.4f && originalTone > 0.2f, "Reforzar: suenan la muestra y el original juntos",
              juce::String(sampleTone, 3) + " / " + juce::String(originalTone, 3));
    }

    // ================= Capas por velocity y round-robin =================
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::sampler, 2.0f);
        setParam(*p, ParamIDs::velSens, 0.0f);
        p->loadSample(0, writeTone("trg_soft.wav", 500.0, 0.5f, 0.25));
        p->loadSample(1, writeTone("trg_hard.wav", 2000.0, 0.5f, 0.25));
        waitForSamples(*p, 2);
        p->setVelocityRange(0, 1, 64);
        p->setVelocityRange(1, 65, 127);

        auto x = silence(1.2);
        addHit(x, 0.2, -4.0f, 3000.0);               // fuerte: capa 2 kHz
        addHit(x, 0.6, -33.0f, 3000.0);              // suave: capa 500 Hz
        const auto cap = run(*p, x);

        const int a = (int) (0.2 * sr) + 1000, b = (int) (0.6 * sr) + 1000;
        check(toneAmplitude(cap.l, a, 2400, 2000.0) > 0.4f && toneAmplitude(cap.l, a, 2400, 500.0) < 0.05f,
              "golpe fuerte: dispara la capa de velocity alta");
        check(toneAmplitude(cap.l, b, 2400, 500.0) > 0.4f && toneAmplitude(cap.l, b, 2400, 2000.0) < 0.05f,
              "golpe suave: dispara la capa de velocity baja");
    }
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::sampler, 2.0f);
        setParam(*p, ParamIDs::velSens, 0.0f);
        p->loadSample(0, writeTone("trg_rr_a.wav", 400.0, 0.5f, 0.2));
        p->loadSample(1, writeTone("trg_rr_b.wav", 800.0, 0.5f, 0.2));
        waitForSamples(*p, 2);

        auto x = silence(2.0);
        for (int i = 0; i < 4; ++i)
            addHit(x, 0.2 + 0.4 * i, -8.0f, 3000.0);
        const auto cap = run(*p, x);

        int seq[4];
        for (int i = 0; i < 4; ++i)
        {
            const int at = (int) ((0.2 + 0.4 * i) * sr) + 1000;
            seq[i] = toneAmplitude(cap.l, at, 2400, 400.0) > toneAmplitude(cap.l, at, 2400, 800.0) ? 0 : 1;
        }
        check(seq[0] != seq[1] && seq[0] == seq[2] && seq[1] == seq[3], "round-robin: las dos muestras se turnan",
              juce::String(seq[0]) + juce::String(seq[1]) + juce::String(seq[2]) + juce::String(seq[3]));
    }

    // ================= Cortar el anterior =================
    {
        auto runChoke = [](bool choke)
        {
            auto p = makeProcessor();
            setParam(*p, ParamIDs::sampler, 2.0f);
            setParam(*p, ParamIDs::velSens, 0.0f);
            setParam(*p, ParamIDs::choke, choke ? 1.0f : 0.0f);
            p->loadSample(0, writeTone("trg_long.wav", 700.0, 0.5f, 1.0));
            waitForSamples(*p, 1);

            auto x = silence(1.2);
            addHit(x, 0.2, -8.0f, 3000.0);
            addHit(x, 0.5, -8.0f, 3000.0);
            const auto cap = run(*p, x);
            // Con dos voces sonando juntas el nivel pasa de 0.5 a ~1.0 después del segundo golpe.
            return peakIn(cap.l, (int) (0.55 * sr), (int) (0.75 * sr));
        };
        const float withoutChoke = runChoke(false);
        const float withChoke = runChoke(true);
        check(withoutChoke > 0.8f && withChoke < 0.6f, "cortar anterior: la muestra previa se apaga",
              juce::String(withoutChoke, 3) + " vs " + juce::String(withChoke, 3));
    }

    // ================= Escuchar la detección y bypass =================
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::listen, 1.0f);
        setParam(*p, ParamIDs::lowCut, 500.0f);      // 100 Hz queda muy atenuado, 2 kHz pasa
        auto x = silence(0.6);
        for (int i = 0; i < (int) x.size(); ++i)
            x[(size_t) i] = 0.3f * (float) std::sin(2.0 * pi * 100.0 * i / sr) + 0.3f * (float) std::sin(2.0 * pi * 2000.0 * i / sr);
        const auto cap = run(*p, x);
        const float low = toneAmplitude(cap.l, 12000, 4800, 100.0);
        const float high = toneAmplitude(cap.l, 12000, 4800, 2000.0);
        check(high > 0.25f && low < 0.05f, "oír detección: se escucha la señal ya filtrada",
              juce::String(low, 3) + " / " + juce::String(high, 3));
    }
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::bypass, 1.0f);
        auto x = silence(0.8);
        addHit(x, 0.2, -6.0f);
        const auto cap = run(*p, x);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < x.size(); ++i)
            maxDiff = std::max(maxDiff, std::abs(cap.l[i] - x[i]));
        check(cap.events.empty() && maxDiff < 1e-6f, "bypass: el audio pasa intacto y no sale MIDI");
    }

    // ================= Estado =================
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::note, 40.0f);
        setParam(*p, ParamIDs::sampler, 1.0f);
        const auto file = writeTone("trg_state.wav", 600.0, 0.4f, 0.2);
        p->loadSample(2, file);
        waitForSamples(*p, 1);
        p->setVelocityRange(2, 30, 80);

        juce::MemoryBlock state;
        p->getStateInformation(state);

        auto p2 = makeProcessor();
        p2->setStateInformation(state.getData(), (int) state.getSize());
        waitForSamples(*p2, 1);

        int lo = 0, hi = 0;
        p2->getVelocityRange(2, lo, hi);
        check((int) p2->apvts.getRawParameterValue(ParamIDs::note)->load() == 40, "estado: los parámetros se restauran");
        check(p2->getResidentSamples() == 1 && lo == 30 && hi == 80, "estado: la muestra y su rango se recargan",
              juce::String(lo) + "-" + juce::String(hi));
    }

    std::cout << (failures == 0 ? "\nTODO OK" : (juce::String("\nHAY FALLAS: ") + juce::String(failures)).toRawUTF8()) << std::endl;
    return failures == 0 ? 0 : 1;
}
