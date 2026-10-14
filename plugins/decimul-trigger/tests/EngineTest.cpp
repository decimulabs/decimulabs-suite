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

    // "Instrumentos" sintéticos con distinta forma espectral, para probar la huella.
    enum class Kind { snare, hat, kick };

    void addDrum(std::vector<float>& x, double atSeconds, float peakDb, Kind kind)
    {
        std::vector<double> freqs;
        double tau = 0.03;
        switch (kind)
        {
            case Kind::snare: freqs = { 190.0, 420.0, 900.0, 2400.0, 5200.0 }; tau = 0.05; break;   // cuerpo + chasquido
            case Kind::hat:   freqs = { 6800.0, 8600.0, 10400.0 };              tau = 0.02; break;   // solo agudos
            case Kind::kick:  freqs = { 85.0, 110.0, 140.0 };                   tau = 0.07; break;   // solo graves
        }

        const int start = (int) (atSeconds * sr);
        const int len = (int) (0.25 * sr);
        const float amp = juce::Decibels::decibelsToGain(peakDb) / (float) freqs.size() * 1.6f;
        for (int i = 0; i < len && start + i < (int) x.size(); ++i)
        {
            const double t = (double) i / sr;
            double v = 0.0;
            for (double f : freqs)
                v += std::sin(2.0 * pi * f * t);
            x[(size_t) (start + i)] += amp * (float) (std::exp(-t / tau) * v);
        }
    }
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

        // El editor se abre antes de tocar: al abrirse descarta lo que había en la cola.
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        editor->setSize(1100, 850);

        // 1) Aprende la huella de la caja (8 golpes solos).
        p->startLearning();
        auto learn = silence(2.8);
        for (int i = 0; i < 8; ++i)
            addDrum(learn, 0.15 + 0.3 * i, -9.0f - 1.5f * (float) (i % 3), Kind::snare);
        run(*p, learn);
        p->stopLearning();
        run(*p, silence(0.1));

        // 2) Frase con bleed de hi-hat entre los golpes de caja; rechaza el bleed y graba la caja.
        setParam(*p, ParamIDs::fingerprint, 1.0f);
        p->setCapturing(true);
        auto x = silence(3.6);
        const double times[] = { 0.10, 0.30, 0.50, 0.75, 0.90, 1.10, 1.30, 1.50, 1.70, 1.90, 2.30, 2.50, 2.60, 2.90, 3.10 };
        const float levels[] = { -8, -14, -16, -26, -12, -15, -7, -20, -28, -9, -14, -24, -10, -16, -6 };
        const Kind kinds[] = { Kind::snare, Kind::hat, Kind::snare, Kind::snare, Kind::snare, Kind::hat, Kind::snare,
                               Kind::snare, Kind::snare, Kind::snare, Kind::snare, Kind::snare, Kind::snare,
                               Kind::hat, Kind::snare };
        for (int i = 0; i < 15; ++i)
            addDrum(x, times[i], levels[i], kinds[i]);
        run(*p, x);
        p->setCapturing(false);
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

    // ================= Huella: rechazar bleed (v0.3) =================
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::fingerprint, 1.0f);
        setParam(*p, ParamIDs::match, 0.80f);

        // 1) Aprender: 8 golpes de "caja", con distintos niveles.
        p->startLearning();
        auto learn = silence(3.0);
        for (int i = 0; i < 8; ++i)
            addDrum(learn, 0.2 + 0.3 * i, -8.0f - 2.0f * (float) (i % 4), Kind::snare);
        run(*p, learn);
        p->stopLearning();
        run(*p, silence(0.05));

        check(p->hasFingerprint() && p->getFingerprintHits() == 8, "aprende la huella de 8 golpes",
              juce::String(p->getFingerprintHits()));

        // 2) Tocar mezclado: 3 cajas, 3 hi-hats, 3 bombos. Solo las cajas deben pasar.
        auto mixed = silence(4.0);
        const Kind order[] = { Kind::snare, Kind::hat, Kind::kick, Kind::snare, Kind::hat, Kind::kick,
                               Kind::snare, Kind::hat, Kind::kick };
        for (int i = 0; i < 9; ++i)
            addDrum(mixed, 0.2 + 0.4 * i, -10.0f, order[i]);

        const int rejectedBefore = p->rejectedCount.load();
        const auto cap = run(*p, mixed);
        const auto ons = cap.noteOns();
        check(ons.size() == 3, "con huella: pasan solo los 3 golpes de caja", juce::String((int) ons.size()));
        check(p->rejectedCount.load() - rejectedBefore == 6, "con huella: rechaza los 6 de bleed (hi-hat y bombo)",
              juce::String(p->rejectedCount.load() - rejectedBefore));

        // La cola de un hi-hat sobre la caja no la retriguerea ni la confunde.
        FingerprintBank::Vector tpl {}, last {};
        p->getFingerprint(tpl);
        check(p->getLastHitVector(last), "se conoce la forma del último golpe");

        // 3) Sin rechazo, los 9 pasan.
        setParam(*p, ParamIDs::fingerprint, 0.0f);
        const auto all = run(*p, mixed).noteOns();
        check(all.size() == 9, "sin rechazo pasan los 9 golpes", juce::String((int) all.size()));

        // 4) Otro nivel de golpe del mismo instrumento sigue pasando (la huella es forma, no volumen).
        setParam(*p, ParamIDs::fingerprint, 1.0f);
        auto quiet = silence(1.0);
        addDrum(quiet, 0.2, -30.0f, Kind::snare);
        const auto quietOns = run(*p, quiet).noteOns();
        check(quietOns.size() == 1, "un golpe de caja suave también pasa (forma, no volumen)",
              juce::String((int) quietOns.size()));

        // 5) Estado: la huella se guarda y restaura.
        juce::MemoryBlock state;
        p->getStateInformation(state);
        auto p2 = makeProcessor();
        p2->setStateInformation(state.getData(), (int) state.getSize());
        FingerprintBank::Vector tpl2 {};
        p2->getFingerprint(tpl2);
        float diff = 0.0f;
        for (size_t k = 0; k < tpl.size(); ++k)
            diff = std::max(diff, std::abs(tpl[k] - tpl2[k]));
        check(p2->hasFingerprint() && diff < 1e-4f, "estado: la huella sobrevive al guardado", juce::String(diff, 6));

        // 6) Borrar la huella.
        p->clearFingerprint();
        run(*p, silence(0.05));
        check(!p->hasFingerprint(), "borrar huella");
    }
    {
        // La ventana de análisis se suma a la latencia cuando está en modo alinear.
        auto p = makeProcessor();
        setParam(*p, ParamIDs::align, 1.0f);
        setParam(*p, ParamIDs::fingerprint, 1.0f);
        setParam(*p, ParamIDs::fpWindow, 12.0f);
        run(*p, silence(0.2));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(400);
        check(p->getLatencySamples() == (int) std::lround(0.012 * sr), "con huella la latencia es la ventana de análisis",
              juce::String(p->getLatencySamples()));
    }

    // ================= Captura de muestras (v0.4) =================
    {
        auto p = makeProcessor();
        const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("trg_capture_test");
        root.deleteRecursively();

        p->setCapturing(true);
        auto x = silence(4.2);
        const float levels[] = { -4.0f, -12.0f, -20.0f, -8.0f, -28.0f, -16.0f };
        for (int i = 0; i < 6; ++i)
            addHit(x, 0.3 + 0.6 * i, levels[i], 900.0, 0.03, 0.4);
        run(*p, x);
        p->setCapturing(false);

        check(p->getTakeCount() == 6, "captura: graba los 6 golpes", juce::String(p->getTakeCount()));

        std::vector<int> vels;
        p->getTakeVelocities(vels);
        bool orderOk = vels.size() == 6 && vels[0] > vels[3] && vels[3] > vels[1] && vels[1] > vels[5]
                    && vels[5] > vels[2] && vels[2] > vels[4];
        check(orderOk, "captura: la velocity de cada toma sigue su intensidad");

        const auto result = p->exportTakes("Caja", 3, 2, root);
        check(result.files == 6, "exportar: 3 capas x 2 alternancias = 6 archivos", result.message);
        check(waitForSamples(*p, 6), "exportar: las 6 muestras quedan cargadas en el reproductor");

        // Rangos: cubren 1..127 sin huecos ni solapes
        std::vector<std::pair<int, int>> ranges;
        for (int s = 0; s < 6; ++s)
        {
            int lo = 0, hi = 0;
            p->getVelocityRange(s, lo, hi);
            ranges.emplace_back(lo, hi);
        }
        std::sort(ranges.begin(), ranges.end());
        bool tiled = ranges.front().first == 1 && ranges.back().second == 127;
        for (size_t i = 1; i < ranges.size(); ++i)
            if (ranges[i].first != ranges[i - 1].first && ranges[i].first != ranges[i - 1].second + 1)
                tiled = false;
        check(tiled, "exportar: los rangos de velocity cubren 1-127 sin huecos",
              juce::String(ranges.front().first) + "-" + juce::String(ranges.back().second));

        // El ataque queda 3 ms después del principio del archivo (pre-roll)
        auto files = result.folder.findChildFiles(juce::File::findFiles, false, "*.wav");
        check(files.size() == 6, "exportar: 6 archivos WAV en la carpeta", juce::String(files.size()));
        if (!files.isEmpty())
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(files[0]));
            if (reader != nullptr)
            {
                juce::AudioBuffer<float> b((int) reader->numChannels, (int) reader->lengthInSamples);
                reader->read(&b, 0, (int) reader->lengthInSamples, 0, true, true);
                int first = -1;
                for (int i = 0; i < b.getNumSamples(); ++i)
                    if (std::abs(b.getSample(0, i)) > 0.01f) { first = i; break; }

                const int expected = (int) std::lround(0.003 * sr);
                check(first >= expected - 8 && first <= expected + 24, "exportar: el ataque está a ~3 ms del inicio",
                      juce::String(first) + " (esperado " + juce::String(expected) + ")");
                check(b.getNumSamples() > (int) (0.08 * sr), "exportar: conserva la cola del golpe",
                      juce::String(b.getNumSamples()));
            }
        }

        // Reproductor: los golpes nuevos disparan la capa que corresponde
        setParam(*p, ParamIDs::sampler, 2.0f);
        auto probe = silence(1.0);
        addHit(probe, 0.2, -4.0f, 1500.0, 0.02, 0.2);
        const auto probeCap = run(*p, probe);
        check(peakIn(probeCap.l, (int) (0.2 * sr), (int) (0.45 * sr)) > 0.05f,
              "las muestras creadas suenan al golpear");
        root.deleteRecursively();
    }
    {
        // Un ataque nuevo corta la toma: dos golpes a 80 ms
        auto p = makeProcessor();
        const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("trg_capture_test2");
        root.deleteRecursively();
        p->setCapturing(true);
        auto x = silence(1.4);
        addHit(x, 0.2, -6.0f, 900.0, 0.05, 0.3);
        addHit(x, 0.28, -6.0f, 900.0, 0.05, 0.3);
        addHit(x, 0.9, -6.0f, 900.0, 0.02, 0.3);
        run(*p, x);
        p->setCapturing(false);

        const auto result = p->exportTakes("Corte", 1, 3, root);
        check(p->getTakeCount() == 3 && result.files == 3, "toma cortada por el golpe siguiente: 3 tomas",
              juce::String(p->getTakeCount()) + " / " + juce::String(result.files));

        auto files = result.folder.findChildFiles(juce::File::findFiles, false, "*.wav");
        std::vector<int> lengths;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        for (auto& f : files)
            if (std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(f)); reader != nullptr)
                lengths.push_back((int) reader->lengthInSamples);
        std::sort(lengths.begin(), lengths.end());
        check(!lengths.empty() && lengths.front() < (int) (0.10 * sr),
              "la toma del primer golpe termina donde empieza el segundo",
              lengths.empty() ? juce::String() : juce::String(lengths.front() * 1000.0 / sr, 1) + " ms");
        root.deleteRecursively();
    }
    {
        // Captura + huella: de una mezcla, graba solo la caja
        auto p = makeProcessor();
        setParam(*p, ParamIDs::fingerprint, 1.0f);
        p->startLearning();
        auto learn = silence(2.0);
        for (int i = 0; i < 5; ++i)
            addDrum(learn, 0.2 + 0.3 * i, -8.0f, Kind::snare);
        run(*p, learn);
        p->stopLearning();
        run(*p, silence(0.05));

        p->setCapturing(true);
        auto mixed = silence(3.0);
        for (int i = 0; i < 6; ++i)
            addDrum(mixed, 0.2 + 0.45 * i, -10.0f, i % 2 == 0 ? Kind::snare : Kind::hat);
        run(*p, mixed);
        p->setCapturing(false);
        check(p->getTakeCount() == 3, "captura con huella: solo se graban los golpes de la caja",
              juce::String(p->getTakeCount()));
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
