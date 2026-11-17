#pragma once

// Motor de Vise (compresor de bus): solo biblioteca estándar, se prueba sin JUCE.
//
// Compresor de "pegamento" para grupos y master, pensado para tener pocas perillas y sonar bien:
//   entrada ► filtro pasa-altos del sidechain ► detector (pico con caída suave o RMS) ► enlace estéreo ►
//   curva de ganancia con rodilla suave ► suavizado en dB con ataque y caída (caída automática opcional) ►
//   ganancia + makeup ► mezcla paralela con la señal seca ► salida
// Todo en el dominio logarítmico, sin latencia (no mira hacia adelante).

#include <algorithm>
#include <cmath>

namespace vise
{
inline float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }
inline float gainToDb(float g) { return 20.0f * std::log10(std::max(g, 1.0e-9f)); }

struct Params
{
    float thresholdDb = -18.0f;     // -40 .. 0
    float ratio = 4.0f;             // 1.1 .. 20 (20 se comporta casi como limitador)
    float attackMs = 10.0f;         // 0.1 .. 50
    float releaseMs = 300.0f;       // 20 .. 1500
    bool autoRelease = false;       // la caída depende de cuánto se está comprimiendo
    float kneeDb = 6.0f;            // 0 .. 12
    float makeupDb = 0.0f;          // 0 .. 24
    bool autoMakeup = false;
    float mix = 100.0f;             // % de señal comprimida (el resto es la original: compresión paralela)
    float scHpHz = 20.0f;           // 20 (apagado) .. 400 Hz: pasa-altos del sidechain
    float link = 100.0f;            // % de enlace estéreo
    bool rms = false;               // detector RMS (más "musical") en vez de pico
};

class BusCompressor
{
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        peakDecay = std::exp(-1.0f / (0.012f * (float) sr));
        rmsCoef = std::exp(-1.0f / (0.020f * (float) sr));
        smoothCoef = 1.0f - std::exp(-1.0f / (0.02f * (float) sr));
        reset();
        setParams(params);
        thrS = params.thresholdDb; invRatioS = 1.0f / params.ratio; kneeS = params.kneeDb;
        makeupS = computeMakeup(params); mixS = params.mix * 0.01f; linkS = params.link * 0.01f;
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            hpZ1[c] = hpZ2[c] = 0.0;
            peakEnv[c] = 0.0f;
            power[c] = 0.0f;
            grDb[c] = 0.0f;
        }
        meterGr = 0.0f; meterIn = -120.0f;
        accGr = 0.0f; accIn = 0.0f;
    }

    void setParams(const Params& p)
    {
        params = p;
        atkCoef = std::exp(-1.0f / (std::max(p.attackMs, 0.05f) * 0.001f * (float) sr));
        relCoef = std::exp(-1.0f / (std::max(p.releaseMs, 5.0f) * 0.001f * (float) sr));
        setHighpass(p.scHpHz);
    }

    /** Procesa en el lugar (estéreo). R puede ser nullptr (mono: se usa solo L). */
    void process(float* l, float* r, int n)
    {
        const bool stereo = r != nullptr;
        for (int i = 0; i < n; ++i)
        {
            thrS += (params.thresholdDb - thrS) * smoothCoef;
            invRatioS += (1.0f / params.ratio - invRatioS) * smoothCoef;
            kneeS += (params.kneeDb - kneeS) * smoothCoef;
            makeupS += (computeMakeup(params) - makeupS) * smoothCoef;
            mixS += (params.mix * 0.01f - mixS) * smoothCoef;
            linkS += (params.link * 0.01f - linkS) * smoothCoef;

            const float x[2] = { l[i], stereo ? r[i] : l[i] };
            float level[2];
            for (int c = 0; c < (stereo ? 2 : 1); ++c)
            {
                const float s = highpass(c, x[c]);
                if (params.rms)
                {
                    power[c] = rmsCoef * power[c] + (1.0f - rmsCoef) * s * s;
                    level[c] = std::sqrt(power[c]) * 1.41421356f;          // seno de pico 1.0 = 0 dBFS
                }
                else
                {
                    peakEnv[c] = std::max(std::abs(s), peakEnv[c] * peakDecay);
                    level[c] = peakEnv[c];
                }
            }
            if (!stereo)
                level[1] = level[0];
            const float mx = std::max(level[0], level[1]);

            float gain[2];
            for (int c = 0; c < 2; ++c)
            {
                const float detected = level[c] + (mx - level[c]) * linkS;
                const float inDb = gainToDb(detected);
                const float target = gainComputer(inDb);                 // <= 0 dB

                // suavizado en dB: ataque cuando hay que comprimir más, caída cuando hay que soltar
                float rc = relCoef;
                if (params.autoRelease)
                {
                    const float ms = std::min(60.0f + 45.0f * std::abs(grDb[c]), 1500.0f);
                    rc = std::exp(-1.0f / (ms * 0.001f * (float) sr));
                }
                const float coef = target < grDb[c] ? atkCoef : rc;
                grDb[c] = coef * grDb[c] + (1.0f - coef) * target;
                gain[c] = dbToGain(grDb[c] + makeupS);
            }

            const float wetL = x[0] * gain[0];
            l[i] = x[0] + (wetL - x[0]) * mixS;
            if (stereo)
            {
                const float wetR = x[1] * gain[1];
                r[i] = x[1] + (wetR - x[1]) * mixS;
            }

            accGr = std::max(accGr, -std::min(grDb[0], grDb[1]));
            accIn = std::max(accIn, std::max(std::abs(x[0]), std::abs(x[1])));
        }
    }

    /** Lectura para la pantalla: la mayor reducción (dB, positiva) y el pico de la entrada (dBFS) desde la última lectura. */
    void takeMeters(float& grOut, float& inPeakDbOut)
    {
        grOut = accGr;
        inPeakDbOut = gainToDb(accIn);
        accGr = 0.0f;
        accIn = 0.0f;
    }

    /** Reducción actual (dB, positiva) del canal más comprimido. */
    float currentGr() const { return -std::min(grDb[0], grDb[1]); }

    static float autoMakeupDb(float thresholdDb, float ratio)
    {
        return std::clamp(-thresholdDb * (1.0f - 1.0f / ratio) * 0.5f, 0.0f, 24.0f);
    }

