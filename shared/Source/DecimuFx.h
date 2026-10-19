#pragma once

// DSP propio compartido por Acoustic y Guitars (reemplaza los bloques "de referencia" de
// juce::dsp: IIR por bloque, Compressor, Reverb tipo Freeverb, Chorus/Phaser y el delay
// simple). Solo usa la biblioteca estándar: se prueba sin JUCE ni host.
//
// Reglas de todas las clases: nada de asignaciones en el hilo de audio (todo se reserva en
// prepare()), parámetros suavizados por muestra o por tramo de 16 muestras, y cero latencia.

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace decimu::fx
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }
inline float gainToDb(float g) { return 20.0f * std::log10(std::max(g, 1.0e-9f)); }

/** Suavizador de un polo, avanzado por muestra. */
class Smooth
{
public:
    void prepare(double sampleRate, double timeMs)
    {
        coef = (float) (1.0 - std::exp(-1.0 / (std::max(timeMs, 0.01) * 0.001 * sampleRate)));
    }
    void snap(float v) { cur = target = v; }
    void set(float v) { target = v; }
    float next() { cur += (target - cur) * coef; return cur; }
    float value() const { return cur; }
    float getTarget() const { return target; }

private:
    float cur = 0.0f, target = 0.0f, coef = 1.0f;
};

/** Línea de retardo circular con lectura cúbica (Lagrange de 4 puntos).
    Orden por muestra: read(d) primero y push(x) después; read(d) devuelve lo que entró
    hace `d` muestras (d >= 2). */
class DelayBuf
{
public:
    void prepare(int maxDelaySamples)
    {
        int size = 16;
        while (size < maxDelaySamples + 8)
            size <<= 1;
        buf.assign((size_t) size, 0.0f);
        mask = size - 1;
        pos = 0;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
    void push(float x) { buf[(size_t) pos] = x; pos = (pos + 1) & mask; }
    int capacity() const { return mask + 1; }

    float read(float d) const
    {
        d = std::clamp(d, 2.0f, (float) (mask - 4));
        const int i = (int) d;
        const float f = d - (float) i;
        const float ym1 = buf[(size_t) ((pos - (i - 1)) & mask)];
        const float y0  = buf[(size_t) ((pos - i) & mask)];
        const float y1  = buf[(size_t) ((pos - (i + 1)) & mask)];
        const float y2  = buf[(size_t) ((pos - (i + 2)) & mask)];
        const float c1 = y1 - ym1 * (1.0f / 3.0f) - y0 * 0.5f - y2 * (1.0f / 6.0f);
        const float c2 = 0.5f * (ym1 + y1) - y0;
        const float c3 = (y2 - ym1) * (1.0f / 6.0f) + 0.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    }

private:
    std::vector<float> buf;
    int mask = 15, pos = 0;
};

//==============================================================================
/** Banda de EQ biquad (shelf, peak, pasa-bajos/altos) con frecuencia y ganancia suavizadas:
    los coeficientes se recalculan cada 16 muestras sin crear objetos. Las fórmulas son las
    mismas de juce::dsp::IIR::Coefficients (el color del EQ no cambia, solo desaparecen los
    escalones al mover una perilla). */
class EqBand
{
public:
    enum class Type { lowShelf, highShelf, peak, lowpass, highpass };

    void prepare(double sampleRate, Type t, float q = 0.70710678f)
    {
        sr = sampleRate;
        type = t;
        Q = q;
        chunkCoef = (float) (1.0 - std::exp(-16.0 / (0.02 * sampleRate)));
        reset();
        set(1000.0f, 0.0f, true);
    }

    void reset() { for (auto& v : z1) v = 0.0; for (auto& v : z2) v = 0.0; }

    void set(float freqHz, float gainDb, bool jump = false)
    {
        tLogF = std::log(std::max(freqHz, 2.0f));
        tGain = gainDb;
        if (jump)
        {
            sLogF = tLogF;
            sGain = tGain;
            compute();
        }
    }

    void process(float* l, float* r, int n)
    {
        for (int start = 0; start < n; start += 16)
        {
            if (std::abs(tLogF - sLogF) > 1.0e-5f || std::abs(tGain - sGain) > 1.0e-4f)
            {
                sLogF += (tLogF - sLogF) * chunkCoef;
                sGain += (tGain - sGain) * chunkCoef;
                compute();
            }
            const int end = std::min(n, start + 16);
            for (int i = start; i < end; ++i)
            {
                l[i] = tick(0, l[i]);
                if (r != nullptr)
                    r[i] = tick(1, r[i]);
            }
        }
    }

