// Test del DSP propio de la v0.5 (shared/Source/DecimuFx.h) y del procesador de Acoustic.
// Compilar con: cmake -DDECIMU_ACOUSTIC_TESTS=ON y correr AcousticEngineTest.exe.
// Todas las verificaciones imprimen [ OK ] / [FAIL]; al final "TODO OK" si no falló ninguna.

#include "../Source/PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>

using namespace decimu::fx;

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

    double rmsOf(const std::vector<float>& v, size_t from = 0, size_t to = 0)
    {
        if (to == 0 || to > v.size()) to = v.size();
        double s = 0.0;
        for (size_t i = from; i < to; ++i) s += (double) v[i] * v[i];
        return std::sqrt(s / (double) std::max<size_t>(1, to - from));
    }

    std::vector<float> whiteNoise(int n, float amp, unsigned seed = 1)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> d(-1.0f, 1.0f);
        std::vector<float> v((size_t) n);
        for (auto& x : v) x = d(rng) * amp;
        return v;
    }

    std::vector<float> sine(int n, double freq, float amp)
    {
        std::vector<float> v((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = amp * (float) std::sin(2.0 * decimu::fx::kPi * freq * i / sr);
        return v;
    }

    bool allFinite(const std::vector<float>& v, float limit = 50.0f)
    {
        for (float x : v)
            if (!std::isfinite(x) || std::abs(x) > limit)
                return false;
        return true;
    }

    // Decaimiento de Schroeder: RT60 extrapolado desde el tramo -5 dB a -25 dB.
    double rt60Of(const std::vector<float>& h)
    {
        std::vector<double> edc(h.size());
        double acc = 0.0;
        for (size_t i = h.size(); i-- > 0;)
        {
            acc += (double) h[i] * h[i];
            edc[i] = acc;
        }
        const double e0 = edc[0];
        size_t i1 = 0, i2 = 0;
        for (size_t i = 0; i < edc.size(); ++i)
        {
            const double d = 10.0 * std::log10(std::max(edc[i] / e0, 1.0e-30));
            if (i1 == 0 && d <= -5.0) i1 = i;
            if (i2 == 0 && d <= -25.0) { i2 = i; break; }
        }
        if (i1 == 0 || i2 <= i1)
            return -1.0;
        return 3.0 * (double) (i2 - i1) / sr;
    }

    // Densidad de ecos normalizada (Abel y Huang): 1 = campo difuso gaussiano.
    double echoDensity(const std::vector<float>& h, int centre, int win)
    {
        const int a = std::max(0, centre - win / 2), b = std::min((int) h.size(), centre + win / 2);
        double s = 0.0;
        for (int i = a; i < b; ++i) s += (double) h[(size_t) i] * h[(size_t) i];
        const double sigma = std::sqrt(s / (double) (b - a));
        int count = 0;
        for (int i = a; i < b; ++i)
            if (std::abs(h[(size_t) i]) > sigma) ++count;
        return ((double) count / (double) (b - a)) / 0.3173;
    }

    std::vector<float> bandFiltered(const std::vector<float>& x, bool low, float freq)
    {
        EqBand f[4];
        for (auto& b : f)
        {
            b.prepare(sr, low ? EqBand::Type::lowpass : EqBand::Type::highpass);
            b.set(freq, 0.0f, true);
        }
        std::vector<float> y = x;
        for (auto& b : f)
            b.process(y.data(), nullptr, (int) y.size());
        return y;
    }

    struct Ir { std::vector<float> l, r; };

    Ir reverbImpulse(FdnReverb& rev, int seconds)
    {
        const int n = (int) (sr * seconds);
        std::vector<float> in((size_t) n, 0.0f), l((size_t) n), r((size_t) n);
        in[4800] = 1.0f; // 100 ms después, con el reverb ya asentado
        rev.process(in.data(), in.data(), l.data(), r.data(), n);
        return { l, r };
    }

    void setParam(DecimulAcousticProcessor& p, const juce::String& id, float plain)
    {
        auto* param = p.apvts.getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    }

    // Procesa estéreo en bloques de 512 con las señales dadas (modifica una copia).
    void runStereo(DecimulAcousticProcessor& p, std::vector<float>& l, std::vector<float>& r)
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

    std::unique_ptr<DecimulAcousticProcessor> makeProcessor()
    {
        auto p = std::make_unique<DecimulAcousticProcessor>();
        p->setRateAndBufferSizeDetails(sr, blockSize);
        p->prepareToPlay(sr, blockSize);
        return p;
    }

    // Genera archivos de audio para comparar a oído el procesamiento anterior (juce::dsp) con el nuevo.
    void writeWav(const juce::File& file, const std::vector<float>& l, const std::vector<float>& r)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(file), sr, 2, 16, {}, 0));
        if (w == nullptr) { std::cout << "no se pudo escribir " << file.getFullPathName().toRawUTF8() << std::endl; return; }
        const float* ch[2] = { l.data(), r.data() };
        w->writeFromFloatArrays(ch, 2, (int) l.size());
    }

    // Guitarra pulsada (Karplus-Strong): arpegio y acorde al final, con silencio para oir las colas.
    std::vector<float> pluckPhrase()
    {
        std::vector<float> out((size_t) (sr * 9), 0.0f);
        std::mt19937 rng(5);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        auto pluck = [&](double startSec, double freq, float amp)
        {
            const int n = (int) (sr / freq);
            std::vector<float> line((size_t) n);
            for (auto& v : line) v = u(rng);
            size_t pos = 0;
            for (size_t i = (size_t) (startSec * sr); i < out.size() && i < (size_t) ((startSec + 3.0) * sr); ++i)
            {
                const float a = line[pos], b = line[(pos + 1) % line.size()];
                const float y = 0.4985f * (a + b);
                line[pos] = y;
                pos = (pos + 1) % line.size();
                out[i] += amp * y;
            }
        };
        const double notes[] = { 220.0, 261.63, 329.63, 440.0 };
        for (int i = 0; i < 4; ++i) pluck(0.2 + 0.45 * i, notes[i], 0.5f);
        pluck(4.5, 110.0, 0.5f); pluck(4.53, 164.81, 0.5f); pluck(4.56, 220.0, 0.5f); pluck(4.59, 277.18, 0.5f);
        return out;
    }

    void runDemos(const juce::File& dir)
    {
        dir.createDirectory();
        const auto dry = pluckPhrase();
        writeWav(dir.getChildFile("00_seca.wav"), dry, dry);

        struct Space { const char* name; int type; float roomSize, damping; };
        const Space spaces[] = { { "room", 0, 0.3f, 0.5f }, { "hall", 1, 0.6f, 0.4f }, { "cathedral", 2, 0.9f, 0.2f } };
        int idx = 1;
        for (const auto& sp : spaces)
        {
            // ANTES: Freeverb de juce::dsp, Space A al 30 %
            juce::dsp::Reverb old;
            old.prepare({ sr, 4096u, 2u });
            juce::dsp::Reverb::Parameters rp;
            rp.roomSize = sp.roomSize; rp.damping = sp.damping; rp.width = 1.0f; rp.wetLevel = 0.3f * 0.85f; rp.dryLevel = 0.0f;
            old.setParameters(rp);
            juce::AudioBuffer<float> ob(2, (int) dry.size());
            for (int i = 0; i < ob.getNumSamples(); ++i) { ob.setSample(0, i, dry[(size_t) i]); ob.setSample(1, i, dry[(size_t) i]); }
            for (int s = 0; s < ob.getNumSamples(); s += 4096)
            {
                const int n = std::min(4096, ob.getNumSamples() - s);
                juce::dsp::AudioBlock<float> blk(ob);
                auto sub = blk.getSubBlock((size_t) s, (size_t) n);
                juce::dsp::ProcessContextReplacing<float> ctx(sub);
                old.process(ctx);
            }
            std::vector<float> ol(ob.getReadPointer(0), ob.getReadPointer(0) + ob.getNumSamples()),
                               orr(ob.getReadPointer(1), ob.getReadPointer(1) + ob.getNumSamples());
            for (size_t i = 0; i < ol.size(); ++i) { ol[i] += dry[i]; orr[i] += dry[i]; }
            writeWav(dir.getChildFile(juce::String(idx).paddedLeft('0', 2) + "_reverb_" + sp.name + "_ANTES.wav"), ol, orr);

            // AHORA: Space A del plugin al 30 %
            auto p = makeProcessor();
            setParam(*p, ParamIDs::spaceAMix, 30.0f);
            setParam(*p, ParamIDs::spaceAType, (float) sp.type);
            auto nl = dry, nr = dry;
            runStereo(*p, nl, nr);
            writeWav(dir.getChildFile(juce::String(idx + 1).paddedLeft('0', 2) + "_reverb_" + sp.name + "_AHORA.wav"), nl, nr);
            idx += 2;
        }

        // Echo: negra a 120 BPM (500 ms), feedback 55 %, mezcla 50 %. ANTES = delay simple sin filtro.
        {
            std::vector<float> ol = dry, orr = dry;
            const int d = (int) (0.5 * sr);
            std::vector<float> lineL((size_t) d, 0.0f);
            size_t pos = 0;
            for (size_t i = 0; i < dry.size(); ++i)
            {
                const float delayed = lineL[pos];
                lineL[pos] = dry[i] + delayed * 0.55f;
                pos = (pos + 1) % lineL.size();
                ol[i] = dry[i] + delayed * 0.5f;
                orr[i] = ol[i];
            }
            writeWav(dir.getChildFile("07_echo_negra_ANTES.wav"), ol, orr);
            auto p = makeProcessor();
            setParam(*p, ParamIDs::echoMix, 50.0f);
            setParam(*p, ParamIDs::echoDivision, 2.0f);
            setParam(*p, ParamIDs::echoFeedback, 55.0f);
            auto nl = dry, nr = dry;
            runStereo(*p, nl, nr);
            writeWav(dir.getChildFile("08_echo_negra_AHORA.wav"), nl, nr);
        }
        std::cout << "Archivos escritos en " << dir.getFullPathName().toRawUTF8() << std::endl;
    }
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc >= 3 && juce::String(argv[1]) == "--demo")
    {
        runDemos(juce::File(argv[2]));
        return 0;
    }

    // ------------------------------------------------------------------ EQ
    {
        struct Case { EqBand::Type t; float f, g, q; };
        const Case cases[] = {
            { EqBand::Type::lowShelf, 200.0f, 6.0f, 0.7071f }, { EqBand::Type::lowShelf, 80.0f, -9.0f, 0.7071f },
            { EqBand::Type::highShelf, 8000.0f, 7.0f, 0.7071f }, { EqBand::Type::highShelf, 5000.0f, -12.0f, 0.7071f },
            { EqBand::Type::peak, 800.0f, 4.0f, 1.0f }, { EqBand::Type::lowpass, 6500.0f, 0.0f, 0.70710678f },
            { EqBand::Type::highpass, 120.0f, 0.0f, 0.70710678f } };
        double worst = 0.0;
        for (const auto& c : cases)
        {
            EqBand band;
            band.prepare(sr, c.t, c.q);
            band.set(c.f, c.g, true);
            juce::dsp::IIR::Coefficients<float>::Ptr ref;
            const float lin = juce::Decibels::decibelsToGain(c.g);
            switch (c.t)
            {
                case EqBand::Type::lowShelf:  ref = juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr, c.f, c.q, lin); break;
                case EqBand::Type::highShelf: ref = juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr, c.f, c.q, lin); break;
                case EqBand::Type::peak:      ref = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, c.f, c.q, lin); break;
                case EqBand::Type::lowpass:   ref = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, c.f); break;
                case EqBand::Type::highpass:  ref = juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, c.f); break;
            }
            double caseWorst = 0.0, atF = 0.0;
            for (double f = 30.0; f < 18000.0; f *= 1.37)
            {
                const double d = std::abs(dbOf(band.magnitudeAt(f)) - dbOf(ref->getMagnitudeForFrequency(f, sr)));
                if (d > caseWorst) { caseWorst = d; atF = f; }
            }
            if (caseWorst > 0.01) std::cout << "       EQ caso tipo " << (int) c.t << " f=" << c.f << " g=" << c.g << ": diferencia " << caseWorst << " dB en " << atF << " Hz" << std::endl;
            worst = std::max(worst, caseWorst);
        }
        check(worst < 0.02, "EQ propio = coeficientes de JUCE en 7 configuraciones (mismo color)",
              juce::String(worst, 5) + " dB de diferencia máxima");
    }
    {
        // Mover la ganancia de golpe de 0 a +12 dB: la amplitud sube sin escalones.
        EqBand band;
        band.prepare(sr, EqBand::Type::peak, 1.0f);
        band.set(1000.0f, 0.0f, true);
        auto x = sine((int) (sr * 0.6), 1000.0, 0.25f);
        std::vector<float> y = x;
        const int half = (int) (sr * 0.1);
        band.process(y.data(), nullptr, half);
        band.set(1000.0f, 12.0f);
        band.process(y.data() + half, nullptr, (int) y.size() - half);
        // amplitud por ventanas de 1 ms (48 muestras = 1 ms exacto de 1 kHz)
        double maxStep = 0.0, prev = 0.0;
        double last = 0.0;
        for (size_t i = (size_t) half - 480; i + 48 < y.size(); i += 48)
        {
            float a = 0.0f;
            for (size_t k = 0; k < 48; ++k) a = std::max(a, std::abs(y[i + k]));
            const double d = dbOf(a);
            if (prev != 0.0) maxStep = std::max(maxStep, std::abs(d - prev));
            prev = d;
            last = d;
        }
        check(maxStep < 1.2, "EQ: el salto de +12 dB se desliza (paso máximo por ms)", juce::String(maxStep, 2) + " dB/ms");
        check(std::abs(last - dbOf(0.25 * 3.98107)) < 0.3, "EQ: llega a +12 dB", juce::String(last - dbOf(0.25), 2) + " dB");
    }

    // ------------------------------------------------------------------ Compresor
    {
        LinkedCompressor c;
        c.prepare(sr);
        c.set(-18.0f, 4.0f, 0.0f, 1.0f, 100.0f, 0.0f);
        auto l = sine((int) sr, 1000.0, 0.5f); // -6 dBFS
        auto r = l;
        c.process(l.data(), r.data(), (int) l.size());
        const double outPeak = dbOf(*std::max_element(l.begin() + 24000, l.end()));
        check(std::abs(c.getGainReductionDb() - 9.0f) < 0.4f, "Compresor: reducción -6 dBFS, umbral -18, 4:1 = 9 dB",
              juce::String(c.getGainReductionDb(), 2) + " dB");
        check(std::abs(outPeak - (-15.0)) < 0.4, "Compresor: el pico baja a -15 dBFS", juce::String(outPeak, 2) + " dBFS");
    }
    {
        LinkedCompressor c;
        c.prepare(sr);
        c.set(-18.0f, 4.0f, 12.0f, 1.0f, 100.0f, 0.0f);
        auto l = sine((int) sr, 1000.0, 0.1259f); // -18 dBFS = justo en el umbral
        c.process(l.data(), nullptr, (int) l.size());
        check(std::abs(c.getGainReductionDb() - 1.125f) < 0.25f, "Compresor: rodilla suave de 12 dB (-1.1 dB en el umbral)",
              juce::String(c.getGainReductionDb(), 2) + " dB");
    }
    {
        // Ligado: L fuerte, R suave -> los dos bajan lo mismo.
        LinkedCompressor c;
        c.prepare(sr);
        c.set(-18.0f, 4.0f, 0.0f, 1.0f, 100.0f, 0.0f);
        auto l = sine((int) sr, 1000.0, 0.5f);
        auto r = sine((int) sr, 700.0, 0.03f); // -30 dBFS
        const auto r0 = r;
        c.process(l.data(), r.data(), (int) l.size());
        const double gainR = dbOf(rmsOf(r, 24000)) - dbOf(rmsOf(r0, 24000));
        check(std::abs(gainR + c.getGainReductionDb()) < 0.4, "Compresor ligado: el canal suave baja igual que el fuerte",
              "R " + juce::String(gainR, 2) + " dB, reducción " + juce::String(c.getGainReductionDb(), 2) + " dB");
    }
    {
        LinkedCompressor c;
        c.prepare(sr);
        c.set(-18.0f, 8.0f, 6.0f, 1.0f, 400.0f, 3.0f);
        auto quiet = sine(48000, 440.0, 0.01f);
        const auto q0 = quiet;
        c.process(quiet.data(), nullptr, 48000);
        check(c.getGainReductionDb() < 0.01f && std::abs(dbOf(rmsOf(quiet, 4800)) - dbOf(rmsOf(q0, 4800)) - 3.0) < 0.1,
              "Compresor: bajo el umbral no reduce y el makeup suma exacto");
        std::vector<float> z(48000, 0.0f);
        c.process(z.data(), nullptr, 48000);
        check(allFinite(z), "Compresor: silencio sin NaN");
    }

    // ------------------------------------------------------------------ Retardos
    {
        DelayBuf d;
        d.prepare(4000);
        std::vector<float> in = whiteNoise(2000, 1.0f, 5), out(2000);
        for (int i = 0; i < 2000; ++i)
        {
            out[(size_t) i] = d.read(100.0f);
            d.push(in[(size_t) i]);
        }
        check(std::abs(out[1500] - in[1400]) < 1.0e-6f, "DelayBuf: retardo entero exacto");
        DelayBuf d2;
        d2.prepare(4000);
        std::vector<float> imp(1000, 0.0f), o2(1000);
        imp[100] = 1.0f;
        for (int i = 0; i < 1000; ++i) { o2[(size_t) i] = d2.read(200.5f); d2.push(imp[(size_t) i]); }
        double sum = 0.0, centroid = 0.0;
        for (int i = 0; i < 1000; ++i) { sum += o2[(size_t) i]; centroid += (double) i * o2[(size_t) i]; }
        check(std::abs(sum - 1.0) < 1.0e-3 && std::abs(centroid / sum - 300.5) < 0.05, "DelayBuf: retardo fraccionario (200.5) centrado",
              juce::String(centroid / sum, 3));
    }

    // ------------------------------------------------------------------ Reverb
    {
        struct P { const char* name; float size, rt, hf, er, mod; int variant; };
        const P presets[] = { { "Room", 0.55f, 0.55f, 0.55f, 0.9f, 0.3f, 0 }, { "Hall", 1.0f, 1.7f, 0.45f, 0.45f, 0.6f, 0 },
                              { "Cathedral", 1.7f, 4.8f, 0.35f, 0.15f, 0.9f, 0 }, { "Close", 0.4f, 0.35f, 0.6f, 1.0f, 0.2f, 1 },
                              { "Vast", 2.1f, 6.5f, 0.3f, 0.05f, 1.0f, 1 } };
        for (const auto& p : presets)
        {
            FdnReverb rev;
            rev.prepare(sr, p.variant);
            rev.setPreset(p.size, p.rt, p.hf, p.er, p.mod, 1.0f, true);
            rev.setActive(true, 1.0f);
            std::vector<float> warm(4800, 0.0f), wl(4800), wr(4800);
            rev.process(warm.data(), warm.data(), wl.data(), wr.data(), 4800);
            const auto ir = reverbImpulse(rev, (int) std::ceil(p.rt * 2.2 + 1.0));
            const double rtLow = rt60Of(bandFiltered(ir.l, true, 250.0f));
            const double rtHigh = rt60Of(bandFiltered(bandFiltered(ir.l, false, 4000.0f), true, 6000.0f)); // banda de 4 a 6 kHz
            check(std::abs(rtLow / p.rt - 1.0) < 0.25, juce::String("Reverb ") + p.name + ": RT60 graves = " + juce::String(p.rt, 2) + " s",
                  juce::String(rtLow, 2) + " s");
            check(std::abs(rtHigh / (p.rt * p.hf) - 1.0) < 0.35, juce::String("Reverb ") + p.name + ": RT60 a 5 kHz = " + juce::String(p.rt * p.hf, 2) + " s",
                  juce::String(rtHigh, 2) + " s");
            check(allFinite(ir.l, 4.0f) && allFinite(ir.r, 4.0f), juce::String("Reverb ") + p.name + ": estable, sin NaN");
        }
    }
    {
        // Densidad de ecos y decorrelación L/R del Hall, contra el Freeverb de JUCE.
        FdnReverb rev;
        rev.prepare(sr, 0);
        rev.setPreset(1.0f, 1.7f, 0.45f, 0.45f, 0.6f, 1.0f, true);
        rev.setActive(true, 1.0f);
        std::vector<float> warm(4800, 0.0f), wl(4800), wr(4800);
        rev.process(warm.data(), warm.data(), wl.data(), wr.data(), 4800);
        const auto ir = reverbImpulse(rev, 4);

        juce::dsp::Reverb old;
        juce::dsp::ProcessSpec spec { sr, 4096u, 2u };
        old.prepare(spec);
        juce::dsp::Reverb::Parameters rp;
        rp.roomSize = 0.6f; rp.damping = 0.4f; rp.width = 1.0f; rp.wetLevel = 0.85f; rp.dryLevel = 0.0f;
        old.setParameters(rp);
        juce::AudioBuffer<float> ob(2, (int) (sr * 4));
        ob.clear();
        ob.setSample(0, 4800, 1.0f);
        ob.setSample(1, 4800, 1.0f);
        for (int s = 0; s < ob.getNumSamples(); s += 4096)
        {
            const int n = std::min(4096, ob.getNumSamples() - s);
            juce::dsp::AudioBlock<float> blk(ob);
            auto sub = blk.getSubBlock((size_t) s, (size_t) n);
            juce::dsp::ProcessContextReplacing<float> ctx(sub);
            old.process(ctx);
        }
        std::vector<float> oldL(ob.getReadPointer(0), ob.getReadPointer(0) + ob.getNumSamples());

        const double nNew100 = echoDensity(ir.l, 4800 + 4800, 960), nNew250 = echoDensity(ir.l, 4800 + 12000, 960);
        const double nOld100 = echoDensity(oldL, 4800 + 4800, 960), nOld250 = echoDensity(oldL, 4800 + 12000, 960);
        std::cout << "       densidad de ecos (1 = difuso): FDN " << nNew100 << " / " << nNew250
                  << "   Freeverb " << nOld100 << " / " << nOld250 << std::endl;
        check(nNew100 > 0.7 && nNew250 > 0.9, "Reverb Hall: la cola ya es difusa a los 100 y 250 ms");

        double corr = 0.0, el = 0.0, er = 0.0;
        for (int i = 4800; i < 4800 + 96000; ++i)
        {
            corr += (double) ir.l[(size_t) i] * ir.r[(size_t) i];
            el += (double) ir.l[(size_t) i] * ir.l[(size_t) i];
            er += (double) ir.r[(size_t) i] * ir.r[(size_t) i];
        }
        corr /= std::sqrt(el * er);
        check(std::abs(corr) < 0.35, "Reverb: los dos canales están decorrelacionados (estéreo de verdad)", juce::String(corr, 3));

        // nivel contra el reverb viejo: ruido blanco 10 s
        auto noise = whiteNoise((int) (sr * 10), 0.1f, 9);
        std::vector<float> outL(noise.size()), outR(noise.size());
        rev.process(noise.data(), noise.data(), outL.data(), outR.data(), (int) noise.size());
        const double newRatio = dbOf(rmsOf(outL, 96000) / rmsOf(noise, 96000));
        juce::AudioBuffer<float> nb(2, (int) noise.size());
        for (int i = 0; i < (int) noise.size(); ++i) { nb.setSample(0, i, noise[(size_t) i]); nb.setSample(1, i, noise[(size_t) i]); }
        old.reset();
        for (int s = 0; s < nb.getNumSamples(); s += 4096)
        {
            const int n = std::min(4096, nb.getNumSamples() - s);
            juce::dsp::AudioBlock<float> blk(nb);
            auto sub = blk.getSubBlock((size_t) s, (size_t) n);
            juce::dsp::ProcessContextReplacing<float> ctx(sub);
            old.process(ctx);
        }
        std::vector<float> oldOut(nb.getReadPointer(0), nb.getReadPointer(0) + nb.getNumSamples());
        const double oldRatio = dbOf(rmsOf(oldOut, 96000) / rmsOf(noise, 96000));
        std::cout << "       nivel del retorno con ruido: FDN Hall " << newRatio << " dB, Freeverb Hall " << oldRatio << " dB" << std::endl;
        check(newRatio - oldRatio > -5.0 && newRatio - oldRatio < 0.0, "Reverb: nivel entre 0 y 5 dB abajo del reverb anterior (mismo orden, con más aire de headroom)",
              juce::String(newRatio - oldRatio, 2) + " dB");
    }
    {
        // Estabilidad con cambios bruscos de preset y ruido fuerte.
        FdnReverb rev;
        rev.prepare(sr, 0);
        rev.setActive(true, 1.0f);
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        auto noise = whiteNoise((int) (sr * 30), 0.9f, 3);
        std::vector<float> outL(noise.size()), outR(noise.size());
        for (size_t s = 0; s + 24000 <= noise.size(); s += 24000)
        {
            rev.setPreset(0.3f + u(rng) * 1.9f, 0.1f + u(rng) * 11.9f, 0.1f + u(rng) * 0.9f, u(rng), u(rng) * 1.5f, u(rng));
            rev.process(noise.data() + s, noise.data() + s, outL.data() + s, outR.data() + s, 24000);
        }
        check(allFinite(outL, 40.0f) && allFinite(outR, 40.0f), "Reverb: 30 s de ruido fuerte con presets al azar, sin NaN ni explosión");
    }
    {
        // Apagar y volver a prender: no vuelve audio viejo.
        FdnReverb rev;
        rev.prepare(sr, 0);
        rev.setPreset(1.7f, 4.8f, 0.4f, 0.2f, 0.9f, 1.0f, true);
        rev.setActive(true, 1.0f);
        auto burst = whiteNoise(24000, 0.5f, 11);
        std::vector<float> oL(burst.size()), oR(burst.size());
        rev.process(burst.data(), burst.data(), oL.data(), oR.data(), (int) burst.size());
        const double peak = rmsOf(oL, 12000, 24000);
        rev.setActive(false, 1.0f);
        std::vector<float> silence((size_t) sr * 2, 0.0f), sL(silence.size()), sR(silence.size());
        rev.process(silence.data(), silence.data(), sL.data(), sR.data(), (int) silence.size());
        check(rmsOf(sL, 12000, 24000) < peak * 1.0e-3, "Reverb apagado: la salida queda muda (cola drenada en 0.25 s)");
        rev.setActive(true, 1.0f);
        std::vector<float> s2L(48000), s2R(48000);
        rev.process(silence.data(), silence.data(), s2L.data(), s2R.data(), 48000);
        check(dbOf(rmsOf(s2L) / peak) < -70.0, "Reverb: al re-prender no reaparece la cola vieja", juce::String(dbOf(rmsOf(s2L) / peak), 1) + " dB");
    }

    // ------------------------------------------------------------------ Echo
    {
        StereoEcho e;
        e.prepare(sr);
        e.set(250.0f, 0.5f, 1.0f, true);
        std::vector<float> in((size_t) sr * 2, 0.0f), oL(in.size(), 0.0f), oR(in.size(), 0.0f);
        // dejar que los suavizadores asienten
        e.process(in.data(), in.data(), oL.data(), oR.data(), 48000);
        std::fill(oL.begin(), oL.end(), 0.0f); std::fill(oR.begin(), oR.end(), 0.0f);
        in[100] = 0.5f;
        e.process(in.data(), in.data(), oL.data(), oR.data(), (int) in.size());
        auto peakNear = [&](int centre) { float m = 0.0f; for (int i = centre - 40; i < centre + 40; ++i) m = std::max(m, std::abs(oL[(size_t) i])); return m; };
        const float p1 = peakNear(100 + 12000), p2 = peakNear(100 + 24000), p3 = peakNear(100 + 36000);
        check(p1 > 0.3f && p2 < p1 && p2 > 0.05f && p3 < p2, "Echo: repeticiones a 250 ms que van bajando",
              juce::String(p1, 3) + " / " + juce::String(p2, 3) + " / " + juce::String(p3, 3));
        check(p2 / p1 < 0.6f, "Echo: cada repetición baja (feedback 50 % con filtro en el lazo)", juce::String(p2 / p1, 3));
    }
    {
        // Feedback 100 % durante 40 s de ruido: no explota.
        StereoEcho e;
        e.prepare(sr);
        e.set(300.0f, 1.0f, 1.0f, true);
        auto noise = whiteNoise((int) (sr * 40), 0.8f, 2);
        std::vector<float> oL(noise.size(), 0.0f), oR(noise.size(), 0.0f);
        e.process(noise.data(), noise.data(), oL.data(), oR.data(), (int) noise.size());
        check(allFinite(oL, 6.0f), "Echo: feedback 100 % durante 40 s, acotado (saturación suave en el lazo)");
    }
    {
        // Apagar y re-prender: sin repeticiones viejas. Cambio de tempo: sin saltos.
        StereoEcho e;
        e.prepare(sr);
        e.set(500.0f, 0.9f, 1.0f, true);
        auto burst = whiteNoise(24000, 0.5f, 4);
        std::vector<float> oL(burst.size(), 0.0f), oR(burst.size(), 0.0f);
        e.process(burst.data(), burst.data(), oL.data(), oR.data(), (int) burst.size());
        e.set(500.0f, 0.9f, 1.0f, false);
        std::vector<float> z((size_t) sr * 3, 0.0f), zl(z.size(), 0.0f), zr(z.size(), 0.0f);
        e.process(z.data(), z.data(), zl.data(), zr.data(), (int) z.size());
        e.set(500.0f, 0.9f, 1.0f, true);
        std::vector<float> z2l((size_t) sr, 0.0f), z2r((size_t) sr, 0.0f), zz((size_t) sr, 0.0f);
        e.process(zz.data(), zz.data(), z2l.data(), z2r.data(), (int) sr);
        check(rmsOf(z2l) < 1.0e-4, "Echo: al re-prender no aparecen repeticiones viejas", juce::String(rmsOf(z2l), 7));

        StereoEcho t;
        t.prepare(sr);
        t.set(300.0f, 0.4f, 1.0f, true);
        auto tone = sine((int) (sr * 3), 220.0, 0.5f);
        std::vector<float> tl(tone.size(), 0.0f), tr(tone.size(), 0.0f);
        t.process(tone.data(), tone.data(), tl.data(), tr.data(), (int) sr);
        t.set(150.0f, 0.4f, 1.0f, true); // el tempo cambia a la mitad
        t.process(tone.data() + (int) sr, tone.data() + (int) sr, tl.data() + (int) sr, tr.data() + (int) sr, (int) sr * 2);
        double maxStep = 0.0;
        for (size_t i = (size_t) sr + 1; i < tl.size(); ++i) maxStep = std::max(maxStep, (double) std::abs(tl[i] - tl[i - 1]));
        check(maxStep < 0.25, "Echo: el cambio de tempo (300 a 150 ms) no pega saltos", juce::String(maxStep, 3));
    }

    // ------------------------------------------------------------------ Chorus / Phaser
    {
        ChorusFlanger c;
        c.prepare(sr);
        c.setMode(false, true);
        c.setRate(0.8f);
        auto noise = whiteNoise((int) (sr * 3), 0.3f, 6);
        std::vector<float> wl(noise.size()), wr(noise.size());
        c.process(noise.data(), noise.data(), wl.data(), wr.data(), (int) noise.size());
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (size_t i = 24000; i < wl.size(); ++i) { lr += (double) wl[i] * wr[i]; ll += (double) wl[i] * wl[i]; rr += (double) wr[i] * wr[i]; }
        const double corr = lr / std::sqrt(ll * rr);
        check(corr < 0.95 && allFinite(wl, 2.0f), "Chorus: con entrada mono, izquierda y derecha salen distintas (abre el estéreo)", juce::String(corr, 3));
        {
            ChorusFlanger c2;
            c2.prepare(sr);
            c2.setMode(false, true);
            auto tone = sine((int) (sr * 3), 440.0, 0.3f);
            std::vector<float> tl(tone.size()), tr(tone.size());
            c2.process(tone.data(), tone.data(), tl.data(), tr.data(), (int) tone.size());
            check(std::abs(dbOf(rmsOf(tl, 24000)) - dbOf(rmsOf(tone, 24000))) < 0.3, "Chorus: la parte modulada conserva el nivel (tono de 440 Hz)");
        }
    }
    {
        Phaser ph;
        ph.prepare(sr);
        ph.setShape(1200.0f, 0.0f);
        auto noise = whiteNoise((int) (sr * 2), 0.3f, 8);
        std::vector<float> wl(noise.size()), wr(noise.size());
        ph.process(noise.data(), noise.data(), wl.data(), wr.data(), (int) noise.size());
        check(std::abs(dbOf(rmsOf(wl, 12000)) - dbOf(rmsOf(noise, 12000))) < 0.7, "Phaser: las etapas allpass conservan el nivel (sin feedback)");
        ph.setShape(1200.0f, 0.75f);
        std::vector<float> w2l(noise.size()), w2r(noise.size());
        ph.process(noise.data(), noise.data(), w2l.data(), w2r.data(), (int) noise.size());
        check(allFinite(w2l, 40.0f), "Phaser: feedback 0.75 estable");
        // sumado a la seca aparecen muescas: la suma no es igual a la seca
        double diff = 0.0;
        for (size_t i = 24000; i < noise.size(); ++i) diff += std::abs(noise[i] + wl[i] - 2.0f * noise[i]);
        check(diff > 1.0, "Phaser: dry + wet es distinto de la seca (se forman las muescas)");
    }

    // ------------------------------------------------------------------ Procesador de Acoustic
    {
        auto p = makeProcessor();
        check(p->apvts.getParameter("compOversample") == nullptr && p->apvts.getParameter("spaceAPreDelaySync") == nullptr,
              "Acoustic: sin los 3 parámetros que no hacían nada");
    }
    {
        // Todo neutro = transparente, y sin fade-in al arrancar.
        auto p = makeProcessor();
        auto l = whiteNoise((int) (sr * 2), 0.3f, 12);
        auto r = whiteNoise((int) (sr * 2), 0.3f, 13);
        const auto l0 = l, r0 = r;
        runStereo(*p, l, r);
        double worst = 0.0;
        for (size_t i = 0; i < l.size(); ++i) worst = std::max(worst, (double) std::abs(l[i] - l0[i]));
        check(worst < 1.0e-4, "Acoustic: con todo en cero es transparente desde la primera muestra", juce::String(worst, 7));
    }
    {
        // Compresor ligado dentro del plugin.
        auto p = makeProcessor();
        setParam(*p, ParamIDs::compAmount, 100.0f);
        setParam(*p, ParamIDs::compMode, 2.0f);
        auto l = sine((int) sr, 1000.0, 0.5f);
        auto r = sine((int) sr, 700.0, 0.03f);
        const auto r0 = r;
        runStereo(*p, l, r);
        const double gainR = dbOf(rmsOf(r, 24000)) - dbOf(rmsOf(r0, 24000));
        const double gr = p->gainReductionDb.load();
        check(gr > 3.0 && std::abs((gainR - 6.0) + gr) < 0.6, "Acoustic: el compresor reduce los dos canales por igual y el medidor marca la reducción real",
              "reducción " + juce::String(gr, 2) + " dB, canal R " + juce::String(gainR, 2) + " dB (con +6 de makeup)");
    }
    {
        // Reverbs + echo con todo prendido: estable y con CPU de sobra.
        auto p = makeProcessor();
        setParam(*p, ParamIDs::spaceAMix, 60.0f); setParam(*p, ParamIDs::spaceAType, 2.0f);
        setParam(*p, ParamIDs::spaceBMix, 60.0f); setParam(*p, ParamIDs::spaceBType, 2.0f);
        setParam(*p, ParamIDs::echoMix, 50.0f); setParam(*p, ParamIDs::echoDivision, 2.0f);
        setParam(*p, ParamIDs::compAmount, 50.0f); setParam(*p, ParamIDs::lowGain, 6.0f); setParam(*p, ParamIDs::highGain, -6.0f);
        auto l = whiteNoise((int) (sr * 10), 0.3f, 21);
        auto r = whiteNoise((int) (sr * 10), 0.3f, 22);
        const auto t0 = std::chrono::steady_clock::now();
        runStereo(*p, l, r);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(allFinite(l, 10.0f) && allFinite(r, 10.0f), "Acoustic: dos reverbs largos + echo + EQ + compresor, sin NaN ni explosión");
        check(10.0 / secs > 15.0, "Acoustic: rendimiento", juce::String(10.0 / secs, 1) + "x tiempo real");
    }
    {
        // Mover Space A de 0 a 100 % y bajarlo: sin saltos fuertes (mix suavizado).
        auto p = makeProcessor();
        setParam(*p, ParamIDs::spaceAType, 1.0f);
        auto l = sine((int) (sr * 2), 330.0, 0.3f);
        auto r = l;
        std::vector<float> l1(l.begin(), l.begin() + 48000), r1 = l1, l2(l.begin() + 48000, l.end()), r2 = l2;
        runStereo(*p, l1, r1);
        setParam(*p, ParamIDs::spaceAMix, 100.0f);
        runStereo(*p, l2, r2);
        double maxStep = 0.0;
        for (size_t i = 1; i < l2.size(); ++i) maxStep = std::max(maxStep, (double) std::abs(l2[i] - l2[i - 1]));
        check(maxStep < 0.1, "Acoustic: subir Space A de golpe no pega saltos", juce::String(maxStep, 4));
    }

    std::cout << (failures == 0 ? "\nTODO OK" : "\nHAY FALLAS: ") << (failures == 0 ? "" : std::to_string(failures).c_str()) << std::endl;
    return failures == 0 ? 0 : 1;
}
