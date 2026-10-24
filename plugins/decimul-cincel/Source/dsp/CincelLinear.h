#pragma once

// Modo de fase lineal: la curva de todas las bandas se convierte en un filtro FIR simétrico
// (no corre la fase de nada: un bombo sigue alineado con sus armónicos) y se aplica con una
// convolución por FFT particionada. Precio: latencia (media longitud del FIR + 512 muestras)
// y, en resoluciones bajas, menos precisión en los graves.
//
// El FIR se arma en otro hilo (cuesta varios ms en las resoluciones altas) y el hilo de audio lo
// toma al principio de un bloque, fundiendo del FIR viejo al nuevo en 512 muestras. Al
// exportar (render offline) se arma en el mismo hilo de audio, así el resultado es siempre igual.
//
// Mid/Side y bandas de un solo canal: todo es lineal, así que el EQ completo es una matriz 2x2
// de filtros. [L';R'] = Tinv · diag(HM, HS) · T · diag(HL, HR) · [L;R], con T el pasaje a M/S.

#include "CincelFft.h"
#include "CincelFilters.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace cincel
{
constexpr int lpPartition = 512;
constexpr int numLpResolutions = 5;   // Low, Medium, High, Very High, Maximum

/** Escala por frecuencia de muestreo (1, 2 o 4) para mantener la resolución en Hz. */
inline int lpRateScale(double fs) { return fs <= 50000.0 ? 1 : (fs <= 100000.0 ? 2 : 4); }

/** Largo del FIR para una resolución: 2048 ... 32768 muestras a 44.1/48 kHz. */
inline int lpTaps(int resolution, double fs) { return (2048 << std::clamp(resolution, 0, numLpResolutions - 1)) * lpRateScale(fs); }

inline int lpLatency(int resolution, double fs) { return lpPartition + lpTaps(resolution, fs) / 2; }

/** Lo que el armador del FIR necesita saber (copia que el hilo de audio publica). */
struct LpSnapshot
{
    std::array<BandParams, maxBands> bands {};
    std::array<float, maxBands> dynDb {};
    bool gainQ = false;
    bool stereo = true;
    int resolution = 2;
    uint32_t version = 0;
};

/** El FIR (las 1, 2 o 4 entradas de la matriz) ya partido y transformado. */
struct KernelSet
{
    int taps = 0, parts = 0;
    bool cross = false;          // hay Mid/Side: se usan las cuatro entradas
    bool stereo = true;
    uint32_t version = 0;
    std::vector<std::complex<float>> h[4];   // 11, 12, 21, 22 — parts * (P + 1)

    void allocate(int maxParts)
    {
        for (auto& v : h)
            v.assign((size_t) maxParts * (lpPartition + 1), {});
    }
};

/** Arma los FIR a partir de las bandas. Tiene su propia memoria: un objeto por hilo. */
class LinearPhaseDesigner
{
public:
    static constexpr int gridPoints = 2048;

    void prepare(double sampleRate)
    {
        fs = sampleRate;
        const int maxTaps = lpTaps(numLpResolutions - 1, fs);
        for (int r = 0; r < numLpResolutions; ++r)
            designFft[(size_t) r].init(2 * lpTaps(r, fs));
        partFft.init(2 * lpPartition);
        spectrum.assign((size_t) maxTaps + 1, {});
        impulse.assign((size_t) (2 * maxTaps), 0.0);
        taps.assign((size_t) maxTaps, 0.0);
        block.assign((size_t) (2 * lpPartition), 0.0);
        blockSpec.assign((size_t) (lpPartition + 1), {});
        spectrumReal.assign((size_t) maxTaps + 1, 0.0);
        for (auto& c : curves)
            c.assign((size_t) maxTaps + 1, 0.0);

        gridLo = 2.0;
        gridHi = 0.5 * fs;
        gridHz.assign((size_t) gridPoints, 0.0);
        for (auto& v : sums)
            v.assign((size_t) gridPoints, 0.0f);
        sumsTmp.assign((size_t) gridPoints, 0.0f);
        for (auto& c : cache)
            c.db.assign((size_t) gridPoints, 0.0f);
        for (int g = 0; g < gridPoints; ++g)
            gridHz[(size_t) g] = gridLo * std::pow(gridHi / gridLo, g / (double) (gridPoints - 1));
        for (auto& c : cache)
            c.valid = false;
    }

    /** Arma el juego de FIR para `snap` en `out` (que ya tiene la memoria reservada). */
    void build(const LpSnapshot& snap, KernelSet& out)
    {
        const int r = std::clamp(snap.resolution, 0, numLpResolutions - 1);
        const int n = lpTaps(r, fs);
        const int D = 2 * n;
        const int bins = n + 1;

        // Curvas en dB sobre la grilla logarítmica, por componente.
        enum { cStereo, cLeft, cRight, cMid, cSide, numComp };
        auto& sum = sums;
        for (auto& s : sum)
            std::fill(s.begin(), s.end(), 0.0f);
        bool any[numComp] = {};

        for (int i = 0; i < maxBands; ++i)
        {
            BandParams b = snap.bands[(size_t) i];
            if (!isActive(b))
                continue;
            double gain = b.gainDb;
            if (usesDynamics(b) && !b.spectral)
                gain += snap.dynDb[(size_t) i];
            const double q = effectiveQ(b.shape, b.q, gain, snap.gainQ);
            auto& c = cache[(size_t) i];
            if (!c.valid || c.shape != b.shape || c.slope != b.slope || c.freq != b.freq || c.gain != gain || c.q != q)
            {
                const auto a = designBand(b.shape, b.slope, b.freq, gain, q, true);
                for (int g = 0; g < gridPoints; ++g)
                    c.db[(size_t) g] = (float) std::max(-240.0, 20.0 * std::log10(std::max(1.0e-12, a.magnitude(gridHz[(size_t) g]))));
                c.valid = true;
                c.shape = b.shape;
                c.slope = b.slope;
                c.freq = b.freq;
                c.gain = gain;
                c.q = q;
            }
            int comp = cStereo;
            switch (b.place)
            {
                case Placement::Stereo: comp = cStereo; break;
                case Placement::Left: comp = snap.stereo ? cLeft : cStereo; break;
                case Placement::Right: comp = snap.stereo ? cRight : cStereo; break;
                case Placement::Mid: comp = snap.stereo ? cMid : cStereo; break;
                case Placement::Side: comp = snap.stereo ? cSide : -1; break;
            }
            if (comp < 0)
                continue;
            any[comp] = true;
            auto& s = sum[(size_t) comp];
            for (int g = 0; g < gridPoints; ++g)
                s[(size_t) g] += c.db[(size_t) g];
        }

        // Pasa cada componente a la grilla lineal de la FFT (magnitud lineal).
        const double logSpan = std::log(gridHi / gridLo);
        auto toBins = [&](const std::vector<float>& s, std::vector<double>& dst)
        {
            for (int k = 0; k < bins; ++k)
            {
                const double hz = std::max(gridLo, k * fs / D);
                const double pos = std::log(hz / gridLo) / logSpan * (gridPoints - 1);
                const int i0 = std::clamp((int) pos, 0, gridPoints - 2);
                const double fr = std::clamp(pos - i0, 0.0, 1.0);
                const double dbv = s[(size_t) i0] + fr * (s[(size_t) i0 + 1] - s[(size_t) i0]);
                dst[(size_t) k] = std::pow(10.0, dbv / 20.0);
            }
        };

        auto& HL = curves[0];
        auto& HR = curves[1];
        auto& HM = curves[2];
        auto& HS = curves[3];
        {
            auto& lsum = sumsTmp;
            for (int g = 0; g < gridPoints; ++g)
                lsum[(size_t) g] = sum[cStereo][(size_t) g] + sum[cLeft][(size_t) g];
            toBins(lsum, HL);
            for (int g = 0; g < gridPoints; ++g)
                lsum[(size_t) g] = sum[cStereo][(size_t) g] + sum[cRight][(size_t) g];
            toBins(lsum, HR);
        }
        const bool cross = snap.stereo && (any[cMid] || any[cSide]);
        if (cross)
        {
            toBins(sum[cMid], HM);
            toBins(sum[cSide], HS);
        }

        out.taps = n;
        out.parts = n / lpPartition;
        out.cross = cross;
        out.stereo = snap.stereo;
        out.version = snap.version;

        auto& E = spectrumReal;
        E.resize((size_t) bins);
        if (!cross)
        {
            makeKernel(HL, r, out.h[0], n, D);
            if (snap.stereo)
                makeKernel(HR, r, out.h[3], n, D);
            return;
        }
        for (int k = 0; k < bins; ++k) E[(size_t) k] = 0.5 * (HM[(size_t) k] + HS[(size_t) k]) * HL[(size_t) k];
        makeKernel(E, r, out.h[0], n, D);
        for (int k = 0; k < bins; ++k) E[(size_t) k] = 0.5 * (HM[(size_t) k] - HS[(size_t) k]) * HR[(size_t) k];
        makeKernel(E, r, out.h[1], n, D);
        for (int k = 0; k < bins; ++k) E[(size_t) k] = 0.5 * (HM[(size_t) k] - HS[(size_t) k]) * HL[(size_t) k];
        makeKernel(E, r, out.h[2], n, D);
        for (int k = 0; k < bins; ++k) E[(size_t) k] = 0.5 * (HM[(size_t) k] + HS[(size_t) k]) * HR[(size_t) k];
        makeKernel(E, r, out.h[3], n, D);
    }

private:
    /** Respuesta real (fase cero) → FIR centrado de n coeficientes con ventana Kaiser → particiones. */
    void makeKernel(const std::vector<double>& E, int res, std::vector<std::complex<float>>& dst, int n, int D)
    {
        const int bins = n + 1;
        for (int k = 0; k < bins; ++k)
            spectrum[(size_t) k] = { E[(size_t) k], 0.0 };
        designFft[(size_t) res].inverse(spectrum.data(), impulse.data());

        const double beta = 8.0;
        const double i0b = bessel0(beta);
        for (int m = 0; m < n; ++m)
        {
            const int idx = ((m - n / 2) % D + D) % D;
            const double r = (m - n / 2) / (double) (n / 2);
            const double win = std::abs(r) >= 1.0 ? 0.0 : bessel0(beta * std::sqrt(1.0 - r * r)) / i0b;
            taps[(size_t) m] = impulse[(size_t) idx] * win;
        }

        const int parts = n / lpPartition;
        for (int p = 0; p < parts; ++p)
        {
            std::fill(block.begin(), block.end(), 0.0);
            std::copy(taps.begin() + p * lpPartition, taps.begin() + (p + 1) * lpPartition, block.begin());
            partFft.forward(block.data(), blockSpec.data());
            auto* d = dst.data() + (size_t) p * (lpPartition + 1);
            for (int k = 0; k <= lpPartition; ++k)
                d[k] = { (float) blockSpec[(size_t) k].real(), (float) blockSpec[(size_t) k].imag() };
        }
    }

    static double bessel0(double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 60; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1.0e-14 * sum)
                break;
        }
        return sum;
    }

    struct CacheEntry
    {
        bool valid = false;
        Shape shape = Shape::Bell;
        int slope = 0;
        float freq = 0.0f;
        double gain = 0.0, q = 0.0;
        std::vector<float> db;
    };

    double fs = 48000.0, gridLo = 2.0, gridHi = 24000.0;
    std::vector<double> gridHz;
    std::array<CacheEntry, maxBands> cache {};
    std::array<std::vector<float>, 5> sums;
    std::vector<float> sumsTmp;
    std::array<RealFft<double>, numLpResolutions> designFft;
    RealFft<double> partFft;
    std::vector<std::complex<double>> spectrum, blockSpec;
    std::vector<double> impulse, taps, block, spectrumReal;
    std::array<std::vector<double>, 4> curves;
};