    /** Magnitud de la respuesta (lineal) a una frecuencia, con los coeficientes actuales. */
    double magnitudeAt(double freq) const
    {
        const double w = 2.0 * kPi * freq / sr;
        const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1.0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
    }

private:
    float tick(int ch, float x)
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float) y;
    }

    void compute()
    {
        const double f = std::clamp((double) std::exp(sLogF), 2.0, 0.49 * sr);
        const double gainFactor = std::pow(10.0, (double) sGain / 20.0);
        double c[6] {};
        switch (type)
        {
            case Type::lowShelf:
            {
                const double A = std::sqrt(std::max(0.0, gainFactor));
                const double am1 = A - 1.0, ap1 = A + 1.0;
                const double omega = 2.0 * kPi * f / sr, coso = std::cos(omega);
                const double beta = std::sin(omega) * std::sqrt(A) / Q;
                const double am1c = am1 * coso;
                c[0] = A * (ap1 - am1c + beta);  c[1] = A * 2.0 * (am1 - ap1 * coso);  c[2] = A * (ap1 - am1c - beta);
                c[3] = ap1 + am1c + beta;        c[4] = -2.0 * (am1 + ap1 * coso);     c[5] = ap1 + am1c - beta;
                break;
            }
            case Type::highShelf:
            {
                const double A = std::sqrt(std::max(0.0, gainFactor));
                const double am1 = A - 1.0, ap1 = A + 1.0;
                const double omega = 2.0 * kPi * f / sr, coso = std::cos(omega);
                const double beta = std::sin(omega) * std::sqrt(A) / Q;
                const double am1c = am1 * coso;
                c[0] = A * (ap1 + am1c + beta);  c[1] = A * -2.0 * (am1 + ap1 * coso); c[2] = A * (ap1 + am1c - beta);
                c[3] = ap1 - am1c + beta;        c[4] = 2.0 * (am1 - ap1 * coso);      c[5] = ap1 - am1c - beta;
                break;
            }
            case Type::peak:
            {
                const double A = std::sqrt(std::max(0.0, gainFactor));
                const double omega = 2.0 * kPi * f / sr;
                const double alpha = std::sin(omega) / (Q * 2.0);
                const double c2 = -2.0 * std::cos(omega);
                c[0] = 1.0 + alpha * A;  c[1] = c2;  c[2] = 1.0 - alpha * A;
                c[3] = 1.0 + alpha / A;  c[4] = c2;  c[5] = 1.0 - alpha / A;
                break;
            }
            case Type::lowpass:
            {
                const double n = 1.0 / std::tan(kPi * f / sr);
                const double n2 = n * n, invQ = 1.0 / Q;
                const double k = 1.0 / (1.0 + invQ * n + n2);
                c[0] = k;  c[1] = 2.0 * k;  c[2] = k;
                c[3] = 1.0;  c[4] = k * 2.0 * (1.0 - n2);  c[5] = k * (1.0 - invQ * n + n2);
                break;
            }
            case Type::highpass:
            {
                const double n = std::tan(kPi * f / sr);
                const double n2 = n * n, invQ = 1.0 / Q;
                const double k = 1.0 / (1.0 + invQ * n + n2);
                c[0] = k;  c[1] = -2.0 * k;  c[2] = k;
                c[3] = 1.0;  c[4] = k * 2.0 * (n2 - 1.0);  c[5] = k * (1.0 - invQ * n + n2);
                break;
            }
        }
        const double inv = 1.0 / c[3];
        b0 = c[0] * inv; b1 = c[1] * inv; b2 = c[2] * inv;
        a1 = c[4] * inv; a2 = c[5] * inv;
    }

    Type type = Type::peak;
    double sr = 48000.0;
    float Q = 0.70710678f, chunkCoef = 0.1f;
    float tLogF = 0, sLogF = 0, tGain = 0, sGain = 0;
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};
};

