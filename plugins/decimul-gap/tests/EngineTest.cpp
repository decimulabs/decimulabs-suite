// Test de integración del motor de Gap (sin UI real ni host): un AudioPlayHead falso
// da tempo/posición y se verifica la ganancia que sale de verdad por processBlock.
//
// Compilar con: cmake -DDECIMU_GAP_TESTS=ON y correr GapEngineTest.exe.
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
                  << (detail.isNotEmpty() ? juce::String("  -> ") + detail : juce::String()).toRawUTF8() << std::endl;
        if (!ok)
            ++failures;
    }

    struct TestPlayHead : juce::AudioPlayHead
    {
        double bpm = 120.0;
        double ppq = 0.0;
        bool playing = true;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm(bpm);
            info.setPpqPosition(ppq);
            info.setIsPlaying(playing);
            return info;
        }
    };

    void setParam(DecimulGapProcessor& p, const juce::String& id, float plain)
    {
        auto* param = p.apvts.getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    }

    struct Capture
    {
        std::vector<float> l, r;
        std::vector<float> uiGainPerBlock;
    };

    // Procesa `numSamples` de una entrada generada por `gen(i, channel)`. Los eventos
    // MIDI son (muestra absoluta, nota).
    Capture run(DecimulGapProcessor& p, TestPlayHead& ph, int numSamples,
                const std::function<float(int, int)>& gen,
                const std::vector<std::pair<int, int>>& noteOns = {},
                int startIndex = 0)
    {
        Capture cap;

        for (int start = 0; start < numSamples; start += blockSize)
        {
            const int n = std::min(blockSize, numSamples - start);
            juce::AudioBuffer<float> block(2, n);
            for (int i = 0; i < n; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    block.setSample(ch, i, gen(startIndex + start + i, ch));

            juce::MidiBuffer midi;
            for (auto& [sample, note] : noteOns)
                if (sample >= start && sample < start + n)
                    midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 100), sample - start);

            p.processBlock(block, midi);

            for (int i = 0; i < n; ++i)
            {
                cap.l.push_back(block.getSample(0, i));
                cap.r.push_back(block.getSample(1, i));
            }
            cap.uiGainPerBlock.push_back(p.uiGain.load());

            ph.ppq += (double) n * ph.bpm / 60.0 / sr;
        }
        return cap;
    }

    // Amplitud de un tono de frecuencia `freq` en una ventana (correlación con seno/coseno).
    float toneAmplitude(const std::vector<float>& x, int start, int len, double freq)
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double w = 2.0 * pi * freq * (double) (start + i) / sr;
            re += (double) x[(size_t) (start + i)] * std::cos(w);
            im += (double) x[(size_t) (start + i)] * std::sin(w);
        }
        return (float) (2.0 / (double) len * std::sqrt(re * re + im * im));
    }

    std::unique_ptr<DecimulGapProcessor> makeProcessor(TestPlayHead& ph)
    {
        auto p = std::make_unique<DecimulGapProcessor>();
        p->setRateAndBufferSizeDetails(sr, blockSize);
        p->prepareToPlay(sr, blockSize);
        p->setPlayHead(&ph);
        setParam(*p, ParamIDs::smooth, 0.0f);   // sin suavizado: se mide la curva pura
        return p;
    }

    constexpr float dc = 0.5f;
    const auto dcSignal = [](int, int) { return dc; };
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // ---- Modo captura de la UI ----
    if (argc >= 3 && juce::String(argv[1]) == "--screenshot")
    {
        TestPlayHead ph;
        ph.bpm = 126.0;
        auto p = makeProcessor(ph);
        setParam(*p, ParamIDs::smooth, 3.0f);
        p->setCurve(GapCurve::preset(0), 0);

        // Un poco de audio para que el cabezal quede en algún punto del ciclo.
        run(*p, ph, (int) (sr * 0.07), [](int i, int) { return 0.4f * (float) std::sin(2.0 * pi * 55.0 * i / sr); });

        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        editor->setSize(1100, 640);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(500);

        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        const juce::File out(argv[2]);
        out.deleteFile();
        juce::PNGImageFormat png;
        juce::FileOutputStream stream(out);
        return (stream.openedOk() && png.writeImageToStream(image, stream)) ? 0 : 1;
    }

    // ================= Modelo de la curva =================
    {
        const auto c = GapCurve::preset(0);   // Classic Pump
        check(std::abs(c.evaluate(0.0f)) < 1e-4f, "curva: arranca en 0 (el kick)", juce::String(c.evaluate(0.0f), 3));
        check(std::abs(c.evaluate(1.0f) - 1.0f) < 1e-4f && std::abs(c.evaluate(0.7f) - 1.0f) < 1e-4f,
              "curva: termina en 1");

        bool monotonic = true;
        float prev = c.evaluate(0.0f);
        for (int i = 1; i <= 200; ++i)
        {
            const float v = c.evaluate((float) i / 200.0f);
            if (v < prev - 1e-5f)
                monotonic = false;
            prev = v;
        }
        check(monotonic, "curva: la recuperación es monótona");

        std::array<float, GapCurve::tableSize + 1> table {};
        c.fillTable(table.data());
        check(std::abs(table[512] - c.evaluate(0.5f)) < 1e-4f, "curva: la tabla coincide con evaluate()");

        const auto xml = c.toXml();
        const auto back = GapCurve::fromXml(*xml);
        check(back.points.size() == c.points.size() && std::abs(back.evaluate(0.3f) - c.evaluate(0.3f)) < 1e-4f,
              "curva: ida y vuelta por XML");

        GapCurve messy;
        messy.points = { { 0.9f, 0.4f, 0.0f }, { 0.2f, 2.0f, 5.0f }, { 0.5f, -1.0f, 0.0f } };
        messy.sanitize();
        check(messy.points.front().x == 0.0f && messy.points.back().x == 1.0f
                  && messy.points[1].y <= 1.0f && messy.points[1].y >= 0.0f,
              "curva: sanitize() ordena, acota y fija los extremos");
    }

    // ================= Modo Sync =================
    {
        TestPlayHead ph;
        auto p = makeProcessor(ph);                       // 120 BPM, rate 1/4 = 24000 muestras
        p->setCurve(GapCurve::preset(0), 0);
        auto cap = run(*p, ph, 48000 * 2, dcSignal);

        auto g = [&](int i) { return cap.l[(size_t) i] / dc; };
        check(g(100) < 0.12f, "sync: al arrancar el beat el volumen cae", juce::String(g(100), 3));
        check(std::abs(g(19200) - 1.0f) < 0.02f, "sync: a 3/4 del beat vuelve a 1", juce::String(g(19200), 3));
        check(g(24000 + 100) < 0.12f, "sync: el siguiente beat vuelve a bombear", juce::String(g(24100), 3));
        check(std::abs(g(6600) - 0.84f) < 0.04f, "sync: la recuperación sigue la forma de la curva",
              juce::String(g(6600), 3));
    }

    // ================= Depth y Mix =================
    {
        TestPlayHead ph;
        auto p = makeProcessor(ph);
        p->setCurve(GapCurve::preset(0), 0);
        setParam(*p, ParamIDs::depth, 50.0f);
        auto cap = run(*p, ph, 24000, dcSignal);
        check(std::abs(cap.l[10] / dc - 0.5f) < 0.05f, "depth 50%: el piso es la mitad", juce::String(cap.l[10] / dc, 3));

        TestPlayHead ph2;
        auto p2 = makeProcessor(ph2);
        p2->setCurve(GapCurve::preset(0), 0);
        setParam(*p2, ParamIDs::mix, 0.0f);
        auto cap2 = run(*p2, ph2, 24000, dcSignal);
        float maxDiff = 0.0f;
        for (float v : cap2.l)
            maxDiff = std::max(maxDiff, std::abs(v - dc));
        check(maxDiff < 1e-4f, "mix 0%: la señal pasa intacta", juce::String(maxDiff, 6));
    }

    // ================= Nudge y Rate =================
    {
        TestPlayHead ph;
        auto p = makeProcessor(ph);
        p->setCurve(GapCurve::preset(0), 0);
        setParam(*p, ParamIDs::nudge, 10.0f);             // +10 ms = 480 muestras de retraso
        auto cap = run(*p, ph, 48000, dcSignal);
        check(cap.l[24000 + 100] / dc > 0.9f, "nudge +10 ms: en el segundo beat la caída todavía no llegó a los 100 samples",
              juce::String(cap.l[24100] / dc, 3));
        check(cap.l[24000 + 480 + 100] / dc < 0.12f, "nudge +10 ms: la caída llega 480 samples después",
              juce::String(cap.l[24580] / dc, 3));

        TestPlayHead ph2;
        auto p2 = makeProcessor(ph2);
        p2->setCurve(GapCurve::preset(0), 0);
        setParam(*p2, ParamIDs::rate, 7.0f);              // 1/8 = 12000 muestras
        auto cap2 = run(*p2, ph2, 24000, dcSignal);
        check(cap2.l[12000 + 100] / dc < 0.12f && cap2.l[9600] / dc > 0.97f, "rate 1/8: el ciclo dura la mitad",
              juce::String(cap2.l[12100] / dc, 3) + " / " + juce::String(cap2.l[9600] / dc, 3));
    }

    // ================= Transporte parado =================
    {
        TestPlayHead ph;
        ph.playing = false;
        auto p = makeProcessor(ph);
        p->setCurve(GapCurve::preset(0), 0);
        auto cap = run(*p, ph, 48000, dcSignal);

        float lo = 10.0f, hi = -10.0f;
        for (float v : cap.l)
        {
            lo = std::min(lo, v / dc);
            hi = std::max(hi, v / dc);
        }
        check(lo < 0.1f && hi > 0.95f, "transporte parado: el reloj interno sigue bombeando",
              juce::String(lo, 3) + " .. " + juce::String(hi, 3));
        check(!p->hostSynced.load(), "transporte parado: hostSynced = false");
    }

    // ================= Trigger MIDI =================
    {
        TestPlayHead ph;
        auto p = makeProcessor(ph);
        p->setCurve(GapCurve::preset(0), 0);
        setParam(*p, ParamIDs::trigger, 1.0f);
        setParam(*p, ParamIDs::midiNote, 36.0f);

        const int noteAt = 30000 + 300;
        auto cap = run(*p, ph, 60000, dcSignal, { { 5000, 40 }, { noteAt, 36 } });

        check(std::abs(cap.l[200] / dc - 1.0f) < 0.01f, "MIDI: antes de la primera nota no hay bombeo",
              juce::String(cap.l[200] / dc, 3));
        check(std::abs(cap.l[4000] / dc - 1.0f) < 0.01f && std::abs(cap.l[5100] / dc - 1.0f) < 0.01f
                  && std::abs(cap.l[9000] / dc - 1.0f) < 0.01f,
              "MIDI: una nota que no es la elegida no dispara",
              juce::String(cap.l[5100] / dc, 3));
        check(cap.l[(size_t) noteAt + 100] / dc < 0.12f, "MIDI: la nota elegida dispara la curva",
              juce::String(cap.l[(size_t) noteAt + 100] / dc, 3));
        check(std::abs(cap.l[(size_t) noteAt + 20000] / dc - 1.0f) < 0.01f, "MIDI: la curva corre una sola vez");
        check(p->triggerCount.load() == 1, "MIDI: un solo disparo registrado", juce::String(p->triggerCount.load()));
    }

    // ================= Trigger por audio =================
    {
        TestPlayHead ph;
        auto p = makeProcessor(ph);
        p->setCurve(GapCurve::preset(0), 0);
        setParam(*p, ParamIDs::trigger, 2.0f);
        setParam(*p, ParamIDs::threshold, -24.0f);

        // "Bajo" de 1 kHz constante + un golpe de kick de 60 Hz (80 ms) a los 0.4 s.
        const int kickStart = (int) (0.4 * sr), kickLen = (int) (0.08 * sr);
        auto gen = [&](int i, int)
        {
            float v = 0.3f * (float) std::sin(2.0 * pi * 1000.0 * i / sr);
            if (i >= kickStart && i < kickStart + kickLen)
                v += 0.8f * (float) std::sin(2.0 * pi * 60.0 * i / sr);
            return v;
        };
        auto cap = run(*p, ph, (int) (1.2 * sr), gen);

        const float before = toneAmplitude(cap.l, (int) (0.2 * sr), 960, 1000.0);
        float lowest = 10.0f;
        for (int w = kickStart; w < kickStart + (int) (0.3 * sr); w += 240)
            lowest = std::min(lowest, toneAmplitude(cap.l, w, 240, 1000.0));
        const float after = toneAmplitude(cap.l, (int) (1.1 * sr), 960, 1000.0);

        check(std::abs(before - 0.3f) < 0.03f, "audio: sin kick no hay bombeo", juce::String(before, 3));
        check(lowest < 0.3f * 0.45f, "audio: el kick detectado hace bajar el bajo", juce::String(lowest, 3));
        check(std::abs(after - 0.3f) < 0.03f, "audio: después del ciclo vuelve a 1", juce::String(after, 3));
        check(p->triggerCount.load() == 1, "audio: un solo disparo (sin rebotes)", juce::String(p->triggerCount.load()));
    }

    // ================= Bandas =================
    {
        auto gen = [](int i, int) {
            return 0.3f * (float) std::sin(2.0 * pi * 50.0 * i / sr) + 0.3f * (float) std::sin(2.0 * pi * 4000.0 * i / sr);
        };

        auto measure = [&](int bandIndex)
        {
            TestPlayHead ph;
            auto p = makeProcessor(ph);
            p->setCurve(GapCurve::preset(0), 0);
            setParam(*p, ParamIDs::band, (float) bandIndex);
            auto cap = run(*p, ph, 48000, gen);
            struct R { float lowQuiet, lowLoud, highQuiet, highLoud; };
            // Segundo beat, para que ya no haya transitorios de arranque.
            return R { toneAmplitude(cap.l, 24000, 480, 50.0), toneAmplitude(cap.l, 24000 + 19200, 480, 50.0),
                       toneAmplitude(cap.l, 24000, 480, 4000.0), toneAmplitude(cap.l, 24000 + 19200, 480, 4000.0) };
        };

        const auto low = measure(1);
        check(low.lowQuiet < low.lowLoud * 0.5f, "banda Low: el bajo se agacha",
              juce::String(low.lowQuiet, 3) + " vs " + juce::String(low.lowLoud, 3));
        check(low.highQuiet > low.highLoud * 0.9f, "banda Low: los agudos no se tocan",
              juce::String(low.highQuiet, 3) + " vs " + juce::String(low.highLoud, 3));

        const auto high = measure(2);
        check(high.highQuiet < high.highLoud * 0.5f && high.lowQuiet > high.lowLoud * 0.9f,
              "banda High: se agachan solo los agudos",
              juce::String(high.highQuiet, 3) + " / " + juce::String(high.lowQuiet, 3));
    }

    // ================= Stereo spread =================
    {
        TestPlayHead ph;
        auto p = makeProcessor(ph);
        p->setCurve(GapCurve::preset(0), 0);
        setParam(*p, ParamIDs::spread, 50.0f);   // R desfasado medio ciclo
        auto cap = run(*p, ph, 48000, dcSignal);
        check(cap.l[100] / dc < 0.12f && cap.r[100] / dc > 0.95f, "spread 50%: L bombea y R todavía no",
              juce::String(cap.l[100] / dc, 3) + " / " + juce::String(cap.r[100] / dc, 3));
        check(cap.r[12000 + 100] / dc < 0.12f && cap.l[12000 + 100] / dc > 0.95f,
              "spread 50%: medio ciclo después se invierten",
              juce::String(cap.l[12100] / dc, 3) + " / " + juce::String(cap.r[12100] / dc, 3));
    }

    // ================= Estado =================
    {
        TestPlayHead ph;
        auto p = makeProcessor(ph);

        GapCurve custom;
        custom.points = { { 0.0f, 0.2f, 0.3f }, { 0.4f, 0.9f, -0.2f }, { 0.7f, 0.1f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
        p->setCurve(custom, -1);
        setParam(*p, ParamIDs::rate, 7.0f);

        juce::MemoryBlock state;
        p->getStateInformation(state);

        auto p2 = makeProcessor(ph);
        p2->setStateInformation(state.getData(), (int) state.getSize());
        const auto restored = p2->getCurve();

        check(restored.points.size() == 4 && std::abs(restored.points[2].y - 0.1f) < 1e-4f
                  && std::abs(restored.points[0].bend - 0.3f) < 1e-4f,
              "estado: la curva a medida sobrevive al guardado");
        check((int) p2->apvts.getRawParameterValue(ParamIDs::rate)->load() == 7, "estado: los parámetros también");
    }

    // ================= Bypass =================
    {
        TestPlayHead ph;
        auto p = makeProcessor(ph);
        p->setCurve(GapCurve::preset(0), 0);
        setParam(*p, ParamIDs::bypass, 1.0f);
        auto cap = run(*p, ph, 4800, dcSignal);
        float maxDiff = 0.0f;
        for (float v : cap.l)
            maxDiff = std::max(maxDiff, std::abs(v - dc));
        check(maxDiff < 1e-6f, "bypass: la señal pasa intacta", juce::String(maxDiff, 7));
    }

    std::cout << (failures == 0 ? "\nTODO OK" : "\nHAY FALLAS: " + juce::String(failures)).toRawUTF8() << std::endl;
    return failures == 0 ? 0 : 1;
}
