#pragma once

// Motor de Scour (efecto lo-fi de seis módulos): solo biblioteca estándar (más DecimuFx.h,
// también solo std), se prueba sin JUCE ni host.
//
// Cadena (estéreo siempre; el procesador pasa mono como estéreo y vuelve a sumar):
//   entrada ► ganancia ► [ruido PRE] ► DISTORT ► DIGITAL ► WOBBLE ► MAGNETIC ► SPACE ►
//   [ruido POST] ► EQ ► ancho ► MIX con la entrada ► salida
// Cada módulo tiene encendido, cantidad (que escala MAGNITUDE) y FLUX: una deriva aleatoria
// lenta que mueve los parámetros del módulo para que no suene siempre igual.
//
// Reglas (las mismas de DecimuFx.h): nada de asignaciones en el hilo de audio, parámetros
// suavizados y latencia cero (el retardo variable de WOBBLE es parte del efecto).

#include <DecimuFx.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace scour
{
using decimu::fx::dbToGain;
using decimu::fx::DelayBuf;
using decimu::fx::EqBand;
using decimu::fx::FdnReverb;
using decimu::fx::Smooth;

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

/** Generador pseudoaleatorio chico (xorshift32); sin asignaciones. */
struct Rng
{
    uint32_t s = 1;
    explicit Rng(uint32_t seed = 1) : s(seed != 0 ? seed : 1u) {}
    uint32_t nextU() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float next() { return (float) (int32_t) nextU() * (1.0f / 2147483648.0f); }   // -1..1
    float uni() { return (float) (nextU() >> 8) * (1.0f / 16777216.0f); }           //  0..1
};

inline float fastTanh(float x)
{
    x = std::clamp(x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

struct OnePole
{
    float a = 0.1f, z = 0.0f;
    void setCutoff(float hz, double sr) { a = 1.0f - std::exp(-kTwoPi * std::min(hz, (float) sr * 0.45f) / (float) sr); }
    float lp(float x) { z += a * (x - z); return z; }
    float hp(float x) { z += a * (x - z); return x - z; }
    void reset() { z = 0.0f; }
};

/** Deriva aleatoria lenta, suavizada, de -1 a +1 (aprox. +/-0.5 en la práctica). Se avanza a la
    tasa de control (cada 16 muestras). */
struct Flux
{
    Rng rng;
    float value = 0.0f, target = 0.0f, coef = 0.01f;
    int hold = 0;
    double ctlRate = 3000.0;

    void prepare(double controlRate, uint32_t seed)
    {
        ctlRate = controlRate;
        rng = Rng(seed);
        coef = 1.0f - std::exp(-1.0f / (float) (0.9 * controlRate));
        value = target = 0.0f;
        hold = 0;
    }
    float next()
    {
        if (--hold <= 0)
        {
            target = rng.next();
            hold = (int) ((0.7f + 1.8f * rng.uni()) * (float) ctlRate);
        }
        value += (target - value) * coef;
        return value;
    }
};

//==============================================================================
/** Ruido de fondo de seis tipos. Cada tipo está normalizado a ~1.0 de RMS con ganancia 1. */
class NoiseGen
{
public:
    static constexpr int kTypes = 6;   // vinilo, cinta, zumbido, estática, VHS, polvo

    void prepare(double sampleRate)
    {
        sr = sampleRate;
        rng[0] = Rng(0x1234567u);
        rng[1] = Rng(0x7654321u);
        for (auto& v : lpA) v.reset();
        for (auto& v : lpB) v.reset();
        for (auto& v : hpA) v.reset();
        for (int c = 0; c < 2; ++c)
        {
            crack[c] = 0.0f;
            burst[c] = 0;
        }
        humPhase = bandPhase = whinePhase = 0.0;
        crackDecay = std::exp(-1.0f / (0.0015f * (float) sr));
        dustDecay = std::exp(-1.0f / (0.004f * (float) sr));
        for (auto& v : hissLp) v.setCutoff(6000.0f, sr);
        for (auto& v : lpA) v.setCutoff(5000.0f, sr);
        for (auto& v : lpB) v.setCutoff(5500.0f, sr);
        for (auto& v : hpA) v.setCutoff(800.0f, sr);
        toneLp[0].reset(); toneLp[1].reset();
        setTone(0.6f);
    }

    /** tone01: 0 oscuro .. 1 brillante. */
    void setTone(float tone01)
    {
        const float hz = 700.0f * std::pow(24.0f, std::clamp(tone01, 0.0f, 1.0f));   // 700 Hz .. 16.8 kHz
        for (auto& v : toneLp) v.setCutoff(hz, sr);
    }

    void next(int type, float& outL, float& outR)
    {
        float v[2] = { 0.0f, 0.0f };
        const float k = kNorm[std::clamp(type, 0, kTypes - 1)];
        switch (type)
        {
            case 0: // vinilo: siseo bajo + chasquidos
                for (int c = 0; c < 2; ++c)
                {
                    const float hiss = hissLp[c].lp(rng[c].next());
                    if (rng[c].uni() < 14.0f / (float) sr)
                        crack[c] = (0.25f + 0.75f * rng[c].uni() * rng[c].uni()) * (rng[c].uni() < 0.04f ? 3.5f : 1.0f);
                    crack[c] *= crackDecay;
                    v[c] = 0.12f * hiss + crack[c] * rng[c].next() * 1.1f;
                }
                break;
            case 1: // cinta: siseo con más agudos
                for (int c = 0; c < 2; ++c)
                    v[c] = hpA[c].hp(rng[c].next());
                break;
            case 2: // zumbido de 50 Hz y armónicos
            {
                humPhase += 50.0 / sr;
                if (humPhase >= 1.0) humPhase -= 1.0;
                const float p = (float) humPhase * kTwoPi;
                const float h = std::sin(p) + 0.55f * std::sin(2.0f * p + 0.7f) + 0.3f * std::sin(3.0f * p + 1.9f)
                                + 0.15f * std::sin(4.0f * p + 0.3f);
                v[0] = h + 0.03f * rng[0].next();
                v[1] = h + 0.03f * rng[1].next();
                break;
            }
            case 3: // estática: ráfagas de ruido brillante
                for (int c = 0; c < 2; ++c)
                {
                    if (burst[c] <= 0 && rng[c].uni() < 9.0f / (float) sr)
                        burst[c] = (int) ((0.008f + 0.06f * rng[c].uni()) * (float) sr);
                    const float white = rng[c].next();
                    const float bright = white - lpA[c].lp(white);
                    v[c] = burst[c] > 0 ? bright * 1.0f : 0.03f * white;
                    if (burst[c] > 0) --burst[c];
                }
                break;
            case 4: // VHS: siseo en bandas que suben y bajan, y el silbido de línea
            {
                bandPhase += 0.37 / sr;
                if (bandPhase >= 1.0) bandPhase -= 1.0;
                const float s = 0.5f + 0.5f * std::sin((float) bandPhase * kTwoPi);
                const float band = 0.15f + 1.6f * s * s * s;
                for (int c = 0; c < 2; ++c)
                {
                    const float x = lpB[c].lp(rng[c].next());
                    v[c] = (x - hpA[c].lp(x)) * band;
                }
                if (sr > 40000.0)
                {
                    whinePhase += 15734.0 / sr;
                    if (whinePhase >= 1.0) whinePhase -= 1.0;
                    const float w = 0.03f * std::sin((float) whinePhase * kTwoPi);
                    v[0] += w; v[1] += w;
                }
                break;
            }
            default: // polvo: motas sueltas, bastante espaciadas
                for (int c = 0; c < 2; ++c)
                {
                    if (rng[c].uni() < 3.0f / (float) sr)
                        crack[c] = 0.4f + 0.6f * rng[c].uni();
                    crack[c] *= dustDecay;
                    v[c] = crack[c] * rng[c].next() * 1.0f + 0.015f * hissLp[c].lp(rng[c].next());
                }
                break;
        }
        outL = toneLp[0].lp(v[0]) * k;
        outR = toneLp[1].lp(v[1]) * k;
    }

    // Constantes de normalización de RMS (medidas con `ScourDspTest --noise`).
    static constexpr float kNorm[kTypes] = { 3.42f, 2.88f, 0.750f, 6.83f, 3.51f, 4.84f };
    // Nivel RMS previsto (dBFS) de cada tipo con la cantidad al 100 % y TONE en 60 %
    static constexpr float kTarget[kTypes] = { -38.0f, -22.0f, -24.0f, -26.0f, -22.0f, -42.0f };

private:
    double sr = 48000.0;
    Rng rng[2];
    OnePole lpA[2], lpB[2], hpA[2], hissLp[2], toneLp[2];
    float crack[2] = { 0.0f, 0.0f };
    int burst[2] = { 0, 0 };
    float crackDecay = 0.99f, dustDecay = 0.99f;
    double humPhase = 0.0, bandPhase = 0.0, whinePhase = 0.0;
};

//==============================================================================
/** Pasa lo que está por debajo de la frecuencia de foco y devuelve el resto por separado
    (low + high = entrada, exacto). Con foco <= 25 Hz no separa (todo es "high"). */
struct BandSplit
{
    // pasa-altos Butterworth de 2º orden (TDF2); lo grave es la entrada menos lo agudo
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] = { 0, 0 }, z2[2] = { 0, 0 };
    float focusHz = 20.0f;
    bool active = false;
    double sampleRate = 48000.0;

    void prepare(double sr) { sampleRate = sr; z1[0] = z1[1] = z2[0] = z2[1] = 0.0; setFocus(20.0f); }
    void setFocus(float hz)
    {
        focusHz = hz;
        active = hz > 25.0f;
        const double w = 2.0 * 3.14159265358979323846 * std::min((double) hz, sampleRate * 0.45) / sampleRate;
        const double c = std::cos(w), alpha = std::sin(w) / (2.0 * 0.70710678);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 + c) * 0.5 / a0; b1 = -(1.0 + c) / a0; b2 = b0;
        a1 = -2.0 * c / a0; a2 = (1.0 - alpha) / a0;
    }
    // devuelve la parte grave; la aguda es x - low
    float low(int ch, float x)
    {
        if (!active)
            return 0.0f;
        const double in = (double) x;
        const double y = b0 * in + z1[ch];
        z1[ch] = b1 * in - a1 * y + z2[ch];
        z2[ch] = b2 * in - a2 * y;
        return (float) (in - y);
    }
};

//==============================================================================
/** Distorsión de cuatro formas, con "foco" (solo distorsiona por encima de una frecuencia). */
class Distortion
{
public:
    static constexpr int kTypes = 4;   // tubo, cinta, diodo, pliegue

    void prepare(double sampleRate)
    {
        sr = sampleRate;
        split.prepare(sampleRate);
        for (int c = 0; c < 2; ++c) { dcZ[c] = 0.0f; dcX[c] = 0.0f; tapeLp[c].reset(); }
        dcCoef = 1.0f - std::exp(-kTwoPi * 18.0f / (float) sampleRate);
        for (auto& v : tapeLp) v.setCutoff(9000.0f, sampleRate);
    }

    void setFocus(float hz) { if (std::abs(hz - split.focusHz) > 0.5f) split.setFocus(hz); }

    /** Se llama a tasa de control con la cantidad efectiva (0..1). */
    void update(float a)
    {
        if (a == lastA) return;
        lastA = a;
        k = 1.0f + 12.0f * a * a + 4.0f * a;
        comp = 1.0f / std::sqrt(k);
    }

    /** a: 0..1 (cantidad efectiva); mix: 0..1. Procesa una muestra por canal. */
    float process(int ch, float x, int type, float a, float mix)
    {
        if (a <= 0.0005f || mix <= 0.0f)
            return x;
        const float low = split.low(ch, x);
        const float high = x - low;
        const float u = k * high;
        float y;
        switch (type)
        {
            case 0:  y = fastTanh(u + 0.18f) - kTubeBias; break;                                         // tubo: asimetrico
            case 1:  y = tapeLp[ch].lp(fastTanh(0.85f * u) * (1.0f / 0.85f)); break;                      // cinta: simetrico y suave
            case 2:  y = u >= 0.0f ? u / (1.0f + 0.8f * u) : u / (1.0f - 1.1f * u); break;                // diodo: asimetrico
            default: y = std::sin(std::clamp(u, -9.0f, 9.0f) * 1.15f); break;                           // pliegue
        }
        y *= comp;
        // saca el DC que dejan las formas asimétricas
        dcZ[ch] += dcCoef * (y - dcZ[ch]);
        const float wet = low + (y - dcZ[ch]);
        return x + (wet - x) * mix;
    }

private:
    double sr = 48000.0;
    BandSplit split;
    OnePole tapeLp[2];
    float dcZ[2] = { 0.0f, 0.0f }, dcX[2] = { 0.0f, 0.0f };
    float dcCoef = 0.002f, k = 1.0f, comp = 1.0f, lastA = -1.0f;
    static inline const float kTubeBias = fastTanh(0.18f);
};

//==============================================================================
/** Reducción de frecuencia de muestreo (sin filtro, con aliasing) y de profundidad de bits. */
class Digital
{
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        split.prepare(sampleRate);
        for (int c = 0; c < 2; ++c) { phase[c] = 1.0f; held[c] = 0.0f; smooth[c].reset(); }
        smoothHz = 20000.0f;
        for (auto& v : smooth) v.setCutoff(smoothHz, sr);
    }

    void setFocus(float hz) { if (std::abs(hz - split.focusHz) > 0.5f) split.setFocus(hz); }
    void setSmooth(float hz) { if (std::abs(hz - smoothHz) > 1.0f) { smoothHz = hz; for (auto& v : smooth) v.setCutoff(hz, sr); } }

    /** Se llama a tasa de control. a: 0..1; balance: 0 = solo frecuencia, 1 = solo bits. */
    void update(float a, float balance)
    {
        if (a == lastA && balance == lastBal) return;
        lastA = a; lastBal = balance;
        const float wr = balance <= 0.5f ? 1.0f : (1.0f - balance) * 2.0f;
        const float wb = balance >= 0.5f ? 1.0f : balance * 2.0f;
        ratio = std::pow(2200.0f / (float) sr, a * wr);                       // 1 .. 2200 Hz / sr
        const float bits = 16.0f - 12.0f * a * wb;                            // 16 .. 4 bits
        levels = std::pow(2.0f, bits - 1.0f);
        invLevels = 1.0f / levels;
    }

    float process(int ch, float x, float a, float mix)
    {
        if (a <= 0.0005f || mix <= 0.0f)
            return x;
        const float low = split.low(ch, x);
        const float high = x - low;
        phase[ch] += ratio;
        if (phase[ch] >= 1.0f)
        {
            phase[ch] -= 1.0f;
            held[ch] = high;
        }
        float y = std::floor(held[ch] * levels + 0.5f) * invLevels;
        y = smooth[ch].lp(y);
        const float wet = low + y;
        return x + (wet - x) * mix;
    }

private:
    double sr = 48000.0;
    BandSplit split;
    OnePole smooth[2];
    float phase[2] = { 1.0f, 1.0f }, held[2] = { 0.0f, 0.0f };
    float smoothHz = 20000.0f;
    float ratio = 1.0f, levels = 32768.0f, invLevels = 1.0f / 32768.0f, lastA = -1.0f, lastBal = -1.0f;
};