//==============================================================================
/** Compresor con detector ligado entre canales (el mismo recorte en L y R: la imagen no se
    inclina), cálculo de ganancia en dB con rodilla suave, suavizado de ataque/release sobre
    la reducción y medidor de reducción real (sin el makeup). Cero latencia. */
class LinkedCompressor
{
public:
    void prepare(double sampleRate) { sr = sampleRate; reset(); }
    void reset() { grDb = 0.0f; meter = 0.0f; }

    void set(float thresholdDb, float ratio, float kneeDb, float attackMs, float releaseMs, float makeupDb)
    {
        thr = thresholdDb;
        ratioInv = 1.0f / std::max(ratio, 1.0f);
        knee = std::max(kneeDb, 0.0f);
        atk = (float) std::exp(-1.0 / (std::max(attackMs, 0.05f) * 0.001 * sr));
        rel = (float) std::exp(-1.0 / (std::max(releaseMs, 1.0f) * 0.001 * sr));
        makeup = makeupDb;
    }

    /** `r` puede ser nullptr (mono). */
    void process(float* l, float* r, int n)
    {
        float worst = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float lvl = std::abs(l[i]);
            if (r != nullptr)
                lvl = std::max(lvl, std::abs(r[i]));
            const float xdb = 20.0f * std::log10(lvl + 1.0e-9f);
            const float over = xdb - thr;

            float target = 0.0f; // reducción pedida (<= 0 dB)
            if (knee < 1.0e-3f)
                target = over > 0.0f ? over * (ratioInv - 1.0f) : 0.0f;
            else if (2.0f * over > knee)
                target = over * (ratioInv - 1.0f);
            else if (2.0f * over >= -knee)
            {
                const float t = over + knee * 0.5f;
                target = (ratioInv - 1.0f) * t * t / (2.0f * knee);
            }

            const float c = target < grDb ? atk : rel;
            grDb = c * grDb + (1.0f - c) * target;
            worst = std::min(worst, grDb);

            const float g = std::exp((grDb + makeup) * 0.115129255f);
            l[i] *= g;
            if (r != nullptr)
                r[i] *= g;
        }
        meter = -worst;
    }

    /** Reducción máxima del último bloque, en dB positivos. */
    float getGainReductionDb() const { return meter; }

private:
    double sr = 48000.0;
    float thr = 0.0f, ratioInv = 1.0f, knee = 0.0f, atk = 0.0f, rel = 0.0f, makeup = 0.0f;
    float grDb = 0.0f, meter = 0.0f;
};

//==============================================================================
/** Retardo estéreo con tiempo suavizado (cambiar el pre-delay o el tempo no pega saltos). */
class SmoothDelay
{
public:
    void prepare(double sampleRate, double maxSeconds)
    {
        sr = sampleRate;
        for (auto& b : buf)
            b.prepare((int) (maxSeconds * sampleRate) + 4);
        maxSamples = (float) (maxSeconds * sampleRate);
        time.prepare(sampleRate, 60.0);
        time.snap(2.0f);
        reset();
    }
    void reset() { for (auto& b : buf) b.clear(); }
    void setMs(float ms, bool jump = false)
    {
        const float d = std::clamp(ms * 0.001f * (float) sr, 2.0f, maxSamples);
        time.set(d);
        if (jump) time.snap(d);
    }
    void process(float* l, float* r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float d = time.next();
            const float yl = buf[0].read(d);
            buf[0].push(l[i]);
            l[i] = yl;
            if (r != nullptr)
            {
                const float yr = buf[1].read(d);
                buf[1].push(r[i]);
                r[i] = yr;
            }
        }
    }

private:
    double sr = 48000.0;
    float maxSamples = 1000.0f;
    std::array<DelayBuf, 2> buf;
    Smooth time;
};

//==============================================================================
/** Reverb de red de retardos realimentados (FDN de 8 líneas, matriz de Householder):
    difusión de entrada con allpass, retardos modulados (sin sonido metálico), caída más
    rápida en agudos (`hfRatio`) y reflexiones tempranas. Dos `variant` con retardos y
    LFO distintos, para que Space A y Space B no sean el mismo reverb con otro valor. */
class FdnReverb
{
public:
    static constexpr int N = 8;

