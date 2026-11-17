// Test del procesador de Vise (el motor se prueba aparte, sin JUCE, en ViseDspTest.cpp).
// Compilar con: cmake -DDECIMU_VISE_TESTS=ON y correr ViseEngineTest.exe.
// Con `--screenshot ruta.png` guarda una captura de la UI con una frase de ejemplo.

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

    double rms(const std::vector<float>& v, int from, int to)
    {
        double s = 0.0;
        for (int i = from; i < to; ++i) s += (double) v[(size_t) i] * v[(size_t) i];
        return std::sqrt(s / (double) std::max(1, to - from));
    }

    void setParam(DecimulViseProcessor& p, const juce::String& id, float plain)
    {
        auto* param = p.apvts.getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    }

    void runStereo(DecimulViseProcessor& p, std::vector<float>& l, std::vector<float>& r)
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

    std::unique_ptr<DecimulViseProcessor> makeProcessor()
    {
        auto p = std::make_unique<DecimulViseProcessor>();
        p->setRateAndBufferSizeDetails(sr, blockSize);
        p->prepareToPlay(sr, blockSize);
        return p;
    }

    std::vector<float> tone(int n, double f, float db)
    {
        std::vector<float> v((size_t) n);
        const float a = std::pow(10.0f, db / 20.0f);
        for (int i = 0; i < n; ++i) v[(size_t) i] = a * (float) std::sin(2.0 * pi * f * i / sr);
        return v;
    }

    // Batería de juguete: golpes de ruido con caída sobre un colchón de tono, para la captura.
    std::vector<float> drumLoop(int n)
    {
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        std::vector<float> v((size_t) n, 0.0f);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = 0.05f * (float) std::sin(2.0 * pi * 110.0 * i / sr) + 0.02f * u(rng);
        for (double t0 = 100.0; t0 < n * 1000.0 / sr - 300.0; t0 += 375.0)
            for (int i = ms(t0); i < std::min(n, ms(t0 + 280.0)); ++i)
            {
                const double env = std::exp(-(double) (i - ms(t0)) / ms(70.0));
                v[(size_t) i] += (float) (0.85 * env) * (0.6f * u(rng) + 0.4f * (float) std::sin(2.0 * pi * 60.0 * (i - ms(t0)) / sr));
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
        setParam(*p, ParamIDs::threshold, -22.0f);
        setParam(*p, ParamIDs::ratio, 4.0f);
        setParam(*p, ParamIDs::attack, 12.0f);
        setParam(*p, ParamIDs::release, 220.0f);
        setParam(*p, ParamIDs::autoMakeup, 1.0f);
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        if (const auto shotSize = juce::SystemStats::getEnvironmentVariable("DECIMU_SHOT_SIZE", {}); shotSize.contains("x"))
            editor->setSize(shotSize.upToFirstOccurrenceOf("x", false, false).getIntValue(),
                            shotSize.fromFirstOccurrenceOf("x", false, false).getIntValue());

        auto l = drumLoop(ms(2600.0));
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

    const int n = ms(2000.0);
    const int a = ms(1000.0);

    // ---- latencia y formatos ----
    {
        auto p = makeProcessor();
        check(p->getLatencySamples() == 0, "Procesador: latencia cero");
        check(!p->acceptsMidi() && !p->producesMidi(), "Procesador: sin MIDI");
        auto m = std::make_unique<DecimulViseProcessor>();
        m->setPlayConfigDetails(1, 1, sr, blockSize);
        m->prepareToPlay(sr, blockSize);
        check(m->getTotalNumInputChannels() == 1, "Procesador: acepta pistas mono");
        // mono comprime
        setParam(*m, ParamIDs::attack, 1.0f);
        auto in = tone(n, 1000.0, -6.0f);
        juce::MidiBuffer midi;
        std::vector<float> outv((size_t) n);
        for (size_t start = 0; start < in.size(); start += blockSize)
        {
            const int len = (int) std::min<size_t>(blockSize, in.size() - start);
            juce::AudioBuffer<float> b(1, len);
            for (int i = 0; i < len; ++i) b.setSample(0, i, in[start + (size_t) i]);
            m->processBlock(b, midi);
            for (int i = 0; i < len; ++i) outv[start + (size_t) i] = b.getSample(0, i);
        }
        check(dbOf(rms(in, a, n)) - dbOf(rms(outv, a, n)) > 6.0, "Mono: comprime");
    }

    // ---- compresión con los valores de fábrica y mediante parámetros ----
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::attack, 1.0f);
        auto in = tone(n, 1000.0, -6.0f);
        auto l = in, r = in;
        runStereo(*p, l, r);
        const double d = dbOf(rms(in, a, n)) - dbOf(rms(l, a, n));
        check(d > 7.0 && d < 10.5, "Valores de fábrica (-18 dB, 4:1, rodilla 6): baja unos 9 dB", juce::String(d, 2) + " dB");
        check(p->currentGrDb.load() > 6.0f, "El medidor de reducción refleja la compresión", juce::String(p->currentGrDb.load(), 1) + " dB");
    }

    // ---- bypass ----
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::bypass, 1.0f);
        auto in = tone(n, 1000.0, -6.0f);
        auto l = in, r = in;
        runStereo(*p, l, r);
        double maxDiff = 0.0;
        for (int i = ms(100.0); i < n; ++i) maxDiff = std::max(maxDiff, (double) std::abs(l[(size_t) i] - in[(size_t) i]));
        check(maxDiff < 1.0e-6, "Bypass: deja pasar la entrada intacta", juce::String(maxDiff));
    }

    // ---- salida ----
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::threshold, 0.0f);
        setParam(*p, ParamIDs::outputTrim, -6.0f);
        auto in = tone(n, 1000.0, -20.0f);
        auto l = in, r = in;
        runStereo(*p, l, r);
        const double d = dbOf(rms(l, a, n)) - dbOf(rms(in, a, n));
        check(std::abs(d + 6.0) < 0.1, "Output -6 dB baja la salida 6 dB", juce::String(d, 2) + " dB");
    }

    // ---- bloques raros ----
    {
        auto p = makeProcessor();
        juce::MidiBuffer midi;
        bool finite = true;
        for (int len : { 1, 7, 64, 2048, 3, 480 })
            for (int rep = 0; rep < 40; ++rep)
            {
                juce::AudioBuffer<float> b(2, len);
                for (int i = 0; i < len; ++i) { b.setSample(0, i, 0.7f * (float) std::sin(i * 0.1)); b.setSample(1, i, 0.3f); }
                p->processBlock(b, midi);
                for (int i = 0; i < len; ++i)
                    if (!std::isfinite(b.getSample(0, i)) || !std::isfinite(b.getSample(1, i))) finite = false;
            }
        check(finite, "Bloques de 1, 7, 64, 2048, 3 y 480 muestras: salida finita");
    }

    // ---- estado ----
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::threshold, -31.5f);
        setParam(*p, ParamIDs::ratio, 7.5f);
        setParam(*p, ParamIDs::autoRelease, 1.0f);
        setParam(*p, ParamIDs::rms, 1.0f);
        setParam(*p, ParamIDs::scHp, 150.0f);
        juce::MemoryBlock state;
        p->getStateInformation(state);
        auto p2 = makeProcessor();
        p2->setStateInformation(state.getData(), (int) state.getSize());
        auto v = [&](const juce::String& id) { return p2->apvts.getRawParameterValue(id)->load(); };
        check(std::abs(v(ParamIDs::threshold) + 31.5f) < 0.2f && std::abs(v(ParamIDs::ratio) - 7.5f) < 0.2f
                  && v(ParamIDs::autoRelease) > 0.5f && v(ParamIDs::rms) > 0.5f && std::abs(v(ParamIDs::scHp) - 150.0f) < 2.0f,
              "El estado se guarda y se restaura");
    }

    // ---- cuadros para la pantalla ----
    {
        auto p = makeProcessor();
        auto in = tone(ms(1000.0), 1000.0, -6.0f);
        auto l = in, r = in;
        runStereo(*p, l, r);
        DecimulViseProcessor::Frame frames[1024];
        const int got = p->readFrames(frames, 1024);
        bool sane = got > 100;
        float maxGr = 0.0f;
        for (int i = 0; i < got; ++i) { maxGr = std::max(maxGr, frames[i].grDb); if (frames[i].inDb > 1.0f) sane = false; }
        check(sane && maxGr > 6.0f, "Entrega cuadros de nivel y reducción a la pantalla", juce::String(got) + " cuadros, reducción máxima " + juce::String(maxGr, 1) + " dB");
    }

    // ---- parámetros ----
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
        check(ok, "Parámetros: todos con valor de fábrica en rango", juce::String(p->getParameters().size()) + " parámetros");
    }

    std::cout << (failures == 0 ? "\nTODO OK" : (juce::String("\nHAY FALLAS: ") + juce::String(failures)).toRawUTF8()) << std::endl;
    return failures == 0 ? 0 : 1;
}