//==============================================================================
/** Temblor de cinta: retardo variable con dos LFO (wow lento y flutter rápido) más una deriva
    aleatoria. Es un vibrato (cambia el tono de verdad); el retardo medio crece con la
    profundidad. */
class Wobble
{
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        for (auto& d : delay) { d.prepare((int) (0.08 * sampleRate)); d.clear(); }
        wowPh = 0.0f; flutPh = 0.0f;
        rng = Rng(0xBEEF01u);
        drift = driftTarget = 0.0f;
        driftHold = 0;
        driftCoef = 1.0f - std::exp(-1.0f / (0.35f * (float) sampleRate));
    }

    /** a: 0..1; balance: 0 = solo wow, 1 = solo flutter. */
    void process(float& l, float& r, float a, float balance, float wowHz, float flutHz, float stereo, float mix)
    {
        // las líneas siempre reciben señal: así prender el módulo no trae audio viejo
        const float inL = l, inR = r;
        if (a <= 0.0005f || mix <= 0.0f)
        {
            delay[0].push(inL); delay[1].push(inR);
            return;
        }
        if (--driftHold <= 0)
        {
            driftTarget = rng.next();
            driftHold = (int) ((0.25f + 0.8f * rng.uni()) * (float) sr);
        }
        drift += (driftTarget - drift) * driftCoef;

        const float ww = balance <= 0.5f ? 1.0f : (1.0f - balance) * 2.0f;
        const float wf = balance >= 0.5f ? 1.0f : balance * 2.0f;
        const float wowCents = 45.0f * a * ww;
        const float flutCents = 14.0f * a * wf;
        const float wowA = wowCents * 0.000578f * (float) sr / (kTwoPi * std::max(wowHz, 0.1f));
        const float flutA = flutCents * 0.000578f * (float) sr / (kTwoPi * std::max(flutHz, 1.0f));

        wowPh += wowHz / (float) sr * (1.0f + 0.25f * drift);
        flutPh += flutHz / (float) sr * (1.0f + 0.15f * drift);
        if (wowPh >= 1.0f) wowPh -= 1.0f;
        if (flutPh >= 1.0f) flutPh -= 1.0f;

        float out[2];
        const float in[2] = { inL, inR };
        for (int c = 0; c < 2; ++c)
        {
            const float off = c == 0 ? 0.0f : stereo;
            float pw = wowPh + 0.5f * off, pf = flutPh + 0.33f * off;
            pw -= std::floor(pw); pf -= std::floor(pf);
            const float d = 3.0f + wowA * (1.0f + std::sin(pw * kTwoPi)) + flutA * (1.0f + std::sin(pf * kTwoPi))
                            + 0.12f * (wowA + flutA) * drift;
            out[c] = delay[c].read(d);
            delay[c].push(in[c]);
        }
        l = inL + (out[0] - inL) * mix;
        r = inR + (out[1] - inR) * mix;
    }