    void prepare(double sampleRate, int variantIndex)
    {
        sr = sampleRate;
        variant = variantIndex;
        const double scale = sampleRate / 1000.0;
        const int maxLine = (int) (kMaxSize * 82.0 * scale) + (int) (0.004 * sampleRate) + 16;
        for (auto& l : lines)
            l.prepare(maxLine);
        erBuf.prepare((int) (0.12 * sampleRate) + 8);

        static const float kBase[2][N] = {
            { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 61.7f, 71.3f, 79.9f },
            { 23.3f, 31.7f, 39.1f, 47.9f, 52.9f, 58.3f, 67.7f, 76.1f } };
        static const float kRate[2][N] = {
            { 0.31f, 0.43f, 0.53f, 0.67f, 0.79f, 0.89f, 1.03f, 1.19f },
            { 0.37f, 0.47f, 0.59f, 0.71f, 0.83f, 0.97f, 1.09f, 1.27f } };
        const int v = std::clamp(variantIndex, 0, 1);
        for (int i = 0; i < N; ++i)
        {
            baseMs[(size_t) i] = kBase[v][i];
            lfoInc[(size_t) i] = kRate[v][i] / (float) sampleRate;
        }

        static const int kApMs10[2][4] = { { 51, 79, 113, 161 }, { 47, 73, 127, 149 } }; // décimas de ms
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k)
            {
                auto& ap = (c == 0 ? apL : apR)[(size_t) k];
                const int len = std::max(8, (int) (kApMs10[c][k] * 0.1 * scale) + (v == 1 ? 3 : 0) + c * 2);
                ap.buf.assign((size_t) len, 0.0f);
                ap.pos = 0;
                ap.g = k < 2 ? 0.62f : 0.5f;
            }

