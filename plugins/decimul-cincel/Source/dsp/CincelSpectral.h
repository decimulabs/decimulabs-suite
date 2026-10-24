#pragma once

// Dinámica espectral: en vez de mover la ganancia de toda la banda, la banda se divide en los
// bins de una FFT y cada uno se comprime (o expande) por su cuenta. Así, una resonancia que
// aparece y desaparece se baja sola, sin tocar lo que la rodea. Se apoya en la forma de la banda
// para saber cuánto puede actuar en cada frecuencia (en el centro de un bell, todo el rango; en
// los costados, menos).
//
// Trabaja con STFT (ventana Hann, solapamiento de 75 %). Latencia: el tamaño de la FFT (2048
// muestras a 44.1/48 kHz). Solo se activa (y suma esa latencia) si alguna banda la usa.

#include "CincelFft.h"
#include "CincelIir.h"
#include <vector>

namespace cincel
{
inline bool usesSpectral(const BandParams& b) { return usesDynamics(b) && b.spectral; }

class SpectralDynamics
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        N = 2048 * (fs <= 50000.0 ? 1 : (fs <= 100000.0 ? 2 : 4));
        hop = N / 4;
        bins = N / 2 + 1;
        fft.init(N);
        win.resize((size_t) N);
        for (int i = 0; i < N; ++i)
            win[(size_t) i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / N);
        for (int ch = 0; ch < 4; ++ch)
        {
            inFifo[ch].assign((size_t) N, 0.0);
            spec[ch].assign((size_t) bins, {});
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            outFifo[ch].assign((size_t) N, 0.0);
            accum[ch].assign((size_t) (2 * N), 0.0);
        }
        frame.assign((size_t) N, 0.0);
        for (auto& g : gains)
            g.assign((size_t) bins, 1.0);
        for (auto& b : state)
        {
            b.env.assign((size_t) bins, -120.0f);
            b.weight.assign((size_t) bins, 0.0f);
            b.valid = false;
        }
        reset();
    }

    void reset()
    {
        for (auto& v : inFifo)
            std::fill(v.begin(), v.end(), 0.0);
        for (auto& v : outFifo)
            std::fill(v.begin(), v.end(), 0.0);
        for (auto& v : accum)
            std::fill(v.begin(), v.end(), 0.0);
        for (auto& b : state)
            std::fill(b.env.begin(), b.env.end(), -120.0f);
        rover = N - hop;
    }

    /** Una muestra entra en la posición N - hop + j del cuadro y sale 4 cuadros después: N. */
    int latency() const { return N; }

    /** Procesa en el lugar. `bands`: parámetros efectivos (con la escala de ganancia). */
    void process(double* L, double* R, const double* scL, const double* scR, int n, const BandParams* bands,
                 const DynamicsSettings& dyn)
    {
        const int lat = N - hop;
        const bool stereo = R != nullptr;
        const bool haveSc = scL != nullptr;
        for (int i = 0; i < n; ++i)
        {
            inFifo[0][(size_t) rover] = L[i];
            if (stereo)
                inFifo[1][(size_t) rover] = R[i];
            if (haveSc)
            {
                inFifo[2][(size_t) rover] = scL[i];
                inFifo[3][(size_t) rover] = scR != nullptr ? scR[i] : scL[i];
            }
            L[i] = outFifo[0][(size_t) (rover - lat)];
            if (stereo)
                R[i] = outFifo[1][(size_t) (rover - lat)];
            if (++rover >= N)
            {
                rover = lat;
                runFrame(stereo, haveSc, bands, dyn);
            }
        }
    }

    DynamicsTelemetry* telemetry = nullptr;