private:
    double sr = 48000.0;
    DelayBuf delay[2];
    float wowPh = 0.0f, flutPh = 0.0f;
    Rng rng;
    float drift = 0.0f, driftTarget = 0.0f, driftCoef = 0.0001f;
    int driftHold = 0;
};

//==============================================================================
/** Cinta gastada: pérdida de agudos (wear), temblor de nivel (flutter) y caídas de volumen. */
class Magnetic
{
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        for (int c = 0; c < 2; ++c)
        {
            rng[c] = Rng(0xC0FFEEu + (uint32_t) c * 977u);
            wearLp[c].reset(); wearLp2[c].reset();
            wander[c] = wanderTarget[c] = 0.0f;
            wanderHold[c] = 0;
            drop[c] = 1.0f; dropHold[c] = 0; dropDepth[c] = 0.0f;
            flutPh[c] = 0.25f * (float) c;
        }
        lastCutoff = 0.0f;
        wanderCoef = 1.0f - std::exp(-1.0f / (0.6f * (float) sampleRate));
        dropAtk = 1.0f - std::exp(-1.0f / (0.006f * (float) sampleRate));
        dropRel = 1.0f - std::exp(-1.0f / (0.07f * (float) sampleRate));
    }

    /** a: 0..1; balance: 0 = solo wear, 1 = solo flutter; rateHz: velocidad; drops: 0..1; stereo: 0..1. */
    void process(float& l, float& r, float a, float balance, float rateHz, float drops, float stereo)
    {
        if (a <= 0.0005f)
        {
            wearLp[0].z = l; wearLp[1].z = r; wearLp2[0].z = l; wearLp2[1].z = r;
            return;
        }
        const float ww = balance <= 0.5f ? 1.0f : (1.0f - balance) * 2.0f;
        const float wf = balance >= 0.5f ? 1.0f : balance * 2.0f;
        const float cutoff = 18000.0f * std::pow(3200.0f / 18000.0f, a * ww);
        const float rateScale = rateHz / 6.0f;

        float g[2], in[2] = { l, r }, out[2];
        for (int c = 0; c < 2; ++c)
        {
            // temblor lento del nivel
            if (--wanderHold[c] <= 0)
            {
                wanderTarget[c] = rng[c].next();
                wanderHold[c] = (int) ((0.3f + 0.9f * rng[c].uni()) / rateScale * (float) sr);
            }
            wander[c] += (wanderTarget[c] - wander[c]) * wanderCoef * rateScale;
            // flutter rápido
            flutPh[c] += rateHz * 1.7f / (float) sr * (1.0f + 0.2f * wander[c]);
            if (flutPh[c] >= 1.0f) flutPh[c] -= 1.0f;
            const float flut = 0.5f + 0.5f * std::sin(flutPh[c] * kTwoPi);

            // caídas de volumen
            if (dropHold[c] > 0)
                --dropHold[c];
            else if (rng[c].uni() < drops * 1.6f / (float) sr)
            {
                dropHold[c] = (int) ((0.03f + 0.14f * rng[c].uni()) * (float) sr);
                dropDepth[c] = dbToGain(-(5.0f + 20.0f * a * (0.4f + 0.6f * rng[c].uni())));
            }
            const float dropTarget = dropHold[c] > 0 ? dropDepth[c] : 1.0f;
            drop[c] += (dropTarget - drop[c]) * (dropTarget < drop[c] ? dropAtk : dropRel);

            const float wanderLn = 0.2993f * a * ww * wander[c];     // 2.6 dB en neperios
            g[c] = (1.0f + wanderLn + 0.5f * wanderLn * wanderLn) * (1.0f - 0.38f * a * wf * flut) * drop[c];
        }
        // con stereo 0 los dos canales comparten la modulación del izquierdo
        const float gl = g[0];
        const float gr = g[0] + (g[1] - g[0]) * stereo;
        const float gains[2] = { gl, gr };
        for (int c = 0; c < 2; ++c)
        {
            if (std::abs(cutoff - lastCutoff) > lastCutoff * 0.004f)
            {
                lastCutoff = cutoff;
                for (int k = 0; k < 2; ++k) { wearLp[k].setCutoff(cutoff, sr); wearLp2[k].setCutoff(cutoff * 1.6f, sr); }
            }
            const float filtered = wearLp2[c].lp(wearLp[c].lp(in[c]));
            out[c] = filtered * gains[c];
        }
        l = out[0]; r = out[1];
    }