        sizeS.prepare(sampleRate, 120.0);  rtS.prepare(sampleRate, 80.0);   hfS.prepare(sampleRate, 80.0);
        erS.prepare(sampleRate, 40.0);     modS.prepare(sampleRate, 80.0);  widthS.prepare(sampleRate, 40.0);
        inS.prepare(sampleRate, 25.0);     wetS.prepare(sampleRate, 25.0);  drainS.prepare(sampleRate, 60.0);
        setPreset(1.0f, 1.5f, 0.5f, 0.3f, 0.6f, 1.0f, true);
        inS.snap(0.0f);
        wetS.snap(0.0f);
        drainS.snap(0.0f);
        reset();
    }

    void reset()
    {
        for (auto& l : lines) l.clear();
        for (auto& v : lp) v = 0.0f;
        for (auto& a : apL) std::fill(a.buf.begin(), a.buf.end(), 0.0f);
        for (auto& a : apR) std::fill(a.buf.begin(), a.buf.end(), 0.0f);
        erBuf.clear();
        for (int i = 0; i < N; ++i)
            lfoPhase[(size_t) i] = (float) i / (float) N;
        updateLineCoefs();
    }

    /** size: escala de los retardos (0.35 cuarto chico .. 2.2 catedral); rt60 en segundos;
        hfRatio: duración de los agudos respecto de los graves (1 = igual, 0.3 = muy apagado). */
    void setPreset(float size, float rt60Sec, float hfRatio, float erLevel, float modDepth, float width,
                   bool jump = false)
    {
        sizeS.set(std::clamp(size, 0.3f, kMaxSize));
        rtS.set(std::clamp(rt60Sec, 0.1f, 12.0f));
        hfS.set(std::clamp(hfRatio, 0.1f, 1.0f));
        erS.set(erLevel);
        modS.set(modDepth);
        widthS.set(width);
        if (jump)
        {
            sizeS.snap(sizeS.getTarget()); rtS.snap(rtS.getTarget()); hfS.snap(hfS.getTarget());
            erS.snap(erLevel); modS.snap(modDepth); widthS.snap(width);
        }
    }

    /** Activo = entra señal y suena. Inactivo = la cola se drena rápido (no vuelve audio viejo). */
    void setActive(bool shouldBeActive, float mix)
    {
        inS.set(shouldBeActive ? 1.0f : 0.0f);
        wetS.set(shouldBeActive ? std::clamp(mix, 0.0f, 1.0f) : 0.0f);
        drainS.set(shouldBeActive ? 0.0f : 1.0f);
    }

    /** Escribe el reverb (solo la parte húmeda) en outL/outR. `inR` puede ser nullptr. */
    void process(const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        const float srF = (float) sr;
        for (int i = 0; i < n; ++i)
        {
            if ((i & 15) == 0)
                updateLineCoefs();

            const float size = sizeS.next();
            const float erLevel = erS.next();
            const float mod = modS.next();
            const float width = widthS.next();
            const float inGain = inS.next();
            const float wet = wetS.next();
            rtS.next(); hfS.next(); drainS.next();

            const float xl = inL[i] * inGain;
            const float xr = (inR != nullptr ? inR[i] : inL[i]) * inGain;

            // difusión de entrada
            float dl = xl, dr = xr;
            for (auto& a : apL) dl = a.process(dl);
            for (auto& a : apR) dr = a.process(dr);

            // leer las 8 líneas (retardo = base * size + modulación lenta)
            const float modAmp = mod * 0.00025f * srF * (0.5f + 0.4f * size);
            std::array<float, N> y;
            float sum = 0.0f;
            for (int k = 0; k < N; ++k)
            {
                auto& ph = lfoPhase[(size_t) k];
                ph += lfoInc[(size_t) k];
                if (ph >= 1.0f) ph -= 1.0f;
                const float d = baseMs[(size_t) k] * 0.001f * srF * size + modAmp * std::sin(6.2831853f * ph);
                const float v = lines[(size_t) k].read(d);
                const float f = lineG[(size_t) k] * v + lineP[(size_t) k] * lp[(size_t) k];
                lp[(size_t) k] = f;
                y[(size_t) k] = f;
                sum += f;
            }

            // Householder: x - (2/N) * suma
            const float s = sum * (2.0f / (float) N);
            for (int k = 0; k < N; ++k)
            {
                const float inj = (k & 1) ? dr : dl;
                const float sign = (k & 2) ? -1.0f : 1.0f;
                lines[(size_t) k].push(y[(size_t) k] - s + kInject * sign * inj);
            }

            // salida decorrelada (dos filas ortogonales de Hadamard)
            float wl = 0.0f, wr = 0.0f;
            for (int k = 0; k < N; ++k)
            {
                wl += ((k & 1) ? -1.0f : 1.0f) * y[(size_t) k];
                wr += ((k & 2) ? -1.0f : 1.0f) * y[(size_t) k];
            }
            wl *= kOut;
            wr *= kOut;

            // reflexiones tempranas (de la suma a mono)
            const float mono = 0.5f * (xl + xr);
            float el = 0.0f, er = 0.0f;
            const float tScale = (0.6f + 0.4f * size) * 0.001f * srF;
            for (int t = 0; t < kTaps; ++t)
            {
                el += kErGain[t] * erBuf.read(kErL[variant & 1][t] * tScale);
                er += kErGain[t] * erBuf.read(kErR[variant & 1][t] * tScale);
            }
            erBuf.push(mono);
            wl += el * erLevel * kErOut;
            wr += er * erLevel * kErOut;

            const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
            outL[i] = (mid + side) * wet;
            outR[i] = (mid - side) * wet;
        }
    }

    static constexpr float kMaxSize = 2.2f;
    static constexpr float kInject = 0.35f;
    static constexpr float kOut = 1.6f;      // calibrada con tests/EngineTest.cpp: Hall ~4-5 dB abajo del Freeverb anterior con ruido, ~igual con guitarra pulsada
    static constexpr float kErOut = 0.5f;
    static constexpr int kTaps = 6;

