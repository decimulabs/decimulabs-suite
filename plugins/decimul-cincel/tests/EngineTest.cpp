// Test de integración de Cincel (el plugin entero, con JUCE): parámetros, estado, presets, A/B,
// deshacer, latencia informada, mono y sidechain. El motor de audio en sí se prueba a fondo en
// tests/DspTest.cpp (sin JUCE).
// Compilar con: cmake -DDECIMU_CINCEL_TESTS=ON y correr CincelEngineTest.exe.
// Con `--screenshot ruta.png [match|piano|ms|dyn]` guarda una captura de la UI.

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
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

    std::vector<float> tone(int n, double f, double amp)
    {
        std::vector<float> v((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (float) (amp * std::sin(2.0 * pi * f * i / sr));
        return v;
    }

    void setParam(DecimulCincelProcessor& p, const juce::String& id, float plain)
    {
        auto* param = p.apvts.getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    }

    std::unique_ptr<DecimulCincelProcessor> makeProcessor()
    {
        auto p = std::make_unique<DecimulCincelProcessor>();
        p->setRateAndBufferSizeDetails(sr, blockSize);
        p->prepareToPlay(sr, blockSize);
        return p;
    }

    /** Procesa L/R (y sidechain si se pasa) en bloques; el buffer lleva 4 canales si hay sidechain. */
    void run(DecimulCincelProcessor& p, std::vector<float>& l, std::vector<float>& r, const std::vector<float>* sc = nullptr)
    {
        juce::MidiBuffer midi;
        const int chans = p.getTotalNumInputChannels();
        for (size_t start = 0; start < l.size(); start += blockSize)
        {
            const int n = (int) std::min<size_t>(blockSize, l.size() - start);
            juce::AudioBuffer<float> b(std::max(2, chans), n);
            b.clear();
            for (int i = 0; i < n; ++i)
            {
                b.setSample(0, i, l[start + (size_t) i]);
                b.setSample(1, i, r[start + (size_t) i]);
                if (sc != nullptr && chans >= 4)
                {
                    b.setSample(2, i, (*sc)[start + (size_t) i]);
                    b.setSample(3, i, (*sc)[start + (size_t) i]);
                }
            }
            p.processBlock(b, midi);
            for (int i = 0; i < n; ++i)
            {
                l[start + (size_t) i] = b.getSample(0, i);
                r[start + (size_t) i] = b.getSample(1, i);
            }
        }
    }

    cincel::BandParams bell(float f, float g, float q)
    {
        cincel::BandParams b;
        b.used = true;
        b.shape = cincel::Shape::Bell;
        b.freq = f;
        b.gainDb = g;
        b.q = q;
        return b;
    }

    void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

    /** Señal "musical" para la captura: ruido rosado aproximado más algunos tonos. */
    std::vector<float> musicLike(int n, unsigned seed)
    {
        std::mt19937 rng(seed);
        std::normal_distribution<float> g(0.0f, 1.0f);
        std::vector<float> v((size_t) n);
        float b0 = 0, b1 = 0, b2 = 0;
        for (int i = 0; i < n; ++i)
        {
            const float w = g(rng);
            b0 = 0.99765f * b0 + w * 0.0990460f;
            b1 = 0.96300f * b1 + w * 0.2965164f;
            b2 = 0.57000f * b2 + w * 1.0526913f;
            const double t = i / sr;
            v[(size_t) i] = 0.035f * (b0 + b1 + b2 + w * 0.1848f)
                            + (float) (0.12 * std::sin(2 * pi * 110 * t) + 0.05 * std::sin(2 * pi * 220 * t)
                                       + 0.04 * std::sin(2 * pi * 660 * t) + 0.02 * std::sin(2 * pi * 2500 * t)
                                       + 0.012 * std::sin(2 * pi * 5200 * t));
        }
        return v;
    }
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // ---- Captura de la UI ----
    if (argc >= 3 && juce::String(argv[1]) == "--screenshot")
    {
        const juce::String variant = argc >= 4 ? juce::String(argv[3]) : juce::String();
        auto p = makeProcessor();
        if (variant == "ms")
            p->loadFactoryPreset(7);
        else if (variant == "dyn")
            p->loadFactoryPreset(8);
        else
        {
            p->loadFactoryPreset(1);
            auto b = bell(5200.0f, 0.0f, 3.0f);
            b.dynamic = true;
            b.rangeDb = -6.0f;
            b.thresholdDb = -46.0f;
            p->writeBand(4, b);
        }
        if (variant == "piano")
            p->uiState().setProperty("piano", true, nullptr);
        if (variant == "solo")
            p->soloBand.store(2);
        if (variant == "low")
            p->writeBand(5, bell(150.0f, -9.0f, 1.5f));

        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        if (const auto shotSize = juce::SystemStats::getEnvironmentVariable("DECIMU_SHOT_SIZE", {}); shotSize.contains("x"))
            editor->setSize(shotSize.upToFirstOccurrenceOf("x", false, false).getIntValue(),
                            shotSize.fromFirstOccurrenceOf("x", false, false).getIntValue());
        auto* ed = dynamic_cast<DecimulCincelEditor*>(editor.get());
        if (const auto sc = juce::SystemStats::getEnvironmentVariable("DECIMU_SHOT_SCALE", {}); sc.isNotEmpty())
            ed->setUiScale(sc.getFloatValue());
        if (variant == "match")
            ed->openMatchPanel(true);

        const int n = (int) (sr * 3.0);
        auto l = musicLike(n, 3);
        auto r = musicLike(n, 4);
        for (int k = 0; k < 6; ++k)
        {
            const int part = n / 6;
            std::vector<float> a(l.begin() + k * part, l.begin() + (k + 1) * part);
            std::vector<float> b(r.begin() + k * part, r.begin() + (k + 1) * part);
            run(*p, a, b);
            pump(80);
        }
        ed->getDisplay().selectOnly(variant == "ms" ? 1 : (variant == "low" ? 5 : 2));
        pump(400);

        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        const juce::File out(argv[2]);
        out.deleteFile();
        juce::PNGImageFormat png;
        juce::FileOutputStream stream(out);
        return (stream.openedOk() && png.writeImageToStream(image, stream)) ? 0 : 1;
    }

    const int n = (int) (sr * 0.8);
    const int a = n - 19200, bEnd = n;

    // ---- Sin bandas no toca nada ----
    {
        auto p = makeProcessor();
        auto l = tone(n, 440.0, 0.3), r = tone(n, 660.0, 0.3);
        const auto l0 = l, r0 = r;
        run(*p, l, r);
        double err = 0.0;
        for (int i = 0; i < n; ++i)
            err = std::max(err, (double) std::max(std::abs(l[(size_t) i] - l0[(size_t) i]), std::abs(r[(size_t) i] - r0[(size_t) i])));
        check(err < 1.0e-6, "Sin bandas la señal pasa intacta (latencia cero)", juce::String(err));
    }

    // ---- Una banda desde los parámetros ----
    {
        auto p = makeProcessor();
        p->writeBand(0, bell(1000.0f, 6.0f, 1.0f));
        auto l = tone(n, 1000.0, 0.2), r = l;
        run(*p, l, r);
        const double g = dbOf(toneAmp(l, 1000.0, a, bEnd) / 0.2);
        check(std::abs(g - 6.0) < 0.05, "Bell de +6 dB en 1 kHz, puesto con los parámetros", juce::String(g, 3) + " dB");
        check(p->firstFreeBand() == 1, "La primera banda libre es la 2");
    }

    // ---- Latencia informada al host según el modo ----
    {
        auto p = makeProcessor();
        check(p->getLatencySamples() == 0, "Latencia cero: 0 muestras informadas");
        setParam(*p, CincelIDs::mode, 2.0f);
        pump(150);
        const int lp = p->getLatencySamples();
        check(lp == cincel::lpLatency(2, sr), "Fase lineal (High): latencia informada", juce::String(lp) + " muestras");
        setParam(*p, CincelIDs::mode, 1.0f);
        pump(150);
        check(p->getLatencySamples() == 64, "Fase natural: latencia informada", juce::String(p->getLatencySamples()) + " muestras");

        // Y el audio sale con esa demora exacta. Como al exportar (offline): el FIR se arma en el
        // acto, así el resultado no depende de qué tan ocupada esté la compu.
        p->setNonRealtime(true);
        std::vector<float> l((size_t) n, 0.0f), r((size_t) n, 0.0f);
        l[2000] = r[2000] = 1.0f;
        setParam(*p, CincelIDs::mode, 2.0f);
        pump(150);
        run(*p, l, r);   // el primer tramo funde al cambiar de modo
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        l[2000] = r[2000] = 1.0f;
        run(*p, l, r);
        int peak = 0;
        for (int i = 0; i < n; ++i)
            if (std::abs(l[(size_t) i]) > std::abs(l[(size_t) peak]))
                peak = i;
        check(peak - 2000 == p->getLatencySamples(), "El audio sale con la demora informada", "pico en +" + juce::String(peak - 2000));
    }

    // ---- Estado: se guarda y se restaura ----
    {
        auto p = makeProcessor();
        auto b = bell(3300.0f, -4.5f, 2.2f);
        b.place = cincel::Placement::Side;
        b.dynamic = true;
        b.rangeDb = -3.0f;
        p->writeBand(5, b);
        setParam(*p, CincelIDs::mode, 1.0f);
        setParam(*p, CincelIDs::character, 2.0f);
        p->uiState().setProperty("range", 3, nullptr);
        juce::MemoryBlock state;
        p->getStateInformation(state);
        auto p2 = makeProcessor();
        p2->setStateInformation(state.getData(), (int) state.getSize());
        const auto r = p2->readBand(5);
        check(r.used && std::abs(r.freq - 3300.0f) < 1.0f && std::abs(r.gainDb + 4.5f) < 0.02f && std::abs(r.q - 2.2f) < 0.01f
                  && r.place == cincel::Placement::Side && r.dynamic && std::abs(r.rangeDb + 3.0f) < 0.02f
                  && p2->globalValue(CincelIDs::mode) > 0.5f && p2->globalValue(CincelIDs::mode) < 1.5f
                  && (int) p2->uiState().getProperty("range", 0) == 3,
              "Estado: bandas, modo y ajustes de pantalla se guardan y se restauran");
    }

    // ---- Presets de fábrica ----
    {
        auto p = makeProcessor();
        p->writeBand(10, bell(500.0f, 3.0f, 1.0f));
        p->loadFactoryPreset(1);
        int used = 0;
        for (int i = 0; i < cincel::maxBands; ++i)
            used += p->readBand(i).used ? 1 : 0;
        check(used == 4 && !p->readBand(10).used && p->currentPresetName == "Vocal - presence and air",
              "Preset de fábrica: reemplaza las bandas", juce::String(used) + " bandas");
        // Guardar y volver a cargar un preset de usuario.
        const auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cincel-test-preset.xml");
        check(p->saveUserPreset(f), "Preset de usuario: se guarda");
        p->resetAll(true);
        check(p->loadPresetFile(f) && p->readBand(3).used && p->readBand(3).shape == cincel::Shape::HighShelf,
              "Preset de usuario: se vuelve a cargar");
        f.deleteFile();
        check(p->factoryPresetNames().size() >= 10, "Hay presets de fábrica", juce::String(p->factoryPresetNames().size()));
    }

    // ---- A/B ----
    {
        auto p = makeProcessor();
        p->writeBand(0, bell(1000.0f, 6.0f, 1.0f));
        p->switchAB();                      // a B (copia de A)
        const bool bStartsAsA = p->readBand(0).used && std::abs(p->readBand(0).gainDb - 6.0f) < 0.01f;
        p->writeBand(0, bell(1000.0f, -9.0f, 1.0f));
        p->switchAB();                      // de vuelta a A
        const bool aKept = std::abs(p->readBand(0).gainDb - 6.0f) < 0.01f;
        p->switchAB();
        const bool bKept = std::abs(p->readBand(0).gainDb + 9.0f) < 0.01f;
        check(bStartsAsA && aKept && bKept, "A/B: cada lado guarda su ajuste");
    }

    // ---- Deshacer ----
    {
        auto p = makeProcessor();
        p->undoManager.beginNewTransaction();
        p->writeBand(0, bell(1000.0f, 6.0f, 1.0f));
        pump(200);
        p->undoManager.beginNewTransaction();
        p->writeBand(0, bell(2000.0f, -3.0f, 1.0f));
        pump(200);
        p->undoManager.undo();
        pump(100);
        const auto b = p->readBand(0);
        check(std::abs(b.freq - 1000.0f) < 1.0f && std::abs(b.gainDb - 6.0f) < 0.02f, "Deshacer vuelve al cambio anterior",
              juce::String(b.freq, 1) + " Hz, " + juce::String(b.gainDb, 2) + " dB");
    }

    // ---- Mono ----
    {
        auto p = std::make_unique<DecimulCincelProcessor>();
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add(juce::AudioChannelSet::mono());
        layout.inputBuses.add(juce::AudioChannelSet::disabled());
        layout.outputBuses.add(juce::AudioChannelSet::mono());
        const bool ok = p->setBusesLayout(layout);
        p->setRateAndBufferSizeDetails(sr, blockSize);
        p->prepareToPlay(sr, blockSize);
        p->writeBand(0, bell(1000.0f, -6.0f, 1.0f));
        juce::MidiBuffer midi;
        auto v = tone(n, 1000.0, 0.3);
        for (size_t s = 0; s < v.size(); s += blockSize)
        {
            const int len = (int) std::min<size_t>(blockSize, v.size() - s);
            juce::AudioBuffer<float> buf(1, len);
            buf.copyFrom(0, 0, v.data() + s, len);
            p->processBlock(buf, midi);
            std::copy(buf.getReadPointer(0), buf.getReadPointer(0) + len, v.begin() + (long) s);
        }
        const double g = dbOf(toneAmp(v, 1000.0, a, bEnd) / 0.3);
        check(ok && std::abs(g + 6.0) < 0.05, "Mono: anda igual", juce::String(g, 2) + " dB");
    }

    // ---- Sidechain ----
    {
        auto p = std::make_unique<DecimulCincelProcessor>();
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add(juce::AudioChannelSet::stereo());
        layout.inputBuses.add(juce::AudioChannelSet::stereo());
        layout.outputBuses.add(juce::AudioChannelSet::stereo());
        const bool ok = p->setBusesLayout(layout);
        p->setRateAndBufferSizeDetails(sr, blockSize);
        p->prepareToPlay(sr, blockSize);
        auto b = bell(1000.0f, 0.0f, 1.0f);
        b.dynamic = true;
        b.sidechain = true;
        b.rangeDb = -10.0f;
        b.thresholdDb = -40.0f;
        p->writeBand(0, b);
        auto l = tone(n, 1000.0, 0.01), r = l;
        auto sc = tone(n, 1000.0, 0.5);
        run(*p, l, r, &sc);
        const double g = dbOf(toneAmp(l, 1000.0, a, bEnd) / 0.01);
        check(ok && p->sidechainConnected.load() && g < -9.0, "Sidechain: la banda dinámica escucha la otra señal", juce::String(g, 2) + " dB");
    }

    // ---- Uso con el mouse (eventos simulados sobre la pantalla) ----
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        auto* ed = dynamic_cast<DecimulCincelEditor*>(editor.get());
        auto& d = ed->getDisplay();
        d.resized();
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        auto ev = [&](juce::Point<float> pos, juce::ModifierKeys mods, int clicks, bool dragged, juce::Point<float> down)
        {
            return juce::MouseEvent(src, pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &d, &d, juce::Time::getCurrentTime(), down,
                                    juce::Time::getCurrentTime(), clicks, dragged);
        };
        const juce::ModifierKeys none, left(juce::ModifierKeys::leftButtonModifier);

        // Doble clic en 1 kHz / +6 dB: campana nueva ahí.
        const auto at = d.pointFor(1000.0, 6.0);
        d.mouseDown(ev(at, left, 1, false, at));
        d.mouseUp(ev(at, none, 1, false, at));
        d.mouseDoubleClick(ev(at, left, 2, false, at));
        d.refresh();
        auto b = p->readBand(0);
        check(b.used && b.shape == cincel::Shape::Bell && std::abs(std::log2(b.freq / 1000.0)) < 0.05 && std::abs(b.gainDb - 6.0f) < 0.3f
                  && d.getPrimary() == 0,
              "Mouse: doble clic crea una campana donde se hizo clic", juce::String(b.freq, 0) + " Hz, " + juce::String(b.gainDb, 2) + " dB");

        // El clic extra para agarrar la banda recién creada (un "tercer clic") no la apaga.
        d.mouseDoubleClick(ev(at, left, 3, false, at));
        d.mouseDoubleClick(ev(at, left, 2, false, at));
        d.refresh();
        check(p->readBand(0).enabled, "Mouse: el clic extra justo después de crear la banda no la apaga");

        // Solo del panel como en el Pro-Q: se escucha mientras está apretado y se barre arrastrando.
        {
            auto& solo = ed->getBandPanel().getSoloButton();
            auto sev = [&](juce::Point<float> pos, juce::ModifierKeys mods, bool dragged)
            {
                return juce::MouseEvent(src, pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &solo, &solo, juce::Time::getCurrentTime(),
                                        { 10.0f, 10.0f }, juce::Time::getCurrentTime(), 1, dragged);
            };
            const float f0 = p->readBand(0).freq;
            solo.mouseDown(sev({ 10.0f, 10.0f }, left, false));
            const bool held = p->soloBand.load() == 0;
            solo.mouseDrag(sev({ 100.0f, 10.0f }, left, true));
            const float f1 = p->readBand(0).freq;
            solo.mouseUp(sev({ 100.0f, 10.0f }, none, true));
            check(held && f1 > f0 * 1.8f && p->soloBand.load() == -1, "Solo (auriculares): suena mientras está apretado, arrastrar barre la frecuencia, al soltar vuelve",
                  juce::String(f0, 0) + " -> " + juce::String(f1, 0) + " Hz");
            auto bb = p->readBand(0);
            bb.freq = f0;
            p->writeBand(0, bb);
            d.refresh();
        }

        // Arrastrar una octava a la derecha y 3 dB abajo.
        const auto to = d.pointFor(2000.0, 3.0);
        d.mouseDown(ev(at, left, 1, false, at));
        d.mouseDrag(ev(to, left, 1, true, at));
        d.mouseUp(ev(to, none, 1, true, at));
        d.refresh();
        b = p->readBand(0);
        check(std::abs(std::log2(b.freq / 2000.0)) < 0.05 && std::abs(b.gainDb - 3.0f) < 0.3f, "Mouse: arrastrar mueve frecuencia y ganancia",
              juce::String(b.freq, 0) + " Hz, " + juce::String(b.gainDb, 2) + " dB");

        // Rueda: Q.
        const float q0 = b.q;
        juce::MouseWheelDetails w {};
        w.deltaY = 0.25f;
        d.mouseWheelMove(ev(to, none, 0, false, to), w);
        d.refresh();
        check(p->readBand(0).q > q0 * 1.2f, "Mouse: la rueda cambia la Q", juce::String(q0, 2) + " -> " + juce::String(p->readBand(0).q, 2));

        // Alt mientras se arrastra: solo, y se suelta al soltar.
        d.mouseDown(ev(to, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier), 1, false, to));
        const bool soloOn = p->soloBand.load() == 0;
        d.mouseUp(ev(to, none, 1, false, to));
        check(soloOn && p->soloBand.load() == -1, "Mouse: Alt al arrastrar escucha la banda sola");

        // Doble clic pegado al borde izquierdo: corte de graves.
        const auto edge = d.pointFor(12.0, 0.0);
        d.mouseDoubleClick(ev(edge, left, 2, false, edge));
        d.refresh();
        check(p->readBand(1).used && p->readBand(1).shape == cincel::Shape::LowCut, "Mouse: doble clic en el borde crea un corte");

        // Supr borra la banda elegida.
        d.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
        d.refresh();
        check(!p->readBand(1).used && p->readBand(0).used, "Teclado: Supr borra la banda elegida");

        // Doble clic en un punto: la apaga.
        const auto dot = d.pointFor(p->readBand(0).freq, p->readBand(0).gainDb);
        d.mouseDoubleClick(ev(dot, left, 2, false, dot));
        check(!p->readBand(0).enabled, "Mouse: doble clic en un punto apaga la banda");

        // El panel de la banda es fijo: mover la ganancia de arriba a abajo no lo cambia de lugar,
        // y nunca se superpone con el gráfico (ni Match / Instancias abiertos).
        {
            p->writeBand(0, bell(1000.0f, 9.0f, 1.0f));
            d.selectOnly(0);
            pump(100);
            const auto before = ed->getBandPanel().getBounds();
            bool moved = false;
            for (float gdb : { 6.0f, 0.0f, -6.0f, -12.0f, 3.0f })
            {
                p->writeBand(0, bell(1000.0f, gdb, 1.0f));
                pump(70);
                moved = moved || ed->getBandPanel().getBounds() != before;
            }
            const bool apart = !ed->getBandPanel().getBounds().intersects(d.getBounds());
            ed->openMatchPanel(true);
            pump(50);
            bool matchApart = true;
            for (int i = 0; i < ed->getNumChildComponents(); ++i)
                if (auto* c = ed->getChildComponent(i); c != &d && c->isVisible() && dynamic_cast<MatchPanel*>(c) != nullptr)
                    matchApart = !c->getBounds().intersects(d.getBounds());
            ed->openMatchPanel(false);
            check(!moved && apart && matchApart, "Panel de banda fijo: no salta al mover la ganancia y no tapa el gráfico",
                  before.toString() + (moved ? " (se movió)" : ""));
        }

        // Teclado: solo la pantalla toma el foco (los botones no se comen la barra espaciadora del DAW).
        {
            int grabbers = 0;
            juce::String names;
            std::function<void(juce::Component&)> walk = [&](juce::Component& c)
            {
                for (auto* ch : c.getChildren())
                {
                    // Botones, menús y perillas son los que reaccionan a la barra espaciadora o a las flechas.
                    const bool actsOnKeys = dynamic_cast<juce::Button*>(ch) != nullptr || dynamic_cast<juce::ComboBox*>(ch) != nullptr
                                            || dynamic_cast<juce::Slider*>(ch) != nullptr;
                    if (ch != &d && actsOnKeys && ch->getWantsKeyboardFocus() && ch->isVisible())
                        { ++grabbers; names << typeid(*ch).name() << "/" << ch->getParentComponent()->getComponentID() << " "; }
                    walk(*ch);
                }
            };
            walk(*ed);
            check(grabbers == 0 && d.getWantsKeyboardFocus() && !d.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)),
                  "Teclado: ningún control se queda con la barra espaciadora (la pantalla la deja pasar)",
                  juce::String(grabbers) + " controles que toman el foco " + names);
        }

        // Tamaño: al 75 % la ventana se achica y adentro todo sigue con el mismo armado.
        {
            ed->setUiScale(1.0f);
            pump(30);
            const auto logical = d.getBounds();
            ed->setUiScale(0.75f);
            pump(30);
            check(ed->getWidth() == 960 && ed->getHeight() == 645 && d.getBounds() == logical, "Tamaño: la interfaz se escala entera (ventana chica, mismo armado)",
                  logical.toString() + " -> " + d.getBounds().toString());
        }

        // Sin audio, el analizador deja de recalcular (y la pantalla de redibujarse).
        {
            pump(2600);
            const auto v1 = p->postAnalyzer.version;
            pump(700);
            check(p->postAnalyzer.version == v1, "Reposo: sin audio el analizador se queda quieto (no gasta CPU)");
        }

        // Sketch: un trazo arma las bandas.
        d.sketchMode = true;
        const auto s0 = d.pointFor(60.0, -4.0);
        d.mouseDown(ev(s0, left, 1, false, s0));
        for (int k = 1; k <= 60; ++k)
        {
            const double hz = 60.0 * std::pow(200.0, k / 60.0);
            const double db = 6.0 * std::sin(k / 60.0 * pi) - 4.0 * (1.0 - k / 60.0);
            d.mouseDrag(ev(d.pointFor(hz, db), left, 1, true, s0));
        }
        d.mouseUp(ev(d.pointFor(12000.0, 0.0), none, 1, true, s0));
        int used = 0;
        for (int i = 0; i < cincel::maxBands; ++i)
            used += p->readBand(i).used ? 1 : 0;
        check(used >= 2 && used <= 12, "Sketch: el trazo se convierte en bandas", juce::String(used) + " bandas");
    }

    // ---- Lista de instancias ----
    {
        auto p1 = makeProcessor();
        auto p2 = makeProcessor();
        const auto others = InstanceRegistry::get().others(p1->instanceId);
        bool sees = false;
        for (const auto& e : others)
            sees = sees || e.id == p2->instanceId;
        check(sees, "Lista de instancias: una ve a la otra", juce::String((int) others.size()) + " otras");
    }

    std::cout << (failures == 0 ? "\nTODO OK" : (juce::String("\nHAY FALLAS: ") + juce::String(failures)).toRawUTF8()) << std::endl;
    return failures == 0 ? 0 : 1;
}