private:
    double sr = 48000.0;
    Rng rng[2];
    OnePole wearLp[2], wearLp2[2];
    float wander[2], wanderTarget[2];
    int wanderHold[2];
    float flutPh[2];
    float drop[2], dropDepth[2];
    int dropHold[2];
    float wanderCoef = 0.0001f, dropAtk = 0.01f, dropRel = 0.001f, lastCutoff = 0.0f;
};

//==============================================================================
/** Reverb lo-fi: predelay, filtro de foco en el envío y el FDN de la suite. */
class Space
{
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        reverb.prepare(sampleRate, 0);
        reverb.setPreset(0.9f, 1.5f, 0.45f, 0.4f, 0.3f, 0.7f, true);
        reverb.setActive(false, 0.0f);
        for (auto& d : pre) { d.prepare((int) (0.25 * sampleRate)); d.clear(); }
        for (int c = 0; c < 2; ++c) { sendLp[c].reset(); sendHp[c].reset(); }
        setFocus(0.0f);
        predelaySamples = 2.0f;
    }

    void reset() { reverb.reset(); for (auto& d : pre) d.clear(); }

    /** decay01 0..1; stereo01 0..1; focus -1..+1 (oscurece / adelgaza el envío). */
    void setShape(float decay01, float stereo01, float focus, float preMs, bool active, float wet01)
    {
        const float rt60 = 0.3f * std::pow(20.0f, std::clamp(decay01, 0.0f, 1.0f));
        reverb.setPreset(0.5f + decay01, rt60, 0.45f, 0.4f, 0.3f, stereo01);
        reverb.setActive(active, wet01);
        wantActive = active;
        predelaySamples = std::max(2.0f, preMs * 0.001f * (float) sr);
        if (std::abs(focus - lastFocus) > 0.002f) setFocus(focus);
    }

    /** Suma la parte húmeda a l/r (el seco se queda como está). */
    void process(float* l, float* r, int n)
    {
        idle = wantActive ? 0 : idle + n;
        if (idle > (int) sr)
        {
            // apagado hace más de 1 s: la cola ya se drenó, no hace falta correr el reverb
            for (int i = 0; i < n; ++i) { pre[0].push(l[i]); pre[1].push(r[i]); }
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            float s[2] = { l[i], r[i] };
            for (int c = 0; c < 2; ++c)
            {
                const float delayed = pre[c].read(predelaySamples);
                pre[c].push(s[c]);
                sendBuf[c][(size_t) i] = sendHp[c].hp(sendLp[c].lp(delayed));
            }
        }
        reverb.process(sendBuf[0].data(), sendBuf[1].data(), wetBuf[0].data(), wetBuf[1].data(), n);
        for (int i = 0; i < n; ++i)
        {
            l[i] += wetBuf[0][(size_t) i];
            r[i] += wetBuf[1][(size_t) i];
        }
    }

    static constexpr int kMaxBlock = 128;