private:
    struct Allpass
    {
        std::vector<float> buf;
        int pos = 0;
        float g = 0.6f;
        float process(float x)
        {
            const float d = buf[(size_t) pos];
            const float y = -g * x + d;
            buf[(size_t) pos] = x + g * y;
            if (++pos >= (int) buf.size()) pos = 0;
            return y;
        }
    };

    void updateLineCoefs()
    {
        const double size = sizeS.value();
        const double rt = rtS.value() * (1.0 - drainS.value()) + 0.12 * drainS.value();
        const double hf = hfS.value();
        const double rtHf = std::max(rt * hf, 0.05);
        // `hfRatio` es la duración a 5 kHz respecto de los graves: se resuelve el polo del
        // filtro de cada línea para que su ganancia a 5 kHz sea exactamente la pedida
        // (|H| = (1-p) / sqrt(1 + p^2 - 2 p cos w)).
        const double cw = std::cos(2.0 * kPi * std::min(5000.0, 0.45 * sr) / sr);
        for (int k = 0; k < N; ++k)
        {
            const double len = baseMs[(size_t) k] * 0.001 * sr * size;
            const double gDc = std::pow(10.0, -3.0 * len / (rt * sr));
            const double r = std::pow(10.0, -3.0 * len / sr * (1.0 / rtHf - 1.0 / rt));
            const double r2 = r * r;
            double p = 0.0;
            if (r2 < 0.9999)
            {
                const double a = 1.0 - r2 * cw;
                p = (a - std::sqrt(std::max(0.0, a * a - (1.0 - r2) * (1.0 - r2)))) / (1.0 - r2);
            }
            p = std::clamp(p, 0.0, 0.98);
            lineG[(size_t) k] = (float) (gDc * (1.0 - p));
            lineP[(size_t) k] = (float) p;
        }
    }

    static constexpr float kErL[2][kTaps] = { { 7.3f, 13.1f, 19.7f, 27.3f, 37.9f, 47.3f },
                                              { 5.9f, 11.3f, 17.9f, 25.1f, 35.3f, 51.7f } };
    static constexpr float kErR[2][kTaps] = { { 9.1f, 15.7f, 23.3f, 31.1f, 41.3f, 53.9f },
                                              { 8.3f, 14.9f, 21.1f, 29.9f, 43.7f, 57.1f } };
    static constexpr float kErGain[kTaps] = { 0.7f, -0.55f, 0.45f, -0.35f, 0.28f, -0.2f };

    double sr = 48000.0;
    int variant = 0;
    std::array<DelayBuf, N> lines;
    std::array<float, N> baseMs {}, lfoPhase {}, lfoInc {}, lp {}, lineG {}, lineP {};
    std::array<Allpass, 4> apL, apR;
    DelayBuf erBuf;
    Smooth sizeS, rtS, hfS, erS, modS, widthS, inS, wetS, drainS;
};

//==============================================================================
/** Echo estéreo: tiempo suavizado, filtro (pasa-altos y pasa-bajos) y saturación suave
    dentro del lazo (las repeticiones se oscurecen como en una cinta y nunca explotan), y
    al apagarse se vacía solo (no vuelven repeticiones viejas). */
class StereoEcho
{
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        for (auto& b : buf)
            b.prepare((int) (2.7 * sampleRate));
        maxDelay = (float) (2.6 * sampleRate);
        timeS.prepare(sampleRate, 90.0);  fbS.prepare(sampleRate, 30.0);
        inS.prepare(sampleRate, 25.0);    mixS.prepare(sampleRate, 25.0);
        timeS.snap(0.12f * (float) sampleRate);
        fbS.snap(0.0f); inS.snap(0.0f); mixS.snap(0.0f);
        lpCoef = (float) (1.0 - std::exp(-2.0 * kPi * 4500.0 / sampleRate));
        hpCoef = (float) (1.0 - std::exp(-2.0 * kPi * 140.0 / sampleRate));
        reset();
    }

    void reset()
    {
        for (auto& b : buf) b.clear();
        for (auto& v : lpState) v = 0.0f;
        for (auto& v : hpState) v = 0.0f;
    }

    void set(float delayMs, float feedback01, float mix01, bool active)
    {
        timeS.set(std::clamp(delayMs * 0.001f * (float) sr, 2.0f, maxDelay));
        fbS.set(active ? std::clamp(feedback01, 0.0f, 1.0f) : 0.0f);
        inS.set(active ? 1.0f : 0.0f);
        mixS.set(active ? std::clamp(mix01, 0.0f, 1.0f) : 0.0f);
    }

    /** Suma el eco a outL/outR. `inR` puede ser nullptr (mono). */
    void process(const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float d = timeS.next();
            const float fb = fbS.next();
            const float in = inS.next();
            const float mix = mixS.next();
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* src = ch == 0 ? inL : (inR != nullptr ? inR : inL);
                float* dst = ch == 0 ? outL : outR;
                const float echo = buf[(size_t) ch].read(d);

                lpState[(size_t) ch] += lpCoef * (echo - lpState[(size_t) ch]);
                hpState[(size_t) ch] += hpCoef * (lpState[(size_t) ch] - hpState[(size_t) ch]);
                const float filtered = lpState[(size_t) ch] - hpState[(size_t) ch];

                buf[(size_t) ch].push(src[i] * in + softClip(filtered) * fb);
                dst[i] += echo * mix;
            }
        }
    }

