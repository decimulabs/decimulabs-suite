// Test del procesador de Sarro (el motor se prueba aparte, sin JUCE, en SarroDspTest.cpp).
// Compilar con: cmake -DDECIMU_SARRO_TESTS=ON y correr SarroEngineTest.exe.
// Con `--screenshot ruta.png` guarda una captura de la UI con una señal de ejemplo (ruido con picos).

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

    void setParam(DecimulSarroProcessor& p, const juce::String& id, float plain)
    {
        auto* param = p.apvts.getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    }

    void runStereo(DecimulSarroProcessor& p, std::vector<float>& l, std::vector<float>& r)
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

    std::unique_ptr<DecimulSarroProcessor> makeProcessor()
    {
        auto p = std::make_unique<DecimulSarroProcessor>();
        p->setRateAndBufferSizeDetails(sr, blockSize);
        p->prepareToPlay(sr, blockSize);
        return p;
    }

    // Una voz de juguete: armónicos de un tono más un "ring" fijo de 3.1 kHz y un silbido de 7 kHz, sobre ruido suave.
    std::vector<float> ringingVoice(int n)
    {
        std::mt19937 rng(5);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        std::vector<float> v((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            double s = 0.0;
            for (int h = 1; h <= 12; ++h)
                s += std::sin(2.0 * pi * 180.0 * h * t) * 0.07 / h;
            s += 0.10 * std::sin(2.0 * pi * 3100.0 * t) + 0.06 * std::sin(2.0 * pi * 7000.0 * t);
            v[(size_t) i] = (float) s + 0.025f * u(rng);
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
        setParam(*p, ParamIDs::depth, 70.0f);
        setParam(*p, ParamIDs::sharpness, 65.0f);
        setParam(*p, ParamIDs::sensitivity, 60.0f);
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        if (const auto shotSize = juce::SystemStats::getEnvironmentVariable("DECIMU_SHOT_SIZE", {}); shotSize.contains("x"))
            editor->setSize(shotSize.upToFirstOccurrenceOf("x", false, false).getIntValue(),
                            shotSize.fromFirstOccurrenceOf("x", false, false).getIntValue());

        auto l = ringingVoice(ms(3000.0));
        auto r = l;
        // para la captura corremos de a poco y dejamos que la pantalla lea el último estado
        runStereo(*p, l, r);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(600);
        // el motor ya terminó: una última pasada corta mantiene el espectro de la voz en pantalla
        auto tail = ringingVoice(ms(400.0));
        auto tailR = tail;
        runStereo(*p, tail, tailR);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(400);

        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        const juce::File out(argv[2]);
        out.deleteFile();
        juce::PNGImageFormat png;
        juce::FileOutputStream stream(out);
        return (stream.openedOk() && png.writeImageToStream(image, stream)) ? 0 : 1;
    }

    const int n = ms(4000.0);
    const int from = ms(1500.0), to = ms(3800.0);

    // ---- latencia informada y formatos ----
    {
        auto p = makeProcessor();
        check(p->getLatencySamples() == 2048, "Procesador: informa la latencia al host", juce::String(p->getLatencySamples()) + " muestras");
        check(!p->acceptsMidi() && !p->producesMidi(), "Procesador: sin MIDI");
        auto m = std::make_unique<DecimulSarroProcessor>();
        m->setPlayConfigDetails(1, 1, sr, blockSize);
        m->prepareToPlay(sr, blockSize);
        check(m->getTotalNumInputChannels() == 1, "Procesador: acepta pistas mono");
    }

    // ---- baja un ring fijo y deja el resto ----
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::depth, 100.0f);
        setParam(*p, ParamIDs::sharpness, 70.0f);
        setParam(*p, ParamIDs::sensitivity, 70.0f);
        setParam(*p, ParamIDs::attack, 5.0f);
        auto in = ringingVoice(n);
        auto l = in, r = in;
        runStereo(*p, l, r);
        const int lat = 2048;
        const double dRing = dbOf(toneAmp(l, 3100.0, from + lat, to)) - dbOf(toneAmp(in, 3100.0, from, to - lat));
        const double dBody = dbOf(toneAmp(l, 180.0, from + lat, to)) - dbOf(toneAmp(in, 180.0, from, to - lat));
        check(dRing < -8.0 && std::abs(dBody) < 3.0, "Baja el ring de 3.1 kHz y deja casi intacto el cuerpo de la voz (a DEPTH 100 abusa un poco de la fundamental)",
              juce::String(dRing, 1) + " dB y " + juce::String(dBody, 2) + " dB");
        check(p->getMaxReduction() > 5.0f, "El medidor de reducción refleja el trabajo", juce::String(p->getMaxReduction(), 1) + " dB");
    }

    // ---- bypass y mix 0: sale la entrada retrasada ----
    {
        for (int mode = 0; mode < 2; ++mode)
        {
            auto p = makeProcessor();
            setParam(*p, ParamIDs::depth, 100.0f);
            setParam(*p, mode == 0 ? ParamIDs::bypass : ParamIDs::mix, mode == 0 ? 1.0f : 0.0f);
            auto in = ringingVoice(n);
            auto l = in, r = in;
            runStereo(*p, l, r);
            double maxDiff = 0.0;
            for (int i = ms(500.0); i < n; ++i) maxDiff = std::max(maxDiff, (double) std::abs(l[(size_t) i] - in[(size_t) (i - 2048)]));
            check(maxDiff < 1.0e-4, mode == 0 ? "Bypass: sale la original con el mismo retardo" : "Mix 0: sale la original con el mismo retardo", juce::String(maxDiff, 8));
        }
    }

    // ---- bloques raros ----
    {
        auto p = makeProcessor();
        juce::MidiBuffer midi;
        bool finite = true;
        for (int len : { 1, 7, 64, 2048, 3, 480, 5000 })
            for (int rep = 0; rep < 30; ++rep)
            {
                juce::AudioBuffer<float> b(2, len);
                for (int i = 0; i < len; ++i) { b.setSample(0, i, 0.5f * (float) std::sin(i * 0.1)); b.setSample(1, i, 0.3f); }
                p->processBlock(b, midi);
                for (int i = 0; i < len; ++i)
                    if (!std::isfinite(b.getSample(0, i)) || !std::isfinite(b.getSample(1, i))) finite = false;
            }
        check(finite, "Bloques de 1, 7, 64, 2048, 3, 480 y 5000 muestras: salida finita");
    }

    // ---- estado ----
    {
        auto p = makeProcessor();
        setParam(*p, ParamIDs::depth, 33.0f);
        setParam(*p, ParamIDs::lowFocus, 400.0f);
        setParam(*p, ParamIDs::delta, 1.0f);
        setParam(*p, ParamIDs::trim, -4.5f);
        juce::MemoryBlock state;
        p->getStateInformation(state);
        auto p2 = makeProcessor();
        p2->setStateInformation(state.getData(), (int) state.getSize());
        auto v = [&](const juce::String& id) { return p2->apvts.getRawParameterValue(id)->load(); };
        check(std::abs(v(ParamIDs::depth) - 33.0f) < 0.6f && std::abs(v(ParamIDs::lowFocus) - 400.0f) < 3.0f && v(ParamIDs::delta) > 0.5f
                  && std::abs(v(ParamIDs::trim) + 4.5f) < 0.2f,
              "El estado se guarda y se restaura");
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
