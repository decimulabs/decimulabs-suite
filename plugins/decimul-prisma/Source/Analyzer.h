#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "dsp/PrismaFft.h"
#include <array>
#include <vector>

/**
    Analizador de espectro de Prisma. El hilo de audio solo copia muestras (suma mono) a una cola;
    el hilo de mensajes saca esas muestras, calcula la FFT de las últimas N (según la resolución
    elegida) y la pasa a una grilla logarítmica fija de `points` puntos (10 Hz a 30 kHz), en dB
    calibrados: una senoidal a 0 dBFS se ve en 0 dB.
*/
class SpectrumAnalyzer
{
public:
    static constexpr int points = 400;
    static constexpr double loHz = 10.0, hiHz = 30000.0;
    static constexpr int numResolutions = 4;   // 2048, 4096, 8192, 16384 (a 44.1/48 kHz)
    static constexpr int numSpeeds = 5;

    static double pointHz(int i) { return loHz * std::pow(hiHz / loHz, i / (double) (points - 1)); }

    void prepare(double fs)
    {
        sampleRate = fs;
        scale = fs <= 50000.0 ? 1 : (fs <= 100000.0 ? 2 : 4);
        const int maxN = (2048 << (numResolutions - 1)) * scale;
        history.assign((size_t) maxN, 0.0f);
        histPos = 0;
        fifoData.assign((size_t) fifoSize, 0.0f);
        fifo.reset();
        frame.assign((size_t) maxN, 0.0f);
        spec.assign((size_t) (maxN / 2 + 1), {});
        for (int r = 0; r < numResolutions; ++r)
        {
            const int n = (2048 << r) * scale;
            auto& R = res[(size_t) r];
            R.fft.init(n);
            R.window.resize((size_t) n);
            double sum = 0.0;
            for (int i = 0; i < n; ++i)
            {
                R.window[(size_t) i] = (float) (0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * i / n));
                sum += R.window[(size_t) i];
            }
            R.norm = (float) (2.0 / sum);
            // Bins que caen en cada punto de la grilla (o interpolación si el punto es más fino que un bin).
            for (int i = 0; i < points; ++i)
            {
                const double fLo = i == 0 ? pointHz(0) : std::sqrt(pointHz(i - 1) * pointHz(i));
                const double fHi = i == points - 1 ? pointHz(i) : std::sqrt(pointHz(i) * pointHz(i + 1));
                const double binHz = fs / n;
                R.kLo[(size_t) i] = juce::jlimit(1, n / 2, (int) std::ceil(fLo / binHz));
                R.kHi[(size_t) i] = juce::jlimit(1, n / 2, (int) std::floor(fHi / binHz));
                R.exact[(size_t) i] = (float) (pointHz(i) / binHz);
            }
        }
        db.fill(-120.0f);
        peak.fill(-120.0f);
        level = -120.0f;
    }

    /** Hilo de audio: agrega muestras (si hay dos canales, su promedio). */
    void push(const float* a, const float* b, int n)
    {
        int s1, z1, s2, z2;
        fifo.prepareToWrite(n, s1, z1, s2, z2);
        auto put = [&](int start, int count, int offset)
        {
            for (int i = 0; i < count; ++i)
            {
                const float v = b != nullptr ? 0.5f * (a[offset + i] + b[offset + i]) : a[offset + i];
                fifoData[(size_t) (start + i)] = v;
            }
        };
        put(s1, z1, 0);
        put(s2, z2, z1);
        fifo.finishedWrite(z1 + z2);
    }

    /** Hilo de mensajes: incorpora lo nuevo y recalcula. Devuelve si había señal nueva. */
    bool update(int resolution, int speed, bool freeze)
    {
        int s1, z1, s2, z2;
        const int ready = fifo.getNumReady();
        fifo.prepareToRead(ready, s1, z1, s2, z2);
        auto take = [&](int start, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                history[(size_t) histPos] = fifoData[(size_t) (start + i)];
                histPos = (histPos + 1) % (int) history.size();
            }
        };
        take(s1, z1);
        take(s2, z2);
        fifo.finishedRead(z1 + z2);
        if (freeze)
            return false;

        const int r = juce::jlimit(0, numResolutions - 1, resolution);
        auto& R = res[(size_t) r];
        const int n = R.fft.size();
        const int hs = (int) history.size();
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float v = history[(size_t) ((histPos - n + i + hs) % hs)];
            pk = std::max(pk, std::abs(v));
            frame[(size_t) i] = v * R.window[(size_t) i];
        }
        level = juce::Decibels::gainToDecibels(pk, -120.0f);
        R.fft.forward(frame.data(), spec.data());

        static constexpr float decay[numSpeeds] = { 0.04f, 0.08f, 0.16f, 0.32f, 0.6f };
        const float fall = decay[juce::jlimit(0, numSpeeds - 1, speed)];
        for (int i = 0; i < points; ++i)
        {
            float mag;
            const int kLo = R.kLo[(size_t) i], kHi = R.kHi[(size_t) i];
            if (kHi >= kLo)
            {
                mag = 0.0f;
                for (int k = kLo; k <= kHi; ++k)
                    mag = std::max(mag, std::abs(spec[(size_t) k]));
            }
            else
            {
                const float x = R.exact[(size_t) i];
                const int k0 = juce::jlimit(0, n / 2 - 1, (int) x);
                const float t = juce::jlimit(0.0f, 1.0f, x - (float) k0);
                mag = (1.0f - t) * std::abs(spec[(size_t) k0]) + t * std::abs(spec[(size_t) k0 + 1]);
            }
            const float d = juce::Decibels::gainToDecibels(mag * R.norm, -120.0f);
            auto& cur = db[(size_t) i];
            cur += (d > cur ? 0.7f : fall) * (d - cur);
            peak[(size_t) i] = std::max(peak[(size_t) i] - 0.15f, cur);
        }
        return z1 + z2 > 0;
    }

    void clearPeaks() { peak.fill(-120.0f); }

    std::array<float, points> db {}, peak {};
    float level = -120.0f;

private:
    struct Resolution
    {
        prisma::RealFft<float> fft;
        std::vector<float> window;
        float norm = 1.0f;
        std::array<int, points> kLo {}, kHi {};
        std::array<float, points> exact {};
    };

    static constexpr int fifoSize = 1 << 16;
    double sampleRate = 48000.0;
    int scale = 1;
    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoData, history, frame;
    std::vector<std::complex<float>> spec;
    int histPos = 0;
    std::array<Resolution, numResolutions> res;
};