private:
    static float softClip(float x)
    {
        x = std::clamp(x, -3.0f, 3.0f);
        return x * (27.0f + x * x) / (27.0f + 9.0f * x * x); // tanh aproximada, ganancia 1 cerca de 0
    }

    double sr = 48000.0;
    float maxDelay = 1000.0f, lpCoef = 0.5f, hpCoef = 0.01f;
    std::array<DelayBuf, 2> buf;
    std::array<float, 2> lpState {}, hpState {};
    Smooth timeS, fbS, inS, mixS;
};

//==============================================================================
/** Chorus / Flanger estéreo: el LFO de la derecha va desfasado un cuarto de ciclo, así una
    guitarra mono se abre en el panorama (el chorus de JUCE modulaba igual los dos lados).
    Devuelve solo la parte modulada. */
class ChorusFlanger
{
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        for (auto& b : buf)
            b.prepare((int) (0.03 * sampleRate) + 8);
        centreS.prepare(sampleRate, 40.0);  depthS.prepare(sampleRate, 40.0);
        fbS.prepare(sampleRate, 40.0);      rateS.prepare(sampleRate, 60.0);
        setMode(false, true);
        rateS.snap(0.8f);
        reset();
    }
    void reset() { for (auto& b : buf) b.clear(); last[0] = last[1] = 0.0f; }

    void setMode(bool flanger, bool jump = false)
    {
        centreS.set(flanger ? 3.5f : 7.5f);
        depthS.set(flanger ? 2.5f : 2.5f);
        fbS.set(flanger ? 0.5f : 0.0f);
        if (jump) { centreS.snap(centreS.getTarget()); depthS.snap(depthS.getTarget()); fbS.snap(fbS.getTarget()); }
    }
    void setRate(float hz) { rateS.set(hz); }

    void process(const float* inL, const float* inR, float* wetL, float* wetR, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            phase += rateS.next() / (float) sr;
            if (phase >= 1.0f) phase -= 1.0f;
            const float centre = centreS.next(), depth = depthS.next(), fb = fbS.next();
            for (int ch = 0; ch < 2; ++ch)
            {
                float ph = phase + (ch == 0 ? 0.0f : 0.25f);
                if (ph >= 1.0f) ph -= 1.0f;
                const float dMs = std::max(1.0f, centre + depth * std::sin(6.2831853f * ph));
                const float y = buf[(size_t) ch].read(dMs * 0.001f * (float) sr);
                const float x = ch == 0 ? inL[i] : (inR != nullptr ? inR[i] : inL[i]);
                buf[(size_t) ch].push(x + fb * y);
                (ch == 0 ? wetL : wetR)[i] = y;
                last[(size_t) ch] = y;
            }
        }
    }

private:
    double sr = 48000.0;
    float phase = 0.0f;
    std::array<DelayBuf, 2> buf;
    std::array<float, 2> last {};
    Smooth centreS, depthS, fbS, rateS;
};

/** Phaser de 6 etapas allpass por canal, con la derecha desfasada. Devuelve la señal con
    fase barrida (se suma a la seca afuera, así se forman las muescas). */
class Phaser
{
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        rateS.prepare(sampleRate, 60.0);
        rateS.snap(0.8f);
        reset();
    }
    void reset() { for (auto& c : z) for (auto& v : c) v = 0.0f; fbState[0] = fbState[1] = 0.0f; }
    void setRate(float hz) { rateS.set(hz); }
    void setShape(float centreHz, float feedback) { centre = centreHz; feedback_ = feedback; }

    void process(const float* inL, const float* inR, float* wetL, float* wetR, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            phase += rateS.next() / (float) sr;
            if (phase >= 1.0f) phase -= 1.0f;
            if ((i & 7) == 0)
                for (int ch = 0; ch < 2; ++ch)
                {
                    float ph = phase + (ch == 0 ? 0.0f : 0.25f);
                    if (ph >= 1.0f) ph -= 1.0f;
                    const float f = std::min(centre * std::pow(4.0f, std::sin(6.2831853f * ph)), 0.45f * (float) sr);
                    const float t = std::tan((float) kPi * f / (float) sr);
                    coef[(size_t) ch] = (t - 1.0f) / (t + 1.0f);
                }
            for (int ch = 0; ch < 2; ++ch)
            {
                float x = (ch == 0 ? inL[i] : (inR != nullptr ? inR[i] : inL[i])) + feedback_ * fbState[(size_t) ch];
                for (int s = 0; s < kStages; ++s)
                {
                    const float y = coef[(size_t) ch] * x + z[(size_t) ch][(size_t) s];
                    z[(size_t) ch][(size_t) s] = x - coef[(size_t) ch] * y;
                    x = y;
                }
                fbState[(size_t) ch] = x;
                (ch == 0 ? wetL : wetR)[i] = x;
            }
        }
    }

    static constexpr int kStages = 6;

