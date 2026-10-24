#pragma once

// Motor IIR de Cincel: las 24 bandas como cascadas de biquads (magnitud igualada a la analógica).
// Lo usan el modo "latencia cero" (a la frecuencia del proyecto) y el modo "fase natural"
// (el mismo motor, sobremuestreado: ver CincelOversampler.h).
//
// Cómo trabaja, por tramos de 32 muestras:
//   1. Cada banda suaviza frecuencia (en escala logarítmica), ganancia y Q hacia lo pedido, y
//      si tiene dinámica mide su zona (filtro de detección + envolvente) y calcula cuánto mover
//      la ganancia. Si algo cambió, recalcula sus biquads.
//   2. El audio pasa banda por banda, en orden: primero las de estéreo / izquierda / derecha y
//      después, en Mid/Side, las de centro y lados.
// Un cambio de forma, pendiente o ubicación, o prender/apagar la banda, no se hace de golpe: la
// banda se funde a "sin efecto" en 10 ms, se rearma y vuelve (sin clics).

#include "CincelFilters.h"
#include <atomic>

namespace cincel
{
/** Ajustes de la dinámica compartidos por todas las bandas. */
struct DynamicsSettings
{
    double attackMs = 10.0;
    double releaseMs = 120.0;
};

/** Ley del EQ dinámico: rodilla suave de 6 dB y 2:1 hasta llegar al rango. Devuelve cuánto se
    mueve la ganancia (dB, con el signo del rango). */
inline double dynamicOffsetDb(double levelDb, double thresholdDb, double rangeDb)
{
    const double over = levelDb - thresholdDb;
    double y;
    if (over <= -3.0)
        y = 0.0;
    else if (over >= 3.0)
        y = over;
    else
        y = (over + 3.0) * (over + 3.0) / 12.0;
    const double amount = std::min(0.5 * y, std::abs(rangeDb));
    return rangeDb < 0.0 ? -amount : amount;
}

/** Detector de una banda dinámica: filtra la zona de la banda y sigue su nivel. */
class BandDetector
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        reset();
    }

    void reset()
    {
        for (auto& ch : st)
            for (auto& s : ch)
                s = {};
        env = 0.0;
        avgDb = -60.0;
        levelDb = -120.0;
        designedLf = designedLq = 1.0e9;
    }

    /** Rehace el filtro si la frecuencia o la Q se movieron. */
    void design(Shape shape, double logF, double logQ)
    {
        if (std::abs(logF - designedLf) < 1.0e-3 && std::abs(logQ - designedLq) < 1.0e-3 && shape == designedShape)
            return;
        designedLf = logF;
        designedLq = logQ;
        designedShape = shape;
        filt = toDigital(designAudition(shape, std::exp(logF), std::exp(logQ), true), fs);
    }

    /** Corre `len` muestras. `a`, `b`: las dos señales a vigilar (b puede ser nula). */
    void run(const double* a, const double* b, int len, double attackCoef, double releaseCoef)
    {
        for (int k = 0; k < len; ++k)
        {
            double ya = a[k], yb = b != nullptr ? b[k] : 0.0;
            for (int s = 0; s < filt.count; ++s)
            {
                ya = tick(filt.s[(size_t) s], st[0][(size_t) s], ya);
                if (b != nullptr)
                    yb = tick(filt.s[(size_t) s], st[1][(size_t) s], yb);
            }
            const double p = std::max(ya * ya, yb * yb);
            env += (p > env ? attackCoef : releaseCoef) * (p - env);
        }
        if (!(env > 1.0e-30))
            env = 0.0;
        levelDb = 10.0 * std::log10(2.0 * env + 1.0e-24);
    }

    /** Promedio lento del nivel, para el umbral automático. */
    void trackAverage(int len)
    {
        if (levelDb > -90.0)
            avgDb += (levelDb - avgDb) * (1.0 - std::exp(-len / (2.0 * fs)));
    }

    double levelDb = -120.0;
    double avgDb = -60.0;

private:
    double fs = 48000.0;
    DigitalDesign filt;
    std::array<std::array<BiquadState, maxSections>, 2> st {};
    double env = 0.0;
    double designedLf = 1.0e9, designedLq = 1.0e9;
    Shape designedShape = Shape::Bell;
};

/** Medidas de la dinámica para la pantalla (se leen desde otro hilo). */
struct DynamicsTelemetry
{
    std::array<std::atomic<float>, maxBands> offsetDb {};
    std::array<std::atomic<float>, maxBands> levelDb {};
    std::array<std::atomic<float>, maxBands> thresholdDb {};
};

