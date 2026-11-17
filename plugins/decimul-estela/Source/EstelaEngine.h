#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

/**
    Motor de DecimuLabs Estela (Ducking Delay): delay estéreo que se agacha mientras suena la
    señal de entrada y vuelve en los silencios. Tiempo sincronizado al tempo (o en ms),
    realimentación con filtros pasa-altos y pasa-bajos, ping-pong, temblor de cinta (WOBBLE) y
    mezcla. C++ puro, sin JUCE, sin latencia.
*/
namespace estela
{
    constexpr double pi = 3.14159265358979323846;

    /** Sigue el nivel de la entrada y da la ganancia del "agachado" (1 = sin agachar). */
    struct Ducker
    {
        float env = 0.0f;
        float gain = 1.0f;
        float process(float in, double sr, float amount, float thresholdDb, float releaseMs)
        {
            const float rect = std::fabs(in);
            const float atk = (float) (1.0 - std::exp(-1.0 / (0.005 * sr)));
            const float rel = (float) (1.0 - std::exp(-1.0 / (std::max(10.0f, releaseMs) * 0.001 * sr)));
            // El detector suelta rápido (60 ms); el RELEASE es lo que tarda en volver el efecto.
            const float envRel = (float) (1.0 - std::exp(-1.0 / (0.06 * sr)));
            env += (rect > env ? atk : envRel) * (rect - env);
            const float envDb = 20.0f * std::log10(env + 1.0e-9f);
            // Hasta 30 dB menos cuando la entrada pasa 12 dB por encima del umbral.
            const float over = std::clamp((envDb - thresholdDb) / 12.0f, 0.0f, 1.0f);
            const float target = std::pow(10.0f, -amount * 30.0f * over / 20.0f);
            gain += (target < gain ? atk : rel) * (target - gain);
            return gain;
        }
    };

    struct Params
    {
        float timeMs = 375.0f;      // tiempo del eco (ya calculado si es sincronizado)
        float feedback = 0.45f;     // 0..0.95
        bool pingPong = false;
        float lowCutHz = 150.0f;
        float highCutHz = 6000.0f;
        float wobble = 0.0f;        // 0..1: temblor de cinta
        float duck = 0.7f;          // 0..1
        float thresholdDb = -30.0f;
        float releaseMs = 300.0f;
        float mix = 0.3f;           // 0..1
    };

    class Engine
    {
    public:
        void prepare(double sampleRate)
        {
            sr = sampleRate > 1000.0 ? sampleRate : 44100.0;
            const size_t len = (size_t) (sr * 2.6) + 4;      // hasta 2.5 s de eco
            bufL.assign(len, 0.0f);
            bufR.assign(len, 0.0f);
            reset();
        }

        void reset()
        {
            std::fill(bufL.begin(), bufL.end(), 0.0f);
            std::fill(bufR.begin(), bufR.end(), 0.0f);
            write = 0;
            lpL = lpR = hpL = hpR = hpxL = hpxR = 0.0f;
            phase = 0.0;
            smoothTime = -1.0;
            ducker = {};
        }

        void process(float* l, float* r, int n, const Params& p)
        {
            const int len = (int) bufL.size();
            const double targetTime = std::clamp((double) p.timeMs, 1.0, 2500.0) * 0.001 * sr;
            if (smoothTime < 0.0) smoothTime = targetTime;
            const float lp = (float) std::exp(-2.0 * pi * std::clamp((double) p.highCutHz, 500.0, 20000.0) / sr);
            const float hp = (float) std::exp(-2.0 * pi * std::clamp((double) p.lowCutHz, 20.0, 2000.0) / sr);
            const float fb = std::clamp(p.feedback, 0.0f, 0.95f);
            const double wobbleDepth = p.wobble * 0.004 * sr;        // hasta ±4 ms de vaivén
            const double wobbleInc = 2.0 * pi * 0.9 / sr;

            for (int i = 0; i < n; ++i)
            {
                smoothTime += 0.0005 * (targetTime - smoothTime);     // cambiar el tiempo no hace clics
                phase += wobbleInc;
                if (phase > 2.0 * pi) phase -= 2.0 * pi;
                const double d = std::clamp(smoothTime + wobbleDepth * (0.6 * std::sin(phase) + 0.4 * std::sin(phase * 3.7)),
                                            1.0, (double) len - 3.0);
                const float el = read(bufL, d), er = read(bufR, d);

                // Filtros de los ecos: cada vuelta se oscurece y pierde graves.
                lpL = (1.0f - lp) * el + lp * lpL;  lpR = (1.0f - lp) * er + lp * lpR;
                const float hl = hp * (hpL + lpL - hpxL); hpxL = lpL; hpL = hl;
                const float hr = hp * (hpR + lpR - hpxR); hpxR = lpR; hpR = hr;

                const float inL = l[i], inR = r[i];
                if (p.pingPong)
                {
                    // La entrada (en mono) arranca por la izquierda y cada eco cruza de lado.
                    bufL[(size_t) write] = 0.5f * (inL + inR) + fb * hr;
                    bufR[(size_t) write] = fb * hl;
                }
                else
                {
                    bufL[(size_t) write] = inL + fb * hl;
                    bufR[(size_t) write] = inR + fb * hr;
                }
                write = (write + 1) % len;

                const float g = ducker.process(0.5f * (inL + inR), sr, p.duck, p.thresholdDb, p.releaseMs);
                l[i] = inL + p.mix * g * hl;
                r[i] = inR + p.mix * g * hr;
            }
        }

        float duckGain() const { return ducker.gain; }

    private:
        float read(const std::vector<float>& b, double delay) const
        {
            const int len = (int) b.size();
            double pos = (double) write - delay;
            while (pos < 0) pos += len;
            const int i0 = (int) pos;
            const float f = (float) (pos - i0);
            return b[(size_t) i0] + f * (b[(size_t) ((i0 + 1) % len)] - b[(size_t) i0]);
        }

        double sr = 48000.0;
        std::vector<float> bufL, bufR;
        int write = 0;
        float lpL = 0, lpR = 0, hpL = 0, hpR = 0, hpxL = 0, hpxR = 0;
        double phase = 0.0, smoothTime = -1.0;
        Ducker ducker;
    };
}
