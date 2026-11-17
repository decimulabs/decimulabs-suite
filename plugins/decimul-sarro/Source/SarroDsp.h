#pragma once

// Motor de Sarro (supresor de resonancias): solo biblioteca estándar, se prueba sin JUCE.
//
// Análisis y síntesis por STFT (ventana raíz de Hann, 75 % de solapamiento): en cada cuadro se mide
// el espectro de cada canal, se compara cada bin contra el "piso" del espectro de su alrededor
// (promedio en dB sobre una ventana que mide una fracción de octava) y lo que sobresale por encima
// del umbral se baja, hasta DEPTH. La reducción se suaviza en el tiempo (ataque y caída) y entre
// bins vecinos (para que no queden "agujeros" que suenen a burbujas), se limita a la zona elegida
// (LOW/HIGH FOCUS) y se enlaza entre canales. La señal original se retrasa lo mismo que el
// procesamiento (latencia = tamaño de la FFT) para poder mezclar y para el modo DELTA.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace sarro
{
constexpr float kPi = 3.14159265358979f;

struct Params
{
    float depth = 50.0f;           // 0..100 %: cuánto se baja lo que sobresale
    float sharpness = 50.0f;       // 0..100 %: qué tan angosto tiene que ser un pico para tratarlo
    float sensitivity = 50.0f;     // 0..100 %: cuánto tiene que sobresalir para tratarlo (más = detecta más)
    float attackMs = 10.0f;        // 1..200
    float releaseMs = 150.0f;      // 20..1500
    float lowHz = 80.0f;           // zona de trabajo
    float highHz = 16000.0f;
    float link = 100.0f;           // 0..100 % enlace estéreo
    float mix = 100.0f;            // 0..100 %
    float trimDb = 0.0f;           // salida
    bool delta = false;            // oír solo lo que se está quitando
    bool bypass = false;           // sale la original (con el mismo retardo)
};

/** FFT compleja de potencias de 2 (iterativa, in situ). */
class Fft
{
public:
    void prepare(int size)
    {
        n = size;
        cosT.resize((size_t) n / 2);
        sinT.resize((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
        {
            cosT[(size_t) i] = std::cos(2.0f * kPi * (float) i / (float) n);
            sinT[(size_t) i] = -std::sin(2.0f * kPi * (float) i / (float) n);
        }
        rev.resize((size_t) n);
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    void forward(float* re, float* im) const
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int k = 0; k < half; ++k)
                {
                    const float wr = cosT[(size_t) (k * step)], wi = sinT[(size_t) (k * step)];
                    const int a = i + k, b = i + k + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr; im[a] += ti;
                }
        }
    }

    /** Inversa (con escala 1/n): conj(FFT(conj(x)))/n. */
    void inverse(float* re, float* im) const
    {
        for (int i = 0; i < n; ++i) im[i] = -im[i];
        forward(re, im);
        const float s = 1.0f / (float) n;
        for (int i = 0; i < n; ++i) { re[i] *= s; im[i] = -im[i] * s; }
    }

private:
    int n = 0;
    std::vector<float> cosT, sinT;
    std::vector<int> rev;
};

class ResonanceSuppressor
{
public:
    static constexpr int kDisplay = 128;          // puntos del espectro para la pantalla (20 Hz a 20 kHz, logarítmico)

    void prepare(double sampleRate)
    {
        sr = sampleRate;
        N = sampleRate > 88000.0 ? 4096 : 2048;
        hop = N / 4;
        K = N / 2 + 1;
        fft.prepare(N);

        win.resize((size_t) N);
        for (int i = 0; i < N; ++i)
            win[(size_t) i] = std::sin(kPi * ((float) i + 0.5f) / (float) N);        // raíz de Hann
        float sum = 0.0f;
        for (int j = 0; j < 4; ++j)
            sum += win[(size_t) (j * hop)] * win[(size_t) (j * hop)];
        norm = 1.0f / sum;

        for (int c = 0; c < 2; ++c)
        {
            inRing[c].assign((size_t) N, 0.0f);
            outRing[c].assign((size_t) N, 0.0f);
            dryRing[c].assign((size_t) N, 0.0f);
            state[c].assign((size_t) K, 0.0f);
            powSm[c].assign((size_t) K, 0.0f);
        }
        zr.assign((size_t) N, 0.0f); zi.assign((size_t) N, 0.0f);
        for (auto* v : { &dbL, &dbR, &redL, &redR, &tmp, &weight, &cs })
            v->assign((size_t) K + 2, 0.0f);
        reL.assign((size_t) K, 0.0f); imL.assign((size_t) K, 0.0f); reR.assign((size_t) K, 0.0f); imR.assign((size_t) K, 0.0f);

        w = 0;
        count = 0;
        mixS = 1.0f;

        // pesos de la zona de trabajo y rangos de la pantalla
        for (int k = 0; k < K; ++k)
            weight[(size_t) k] = 1.0f;
        const float fMax = std::min(20000.0f, (float) sr * 0.45f);
        for (int d = 0; d < kDisplay; ++d)
        {
            const float f0 = 20.0f * std::pow(fMax / 20.0f, (float) d / (float) kDisplay);
            const float f1 = 20.0f * std::pow(fMax / 20.0f, (float) (d + 1) / (float) kDisplay);
            dispLo[(size_t) d] = std::clamp((int) std::floor(f0 * (float) N / (float) sr), 1, K - 1);
            dispHi[(size_t) d] = std::clamp(std::max(dispLo[(size_t) d], (int) std::ceil(f1 * (float) N / (float) sr)), 1, K - 1);
            dispSpec[(size_t) d].store(-120.0f);
            dispRed[(size_t) d].store(0.0f);
        }
        setParams(params);
    }

    int latencySamples() const { return N; }

    void setParams(const Params& p)
    {
        params = p;
        attackCoef = std::exp(-(float) hop / (std::max(p.attackMs, 1.0f) * 0.001f * (float) sr));
        releaseCoef = std::exp(-(float) hop / (std::max(p.releaseMs, 5.0f) * 0.001f * (float) sr));
        mixCoef = 1.0f - std::exp(-1.0f / (0.02f * (float) sr));
        trimGain = std::pow(10.0f, p.trimDb * 0.05f);
        for (int k = 0; k < K; ++k)
        {
            const float f = std::max((float) k * (float) sr / (float) N, 1.0f);
            const float lo = std::clamp(std::log2(f / std::max(p.lowHz, 20.0f)) * 2.0f + 0.5f, 0.0f, 1.0f);
            const float hi = std::clamp(std::log2(std::max(p.highHz, 100.0f) / f) * 2.0f + 0.5f, 0.0f, 1.0f);
            weight[(size_t) k] = lo * hi;
        }
    }

    /** Procesa en el lugar (estéreo). R puede ser nullptr (mono). */
    void process(float* l, float* r, int n)
    {
        const bool stereo = r != nullptr;
        for (int i = 0; i < n; ++i)
        {
            const float xl = l[i], xr = stereo ? r[i] : l[i];

            // la señal original sale con el mismo retardo que el procesamiento
            const float dl = dryRing[0][(size_t) w], dr = dryRing[1][(size_t) w];
            dryRing[0][(size_t) w] = xl; dryRing[1][(size_t) w] = xr;
            inRing[0][(size_t) w] = xl; inRing[1][(size_t) w] = xr;
            const float wetL = outRing[0][(size_t) w], wetR = outRing[1][(size_t) w];
            outRing[0][(size_t) w] = 0.0f; outRing[1][(size_t) w] = 0.0f;
            w = (w + 1) & (N - 1);

            if (++count >= hop)
            {
                count = 0;
                processFrame();
            }

            const float target = params.bypass ? 0.0f : params.mix * 0.01f;
            mixS += (target - mixS) * mixCoef;
            const float trim = trimGain;
            float ol, orr;
            if (params.delta)
            {
                ol = (dl - wetL) * (params.bypass ? 0.0f : 1.0f);
                orr = (dr - wetR) * (params.bypass ? 0.0f : 1.0f);
            }
            else
            {
                ol = dl + (wetL - dl) * mixS;
                orr = dr + (wetR - dr) * mixS;
            }
            l[i] = ol * trim;
            if (stereo) r[i] = orr * trim;
        }
    }

    /** Copia lo último que dibujó el motor: espectro de entrada (dBFS) y reducción (dB, positiva). */
    void getDisplay(float* spec, float* red) const
    {
        for (int d = 0; d < kDisplay; ++d)
        {
            spec[d] = dispSpec[(size_t) d].load(std::memory_order_relaxed);
            red[d] = dispRed[(size_t) d].load(std::memory_order_relaxed);
        }
    }

    /** Mayor reducción del último cuadro (dB, positiva). */
    float lastMaxReduction() const { return maxRed.load(std::memory_order_relaxed); }

private:
    void processFrame()
    {
        // cuadro: las últimas N muestras (la más vieja primero), con ventana; L en la parte real y R en la imaginaria
        for (int i = 0; i < N; ++i)
        {
            const size_t idx = (size_t) ((w + i) & (N - 1));
            zr[(size_t) i] = inRing[0][idx] * win[(size_t) i];
            zi[(size_t) i] = inRing[1][idx] * win[(size_t) i];
        }
        fft.forward(zr.data(), zi.data());

        // separar L y R (dos señales reales en una FFT compleja)
        for (int k = 0; k < K; ++k)
        {
            const int m = (N - k) % N;
            const float a = zr[(size_t) k], b = zi[(size_t) k], c = zr[(size_t) m], d = zi[(size_t) m];
            reL[(size_t) k] = 0.5f * (a + c); imL[(size_t) k] = 0.5f * (b - d);
            reR[(size_t) k] = 0.5f * (b + d); imR[(size_t) k] = -0.5f * (a - c);
        }
        imL[0] = imR[0] = 0.0f; imL[(size_t) K - 1] = imR[(size_t) K - 1] = 0.0f;

        const float calibration = 20.0f * std::log10((float) N / kPi);              // una seno de 0 dBFS marca 0 dB
        constexpr float detAlpha = 0.4f;                                            // promedia ~2.5 cuadros: el ruido fluctúa, un pico no
        for (int k = 0; k < K; ++k)
        {
            const float pL = reL[(size_t) k] * reL[(size_t) k] + imL[(size_t) k] * imL[(size_t) k];
            const float pR = reR[(size_t) k] * reR[(size_t) k] + imR[(size_t) k] * imR[(size_t) k];
            powSm[0][(size_t) k] += detAlpha * (pL - powSm[0][(size_t) k]);
            powSm[1][(size_t) k] += detAlpha * (pR - powSm[1][(size_t) k]);
            dbL[(size_t) k] = 10.0f * std::log10(powSm[0][(size_t) k] + 1.0e-12f) - calibration;
            dbR[(size_t) k] = 10.0f * std::log10(powSm[1][(size_t) k] + 1.0e-12f) - calibration;
        }

        computeReduction(dbL, redL, state[0]);
        computeReduction(dbR, redR, state[1]);

        // enlace estéreo y ganancias
        const float link = params.link * 0.01f;
        float worst = 0.0f;
        for (int k = 0; k < K; ++k)
        {
            const float mx = std::max(redL[(size_t) k], redR[(size_t) k]);
            const float rl = redL[(size_t) k] + (mx - redL[(size_t) k]) * link;
            const float rr = redR[(size_t) k] + (mx - redR[(size_t) k]) * link;
            redL[(size_t) k] = rl; redR[(size_t) k] = rr;
            const float gl = std::exp(-rl * 0.11512925f), gr = std::exp(-rr * 0.11512925f);
            reL[(size_t) k] *= gl; imL[(size_t) k] *= gl;
            reR[(size_t) k] *= gr; imR[(size_t) k] *= gr;
            worst = std::max(worst, std::max(rl, rr));
        }
        maxRed.store(worst, std::memory_order_relaxed);
        updateDisplay();

        // recomponer Z = L + jR con simetría hermítica y volver al tiempo
        for (int k = 0; k < K; ++k)
        {
            zr[(size_t) k] = reL[(size_t) k] - imR[(size_t) k];
            zi[(size_t) k] = imL[(size_t) k] + reR[(size_t) k];
            if (k > 0 && k < K - 1)
            {
                const int m = N - k;
                zr[(size_t) m] = reL[(size_t) k] + imR[(size_t) k];
                zi[(size_t) m] = -imL[(size_t) k] + reR[(size_t) k];
            }
        }
        fft.inverse(zr.data(), zi.data());

        // ventana de síntesis y suma al buffer de salida: el primer bloque de este cuadro sale en la próxima muestra
        for (int i = 0; i < N; ++i)
        {
            const size_t idx = (size_t) ((w + i) & (N - 1));
            outRing[0][idx] += zr[(size_t) i] * win[(size_t) i] * norm;
            outRing[1][idx] += zi[(size_t) i] * win[(size_t) i] * norm;
        }
    }

    /** Reducción pedida (dB, positiva) para cada bin de un canal, ya suavizada en tiempo y frecuencia. */
    void computeReduction(const std::vector<float>& db, std::vector<float>& red, std::vector<float>& st)
    {
        // piso del espectro: promedio en dB sobre una ventana de fracción de octava (prefijo acumulado)
        cs[0] = 0.0f;
        for (int k = 0; k < K; ++k)
            cs[(size_t) k + 1] = cs[(size_t) k] + db[(size_t) k];
        const float octaves = 0.62f - 0.46f * params.sharpness * 0.01f;               // 0.62 oct (suave) .. 0.16 oct (agudo)
        const float f = std::pow(2.0f, octaves * 0.5f);
        const float threshold = 12.0f - 10.0f * params.sensitivity * 0.01f;           // 12 dB .. 2 dB sobre el piso
        const float amount = params.depth * 0.01f * 1.25f;

        for (int k = 0; k < K; ++k)
        {
            const int lo = std::max(0, std::min(k - 1, (int) std::floor((float) k / f)));
            const int hi = std::min(K - 1, std::max(k + 1, (int) std::ceil((float) k * f)));
            const float floorDb = (cs[(size_t) hi + 1] - cs[(size_t) lo]) / (float) (hi - lo + 1);
            const float excess = db[(size_t) k] - floorDb - threshold;
            tmp[(size_t) k] = std::min(24.0f, std::max(0.0f, excess) * amount) * weight[(size_t) k];
        }
        // suavizado entre bins vecinos (dos pasadas de [1 2 1] / 4) sin perder la profundidad de los picos
        for (int pass = 0; pass < 2; ++pass)
        {
            red[0] = tmp[0];
            for (int k = 1; k < K - 1; ++k)
                red[(size_t) k] = 0.25f * tmp[(size_t) k - 1] + 0.5f * tmp[(size_t) k] + 0.25f * tmp[(size_t) k + 1];
            red[(size_t) K - 1] = tmp[(size_t) K - 1];
            for (int k = 0; k < K; ++k)
                tmp[(size_t) k] = std::max(red[(size_t) k], 0.8f * tmp[(size_t) k]);
        }
        // suavizado en el tiempo: ataque cuando hay que bajar más, caída cuando se suelta
        for (int k = 0; k < K; ++k)
        {
            const float target = tmp[(size_t) k];
            const float coef = target > st[(size_t) k] ? attackCoef : releaseCoef;
            st[(size_t) k] = coef * st[(size_t) k] + (1.0f - coef) * target;
            red[(size_t) k] = st[(size_t) k];
        }
    }

    void updateDisplay()
    {
        for (int d = 0; d < kDisplay; ++d)
        {
            float spec = -120.0f, red = 0.0f;
            for (int k = dispLo[(size_t) d]; k <= dispHi[(size_t) d]; ++k)
            {
                spec = std::max(spec, std::max(dbL[(size_t) k], dbR[(size_t) k]));
                red = std::max(red, 0.5f * (redL[(size_t) k] + redR[(size_t) k]));
            }
            dispSpec[(size_t) d].store(spec, std::memory_order_relaxed);
            dispRed[(size_t) d].store(red, std::memory_order_relaxed);
        }
    }

    double sr = 48000.0;
    Params params;
    int N = 2048, hop = 512, K = 1025;
    Fft fft;
    std::vector<float> win;
    float norm = 0.5f;
    std::vector<float> inRing[2], outRing[2], dryRing[2], state[2], powSm[2];
    float trimGain = 1.0f;
    std::vector<float> zr, zi, reL, imL, reR, imR, dbL, dbR, redL, redR, tmp, weight, cs;
    int w = 0, count = 0;
    float attackCoef = 0.9f, releaseCoef = 0.99f, mixS = 1.0f, mixCoef = 0.001f;
    std::array<int, kDisplay> dispLo {}, dispHi {};
    std::array<std::atomic<float>, kDisplay> dispSpec, dispRed;
    std::atomic<float> maxRed { 0.0f };
};
} // namespace sarro