/** Calcula la dinámica de todas las bandas (sin tocar el audio): la usan el motor IIR y el modo
    de fase lineal. Las bandas con dinámica espectral no pasan por acá. */
class DynamicsBank
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        for (auto& d : det)
            d.prepare(fs);
        offsets.fill(0.0);
    }

    void reset()
    {
        for (auto& d : det)
            d.reset();
        offsets.fill(0.0);
    }

    /** `bands`: parámetros efectivos. `inL/inR`: la entrada (antes del EQ). `scL/scR`: el
        sidechain (nulo si no hay). `logF/logQ`: frecuencia y Q suavizadas de cada banda. */
    void process(const BandParams* bands, const double* logF, const double* logQ, const DynamicsSettings& dyn,
                 const double* inL, const double* inR, const double* scL, const double* scR, int len,
                 double* scratchA, double* scratchB)
    {
        const double aA = 1.0 - std::exp(-1.0 / (std::max(0.05, dyn.attackMs) * 0.001 * fs));
        const double aR = 1.0 - std::exp(-1.0 / (std::max(1.0, dyn.releaseMs) * 0.001 * fs));
        for (int i = 0; i < maxBands; ++i)
        {
            const auto& b = bands[i];
            if (!usesDynamics(b) || b.spectral)
            {
                offsets[(size_t) i] = 0.0;
                if (telemetry != nullptr && !b.spectral)
                    telemetry->offsetDb[(size_t) i].store(0.0f, std::memory_order_relaxed);
                continue;
            }
            auto& d = det[(size_t) i];
            d.design(b.shape, logF[i], logQ[i]);

            const bool useSc = b.sidechain && scL != nullptr;
            const double* srcL = useSc ? scL : inL;
            const double* srcR = useSc ? (scR != nullptr ? scR : scL) : inR;
            const double* a = srcL;
            const double* c = nullptr;
            if (srcR != nullptr)
            {
                switch (b.place)
                {
                    case Placement::Stereo: a = srcL; c = srcR; break;
                    case Placement::Left: a = srcL; break;
                    case Placement::Right: a = srcR; break;
                    case Placement::Mid:
                        for (int k = 0; k < len; ++k)
                            scratchA[k] = 0.5 * (srcL[k] + srcR[k]);
                        a = scratchA;
                        break;
                    case Placement::Side:
                        for (int k = 0; k < len; ++k)
                            scratchB[k] = 0.5 * (srcL[k] - srcR[k]);
                        a = scratchB;
                        break;
                }
            }
            d.run(a, c, len, aA, aR);
            d.trackAverage(len);
            const double thr = b.autoThreshold ? d.avgDb + 4.0 : b.thresholdDb;
            offsets[(size_t) i] = dynamicOffsetDb(d.levelDb, thr, b.rangeDb);
            if (telemetry != nullptr)
            {
                telemetry->offsetDb[(size_t) i].store((float) offsets[(size_t) i], std::memory_order_relaxed);
                telemetry->levelDb[(size_t) i].store((float) d.levelDb, std::memory_order_relaxed);
                telemetry->thresholdDb[(size_t) i].store((float) thr, std::memory_order_relaxed);
            }
        }
    }

    double offset(int band) const { return offsets[(size_t) band]; }

    DynamicsTelemetry* telemetry = nullptr;

private:
    double fs = 48000.0;
    std::array<BandDetector, maxBands> det {};
    std::array<double, maxBands> offsets {};
};

//==============================================================================
class IirCore
{
public:
    static constexpr int chunk = 32;

    void prepare(double sampleRate)
    {
        fs = sampleRate;
        dynamics.prepare(fs);
        reset();
    }

    void reset()
    {
        for (auto& b : bands)
        {
            for (auto& ch : b.st)
                for (auto& s : ch)
                    s = {};
            b.designedLf = b.designedGain = b.designedLq = b.designedDyn = 1.0e9;
            b.wet = b.wetTarget = b.live ? 1.0 : 0.0;
            b.switching = false;
        }
        dynamics.reset();
    }

    /** Lo pedido para cada banda (ya con la escala de ganancia aplicada). */
    void setParams(const BandParams* p, bool gainQInteraction, const DynamicsSettings& dyn)
    {
        gainQ = gainQInteraction;
        dynSettings = dyn;
        for (int i = 0; i < maxBands; ++i)
            bands[(size_t) i].target = p[i];
    }