private:
    void setFocus(float f)
    {
        lastFocus = f;
        const float lpHz = f < 0.0f ? 400.0f + 19600.0f * (1.0f + f) * (1.0f + f) : 20000.0f;
        const float hpHz = f > 0.0f ? 20.0f + 1800.0f * f * f : 20.0f;
        for (int c = 0; c < 2; ++c) { sendLp[c].setCutoff(lpHz, sr); sendHp[c].setCutoff(hpHz, sr); }
    }

    double sr = 48000.0;
    FdnReverb reverb;
    DelayBuf pre[2];
    OnePole sendLp[2], sendHp[2];
    std::array<float, kMaxBlock> sendBuf[2] {}, wetBuf[2] {};
    float predelaySamples = 2.0f, lastFocus = 99.0f;
    bool wantActive = false;
    int idle = 1 << 30;
};

//==============================================================================
struct Params
{
    float inGainDb = 0.0f, outGainDb = 0.0f, magnitude = 100.0f, mix = 100.0f, width = 100.0f;

    bool noiseOn = true; int noiseType = 0; float noiseAmt = 25.0f, noiseTone = 60.0f, noiseFollow = 0.0f, noiseDuck = 0.0f, noiseFlux = 0.0f;
    bool noisePost = false;
    bool wobOn = true; float wobAmt = 30.0f, wobBalance = 50.0f, wowRate = 0.6f, flutRate = 9.0f, wobStereo = 30.0f, wobMix = 100.0f, wobFlux = 0.0f;
    bool disOn = true; int disType = 0; float disAmt = 25.0f, disFocus = 20.0f, disMix = 100.0f, disFlux = 0.0f;
    bool digOn = true; float digAmt = 20.0f, digBalance = 50.0f, digFocus = 20.0f, digSmooth = 30.0f, digMix = 100.0f, digFlux = 0.0f;
    bool spaceOn = true; float spaceAmt = 25.0f, spaceDecay = 40.0f, spacePre = 20.0f, spaceFocus = 0.0f, spaceStereo = 70.0f, spaceFlux = 0.0f;
    bool magOn = true; float magAmt = 30.0f, magBalance = 50.0f, magRate = 6.0f, magDropouts = 20.0f, magStereo = 50.0f, magFlux = 0.0f;
    bool eqOn = true; float eqLow = 20.0f, eqHigh = 20000.0f, eqTone = 0.0f;
};