/** Tres juegos de FIR que se pasan entre el hilo que los arma y el de audio, sin trabas. */
class KernelExchange
{
public:
    void allocate(int maxParts)
    {
        for (auto& s : slots)
            s.allocate(maxParts);
        ready.store(-1);
        inUseA.store(-1);
        inUseB.store(-1);
    }

    /** Lado del armador: un lugar libre (o -1 si por ahora no hay). */
    int acquireWriteSlot() const
    {
        const int a = inUseA.load(), b = inUseB.load(), r = ready.load();
        for (int i = 0; i < 3; ++i)
            if (i != a && i != b && i != r)
                return i;
        return -1;
    }

    void publish(int slot) { ready.store(slot); }

    /** Lado del audio: toma el último FIR publicado (o -1). */
    int takeReady() { return ready.exchange(-1); }

    KernelSet slots[3];
    std::atomic<int> ready { -1 }, inUseA { -1 }, inUseB { -1 };
};

/** Convolución particionada uniforme (overlap-save) con la matriz 2x2 y fundido entre FIR. */
class LinearPhaseConvolver
{
public:
    static constexpr int P = lpPartition;

    void prepare(int maxParts)
    {
        maxP = maxParts;
        fft.init(2 * P);
        for (int ch = 0; ch < 2; ++ch)
        {
            fdl[ch].assign((size_t) maxParts * (P + 1), {});
            inBuf[ch].assign((size_t) (2 * P), 0.0);
            outBuf[ch].assign((size_t) P, 0.0);
        }
        time.assign((size_t) (2 * P), 0.0);
        spec.assign((size_t) (P + 1), {});
        for (auto& a : acc)
            a.assign((size_t) (P + 1), {});
        yOld[0].assign((size_t) P, 0.0);
        yOld[1].assign((size_t) P, 0.0);
        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill(fdl[ch].begin(), fdl[ch].end(), std::complex<float>());
            std::fill(inBuf[ch].begin(), inBuf[ch].end(), 0.0);
            std::fill(outBuf[ch].begin(), outBuf[ch].end(), 0.0);
        }
        head = 0;
        fill = 0;
    }

    /** Largo de FIR que se espera (los armados para otra resolución se ignoran). */
    int expectedTaps = 0;

    /** Procesa en el lugar. `kx` trae los FIR; `active` es el índice del juego en uso (lo
        actualiza al tomar uno nuevo). */
    void process(double* L, double* R, int n, KernelExchange& kx, int& active)
    {
        for (int i = 0; i < n; ++i)
        {
            inBuf[0][(size_t) (P + fill)] = L[i];
            if (R != nullptr)
                inBuf[1][(size_t) (P + fill)] = R[i];
            L[i] = outBuf[0][(size_t) fill];
            if (R != nullptr)
                R[i] = outBuf[1][(size_t) fill];
            if (++fill == P)
            {
                fill = 0;
                runBlock(R != nullptr, kx, active);
            }
        }
    }

