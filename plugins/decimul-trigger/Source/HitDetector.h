#pragma once

#include <algorithm>
#include <cmath>

/**
    Detector de golpes de percusión, sin dependencias (header-only, se testea solo).

    Cómo decide que hubo un golpe:
    1. Dos seguidores de envolvente sobre |x|: uno rápido (ataque 0.05 ms) y uno lento
       (ataque 8 ms, caída 60 ms) que "sigue la cola" del sonido.
    2. Hay un golpe nuevo cuando el rápido (a) pasa el umbral, (b) supera al lento por
       `onsetRatio` (un golpe sube de golpe; una cola o un tono sostenido no) y (c) supera
       la "máscara": el pico del golpe anterior decayendo con la constante `retrigger`.
       Eso evita que la cola del redoblante, el ring de un tom o el bleed retriguereen,
       pero deja pasar un golpe nuevo más fuerte que lo que queda del anterior (rolls,
       flams).
    3. Detectado el ataque, se mide el pico durante `scanSamples` (la ventana de scan):
       es la intensidad.
    4. Opcional (`extraSamples`): después del scan se espera un rato más antes de
       informar el golpe, para que quien lo usa pueda analizar su espectro (huella) con
       una ventana más larga. El pico informado sigue siendo el del scan.

    Entre un golpe y el siguiente hay además un bloqueo duro de `lockoutSamples`.
*/
class HitDetector
{
public:
    struct Params
    {
        float thresholdLin = 0.03f;      // pico mínimo para considerar un golpe
        int scanSamples = 96;            // ventana de medición de la intensidad
        int extraSamples = 0;            // espera adicional antes de informar (análisis de huella)
        int lockoutSamples = 1440;       // bloqueo duro tras un golpe
        float maskDecay = 0.9995f;       // factor por muestra de la máscara (ver arriba)
        float onsetRatio = 1.8f;         // cuánto más fuerte que la cola tiene que subir
    };

    void prepare(double sampleRate)
    {
        const auto sr = (float) sampleRate;
        fastAttack = 1.0f - std::exp(-1.0f / (0.00005f * sr));
        fastRelease = 1.0f - std::exp(-1.0f / (0.0015f * sr));
        slowAttack = 1.0f - std::exp(-1.0f / (0.008f * sr));
        slowRelease = 1.0f - std::exp(-1.0f / (0.060f * sr));
        reset();
    }

    void reset()
    {
        fastEnv = slowEnv = mask = scanPeak = heldPeak = 0.0f;
        scanning = false;
        classifying = false;
        onsetFlag = false;
        scanLeft = classifyLeft = 0;
        lockout = 0;
    }

    /** true en la muestra en la que se detectó el ataque de un golpe nuevo (después de
        llamar a process() con esa muestra). */
    bool justStarted() const { return onsetFlag; }

    /** Procesa una muestra de la señal de detección. Devuelve true cuando termina de
        medirse (y analizarse) un golpe; en ese caso `peak` es el pico lineal medido en la
        ventana de scan. */
    bool process(float x, const Params& p, float& peak)
    {
        onsetFlag = false;
        const float a = std::abs(x);

        fastEnv += (a > fastEnv ? fastAttack : fastRelease) * (a - fastEnv);
        slowEnv += (a > slowEnv ? slowAttack : slowRelease) * (a - slowEnv);
        mask *= p.maskDecay;
        if (lockout > 0)
            --lockout;

        if (classifying)
        {
            if (--classifyLeft <= 0)
            {
                classifying = false;
                return finish(heldPeak, p, peak);
            }
            return false;
        }

        if (scanning)
        {
            scanPeak = std::max(scanPeak, a);
            if (--scanLeft <= 0)
            {
                scanning = false;
                if (p.extraSamples > 0)
                {
                    classifying = true;
                    classifyLeft = p.extraSamples;
                    heldPeak = scanPeak;
                    return false;
                }
                return finish(scanPeak, p, peak);
            }
            return false;
        }

        if (lockout == 0
            && fastEnv > p.thresholdLin
            && fastEnv > slowEnv * p.onsetRatio
            && fastEnv > mask)
        {
            scanning = true;
            scanLeft = std::max(1, p.scanSamples);
            scanPeak = a;
            onsetFlag = true;
        }

        return false;
    }

private:
    bool finish(float measuredPeak, const Params& p, float& peak)
    {
        peak = measuredPeak;
        mask = measuredPeak;
        lockout = p.lockoutSamples;
        return true;
    }

