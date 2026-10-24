#pragma once

// Sobremuestreo para el modo "fase natural": el motor IIR corre a 4x (a 44.1/48 kHz) o 2x
// (a 88.2/96 kHz). A esa frecuencia la invarianza al impulso deja la magnitud Y la fase iguales
// a las del filtro analógico en todo el rango audible (sin el "apretado" cerca de Nyquist).
// Precio: un poco de latencia (~1.3 ms), por los filtros de media banda de fase lineal.

#include "PrismaIir.h"
#include <vector>

namespace prisma
{
/** Filtro de media banda (halfband) de fase lineal: h[n] = 0.5 sinc((n - M)/2), ventana Kaiser.
    Tiene ceros en todas las muestras pares respecto del centro: la mitad del trabajo se ahorra. */
inline std::vector<double> designHalfband(int M, double beta)
{
    auto bessel0 = [](double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    };
    std::vector<double> h((size_t) (2 * M + 1), 0.0);
    double oddSum = 0.0;
    for (int n = 0; n <= 2 * M; ++n)
    {
        const int d = n - M;
        if (d == 0)
        {
            h[(size_t) n] = 0.5;
            continue;
        }
        if (d % 2 == 0)
            continue;
        const double x = 0.5 * d;
        const double sinc = std::sin(kPi * x) / (kPi * x);
        const double r = (double) d / (double) M;
        const double win = bessel0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / bessel0(beta);
        h[(size_t) n] = 0.5 * sinc * win;
        oddSum += h[(size_t) n];
    }
    // Ganancia exacta en continua (para que el pasabanda quede plano).
    for (int n = 0; n <= 2 * M; ++n)
        if ((n - M) % 2 != 0)
            h[(size_t) n] *= 0.5 / oddSum;
    return h;
}

/** Una etapa x2 (subida y bajada) para un canal. Latencia de ida y vuelta: M muestras de la
    frecuencia más baja. */
class HalfbandStage
{
public:
    void init(const std::vector<double>& coeffs)
    {
        h = coeffs;
        M = ((int) h.size() - 1) / 2;
        up0.clear();
        up1.clear();
        dnE.clear();
        dnO.clear();
        // Subida: y[2m] = 2 Σ h[2k] x[m-k], y[2m+1] = 2 Σ h[2k+1] x[m-k].
        for (int n = 0; n < (int) h.size(); ++n)
        {
            if (h[(size_t) n] == 0.0)
                continue;
            if (n % 2 == 0)
                up0.push_back({ 2.0 * h[(size_t) n], n / 2 });
            else
                up1.push_back({ 2.0 * h[(size_t) n], n / 2 });
        }
        // Bajada: y[m] = Σ h[n] u[2m - n] = Σ_{n par} h[n] uPar[m - n/2] + Σ_{n impar} h[n] uImpar[m - (n+1)/2].
        for (int n = 0; n < (int) h.size(); ++n)
        {
            if (h[(size_t) n] == 0.0)
                continue;
            if (n % 2 == 0)
                dnE.push_back({ h[(size_t) n], n / 2 });
            else
                dnO.push_back({ h[(size_t) n], (n + 1) / 2 });
        }
        int need = 1;
        while (need < (int) h.size() + 4)
            need <<= 1;
        mask = need - 1;
        histUp.assign((size_t) need, 0.0);
        histE.assign((size_t) need, 0.0);
        histO.assign((size_t) need, 0.0);
        reset();
    }

    void reset()
    {
        std::fill(histUp.begin(), histUp.end(), 0.0);
        std::fill(histE.begin(), histE.end(), 0.0);
        std::fill(histO.begin(), histO.end(), 0.0);
        posUp = posDn = 0;
    }

    int latency() const { return M; }

    /** in: n muestras → out: 2n muestras. */
    void upsample(const double* in, double* out, int n)
    {
        for (int m = 0; m < n; ++m)
        {
            posUp = (posUp + 1) & mask;
            histUp[(size_t) posUp] = in[m];
            double a = 0.0, b = 0.0;
            for (const auto& t : up0)
                a += t.c * histUp[(size_t) ((posUp - t.d) & mask)];
            for (const auto& t : up1)
                b += t.c * histUp[(size_t) ((posUp - t.d) & mask)];
            out[2 * m] = a;
            out[2 * m + 1] = b;
        }
    }