class ScourEngine
{
public:
    static constexpr int kBlock = 64;
    static constexpr int kCtl = 16;           // tasa de control: cada 16 muestras

    void prepare(double sampleRate)
    {
        sr = sampleRate;
        const double ctlRate = sampleRate / kCtl;
        noise.prepare(sampleRate);
        dist.prepare(sampleRate);
        digital.prepare(sampleRate);
        wobble.prepare(sampleRate);
        magnetic.prepare(sampleRate);
        space.prepare(sampleRate);
        for (int i = 0; i < kModules; ++i)
        {
            flux[i].prepare(ctlRate, 0x9E3779B1u * (uint32_t) (i + 1));
            onS[i].prepare(ctlRate, 25.0); onS[i].snap(1.0f);
        }
        for (auto& s : amtS) { s.prepare(ctlRate, 25.0); s.snap(0.0f); }
        inS.prepare(ctlRate, 30.0); outS.prepare(ctlRate, 30.0); mixS.prepare(ctlRate, 30.0); widthS.prepare(ctlRate, 30.0);
        eqOnS.prepare(ctlRate, 30.0); eqOnS.snap(0.0f);
        hpS.prepare(ctlRate, 30.0); lpS.prepare(ctlRate, 30.0); hpS.snap(0.0f); lpS.snap(0.0f);
        magnS.prepare(ctlRate, 30.0);
        inS.snap(1.0f); outS.snap(1.0f); mixS.snap(1.0f); widthS.snap(1.0f); magnS.snap(1.0f);

        hp.prepare(sampleRate, EqBand::Type::highpass);
        lp.prepare(sampleRate, EqBand::Type::lowpass);
        low.prepare(sampleRate, EqBand::Type::lowShelf);
        high.prepare(sampleRate, EqBand::Type::highShelf);
        hp.set(20.0f, 0.0f, true); lp.set(20000.0f, 0.0f, true);
        low.set(400.0f, 0.0f, true); high.set(3000.0f, 0.0f, true);

        followEnv = 0.0f;
        envAtk = 1.0f - std::exp(-1.0f / (0.005f * (float) sampleRate));
        envRel = 1.0f - std::exp(-1.0f / (0.15f * (float) sampleRate));
        ctlCounter = 0;
        firstCtl = true;
        for (auto& v : lastAmt) v = 0.0f;
    }

    void reset()
    {
        space.reset();
        prepare(sr);
    }