private:
    void runBlock(bool stereo, KernelExchange& kx, int& active)
    {
        // Entrada: [bloque anterior, bloque nuevo] → espectro → línea de retardo en frecuencia.
        head = (head + maxP - 1) % maxP;
        for (int ch = 0; ch < (stereo ? 2 : 1); ++ch)
        {
            fft.forward(inBuf[ch].data(), spec.data());
            auto* d = fdl[ch].data() + (size_t) head * (P + 1);
            for (int k = 0; k <= P; ++k)
                d[k] = { (float) spec[(size_t) k].real(), (float) spec[(size_t) k].imag() };
            std::copy(inBuf[ch].begin() + P, inBuf[ch].end(), inBuf[ch].begin());
        }

        int fresh = kx.takeReady();
        if (fresh >= 0 && kx.slots[fresh].taps != expectedTaps)
            fresh = -1;   // armado para otra resolución: se descarta
        if (fresh >= 0)
            kx.inUseB.store(fresh);

        if (active < 0)
        {
            if (fresh < 0)
            {
                for (int ch = 0; ch < 2; ++ch)
                    std::fill(outBuf[ch].begin(), outBuf[ch].end(), 0.0);
                return;
            }
            active = fresh;
            kx.inUseA.store(active);
            kx.inUseB.store(-1);
            convolve(kx.slots[active], stereo, outBuf);
            return;
        }

        convolve(kx.slots[active], stereo, outBuf);
        if (fresh >= 0)
        {
            for (int ch = 0; ch < 2; ++ch)
                std::copy(outBuf[ch].begin(), outBuf[ch].end(), yOld[ch].begin());
            convolve(kx.slots[fresh], stereo, outBuf);
            for (int ch = 0; ch < 2; ++ch)
                for (int k = 0; k < P; ++k)
                {
                    const double w = (k + 1) / (double) P;
                    outBuf[ch][(size_t) k] = yOld[ch][(size_t) k] + w * (outBuf[ch][(size_t) k] - yOld[ch][(size_t) k]);
                }
            active = fresh;
            kx.inUseA.store(active);
            kx.inUseB.store(-1);
        }
    }

    void convolve(const KernelSet& ks, bool stereo, std::vector<double>* out)
    {
        const int parts = std::min(ks.parts, maxP);
        const int outs = stereo ? 2 : 1;
        for (int o = 0; o < outs; ++o)
        {
            auto& a = acc[(size_t) o];
            std::fill(a.begin(), a.end(), std::complex<float>());
            for (int in = 0; in < (stereo ? 2 : 1); ++in)
            {
                int entry;
                if (!stereo)
                    entry = 0;
                else if (o == in)
                    entry = o == 0 ? 0 : 3;
                else
                {
                    if (!ks.cross)
                        continue;
                    entry = o == 0 ? 1 : 2;
                }
                const auto& h = ks.h[entry];
                for (int p = 0; p < parts; ++p)
                {
                    const auto* x = fdl[in].data() + (size_t) ((head + p) % maxP) * (P + 1);
                    const auto* hp = h.data() + (size_t) p * (P + 1);
                    const float* xf = reinterpret_cast<const float*>(x);
                    const float* hf = reinterpret_cast<const float*>(hp);
                    float* af = reinterpret_cast<float*>(a.data());
                    for (int k = 0; k <= P; ++k)
                    {
                        const float xr = xf[2 * k], xi = xf[2 * k + 1];
                        const float hr = hf[2 * k], hi = hf[2 * k + 1];
                        af[2 * k] += xr * hr - xi * hi;
                        af[2 * k + 1] += xr * hi + xi * hr;
                    }
                }
            }
            for (int k = 0; k <= P; ++k)
                spec[(size_t) k] = { a[(size_t) k].real(), a[(size_t) k].imag() };
            fft.inverse(spec.data(), time.data());
            std::copy(time.begin() + P, time.end(), out[o].begin());
        }
    }

    int maxP = 1, head = 0, fill = 0;
    RealFft<double> fft;
    std::vector<std::complex<float>> fdl[2];
    std::vector<std::complex<float>> acc[2];
    std::vector<double> inBuf[2], outBuf[2], yOld[2], time;
    std::vector<std::complex<double>> spec;
};