    float fastAttack = 0.3f, fastRelease = 0.01f, slowAttack = 0.003f, slowRelease = 0.0004f;
    float fastEnv = 0.0f, slowEnv = 0.0f, mask = 0.0f;
    float scanPeak = 0.0f, heldPeak = 0.0f;
    bool scanning = false, classifying = false, onsetFlag = false;
    int scanLeft = 0, classifyLeft = 0;
    int lockout = 0;
};

/** Biquad de 2º orden (RBJ) para filtrar la señal de detección. Sin asignaciones de
    memoria al cambiar la frecuencia: seguro para el audio thread. */
class DetectionBiquad
{
public:
    void setLowpass(double sampleRate, double freq, double q = 0.7071) { design(sampleRate, freq, q, Kind::lowpass); }
    void setHighpass(double sampleRate, double freq, double q = 0.7071) { design(sampleRate, freq, q, Kind::highpass); }
    void setBandpass(double sampleRate, double freq, double q) { design(sampleRate, freq, q, Kind::bandpass); }
    void reset() { z1 = z2 = 0.0; }

    float process(float x)
    {
        const double in = (double) x;
        const double out = b0 * in + z1;
        z1 = b1 * in - a1 * out + z2;
        z2 = b2 * in - a2 * out;
        return (float) out;
    }

private:
    enum class Kind { lowpass, highpass, bandpass };

    void design(double sampleRate, double freq, double q, Kind kind)
    {
        const double pi = 3.14159265358979323846;
        const double f = std::clamp(freq, 1.0, sampleRate * 0.45);
        const double w0 = 2.0 * pi * f / sampleRate;
        const double cosw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;

        switch (kind)
        {
            case Kind::highpass:
                b0 = (1.0 + cosw) * 0.5 / a0;
                b1 = -(1.0 + cosw) / a0;
                b2 = b0;
                break;
            case Kind::lowpass:
                b0 = (1.0 - cosw) * 0.5 / a0;
                b1 = (1.0 - cosw) / a0;
                b2 = b0;
                break;
            case Kind::bandpass:   // ganancia pico 0 dB
                b0 = alpha / a0;
                b1 = 0.0;
                b2 = -alpha / a0;
                break;
        }
        a1 = -2.0 * cosw / a0;
        a2 = (1.0 - alpha) / a0;
    }

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;
};

/** Pico lineal -> velocity MIDI (1..127).

    `thresholdDb` es el nivel que da `minVel` y `maxDb` el que da `maxVel`. `curve` va de
    -1 (los golpes suaves suenan más fuertes: comprime) a +1 (más dinámica: los suaves
    quedan más suaves). */
inline int velocityFromPeak(float peakLin, float thresholdDb, float maxDb, float curve, int minVel, int maxVel)
{
    const float peakDb = 20.0f * std::log10(std::max(peakLin, 1.0e-9f));
    const float lo = thresholdDb;
    const float hi = std::max(maxDb, lo + 1.0f);

    float t = std::clamp((peakDb - lo) / (hi - lo), 0.0f, 1.0f);
    t = std::pow(t, std::pow(2.0f, curve * 1.5f));

    const int a = std::min(minVel, maxVel), b = std::max(minVel, maxVel);
    return std::clamp((int) std::lround((float) a + (float) (b - a) * t), 1, 127);
}
