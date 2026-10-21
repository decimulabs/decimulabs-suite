#pragma once

// Motor del modulador de transientes (Ataque): solo biblioteca estándar, se prueba sin JUCE.
//
// Dos envolventes de potencia (una rápida y una lenta) siguen el nivel de la señal (el mayor
// de L y R, así los dos canales se mueven juntos). Su diferencia en dB dice dónde estamos:
//   rápida > lenta  -> ataque (arranca un golpe)        -> manda la perilla ATTACK
//   rápida < lenta  -> cola (el golpe se va apagando)    -> manda la perilla SUSTAIN
// Como es una diferencia en dB, el efecto es el mismo con golpes suaves o fuertes (no depende
// del nivel). La ganancia se suaviza ~1 ms y no hay latencia. Por debajo de ~-75 dBFS no actúa
// (para no levantar el ruido de fondo).

#include <algorithm>
#include <cmath>

namespace ataque
{
class TransientShaper
{
public:
    static constexpr float maxGainDb = 12.0f;

    void prepare(double sampleRate)
    {
        sr = sampleRate;
        reset();
        updateCoefs();
    }

    void reset()
    {
        pFast = pSlow = 0.0f;
        gainDb = 0.0f;
        lastDiffDb = 0.0f;
        lastLevelDb = -120.0f;
        lastFastDb = -120.0f;
    }

    /** attack y sustain de -1 a +1; speed de 0 a 1 (0.5 = normal; más alto = detecta más rápido). */
    void set(float attack, float sustain, float speed01)
    {
        atk = std::clamp(attack, -1.0f, 1.0f);
        sus = std::clamp(sustain, -1.0f, 1.0f);
        const float scale = std::pow(2.0f, (std::clamp(speed01, 0.0f, 1.0f) - 0.5f) * 2.0f);
        if (std::abs(scale - timeScale) > 1.0e-4f)
        {
            timeScale = scale;
            updateCoefs();
        }
    }

    /** Procesa en el lugar. `r` puede ser nullptr (mono). */
    void process(float* l, float* r, int n)
    {
        constexpr float eps = 1.0e-10f;
        for (int i = 0; i < n; ++i)
        {
            const float xl = l[i];
            const float xr = r != nullptr ? r[i] : xl;
            const float p = std::max(xl * xl, xr * xr);

            pFast += (p - pFast) * (p > pFast ? fastAtt : fastRel);
            pSlow += (p - pSlow) * (p > pSlow ? slowAtt : slowRel);

            const float slowDb = 10.0f * std::log10(pSlow + eps);
            const float diff = 10.0f * std::log10((pFast + eps) / (pSlow + eps));
            const float weight = std::clamp((slowDb + 75.0f) * (1.0f / 20.0f), 0.0f, 1.0f);

            float target = diff > 0.0f ? atk * diff : sus * (-diff);
            target = std::clamp(target, -maxGainDb, maxGainDb) * weight;

            gainDb += (target - gainDb) * gainSmooth;
            const float g = std::exp(gainDb * 0.115129255f);
            l[i] = xl * g;
            if (r != nullptr)
                r[i] = xr * g;

            lastDiffDb = diff;
            lastLevelDb = slowDb;
            lastFastDb = 10.0f * std::log10(pFast + eps);
        }
    }

    /** Para afinar el motor en las pruebas: tiempos base en ms (se escalan con Speed). */
    void setTimes(float fastAttMs, float fastRelMs, float slowAttMs, float slowRelMs, float gainMs)
    {
        times[0] = fastAttMs; times[1] = fastRelMs; times[2] = slowAttMs; times[3] = slowRelMs; times[4] = gainMs;
        updateCoefs();
    }

    float getGainDb() const { return gainDb; }
    float getDiffDb() const { return lastDiffDb; }
    float getLevelDb() const { return lastLevelDb; }          // envolvente lenta
    float getFastLevelDb() const { return lastFastDb; }       // envolvente rápida (la que se dibuja)

private:
    void updateCoefs()
    {
        auto coef = [this](float ms) { return (float) (1.0 - std::exp(-timeScale / (ms * 0.001 * sr))); };
        fastAtt = coef(times[0]);
        fastRel = coef(times[1]);
        slowAtt = coef(times[2]);
        slowRel = coef(times[3]);
        gainSmooth = coef(times[4]);
    }

    double sr = 48000.0;
    float times[5] = { 0.5f, 16.0f, 25.0f, 180.0f, 0.8f };
    float atk = 0.0f, sus = 0.0f, timeScale = 1.0f;
    float fastAtt = 0.1f, fastRel = 0.01f, slowAtt = 0.01f, slowRel = 0.001f, gainSmooth = 0.1f;
    float pFast = 0.0f, pSlow = 0.0f, gainDb = 0.0f, lastDiffDb = 0.0f, lastLevelDb = -120.0f, lastFastDb = -120.0f;
};
} // namespace ataque
