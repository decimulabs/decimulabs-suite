// Test del procesador de Scour (el motor se prueba aparte, sin JUCE, en ScourDspTest.cpp).
// Compilar con: cmake -DDECIMU_SCOUR_TESTS=ON y correr ScourEngineTest.exe.
// Con `--screenshot ruta.png` guarda una captura de la UI tocando una frase de ejemplo.

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

    int ms(double t) { return (int) std::lround(t * 0.001 * sr); }

    void setParam(DecimulScourProcessor& p, const juce::String& id, float plain)
    {
        auto* param = p.apvts.getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    }

    void zeroAmounts(DecimulScourProcessor& p)
    {
        for (auto* id : { &ParamIDs::noiseAmt, &ParamIDs::wobAmt, &ParamIDs::disAmt, &ParamIDs::digAmt, &ParamIDs::spaceAmt, &ParamIDs::magAmt })
            setParam(p, *id, 0.0f);
    }

    void runStereo(DecimulScourProcessor& p, std::vector<float>& l, std::vector<float>& r)
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

    std::unique_ptr<DecimulScourProcessor> makeProcessor()
    {
        auto p = std::make_unique<DecimulScourProcessor>();
        p->setRateAndBufferSizeDetails(sr, blockSize);
        p->prepareToPlay(sr, blockSize);
        return p;
    }

    std::vector<float> voice(int n)
    {
        std::vector<float> v((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            const double syl = 0.55 + 0.45 * std::sin(2.0 * pi * 1.6 * t);
            v[(size_t) i] = (float) (syl * (0.22 * std::sin(2.0 * pi * 196.0 * t) + 0.12 * std::sin(2.0 * pi * 392.0 * t)
                                           + 0.07 * std::sin(2.0 * pi * 784.0 * t) + 0.04 * std::sin(2.0 * pi * 1568.0 * t)));
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
        auto p = makeProcessor();
        setParam(*p, ParamIDs::noiseAmt, 45.0f);
        setParam(*p, ParamIDs::noiseFlux, 35.0f);
        setParam(*p, ParamIDs::wobAmt, 55.0f);
        setParam(*p, ParamIDs::disAmt, 40.0f);
        setParam(*p, ParamIDs::digAmt, 25.0f);
        setParam(*p, ParamIDs::spaceAmt, 40.0f);
        setParam(*p, ParamIDs::magAmt, 60.0f);
        setParam(*p, ParamIDs::magDropouts, 55.0f);
        setParam(*p, ParamIDs::eqHigh, 9000.0f);
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        if (const auto shotSize = juce::SystemStats::getEnvironmentVariable("DECIMU_SHOT_SIZE", {}); shotSize.contains("x"))
            editor->setSize(shotSize.upToFirstOccurrenceOf("x", false, false).getIntValue(),
                            shotSize.fromFirstOccurrenceOf("x", false, false).getIntValue());

        const int n = ms(4500.0);
        auto l = voice(n);
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

    const int n = ms(2500.0);

    // ---- latencia, formatos ----
    {
        auto p = makeProcessor();
        check(p->getLatencySamples() == 0, "Procesador: latencia cero");
        check(!p->acceptsMidi() && !p->producesMidi(), "Procesador: sin MIDI");
        auto mono = std::make_unique<DecimulScourProcessor>();
        mono->setPlayConfigDetails(1, 1, sr, blockSize);
        mono->prepareToPlay(sr, blockSize);
        check(mono->getTotalNumInputChannels() == 1 && mono->getTotalNumOutputChannels() == 1, "Procesador: acepta pistas mono");
    }

    // ---- con las cantidades en 0 la señal pasa intacta ----
    {
        auto p = makeProcessor();
        zeroAmounts(*p);
        auto in = voice(n);
        auto l = in, r = in;
        runStereo(*p, l, r);
        double maxDiff = 0.0;
        for (size_t i = 0; i < in.size(); ++i) maxDiff = std::max(maxDiff, (double) std::abs(l[i] - in[i]));
        check(maxDiff < 2.0e-6, "Cantidades en 0: la señal pasa intacta", juce::String(maxDiff));
    }

    // ---- con los valores de fábrica suena distinto pero sano ----
    {
        auto p = makeProcessor();
        auto in = voice(n);
        auto l = in, r = in;
        runStereo(*p, l, r);
        bool finite = true;
        double diff = 0.0, peak = 0.0;
        for (size_t i = 0; i < in.size(); ++i)
        {
            if (!std::isfinite(l[i]) || !std::isfinite(r[i])) finite = false;
            diff += std::abs(l[i] - in[i]);
            peak = std::max(peak, (double) std::abs(l[i]));
        }
        check(finite && diff > 100.0 && peak < 2.0, "Valores de fábrica: el efecto actúa y la salida es sana", "pico " + juce::String(peak, 2));
    }

    // ---- bypass ----
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::bypass, 1.0f);
        auto in = voice(n);
        auto l = in, r = in;
        runStereo(*p, l, r);
        double maxDiff = 0.0;
        for (int i = ms(100.0); i < n; ++i) maxDiff = std::max(maxDiff, (double) std::abs(l[(size_t) i] - in[(size_t) i]));
        check(maxDiff < 1.0e-6, "Bypass: deja pasar la entrada intacta", juce::String(maxDiff));
    }

    // ---- mono ----
    {
        auto m = std::make_unique<DecimulScourProcessor>();
        m->setPlayConfigDetails(1, 1, sr, blockSize);
        m->prepareToPlay(sr, blockSize);
        setParam(*m, ParamIDs::disAmt, 80.0f);
        auto in = voice(n);
        juce::MidiBuffer midi;
        double diff = 0.0;
        bool finite = true;
        for (size_t start = 0; start < in.size(); start += blockSize)
        {
            const int len = (int) std::min<size_t>(blockSize, in.size() - start);
            juce::AudioBuffer<float> b(1, len);
            for (int i = 0; i < len; ++i) b.setSample(0, i, in[start + (size_t) i]);
            m->processBlock(b, midi);
            for (int i = 0; i < len; ++i)
            {
                const float v = b.getSample(0, i);
                if (!std::isfinite(v)) finite = false;
                diff += std::abs(v - in[start + (size_t) i]);
            }
        }
        check(finite && diff > 100.0, "Mono: procesa y la salida es sana");
    }

    // ---- bloques raros (1 muestra, 7, 2048) ----
    {
        auto p = makeProcessor();
        juce::MidiBuffer midi;
        bool finite = true;
        for (int len : { 1, 7, 64, 2048, 3, 480 })
            for (int rep = 0; rep < 40; ++rep)
            {
                juce::AudioBuffer<float> b(2, len);
                for (int i = 0; i < len; ++i) { b.setSample(0, i, 0.2f * (float) std::sin(i * 0.1)); b.setSample(1, i, 0.1f); }
                p->processBlock(b, midi);
                for (int i = 0; i < len; ++i)
                    if (!std::isfinite(b.getSample(0, i)) || !std::isfinite(b.getSample(1, i))) finite = false;
            }
        check(finite, "Bloques de 1, 7, 64, 2048, 3 y 480 muestras: salida finita");
    }

    // ---- estado ----
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::noiseType, 4.0f);
        setParam(*p, ParamIDs::disType, 2.0f);
        setParam(*p, ParamIDs::wobAmt, 77.0f);
        setParam(*p, ParamIDs::spaceFocus, -40.0f);
        setParam(*p, ParamIDs::noisePost, 1.0f);
        setParam(*p, ParamIDs::eqLow, 333.0f);
        juce::MemoryBlock state;
        p->getStateInformation(state);
        auto p2 = makeProcessor();
        p2->setStateInformation(state.getData(), (int) state.getSize());
        auto v = [&](const juce::String& id) { return p2->apvts.getRawParameterValue(id)->load(); };
        check((int) v(ParamIDs::noiseType) == 4 && (int) v(ParamIDs::disType) == 2 && std::abs(v(ParamIDs::wobAmt) - 77.0f) < 0.2f
                  && std::abs(v(ParamIDs::spaceFocus) + 40.0f) < 1.5f && v(ParamIDs::noisePost) > 0.5f && std::abs(v(ParamIDs::eqLow) - 333.0f) < 3.0f,
              "El estado se guarda y se restaura");
    }

    // ---- cuadros para la pantalla ----
    {
        auto p = makeProcessor();
        auto in = voice(ms(1000.0));
        auto l = in, r = in;
        runStereo(*p, l, r);
        DecimulScourProcessor::Frame frames[1024];
        const int got = p->readFrames(frames, 1024);
        bool sane = got > 100;
        for (int i = 0; i < got; ++i) if (frames[i].minV > frames[i].maxV) sane = false;
        check(sane, "Entrega cuadros de la forma de onda a la pantalla", juce::String(got) + " cuadros");
    }

    // ---- todos los parámetros tienen valor por defecto dentro de rango ----
    {
        auto p = makeProcessor();
        bool ok = true;
        for (auto* param : p->getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(param))
            {
                const float v = r->convertFrom0to1(r->getDefaultValue());
                const auto range = r->getNormalisableRange();
                if (v < range.start - 1.0e-4f || v > range.end + 1.0e-4f) ok = false;
            }
        check(ok && p->getParameters().size() > 45, "Parámetros: todos con valor de fábrica en rango", juce::String(p->getParameters().size()) + " parámetros");
    }

    std::cout << (failures == 0 ? "\nTODO OK" : (juce::String("\nHAY FALLAS: ") + juce::String(failures)).toRawUTF8()) << std::endl;
    return failures == 0 ? 0 : 1;
}