    /** in: 2n muestras → out: n muestras. */
    void downsample(const double* in, double* out, int n)
    {
        for (int m = 0; m < n; ++m)
        {
            posDn = (posDn + 1) & mask;
            histE[(size_t) posDn] = in[2 * m];
            histO[(size_t) posDn] = in[2 * m + 1];
            double y = 0.0;
            for (const auto& t : dnE)
                y += t.c * histE[(size_t) ((posDn - t.d) & mask)];
            for (const auto& t : dnO)
                y += t.c * histO[(size_t) ((posDn - t.d) & mask)];
            out[m] = y;
        }
    }

private:
    struct Tap
    {
        double c;
        int d;
    };
    std::vector<double> h;
    int M = 0;
    std::vector<Tap> up0, up1, dnE, dnO;
    std::vector<double> histUp, histE, histO;
    int mask = 0, posUp = 0, posDn = 0;
};

/** El motor IIR sobremuestreado (modo fase natural). */
class NaturalPhaseEngine
{
public:
    void prepare(double sampleRate, int maxBlock)
    {
        baseRate = sampleRate;
        factor = sampleRate <= 50000.0 ? 4 : (sampleRate <= 100000.0 ? 2 : 1);
        core.prepare(sampleRate * factor);

        const auto hA = designHalfband(59, 8.6);   // ~87 dB de rechazo, transición 20 → 24 kHz
        const auto hB = designHalfband(10, 8.5);   // segunda etapa (mucho más holgada)
        for (int ch = 0; ch < 2; ++ch)
        {
            stageA[ch].init(hA);
            stageB[ch].init(hB);
            inA[ch].init(hA);
            inB[ch].init(hB);
        }
        const size_t big = (size_t) (maxBlock * factor + 8);
        for (auto* v : { &bufL, &bufR, &midL, &midR, &inUpL, &inUpR, &inMidL, &inMidR, &scUpL, &scUpR })
            v->assign(big, 0.0);
        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            stageA[ch].reset();
            stageB[ch].reset();
            inA[ch].reset();
            inB[ch].reset();
        }
        core.reset();
    }

    int latency() const
    {
        if (factor == 1)
            return 0;
        if (factor == 2)
            return stageA[0].latency();
        return stageA[0].latency() + stageB[0].latency() / 2;
    }

    int oversampling() const { return factor; }

    void process(double* L, double* R, const double* inL, const double* inR, const double* scL, const double* scR, int n)
    {
        if (factor == 1)
        {
            core.process(L, R, inL, inR, scL, scR, n);
            return;
        }
        const int nu = n * factor;
        const bool stereo = R != nullptr;

        // Señal y copia de la entrada (para la dinámica), las dos subidas igual.
        up(0, L, bufL.data(), midL.data(), n, stageA, stageB);
        up(0, inL, inUpL.data(), inMidL.data(), n, inA, inB);
        if (stereo)
        {
            up(1, R, bufR.data(), midR.data(), n, stageA, stageB);
            up(1, inR, inUpR.data(), inMidR.data(), n, inA, inB);
        }

        // El sidechain solo alimenta detectores de nivel: alcanza con repetir muestras.
        const double* sl = nullptr;
        const double* sr = nullptr;
        if (scL != nullptr)
        {
            for (int i = 0; i < nu; ++i)
                scUpL[(size_t) i] = scL[i / factor];
            sl = scUpL.data();
            if (scR != nullptr)
            {
                for (int i = 0; i < nu; ++i)
                    scUpR[(size_t) i] = scR[i / factor];
                sr = scUpR.data();
            }
        }

        core.process(bufL.data(), stereo ? bufR.data() : nullptr, inUpL.data(), stereo ? inUpR.data() : nullptr, sl, sr, nu);

        down(0, bufL.data(), midL.data(), L, n);
        if (stereo)
            down(1, bufR.data(), midR.data(), R, n);
    }

    IirCore core;

private:
    void up(int ch, const double* in, double* out, double* mid, int n, HalfbandStage* a, HalfbandStage* b)
    {
        if (factor == 2)
        {
            a[ch].upsample(in, out, n);
            return;
        }
        a[ch].upsample(in, mid, n);
        b[ch].upsample(mid, out, 2 * n);
    }

    void down(int ch, const double* in, double* mid, double* out, int n)
    {
        if (factor == 2)
        {
            stageA[ch].downsample(in, out, n);
            return;
        }
        stageB[ch].downsample(in, mid, 2 * n);
        stageA[ch].downsample(mid, out, n);
    }

    double baseRate = 48000.0;
    int factor = 4;
    HalfbandStage stageA[2], stageB[2], inA[2], inB[2];
    std::vector<double> bufL, bufR, midL, midR, inUpL, inUpR, inMidL, inMidR, scUpL, scUpR;
};
} // namespace prisma