    /** Lleva la banda directo a lo pedido, sin suavizar ni fundir (al cargar un estado). */
    void snapToTargets()
    {
        for (auto& b : bands)
        {
            b.topo = b.target;
            b.live = isActive(b.target);
            b.wet = b.wetTarget = b.live ? 1.0 : 0.0;
            b.switching = false;
            b.lf = std::log(b.target.freq);
            b.gain = b.target.gainDb;
            b.lq = std::log(b.target.q);
            b.designedLf = 1.0e9;
            for (auto& ch : b.st)
                for (auto& s : ch)
                    s = {};
        }
    }

    /** L y R se procesan en el lugar (R nulo = mono). `inL/inR`: la entrada sin procesar (para
        la dinámica). `scL/scR`: sidechain o nulos. */
    void process(double* L, double* R, const double* inL, const double* inR, const double* scL, const double* scR, int n)
    {
        for (int start = 0; start < n; start += chunk)
        {
            const int len = std::min(chunk, n - start);
            prepareChunk(inL + start, inR != nullptr ? inR + start : nullptr, scL != nullptr ? scL + start : nullptr,
                         scR != nullptr ? scR + start : nullptr, len);
            runChunk(L + start, R != nullptr ? R + start : nullptr, len);
        }
    }

    DynamicsBank dynamics;

    /** Frecuencia/ganancia/Q suavizadas (para que el solo y la pantalla sigan a la banda). */
    double smoothedFreq(int i) const { return std::exp(bands[(size_t) i].lf); }
    double smoothedQ(int i) const { return std::exp(bands[(size_t) i].lq); }

private:
    struct Band
    {
        BandParams target, topo;
        bool live = false;
        double wet = 0.0, wetTarget = 0.0, wetStart = 0.0;
        bool switching = false;

        double lf = std::log(1000.0), gain = 0.0, lq = 0.0;
        double designedLf = 1.0e9, designedGain = 1.0e9, designedLq = 1.0e9, designedDyn = 1.0e9;
        int sinceDesign = 0;
        DigitalDesign dd;
        std::array<std::array<BiquadState, maxSections>, 2> st {};
    };

    static bool sameTopology(const BandParams& a, const BandParams& b)
    {
        return isActive(a) == isActive(b) && a.shape == b.shape && a.slope == b.slope && a.place == b.place;
    }

    void prepareChunk(const double* inL, const double* inR, const double* scL, const double* scR, int len)
    {
        const double fade = len / (0.010 * fs);
        const double smooth = 1.0 - std::exp(-len / (0.015 * fs));

        std::array<BandParams, maxBands> eff {};
        std::array<double, maxBands> lfs {}, lqs {};

        for (int i = 0; i < maxBands; ++i)
        {
            auto& b = bands[(size_t) i];
            const auto& t = b.target;

            // Cambio de topología: primero se funde a cero, después se rearma.
            if (!sameTopology(b.topo, t))
            {
                if (b.wet <= 0.0)
                {
                    b.topo = t;
                    b.live = isActive(t);
                    b.lf = std::log(t.freq);
                    b.gain = t.gainDb;
                    b.lq = std::log(t.q);
                    for (auto& ch : b.st)
                        for (auto& s : ch)
                            s = {};
                    b.designedLf = 1.0e9;
                    b.wetTarget = b.live ? 1.0 : 0.0;
                    b.switching = false;
                }
                else
                {
                    b.wetTarget = 0.0;
                    b.switching = true;
                }
            }
            else
            {
                // Misma topología (los ajustes de dinámica y demás no la cambian). Si se estaba
                // fundiendo para cambiar y el pedido volvió a lo de antes, vuelve sin rearmarse.
                b.topo = t;
                b.switching = false;
                b.wetTarget = b.live ? 1.0 : 0.0;
            }

            b.wetStart = b.wet;
            if (b.wet < b.wetTarget)
                b.wet = std::min(b.wetTarget, b.wet + fade);
            else if (b.wet > b.wetTarget)
                b.wet = std::max(b.wetTarget, b.wet - fade);

            // Suavizado hacia lo pedido.
            const double tlf = std::log(std::clamp((double) t.freq, 5.0, 40000.0));
            const double tlq = std::log(std::clamp((double) t.q, 0.025, 60.0));
            b.lf += (tlf - b.lf) * smooth;
            b.gain += (t.gainDb - b.gain) * smooth;
            b.lq += (tlq - b.lq) * smooth;
            if (std::abs(tlf - b.lf) < 1.0e-6) b.lf = tlf;
            if (std::abs(t.gainDb - b.gain) < 1.0e-5) b.gain = t.gainDb;
            if (std::abs(tlq - b.lq) < 1.0e-6) b.lq = tlq;

            eff[(size_t) i] = b.topo;
            lfs[(size_t) i] = b.lf;
            lqs[(size_t) i] = b.lq;
        }

        dynamics.process(eff.data(), lfs.data(), lqs.data(), dynSettings, inL, inR, scL, scR, len, scratchA, scratchB);

        for (int i = 0; i < maxBands; ++i)
        {
            auto& b = bands[(size_t) i];
            if (!b.live || (b.wet <= 0.0 && b.wetStart <= 0.0))
                continue;
            const double dyn = dynamics.offset(i);
            ++b.sinceDesign;
            const bool heavy = b.dd.count > 6;
            const bool changed = std::abs(b.lf - b.designedLf) > 1.0e-4 || std::abs(b.gain - b.designedGain) > 0.003
                                 || std::abs(b.lq - b.designedLq) > 1.0e-4 || std::abs(dyn - b.designedDyn) > 0.01;
            const bool first = b.designedLf > 1.0e8;
            if (changed && (first || !heavy || b.sinceDesign >= 4))
            {
                const double g = b.gain + dyn;
                const double q = effectiveQ(b.topo.shape, std::exp(b.lq), g, gainQ);
                b.dd = toDigital(designBand(b.topo.shape, b.topo.slope, std::exp(b.lf), g, q), fs);
                b.designedLf = b.lf;
                b.designedGain = b.gain;
                b.designedLq = b.lq;
                b.designedDyn = dyn;
                b.sinceDesign = 0;
            }
        }
    }