private:
    double sr = 48000.0;
    float phase = 0.0f, centre = 1200.0f, feedback_ = 0.75f;
    std::array<float, 2> coef { 0.0f, 0.0f }, fbState {};
    std::array<std::array<float, kStages>, 2> z {};
    Smooth rateS;
};

//==============================================================================
/** Limiter de pico con anticipación de 1 ms (latencia = latencySamples()), ligado entre
    canales: ninguna muestra de salida pasa del techo. La ganancia necesaria se toma como el
    mínimo de la ventana de anticipación, se le aplica un release de un polo (80 ms) y se
    suaviza con un promedio de la misma longitud, así baja antes de que llegue el pico. */
class Limiter
{
public:
    void prepare(double sampleRate)
    {
        n = std::max(8, (int) std::ceil(0.001 * sampleRate));
        len = n + 1;
        for (auto& d : delay) d.assign((size_t) len, 0.0f);
        req.assign((size_t) len, 1.0f);
        box.assign((size_t) len, 1.0f);
        relCoef = (float) std::exp(-1.0 / (0.08 * sampleRate));
        reset();
    }

    void reset()
    {
        for (auto& d : delay) std::fill(d.begin(), d.end(), 0.0f);
        std::fill(req.begin(), req.end(), 1.0f);
        std::fill(box.begin(), box.end(), 1.0f);
        boxSum = (double) len;
        pos = 0;
        grel = 1.0f;
        meter = 0.0f;
    }

    int latencySamples() const { return n; }
    void setThresholdDb(float db) { ceiling = dbToGain(std::min(db, 24.0f)); }

    /** Solo retrasa el audio (para mantener la latencia informada cuando el plugin está en bypass). */
    void setPassThrough(bool shouldPass) { passThrough = shouldPass; }

    /** `r` puede ser nullptr (mono). */
    void process(float* l, float* r, int numSamples)
    {
        float worst = 1.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float peak = std::abs(l[i]);
            if (r != nullptr)
                peak = std::max(peak, std::abs(r[i]));
            const float g = (!passThrough && peak > ceiling) ? ceiling / peak : 1.0f;

            req[(size_t) pos] = g;
            float gmin = 1.0f;
            for (float v : req)
                gmin = std::min(gmin, v);

            grel = gmin < grel ? gmin : grel + (gmin - grel) * (1.0f - relCoef);

            boxSum += (double) grel - (double) box[(size_t) pos];
            box[(size_t) pos] = grel;
            const float gain = (float) (boxSum / (double) len);
            worst = std::min(worst, gain);

            // el audio sale con n muestras de retraso: se escribe la actual y se lee la de n pasos atrás
            const size_t readPos = (size_t) (pos + 1 >= len ? 0 : pos + 1);
            delay[0][(size_t) pos] = l[i];
            l[i] = delay[0][readPos] * gain;
            if (r != nullptr)
            {
                delay[1][(size_t) pos] = r[i];
                r[i] = delay[1][readPos] * gain;
            }
            if (++pos >= len)
                pos = 0;
        }
        meter = -gainToDb(worst);
    }

    /** Reducción máxima del último bloque, en dB positivos. */
    float getGainReductionDb() const { return meter; }

private:
    int n = 48, len = 49, pos = 0;
    float ceiling = 1.0f, relCoef = 0.999f, grel = 1.0f, meter = 0.0f;
    bool passThrough = false;
    double boxSum = 49.0;
    std::array<std::vector<float>, 2> delay;
    std::vector<float> req, box;
};

} // namespace decimu::fx