/** Hilo que arma los FIR cuando cambian las bandas o la dinámica. */
class LinearPhaseWorker
{
public:
    ~LinearPhaseWorker() { stop(); }

    void start(double sampleRate, KernelExchange* exchange)
    {
        stop();
        designer.prepare(sampleRate);
        kx = exchange;
        running.store(true);
        thread = std::thread([this] { loop(); });
    }

    void stop()
    {
        running.store(false);
        if (thread.joinable())
            thread.join();
    }

    /** Hilo de audio: publica lo pedido (seqlock: no traba nunca al audio). */
    void post(const LpSnapshot& s)
    {
        seq.fetch_add(1, std::memory_order_acq_rel);
        std::atomic_thread_fence(std::memory_order_release);
        mailbox = s;
        std::atomic_thread_fence(std::memory_order_release);
        seq.fetch_add(1, std::memory_order_acq_rel);
    }

    LinearPhaseDesigner designer;

private:
    bool read(LpSnapshot& out)
    {
        for (int tries = 0; tries < 8; ++tries)
        {
            const uint32_t s1 = seq.load(std::memory_order_acquire);
            if (s1 & 1u)
                continue;
            std::atomic_thread_fence(std::memory_order_acquire);
            out = mailbox;
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq.load(std::memory_order_acquire) == s1)
                return true;
        }
        return false;
    }

    static bool sameExceptDyn(const LpSnapshot& a, const LpSnapshot& b)
    {
        return a.version == b.version && a.resolution == b.resolution && a.stereo == b.stereo && a.gainQ == b.gainQ;
    }

    static float dynDistance(const LpSnapshot& a, const LpSnapshot& b)
    {
        float d = 0.0f;
        for (int i = 0; i < maxBands; ++i)
            d = std::max(d, std::abs(a.dynDb[(size_t) i] - b.dynDb[(size_t) i]));
        return d;
    }

    void loop()
    {
        LpSnapshot last, now;
        bool haveLast = false;
        while (running.load())
        {
            if (!paused.load() && read(now) && (!haveLast || !sameExceptDyn(now, last) || dynDistance(now, last) > 0.1f))
            {
                busy.store(true);
                if (!paused.load())
                {
                    const int slot = kx->acquireWriteSlot();
                    if (slot >= 0)
                    {
                        designer.build(now, kx->slots[slot]);
                        kx->publish(slot);
                        last = now;
                        haveLast = true;
                        busy.store(false);
                        continue;
                    }
                }
                busy.store(false);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

public:
    /** Al exportar (offline) el FIR se arma en el hilo de audio: el hilo se queda quieto. */
    std::atomic<bool> paused { false }, busy { false };

private:

    KernelExchange* kx = nullptr;
    std::atomic<bool> running { false };
    std::thread thread;
    std::atomic<uint32_t> seq { 0 };
    LpSnapshot mailbox;
};
} // namespace cincel
