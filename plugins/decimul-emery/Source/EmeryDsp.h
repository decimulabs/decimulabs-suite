#pragma once

// Motor de Emery (de-esser + de-popper): solo biblioteca estándar, se prueba sin JUCE.
//
// Dos secciones en serie, cada una con su detector:
//   SIBILANCIA: pasabanda alrededor de la frecuencia elegida (donde viven las eses).
//   GOLPES:     pasabajos de 4º orden en la frecuencia elegida (donde pega el aire de la p y la b).
// El detector sigue el pico de esa banda (el mayor de L y R, así los dos canales se mueven
// juntos). Lo que pase del umbral se baja, hasta un máximo (AMOUNT). Dos formas de bajarlo:
//   BAND: ese: se resta solo esa banda  ->  salida = entrada - (1 - g) * banda  (el resto no se toca)
//         golpe: un shelf grave que baja según la reducción (en fase con la señal, sin rebotes)
//   WIDE: baja todo el sonido      ->  salida = entrada * g
// Sin latencia (no mira hacia adelante): el primer instante de una ese muy brusca pasa
// apenas antes de que el detector reaccione (0.2 ms).

#include <algorithm>
#include <array>
#include <cmath>

namespace emery
{
inline float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }
inline float gainToDb(float g) { return 20.0f * std::log10(std::max(g, 1.0e-6f)); }

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<double, 2> z1 {}, z2 {};

    void reset() { z1.fill(0.0); z2.fill(0.0); }

    float process(float x, int ch)
    {
        const double in = (double) x;
        const double out = b0 * in + z1[(size_t) ch];
        z1[(size_t) ch] = b1 * in - a1 * out + z2[(size_t) ch];
        z2[(size_t) ch] = b2 * in - a2 * out;
        return (float) out;
    }

    // Pasabanda RBJ con ganancia 0 dB en el centro.
    void setBandpass(double sr, double freq, double q)
    {
        const double w = 2.0 * 3.14159265358979323846 * std::min(freq, sr * 0.45) / sr;
        const double alpha = std::sin(w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = alpha / a0; b1 = 0.0; b2 = -alpha / a0;
        a1 = -2.0 * std::cos(w) / a0; a2 = (1.0 - alpha) / a0;
    }

    // Shelf grave RBJ (pendiente S = 1). Con gainDb = 0 es la identidad.
    void setLowShelf(double sr, double freq, double gainDb)
    {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w = 2.0 * 3.14159265358979323846 * std::min(freq, sr * 0.45) / sr;
        const double c = std::cos(w);
        const double alpha = std::sin(w) / 2.0 * std::sqrt(2.0);
        const double t = 2.0 * std::sqrt(A) * alpha;
        const double a0 = (A + 1.0) + (A - 1.0) * c + t;
        b0 = A * ((A + 1.0) - (A - 1.0) * c + t) / a0;
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * c) / a0;
        b2 = A * ((A + 1.0) - (A - 1.0) * c - t) / a0;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * c) / a0;
        a2 = ((A + 1.0) + (A - 1.0) * c - t) / a0;
    }

    void setLowpass(double sr, double freq, double q)
    {
        const double w = 2.0 * 3.14159265358979323846 * std::min(freq, sr * 0.45) / sr;
        const double alpha = std::sin(w) / (2.0 * q);
        const double c = std::cos(w);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 - c) * 0.5 / a0; b1 = (1.0 - c) / a0; b2 = b0;
        a1 = -2.0 * c / a0; a2 = (1.0 - alpha) / a0;
    }
};

/** Una sección: detector + reducción. Se usa dos veces (eses y golpes). */
struct Section
{
    bool isPop = false;
    double sr = 48000.0;

    // parámetros (los pone el procesador)
    bool on = true;
    bool wide = false;
    float freqTarget = 6500.0f, thresholdDb = -30.0f, maxReductionDb = 10.0f;

    // estado
    float freq = 6500.0f;
    float envDb = -120.0f;       // nivel de la banda (último valor)
    float reductionDb = 0.0f;    // reducción aplicada (positiva = baja)
    float gain = 1.0f;

    Biquad shelf;                // golpe en modo BAND: shelf grave dinámico (la reducción cambia sus coeficientes)
    Biquad f1, f2;               // ese: f1 = pasabanda; golpe: f1 y f2 = pasabajos Butterworth de 4º orden
    float env = 0.0f;
    float envRel = 0.0f, redAtk = 0.0f, redRel = 0.0f;
    int coefCounter = 0;