    void setParams(const Params& p) { params = p; }

    // Valores efectivos del último paso de control (para la UI y los tests)
    float getFlux(int module) const { return fluxValue[module]; }
    float effectiveAmount(int module) const { return lastAmt[module]; }
    float getNoiseEnv() const { return followEnv; }

    /** Procesa en el lugar (estéreo). */
    void process(float* l, float* r, int n)
    {
        for (int start = 0; start < n; start += kBlock)
        {
            const int len = std::min(kBlock, n - start);
            processBlock(l + start, r + start, len);
        }
    }

    enum Module { mNoise = 0, mWobble, mDistort, mDigital, mSpace, mMagnetic, kModules };

private:
    void control()
    {
        const Params& p = params;
        const bool ons[kModules] = { p.noiseOn, p.wobOn, p.disOn, p.digOn, p.spaceOn, p.magOn };
        const float amts[kModules] = { p.noiseAmt, p.wobAmt, p.disAmt, p.digAmt, p.spaceAmt, p.magAmt };
        const float fluxes[kModules] = { p.noiseFlux, p.wobFlux, p.disFlux, p.digFlux, p.spaceFlux, p.magFlux };

        auto setS = [this](Smooth& sm, float v) { sm.set(v); if (firstCtl) sm.snap(v); };
        setS(magnS, p.magnitude * 0.01f);
        const float magn = magnS.next();
        for (int i = 0; i < kModules; ++i)
        {
            setS(onS[i], ons[i] ? 1.0f : 0.0f);
            const float on = onS[i].next();
            fluxValue[i] = flux[i].next();
            const float depth = fluxes[i] * 0.01f;
            // FLUX mueve la cantidad entre 0.35x y 1.65x (a profundidad máxima)
            const float fluxMul = 1.0f + 0.65f * depth * std::clamp(fluxValue[i] * 1.6f, -1.0f, 1.0f);
            setS(amtS[i], std::clamp(amts[i] * 0.01f * magn * fluxMul, 0.0f, 1.0f) * on);
            lastAmt[i] = amtS[i].next();
        }
        setS(inS, dbToGain(p.inGainDb)); setS(outS, dbToGain(p.outGainDb));
        setS(mixS, p.mix * 0.01f); setS(widthS, p.width * 0.01f);
        setS(eqOnS, p.eqOn ? 1.0f : 0.0f);
        setS(hpS, p.eqLow > 25.0f ? 1.0f : 0.0f); setS(lpS, p.eqHigh < 19000.0f ? 1.0f : 0.0f);
        c_hp = hpS.next(); c_lp = lpS.next();
        c_in = inS.next(); c_out = outS.next(); c_mix = mixS.next(); c_width = widthS.next(); c_eq = eqOnS.next();

        // parámetros de módulo que no se mueven por muestra
        dist.update(lastAmt[mDistort]);
        digital.update(lastAmt[mDigital], p.digBalance * 0.01f);
        dist.setFocus(p.disFocus);
        digital.setFocus(p.digFocus);
        digital.setSmooth(20000.0f * std::pow(2500.0f / 20000.0f, std::clamp(p.digSmooth * 0.01f, 0.0f, 1.0f)));
        noise.setTone(p.noiseTone * 0.01f);

        const float fw = fluxValue[mWobble] * p.wobFlux * 0.01f;
        c_wowRate = std::clamp(p.wowRate * (1.0f + 0.5f * fw), 0.05f, 6.0f);
        c_flutRate = std::clamp(p.flutRate * (1.0f + 0.35f * fw), 1.0f, 30.0f);
        const float fs = fluxValue[mSpace] * p.spaceFlux * 0.01f;
        space.setShape(std::clamp(p.spaceDecay * 0.01f + 0.25f * fs, 0.0f, 1.0f), p.spaceStereo * 0.01f, p.spaceFocus * 0.01f,
                       p.spacePre, lastAmt[mSpace] > 0.0005f, lastAmt[mSpace]);

        // EQ final: pasa-altos, pasa-bajos y tilt
        hp.set(std::max(20.0f, p.eqLow), 0.0f, firstCtl);
        lp.set(std::min(20000.0f, p.eqHigh), 0.0f, firstCtl);
        const float tilt = p.eqTone * 0.01f * 6.0f;
        low.set(400.0f, -tilt, firstCtl);
        high.set(3000.0f, tilt, firstCtl);
        firstCtl = false;
    }