    void runBand(Band& b, double* x, int ch, int len)
    {
        double dry[chunk];
        const bool mix = b.wet < 1.0 || b.wetStart < 1.0;
        if (mix)
            std::copy(x, x + len, dry);
        auto& st = b.st[(size_t) ch];
        for (int s = 0; s < b.dd.count; ++s)
        {
            const Biquad c = b.dd.s[(size_t) s];
            BiquadState z = st[(size_t) s];
            for (int k = 0; k < len; ++k)
                x[k] = tick(c, z, x[k]);
            st[(size_t) s] = z;
        }
        if (mix)
        {
            const double step = (b.wet - b.wetStart) / len;
            for (int k = 0; k < len; ++k)
            {
                const double w = b.wetStart + step * (k + 1);
                x[k] = dry[k] + w * (x[k] - dry[k]);
            }
        }
    }

    void runChunk(double* L, double* R, int len)
    {
        bool anyMs = false;
        for (auto& b : bands)
        {
            if (b.wet <= 0.0 && b.wetStart <= 0.0)
                continue;
            const auto pl = b.topo.place;
            if (R == nullptr)
            {
                if (pl != Placement::Side)
                    runBand(b, L, 0, len);
                continue;
            }
            switch (pl)
            {
                case Placement::Stereo: runBand(b, L, 0, len); runBand(b, R, 1, len); break;
                case Placement::Left: runBand(b, L, 0, len); break;
                case Placement::Right: runBand(b, R, 1, len); break;
                case Placement::Mid:
                case Placement::Side: anyMs = true; break;
            }
        }
        if (!anyMs)
            return;

        double M[chunk], S[chunk];
        for (int k = 0; k < len; ++k)
        {
            M[k] = 0.5 * (L[k] + R[k]);
            S[k] = 0.5 * (L[k] - R[k]);
        }
        for (auto& b : bands)
        {
            if (b.wet <= 0.0 && b.wetStart <= 0.0)
                continue;
            if (b.topo.place == Placement::Mid)
                runBand(b, M, 0, len);
            else if (b.topo.place == Placement::Side)
                runBand(b, S, 1, len);
        }
        for (int k = 0; k < len; ++k)
        {
            L[k] = M[k] + S[k];
            R[k] = M[k] - S[k];
        }
    }

    double fs = 48000.0;
    bool gainQ = false;
    DynamicsSettings dynSettings;
    std::array<Band, maxBands> bands {};
    double scratchA[chunk] {}, scratchB[chunk] {};
};
} // namespace cincel