    void prepare(double sampleRate, bool pop)
    {
        sr = sampleRate;
        isPop = pop;
        freq = freqTarget;
        // tiempos: la ese es más rápida que el golpe grave
        envRel = (float) coef(pop ? 40.0 : 8.0);
        redAtk = (float) coef(pop ? 0.6 : 0.25); redRel = (float) coef(pop ? 90.0 : 28.0);
        reset();
        updateFilters();
    }

    void reset()
    {
        f1.reset(); f2.reset(); shelf.reset();
        shelf.setLowShelf(sr, freq, 0.0);
        env = 0.0f; envDb = -120.0f; reductionDb = 0.0f; gain = 1.0f;
    }

    double coef(double ms) const { return std::exp(-1.0 / (ms * 0.001 * sr)); }

    void updateFilters()
    {
        if (isPop)
        {
            f1.setLowpass(sr, freq, 0.5412);
            f2.setLowpass(sr, freq, 1.3066);
        }
        else
            f1.setBandpass(sr, freq, 1.4);
    }

    /** Un paso de suavizado de la frecuencia (llamar cada ~32 muestras). */
    void smoothFreq()
    {
        if (freq == freqTarget)
            return;
        freq += (freqTarget - freq) * 0.2f;
        if (std::abs(freqTarget - freq) < freqTarget * 0.0005f)
            freq = freqTarget;
        updateFilters();
    }

    /** Banda de un canal (lo que se detecta y, en modo BAND, lo que se resta). */
    float band(float x, int ch)
    {
        const float y = f1.process(x, ch);
        return isPop ? f2.process(y, ch) : y;
    }

    /** Sigue el pico de la banda (|b| del mayor canal) y actualiza la ganancia. */
    void detect(float peak)
    {
        env = std::max(std::abs(peak), (float) (envRel * env));   // pico con caída suave
        envDb = gainToDb(env);
        const float target = on ? std::clamp(envDb - thresholdDb, 0.0f, maxReductionDb) : 0.0f;
        const float c = target > reductionDb ? (float) redAtk : (float) redRel;
        reductionDb = c * reductionDb + (1.0f - c) * target;
        if (reductionDb < 0.001f && target == 0.0f) reductionDb = 0.0f;
        gain = dbToGain(-reductionDb);
    }

    /** Pone el shelf grave según la reducción actual (golpes en modo BAND). */
    void updateShelf() { shelf.setLowShelf(sr, freq, wide ? 0.0 : -(double) reductionDb); }

    float apply(float x, float bandSample, int ch)
    {
        if (isPop)
        {
            const float s = shelf.process(x, ch);      // siempre corre, así cambiar BAND/WIDE no hace clicks
            return wide ? x * gain : s;
        }
        return wide ? x * gain : x - (1.0f - gain) * bandSample;
    }
};

class DeEsserPopper
{
public:
    void prepare(double sampleRate)
    {
        ess.prepare(sampleRate, false);
        pop.prepare(sampleRate, true);
    }

    void reset() { ess.reset(); pop.reset(); }

    Section ess, pop;
    bool listenEss = false, listenPop = false;

    /** Procesa en el lugar. R puede ser nullptr (mono). */
    void process(float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            if (++ess.coefCounter >= 32) { ess.coefCounter = 0; ess.smoothFreq(); }
            if (++pop.coefCounter >= 32) { pop.coefCounter = 0; pop.smoothFreq(); }

            const float xl = L[i];
            const float xr = R != nullptr ? R[i] : 0.0f;

            // --- sibilancia ---
            const float bl = ess.band(xl, 0);
            const float br = R != nullptr ? ess.band(xr, 1) : 0.0f;
            ess.detect(std::max(std::abs(bl), std::abs(br)));
            const float yl = ess.apply(xl, bl, 0);
            const float yr = R != nullptr ? ess.apply(xr, br, 1) : 0.0f;

            // --- golpes ---
            const float pl = pop.band(yl, 0);
            const float pr = R != nullptr ? pop.band(yr, 1) : 0.0f;
            pop.detect(std::max(std::abs(pl), std::abs(pr)));
            if ((i & 7) == 0) pop.updateShelf();
            const float zl = pop.apply(yl, pl, 0);
            const float zr = R != nullptr ? pop.apply(yr, pr, 1) : 0.0f;

            if (listenEss || listenPop)
            {
                // Solo lo que se está quitando (de la sección o de las dos).
                const float startL = listenEss ? xl : yl, startR = listenEss ? xr : yr;
                const float endL = listenPop ? zl : yl, endR = listenPop ? zr : yr;
                L[i] = startL - endL;
                if (R != nullptr) R[i] = startR - endR;
            }
            else
            {
                L[i] = zl;
                if (R != nullptr) R[i] = zr;
            }
        }
    }
};
} // namespace emery