    void processBlock(float* l, float* r, int n)
    {
        const Params& p = params;
        std::array<float, kBlock> dryL, dryR;
        for (int i = 0; i < n; ++i)
        {
            if (ctlCounter == 0)
                control();
            ctlCounter = (ctlCounter + 1) % kCtl;

            float xl = l[i] * c_in, xr = r[i] * c_in;
            dryL[(size_t) i] = xl; dryR[(size_t) i] = xr;

            // nivel de la entrada (para FOLLOW y DUCK del ruido)
            const float lvl = std::max(std::abs(xl), std::abs(xr));
            followEnv += (lvl - followEnv) * (lvl > followEnv ? envAtk : envRel);

            // ruido (sumado antes o después de la cadena)
            float nl = 0.0f, nr = 0.0f;
            if (lastAmt[mNoise] > 0.0005f)
            {
                noise.next(p.noiseType, nl, nr);
                const float follow = p.noiseFollow * 0.01f, duck = p.noiseDuck * 0.01f;
                const float lvlN = std::clamp(followEnv * 4.0f, 0.0f, 1.0f);
                const float gDb = -70.0f + 50.0f * lastAmt[mNoise];
                const float g = dbToGain(gDb) * ((1.0f - follow) + follow * lvlN) * (1.0f - duck * lvlN);
                nl *= g; nr *= g;
            }
            if (!p.noisePost) { xl += nl; xr += nr; }

            const float aD = lastAmt[mDistort], aG = lastAmt[mDigital];
            xl = dist.process(0, xl, p.disType, aD, p.disMix * 0.01f);
            xr = dist.process(1, xr, p.disType, aD, p.disMix * 0.01f);
            xl = digital.process(0, xl, aG, p.digMix * 0.01f);
            xr = digital.process(1, xr, aG, p.digMix * 0.01f);
            wobble.process(xl, xr, lastAmt[mWobble], p.wobBalance * 0.01f, c_wowRate, c_flutRate, p.wobStereo * 0.01f, p.wobMix * 0.01f);
            magnetic.process(xl, xr, lastAmt[mMagnetic], p.magBalance * 0.01f, p.magRate, p.magDropouts * 0.01f, p.magStereo * 0.01f);
            tmpL[(size_t) i] = xl; tmpR[(size_t) i] = xr;
            postNoiseL[(size_t) i] = p.noisePost ? nl : 0.0f;
            postNoiseR[(size_t) i] = p.noisePost ? nr : 0.0f;
        }

        space.process(tmpL.data(), tmpR.data(), n);

        // EQ, ancho, mix, salida
        for (int i = 0; i < n; ++i)
        {
            tmpL[(size_t) i] += postNoiseL[(size_t) i];
            tmpR[(size_t) i] += postNoiseR[(size_t) i];
        }
        if (c_eq > 0.0005f || eqWasOn)
        {
            std::copy(tmpL.begin(), tmpL.begin() + n, eqL.begin());
            std::copy(tmpR.begin(), tmpR.begin() + n, eqR.begin());
            // pasa-altos y pasa-bajos siempre corren y se mezclan con un fundido: en los extremos de la
            // perilla no tocan la señal (un filtro en 20 Hz o 20 kHz igual le cambia la fase)
            auto stage = [&](EqBand& f, float amt)
            {
                std::copy(eqL.begin(), eqL.begin() + n, stL.begin());
                std::copy(eqR.begin(), eqR.begin() + n, stR.begin());
                f.process(stL.data(), stR.data(), n);
                for (int i = 0; i < n; ++i)
                {
                    eqL[(size_t) i] += (stL[(size_t) i] - eqL[(size_t) i]) * amt;
                    eqR[(size_t) i] += (stR[(size_t) i] - eqR[(size_t) i]) * amt;
                }
            };
            stage(hp, c_hp);
            stage(lp, c_lp);
            stage(low, 1.0f);
            stage(high, 1.0f);
            for (int i = 0; i < n; ++i)
            {
                tmpL[(size_t) i] += (eqL[(size_t) i] - tmpL[(size_t) i]) * c_eq;
                tmpR[(size_t) i] += (eqR[(size_t) i] - tmpR[(size_t) i]) * c_eq;
            }
            eqWasOn = c_eq > 0.0005f;
        }
        for (int i = 0; i < n; ++i)
        {
            float wl = tmpL[(size_t) i], wr = tmpR[(size_t) i];
            const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * c_width;
            wl = mid + side; wr = mid - side;
            l[i] = (dryL[(size_t) i] + (wl - dryL[(size_t) i]) * c_mix) * c_out;
            r[i] = (dryR[(size_t) i] + (wr - dryR[(size_t) i]) * c_mix) * c_out;
        }
    }

    double sr = 48000.0;
    Params params;

    NoiseGen noise;
    Distortion dist;
    Digital digital;
    Wobble wobble;
    Magnetic magnetic;
    Space space;
    EqBand hp, lp, low, high;
    bool eqWasOn = false;

    Flux flux[kModules];
    Smooth onS[kModules], amtS[kModules];
    Smooth inS, outS, mixS, widthS, eqOnS, magnS, hpS, lpS;
    float fluxValue[kModules] = {}, lastAmt[kModules] = {};
    float c_hp = 0.0f, c_lp = 0.0f, c_in = 1.0f, c_out = 1.0f, c_mix = 1.0f, c_width = 1.0f, c_eq = 0.0f, c_wowRate = 0.6f, c_flutRate = 9.0f;
    float followEnv = 0.0f, envAtk = 0.01f, envRel = 0.001f;
    int ctlCounter = 0;
    bool firstCtl = true;
    std::array<float, kBlock> tmpL {}, tmpR {}, postNoiseL {}, postNoiseR {}, eqL {}, eqR {}, stL {}, stR {};
};
} // namespace scour