private:
    struct BandState
    {
        std::vector<float> env, weight;
        bool valid = false;
        Shape shape = Shape::Bell;
        float freq = 0.0f, q = 0.0f;
        int slope = 0;
        int kLo = 0, kHi = 0;
        double avgDb = -60.0;
    };

    void updateWeights(BandState& s, const BandParams& b)
    {
        if (s.valid && s.shape == b.shape && s.freq == b.freq && s.q == b.q && s.slope == b.slope)
            return;
        s.valid = true;
        s.shape = b.shape;
        s.freq = b.freq;
        s.q = b.q;
        s.slope = b.slope;
        // Forma de la banda con +12 dB, normalizada a 0..1: cuánto puede actuar en cada bin.
        const auto a = designBand(b.shape, b.slope, b.freq, 12.0, b.q, true);
        double peak = 1.0e-9;
        for (int k = 0; k < bins; ++k)
        {
            const double v = std::abs(20.0 * std::log10(std::max(1.0e-9, a.magnitude(std::max(1.0, k * fs / N)))));
            s.weight[(size_t) k] = (float) v;
            peak = std::max(peak, v);
        }
        s.kLo = bins;
        s.kHi = 0;
        for (int k = 0; k < bins; ++k)
        {
            auto& w = s.weight[(size_t) k];
            w = (float) std::clamp(w / peak, 0.0, 1.0);
            if (w > 0.02f)
            {
                s.kLo = std::min(s.kLo, k);
                s.kHi = std::max(s.kHi, k);
            }
        }
    }

    void runFrame(bool stereo, bool haveSc, const BandParams* bands, const DynamicsSettings& dyn)
    {
        const int chans = stereo ? 2 : 1;
        for (int ch = 0; ch < chans; ++ch)
            analyse(ch);
        bool scDone = false;

        for (auto& g : gains)
            std::fill(g.begin(), g.end(), 1.0);

        const double frameSec = hop / fs;
        const double aA = 1.0 - std::exp(-frameSec / (std::max(0.05, dyn.attackMs) * 0.001));
        const double aR = 1.0 - std::exp(-frameSec / (std::max(1.0, dyn.releaseMs) * 0.001));
        const double toAmp = 2.0 / (0.5 * N);   // una senoidal de amplitud A da A en su bin

        bool usedComp[5] = {};
        for (int i = 0; i < maxBands; ++i)
        {
            const auto& b = bands[i];
            if (!usesSpectral(b))
                continue;
            auto& s = state[(size_t) i];
            updateWeights(s, b);
            if (s.kHi < s.kLo)
                continue;

            const bool useSc = b.sidechain && haveSc;
            if (useSc && !scDone)
            {
                analyse(2);
                analyse(3);
                scDone = true;
            }
            const auto& XL = spec[useSc ? 2 : 0];
            const auto& XR = spec[useSc ? 3 : (stereo ? 1 : 0)];

            int comp = (int) b.place;
            if (!stereo)
                comp = b.place == Placement::Side ? -1 : 0;
            if (comp < 0)
                continue;
            usedComp[comp] = true;
            auto& G = gains[(size_t) comp];

            double sumW = 0.0, sumOff = 0.0, sumLev = 0.0;
            const double thrBase = b.thresholdDb;
            for (int k = s.kLo; k <= s.kHi; ++k)
            {
                double mag;
                switch (b.place)
                {
                    case Placement::Stereo: mag = std::max(std::abs(XL[(size_t) k]), std::abs(XR[(size_t) k])); break;
                    case Placement::Left: mag = std::abs(XL[(size_t) k]); break;
                    case Placement::Right: mag = std::abs(XR[(size_t) k]); break;
                    case Placement::Mid: mag = 0.5 * std::abs(XL[(size_t) k] + XR[(size_t) k]); break;
                    default: mag = 0.5 * std::abs(XL[(size_t) k] - XR[(size_t) k]); break;
                }
                if (!stereo)
                    mag = std::abs(XL[(size_t) k]);
                const double lev = 20.0 * std::log10(mag * toAmp + 1.0e-12);
                auto& e = s.env[(size_t) k];
                e += (float) ((lev > e ? aA : aR) * (lev - e));
                const double w = s.weight[(size_t) k];
                const double thr = b.autoThreshold ? s.avgDb + 4.0 : thrBase;
                const double off = dynamicOffsetDb(e, thr, b.rangeDb) * w;
                G[(size_t) k] *= std::pow(10.0, off / 20.0);
                sumW += w;
                sumOff += off * w;
                sumLev += e * w;
            }
            if (sumW > 0.0)
            {
                const double meanLev = sumLev / sumW;
                if (meanLev > -90.0)
                    s.avgDb += (meanLev - s.avgDb) * (1.0 - std::exp(-frameSec / 2.0));
                if (telemetry != nullptr)
                {
                    telemetry->offsetDb[(size_t) i].store((float) (sumOff / sumW), std::memory_order_relaxed);
                    telemetry->levelDb[(size_t) i].store((float) meanLev, std::memory_order_relaxed);
                    telemetry->thresholdDb[(size_t) i].store((float) (b.autoThreshold ? s.avgDb + 4.0 : thrBase),
                                                            std::memory_order_relaxed);
                }
            }
        }

        // Aplica: primero estéreo / izquierda / derecha, después Mid/Side.
        auto& XL = spec[0];
        auto& XR = spec[stereo ? 1 : 0];
        for (int k = 0; k < bins; ++k)
        {
            if (usedComp[0])
            {
                XL[(size_t) k] *= gains[0][(size_t) k];
                if (stereo)
                    XR[(size_t) k] *= gains[0][(size_t) k];
            }
            if (stereo)
            {
                if (usedComp[1])
                    XL[(size_t) k] *= gains[1][(size_t) k];
                if (usedComp[2])
                    XR[(size_t) k] *= gains[2][(size_t) k];
                if (usedComp[3] || usedComp[4])
                {
                    auto m = 0.5 * (XL[(size_t) k] + XR[(size_t) k]);
                    auto sd = 0.5 * (XL[(size_t) k] - XR[(size_t) k]);
                    m *= gains[3][(size_t) k];
                    sd *= gains[4][(size_t) k];
                    XL[(size_t) k] = m + sd;
                    XR[(size_t) k] = m - sd;
                }
            }
        }

        for (int ch = 0; ch < chans; ++ch)
            synthesise(ch);

        // Si este cuadro no miró el sidechain, igual hay que correr su ventana.
        if (haveSc && !scDone)
            for (int ch = 2; ch < 4; ++ch)
                std::copy(inFifo[ch].begin() + hop, inFifo[ch].end(), inFifo[ch].begin());
    }

    void analyse(int ch)
    {
        for (int i = 0; i < N; ++i)
            frame[(size_t) i] = inFifo[ch][(size_t) i] * win[(size_t) i];
        fft.forward(frame.data(), spec[ch].data());
        std::copy(inFifo[ch].begin() + hop, inFifo[ch].end(), inFifo[ch].begin());
    }

    void synthesise(int ch)
    {
        fft.inverse(spec[ch].data(), frame.data());
        auto& acc = accum[ch];
        const double norm = 1.0 / 1.5;   // suma de Hann² con 75 % de solapamiento
        for (int i = 0; i < N; ++i)
            acc[(size_t) i] += frame[(size_t) i] * win[(size_t) i] * norm;
        std::copy(acc.begin(), acc.begin() + hop, outFifo[ch].begin());
        std::copy(acc.begin() + hop, acc.end(), acc.begin());
        std::fill(acc.end() - hop, acc.end(), 0.0);
    }

    double fs = 48000.0;
    int N = 2048, hop = 512, bins = 1025, rover = 0;
    RealFft<double> fft;
    std::vector<double> win, frame;
    std::vector<double> inFifo[4], outFifo[2], accum[2];
    std::vector<std::complex<double>> spec[4];
    std::array<std::vector<double>, 5> gains;
    std::array<BandState, maxBands> state;
};
} // namespace cincel
