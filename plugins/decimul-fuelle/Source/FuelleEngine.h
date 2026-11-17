#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

/**
    Motor de DecimuLabs Fuelle (Ducking Reverb): reverb que se agacha mientras suena la señal y
    "respira" en los silencios. La reverb es del tipo Schroeder/Moorer (8 filtros peine con
    amortiguación + 4 pasa-todos por canal, con los tiempos clásicos de dominio público de
    Freeverb, escalados a la frecuencia de muestreo). Suma predelay, filtro de graves de la cola,
    ancho y mezcla. C++ puro, sin JUCE, sin latencia.
*/
namespace fuelle
{
    constexpr double pi = 3.14159265358979323846;

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
            const float over = std::clamp((envDb - thresholdDb) / 12.0f, 0.0f, 1.0f);
            const float target = std::pow(10.0f, -amount * 30.0f * over / 20.0f);
            gain += (target < gain ? atk : rel) * (target - gain);
            return gain;
        }
    };

    struct Params
    {
        float size = 0.7f;          // 0..1 (largo de la cola)
        float damping = 0.4f;       // 0..1 (cuánto se apagan los agudos)
        float predelayMs = 20.0f;
        float lowCutHz = 200.0f;
        float width = 1.0f;         // 0..1
        float duck = 0.7f;
        float thresholdDb = -30.0f;
        float releaseMs = 400.0f;
        float mix = 0.3f;
    };

    class Engine
    {
    public:
        void prepare(double sampleRate)
        {
            sr = sampleRate > 1000.0 ? sampleRate : 44100.0;
            const double k = sr / 44100.0;
            static const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
            static const int apT[4] = { 556, 441, 341, 225 };
            const int spread = 23;
            for (int c = 0; c < 2; ++c)
            {
                for (int i = 0; i < 8; ++i)
                    combs[(size_t) c][(size_t) i].buf.assign((size_t) ((combT[i] + c * spread) * k) + 1, 0.0f);
                for (int i = 0; i < 4; ++i)
                    aps[(size_t) c][(size_t) i].buf.assign((size_t) ((apT[i] + c * spread) * k) + 1, 0.0f);
            }
            pre.assign((size_t) (sr * 0.25) + 2, 0.0f);
            reset();
        }

        void reset()
        {
            for (auto& ch : combs) for (auto& cf : ch) { std::fill(cf.buf.begin(), cf.buf.end(), 0.0f); cf.idx = 0; cf.store = 0; }
            for (auto& ch : aps) for (auto& a : ch) { std::fill(a.buf.begin(), a.buf.end(), 0.0f); a.idx = 0; }
            std::fill(pre.begin(), pre.end(), 0.0f);
            preIdx = 0;
            hpL = hpR = hpxL = hpxR = 0.0f;
            ducker = {};
        }

        void process(float* l, float* r, int n, const Params& p)
        {
            const float room = 0.7f + 0.28f * std::clamp(p.size, 0.0f, 1.0f);
            const float damp = 0.05f + 0.6f * std::clamp(p.damping, 0.0f, 1.0f);
            const int preLen = (int) pre.size();
            const int preDelay = std::clamp((int) (p.predelayMs * 0.001f * (float) sr), 0, preLen - 2);
            const float hp = (float) std::exp(-2.0 * pi * std::clamp((double) p.lowCutHz, 20.0, 2000.0) / sr);
            const float w = std::clamp(p.width, 0.0f, 1.0f);
            const float wet1 = 0.5f * (1.0f + w), wet2 = 0.5f * (1.0f - w);

            for (int i = 0; i < n; ++i)
            {
                const float inL = l[i], inR = r[i];
                const float mono = 0.5f * (inL + inR);
                pre[(size_t) preIdx] = mono;
                const int rd = (preIdx - preDelay + preLen) % preLen;
                preIdx = (preIdx + 1) % preLen;
                const float input = pre[(size_t) rd] * 0.03f;     // ganancia fija de entrada (como en Freeverb)

                float out[2] = { 0.0f, 0.0f };
                for (int c = 0; c < 2; ++c)
                {
                    float acc = 0.0f;
                    for (auto& cf : combs[(size_t) c])
                    {
                        const float y = cf.buf[(size_t) cf.idx];
                        cf.store = y * (1.0f - damp) + cf.store * damp;
                        cf.buf[(size_t) cf.idx] = input + cf.store * room;
                        cf.idx = (cf.idx + 1) % (int) cf.buf.size();
                        acc += y;
                    }
                    for (auto& a : aps[(size_t) c])
                    {
                        const float b = a.buf[(size_t) a.idx];
                        const float y = -acc + b;
                        a.buf[(size_t) a.idx] = acc + b * 0.5f;
                        a.idx = (a.idx + 1) % (int) a.buf.size();
                        acc = y;
                    }
                    out[c] = acc;
                }
                float wl = out[0] * wet1 + out[1] * wet2;
                float wr = out[1] * wet1 + out[0] * wet2;
                // Graves fuera de la cola (pasa-altos de 1er orden), para que no embarre.
                const float hl = hp * (hpL + wl - hpxL); hpxL = wl; hpL = hl;
                const float hr = hp * (hpR + wr - hpxR); hpxR = wr; hpR = hr;

                const float g = ducker.process(mono, sr, p.duck, p.thresholdDb, p.releaseMs);
                l[i] = inL + p.mix * g * hl;
                r[i] = inR + p.mix * g * hr;
            }
        }

        float duckGain() const { return ducker.gain; }

    private:
        struct Comb { std::vector<float> buf; int idx = 0; float store = 0.0f; };
        struct AllPass { std::vector<float> buf; int idx = 0; };
        double sr = 48000.0;
        std::array<std::array<Comb, 8>, 2> combs;
        std::array<std::array<AllPass, 4>, 2> aps;
        std::vector<float> pre;
        int preIdx = 0;
        float hpL = 0, hpR = 0, hpxL = 0, hpxR = 0;
        Ducker ducker;
    };
}