private:
    static float computeMakeup(const Params& p)
    {
        return p.autoMakeup ? autoMakeupDb(p.thresholdDb, p.ratio) : p.makeupDb;
    }

    /** Reducción pedida (<= 0 dB) para un nivel de entrada en dB, con rodilla suave. */
    float gainComputer(float inDb) const
    {
        const float over = inDb - thrS;
        const float slope = invRatioS - 1.0f;                           // negativa
        if (kneeS > 0.01f && 2.0f * over > -kneeS && 2.0f * over < kneeS)
        {
            const float t = over + 0.5f * kneeS;
            return slope * t * t / (2.0f * kneeS);
        }
        return over > 0.0f ? slope * over : 0.0f;
    }

    void setHighpass(float hz)
    {
        hpActive = hz > 22.0f;
        if (!hpActive)
            return;
        const double w = 2.0 * 3.14159265358979323846 * std::min((double) hz, sr * 0.45) / sr;
        const double c = std::cos(w), alpha = std::sin(w) / (2.0 * 0.70710678);
        const double a0 = 1.0 + alpha;
        hb0 = (1.0 + c) * 0.5 / a0; hb1 = -(1.0 + c) / a0; hb2 = hb0;
        ha1 = -2.0 * c / a0; ha2 = (1.0 - alpha) / a0;
    }

    float highpass(int ch, float x)
    {
        if (!hpActive)
            return x;
        const double in = (double) x;
        const double y = hb0 * in + hpZ1[ch];
        hpZ1[ch] = hb1 * in - ha1 * y + hpZ2[ch];
        hpZ2[ch] = hb2 * in - ha2 * y;
        return (float) y;
    }

    double sr = 48000.0;
    Params params;
    float atkCoef = 0.99f, relCoef = 0.999f, peakDecay = 0.99f, rmsCoef = 0.99f, smoothCoef = 0.01f;
    float thrS = -18.0f, invRatioS = 0.25f, kneeS = 6.0f, makeupS = 0.0f, mixS = 1.0f, linkS = 1.0f;
    float peakEnv[2] = { 0, 0 }, power[2] = { 0, 0 }, grDb[2] = { 0, 0 };
    bool hpActive = false;
    double hb0 = 1, hb1 = 0, hb2 = 0, ha1 = 0, ha2 = 0;
    double hpZ1[2] = { 0, 0 }, hpZ2[2] = { 0, 0 };
    float meterGr = 0.0f, meterIn = -120.0f, accGr = 0.0f, accIn = 0.0f;
};
} // namespace vise
