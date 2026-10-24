#pragma once

// Motor completo de Cincel (sin JUCE): junta los tres modos de proceso, la dinámica espectral,
// el solo de banda, el "carácter", el auto gain y la salida (ganancia, paneo, fase, bypass).
//
// Orden de la señal:
//   entrada → [modo: latencia cero | fase natural | fase lineal] → [dinámica espectral]
//           → [solo de banda] → [carácter] → [auto gain, salida, paneo, fase] → [bypass]
// El bypass y el carácter usan la entrada retrasada lo mismo que el proceso, así nunca se
// desfasan. Cambiar de modo (o prender la dinámica espectral) cambia la latencia: se hace con
// un fundido corto a silencio y de vuelta.

#include "CincelLinear.h"
#include "CincelOversampler.h"
#include "CincelSpectral.h"

namespace cincel
{
enum class Mode : int { ZeroLatency = 0, NaturalPhase, LinearPhase };
enum class Character : int { Clean = 0, Subtle, Warm };

struct GlobalParams
{
    Mode mode = Mode::ZeroLatency;
    int lpResolution = 2;
    Character character = Character::Clean;
    double gainScale = 1.0;      // -1 (todo invertido) ... 2 (el doble)
    bool autoGain = false;
    bool gainQ = false;
    DynamicsSettings dyn;
    double outputDb = 0.0;
    double pan = 0.0;            // -1 izquierda ... 1 derecha (balance)
    bool phaseInvert = false;
    bool bypass = false;
    int soloBand = -1;
};

/** Ganancia que compensa el volumen que suma o quita la curva (no mide el audio: mira la curva,
    como el Pro-Q). Promedio en dB entre 20 Hz y 20 kHz, con más peso en el medio. */
inline double autoGainCompensationDb(const BandParams* bands, bool gainQ)
{
    constexpr int points = 96;
    double sumW = 0.0, sumDb = 0.0;
    std::array<AnalogDesign, maxBands> designs;
    std::array<double, maxBands> weight {};
    for (int i = 0; i < maxBands; ++i)
    {
        const auto& b = bands[i];
        if (!isActive(b) || !shapeHasGain(b.shape))
            continue;
        designs[(size_t) i] = designBand(b.shape, b.slope, b.freq, b.gainDb, effectiveQ(b.shape, b.q, b.gainDb, gainQ));
        weight[(size_t) i] = (b.place == Placement::Stereo || b.place == Placement::Mid) ? 1.0 : 0.5;
    }
    for (int k = 0; k < points; ++k)
    {
        const double t = k / (double) (points - 1);
        const double hz = 20.0 * std::pow(1000.0, t);
        double w = 1.0;
        if (hz < 60.0)
            w = 0.3 + 0.7 * std::log(hz / 20.0) / std::log(3.0);
        else if (hz > 8000.0)
            w = 0.3 + 0.7 * std::log(20000.0 / hz) / std::log(2.5);
        double db = 0.0;
        for (int i = 0; i < maxBands; ++i)
            if (weight[(size_t) i] > 0.0)
                db += weight[(size_t) i] * 20.0 * std::log10(std::max(1.0e-9, designs[(size_t) i].magnitude(hz)));
        sumW += w;
        sumDb += w * db;
    }
    return std::clamp(-sumDb / sumW, -24.0, 24.0);
}

/** "Carácter" (como los modos del Pro-Q 4): satura solo lo que el EQ le cambia a la señal, así
    con la curva plana no colorea nada y cuanto más se mueve, más se nota. */
inline double characterShape(Character c, double d)
{
    if (c == Character::Subtle)
        return std::tanh(0.5 * d) / 0.5;
    // Warm: asimétrica (armónicos pares, como un pasaje por válvula), normalizada a pendiente 1.
    constexpr double k = 0.5, b = 0.3;
    const double t0 = std::tanh(k * b);
    return (std::tanh(k * (d + b)) - t0) / (k * (1.0 - t0 * t0));
}

/** Escuchar una banda sola: se oye solo la zona de la banda (ver designAudition). En las bandas
    que suman o moldean (bell, shelves, tilt, pasabanda) se escucha esa zona ya ecualizada; en las
    que sacan (cortes y notch) se escucha lo que se está sacando, de la señal original. */
inline bool soloHearsInput(Shape s) { return shapeIsCut(s) || s == Shape::Notch; }

class SoloStage
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        reset();
    }

    void reset()
    {
        mix = 0.0;
        current = -1;
        clearStates();
    }

    /** `dryL/dryR`: la entrada alineada con L/R (para los cortes). */
    void process(double* L, double* R, const double* dryL, const double* dryR, int n, int requested, const BandParams* bands)
    {
        const int want = (requested >= 0 && requested < maxBands && isActive(bands[requested])) ? requested : -1;
        const double step = 1.0 / (0.010 * fs);
        for (int start = 0; start < n; start += 32)
        {
            const int len = std::min(32, n - start);
            double target;
            if (current != want)
            {
                target = 0.0;
                if (mix <= 0.0)
                {
                    current = want;
                    clearStates();
                    designed = {};
                    designedValid = false;
                }
            }
            else
                target = current >= 0 ? 1.0 : 0.0;

            if (current >= 0)
            {
                const auto& b = bands[current];
                if (!designedValid || designed.freq != b.freq || designed.q != b.q || designed.shape != b.shape)
                {
                    filt = toDigital(designAudition(b.shape, b.freq, b.q), fs);
                    designed = b;
                    designedValid = true;
                }
            }
            if (current < 0 && mix <= 0.0)
                continue;

            const Placement pl = current >= 0 ? bands[current].place : Placement::Stereo;
            const bool fromInput = current >= 0 && soloHearsInput(bands[current].shape);
            for (int k = start; k < start + len; ++k)
            {
                mix = target > mix ? std::min(target, mix + step) : std::max(target, mix - step);
                const double l = L[k];
                const double r = R != nullptr ? R[k] : l;
                const double sl = fromInput ? dryL[k] : l;
                const double sr = fromInput ? (R != nullptr ? dryR[k] : dryL[k]) : r;
                double yl, yr;
                if (R == nullptr)
                    yl = yr = run(0, sl);
                else
                    switch (pl)
                    {
                        case Placement::Stereo: yl = run(0, sl); yr = run(1, sr); break;
                        case Placement::Left: yl = run(0, sl); yr = 0.0; break;
                        case Placement::Right: yl = 0.0; yr = run(1, sr); break;
                        case Placement::Mid: yl = yr = run(0, 0.5 * (sl + sr)); break;
                        default: yl = run(0, 0.5 * (sl - sr)); yr = -yl; break;
                    }
                L[k] = l + mix * (yl - l);
                if (R != nullptr)
                    R[k] = r + mix * (yr - r);
            }
        }
    }

    int currentBand() const { return mix > 0.0 ? current : -1; }

private:
    double run(int ch, double x)
    {
        for (int s = 0; s < filt.count; ++s)
            x = tick(filt.s[(size_t) s], st[ch][(size_t) s], x);
        return x;
    }

    void clearStates()
    {
        for (auto& c : st)
            for (auto& s : c)
                s = {};
    }

    double fs = 48000.0, mix = 0.0;
    int current = -1;
    DigitalDesign filt;
    BandParams designed;
    bool designedValid = false;
    std::array<std::array<BiquadState, maxSections>, 2> st {};
};

//==============================================================================
class Engine
{
public:
    ~Engine() { worker.stop(); }

    void prepare(double sampleRate, int maxBlockSize, bool isStereo)
    {
        worker.stop();
        fs = sampleRate;
        maxBlock = std::max(32, maxBlockSize);
        stereo = isStereo;

        zl.prepare(fs);
        np.prepare(fs, maxBlock);
        lpDyn.prepare(fs);
        maxParts = lpTaps(numLpResolutions - 1, fs) / lpPartition;
        kx.allocate(maxParts);
        conv.prepare(maxParts);
        syncDesigner.prepare(fs);
        spectral.prepare(fs);
        solo.prepare(fs);

        zl.dynamics.telemetry = &telemetry;
        np.core.dynamics.telemetry = &telemetry;
        lpDyn.telemetry = &telemetry;
        spectral.telemetry = &telemetry;

        for (auto* v : { &bufL, &bufR, &inL, &inR, &scL, &scR, &scratchA, &scratchB })
            v->assign((size_t) maxBlock, 0.0);

        const int maxLatency = lpLatency(numLpResolutions - 1, fs) + spectral.latency() + np.latency() + 16;
        int cap = 1;
        while (cap < maxLatency + maxBlock + 2)
            cap <<= 1;
        delayMask = cap - 1;
        delay[0].assign((size_t) cap, 0.0);
        delay[1].assign((size_t) cap, 0.0);
        delayPos = 0;

        first = true;
        fadeGain = 1.0;
        switching = false;
        activeKernel = -1;
        smOut = smL = smR = 1.0;
        smBypass = 0.0;
        autoGainTargetDb = 0.0;
        smAutoGain = 0.0;
        lastAutoValid = false;
        lpVersion = 1;
        builtVersion = 0;

        worker.start(fs, &kx);
    }

    void release() { worker.stop(); }

    /** El host está exportando (no en tiempo real): el FIR de fase lineal se arma en el acto. */
    void setOffline(bool isOffline)
    {
        offline = isOffline;
        worker.paused.store(isOffline);
    }

    /** L y R (R nulo = mono) se procesan en el lugar. scL/scR: sidechain o nulos. */
    void process(float* L, float* R, const float* scInL, const float* scInR, int n, const BandParams* bands, const GlobalParams& g)
    {
        for (int off = 0; off < n; off += maxBlock)
        {
            const int len = std::min(maxBlock, n - off);
            processBlock(L + off, R != nullptr ? R + off : nullptr, scInL != nullptr ? scInL + off : nullptr,
                         scInR != nullptr ? scInR + off : nullptr, len, bands, g);
        }
    }

    /** Latencia actual (muestras), para avisarle al host. */
    int latency() const { return latencyNow.load(); }

    /** La latencia que va a tener una configuración (válido después de prepare). */
    int latencyFor(Mode m, int res, bool spectralOn) const { return configLatency(m, res, spectralOn); }

    /** Cuánto corrige el auto gain (dB), para mostrar. */
    float autoGainDb() const { return autoGainShown.load(); }

    DynamicsTelemetry telemetry;

    /** Para los tests: el modo con el que se está procesando. */
    Mode currentMode() const { return curMode; }

private:
    int configLatency(Mode m, int res, bool spec) const
    {
        int l = 0;
        if (m == Mode::NaturalPhase)
            l = np.latency();
        else if (m == Mode::LinearPhase)
            l = lpLatency(res, fs);
        if (spec)
            l += spectral.latency();
        return l;
    }

    void applyConfig(Mode m, int res, bool spec, const BandParams* eff, const GlobalParams& g)
    {
        curMode = m;
        curRes = res;
        curSpectral = spec;
        if (m == Mode::ZeroLatency)
        {
            zl.reset();
            zl.setParams(eff, g.gainQ, g.dyn);
            zl.snapToTargets();
        }
        else if (m == Mode::NaturalPhase)
        {
            np.reset();
            np.core.setParams(eff, g.gainQ, g.dyn);
            np.core.snapToTargets();
        }
        else
        {
            conv.reset();
            conv.expectedTaps = lpTaps(res, fs);
            activeKernel = -1;
            kx.inUseA.store(-1);
            kx.inUseB.store(-1);
            kx.takeReady();
            lpDyn.reset();
            ++lpVersion;
        }
        if (spec)
            spectral.reset();
        curLatency = configLatency(m, res, spec);
        latencyNow.store(curLatency);
    }

    static bool sameBand(const BandParams& a, const BandParams& b)
    {
        return a.used == b.used && a.enabled == b.enabled && a.freq == b.freq && a.gainDb == b.gainDb && a.q == b.q
               && a.shape == b.shape && a.slope == b.slope && a.place == b.place && a.dynamic == b.dynamic
               && a.rangeDb == b.rangeDb && a.thresholdDb == b.thresholdDb && a.autoThreshold == b.autoThreshold
               && a.sidechain == b.sidechain && a.spectral == b.spectral;
    }

    void runLinearPhase(int n, const BandParams* eff, const GlobalParams& g, const double* sl, const double* sr)
    {
        // Dinámica: se mide la entrada (sin demora: el FIR la retrasa, así que actúa con anticipación).
        std::array<double, maxBands> lfs {}, lqs {};
        for (int i = 0; i < maxBands; ++i)
        {
            lfs[(size_t) i] = std::log(std::max(5.0f, eff[i].freq));
            lqs[(size_t) i] = std::log(std::max(0.025f, eff[i].q));
        }
        for (int start = 0; start < n; start += 32)
        {
            const int len = std::min(32, n - start);
            lpDyn.process(eff, lfs.data(), lqs.data(), g.dyn, inL.data() + start, stereo ? inR.data() + start : nullptr,
                          sl != nullptr ? sl + start : nullptr, sr != nullptr ? sr + start : nullptr, len,
                          scratchA.data(), scratchB.data());
        }

        bool changed = g.gainQ != lastLpGainQ;
        for (int i = 0; i < maxBands && !changed; ++i)
            changed = !sameBand(eff[i], lastLpBands[(size_t) i]);
        if (changed)
        {
            ++lpVersion;
            for (int i = 0; i < maxBands; ++i)
                lastLpBands[(size_t) i] = eff[i];
            lastLpGainQ = g.gainQ;
        }

        LpSnapshot& s = snapshot;
        s.bands = lastLpBands;
        for (int i = 0; i < maxBands; ++i)
            s.dynDb[(size_t) i] = (float) lpDyn.offset(i);
        s.gainQ = g.gainQ;
        s.stereo = stereo;
        s.resolution = curRes;
        s.version = lpVersion;

        if (offline)
        {
            float dist = 0.0f;
            for (int i = 0; i < maxBands; ++i)
                dist = std::max(dist, std::abs(s.dynDb[(size_t) i] - builtDyn[(size_t) i]));
            if (builtVersion != lpVersion || builtRes != curRes || dist > 0.1f)
            {
                while (worker.busy.load())
                    std::this_thread::yield();
                const int slot = kx.acquireWriteSlot();
                if (slot >= 0)
                {
                    syncDesigner.build(s, kx.slots[slot]);
                    kx.publish(slot);
                    builtVersion = lpVersion;
                    builtRes = curRes;
                    builtDyn = s.dynDb;
                }
            }
        }
        else
            worker.post(s);

        conv.process(bufL.data(), stereo ? bufR.data() : nullptr, n, kx, activeKernel);
    }

    void processBlock(float* L, float* R, const float* scInL, const float* scInR, int n, const BandParams* bands,
                      const GlobalParams& g)
    {
        const bool st = stereo && R != nullptr;

        // Parámetros efectivos: la escala de ganancia afecta ganancias y rangos dinámicos.
        std::array<BandParams, maxBands>& eff = effBands;
        bool anySpectral = false;
        for (int i = 0; i < maxBands; ++i)
        {
            eff[(size_t) i] = bands[i];
            eff[(size_t) i].gainDb = (float) (bands[i].gainDb * g.gainScale);
            eff[(size_t) i].rangeDb = (float) (bands[i].rangeDb * g.gainScale);
            anySpectral = anySpectral || usesSpectral(eff[(size_t) i]);
        }

        // Configuración (cambia la latencia): primero se funde a silencio.
        const Mode wantMode = g.mode;
        const int wantRes = std::clamp(g.lpResolution, 0, numLpResolutions - 1);
        if (first)
        {
            applyConfig(wantMode, wantRes, anySpectral, eff.data(), g);
            first = false;
        }
        else if (!switching
                 && (wantMode != curMode || (wantMode == Mode::LinearPhase && wantRes != curRes) || anySpectral != curSpectral))
            switching = true;

        // Entrada a double; copia sin procesar (para la dinámica y la señal seca).
        for (int i = 0; i < n; ++i)
        {
            bufL[(size_t) i] = inL[(size_t) i] = L[i];
            if (st)
                bufR[(size_t) i] = inR[(size_t) i] = R[i];
        }
        const double* sl = nullptr;
        const double* sr = nullptr;
        if (scInL != nullptr)
        {
            for (int i = 0; i < n; ++i)
                scL[(size_t) i] = scInL[i];
            sl = scL.data();
            if (scInR != nullptr)
            {
                for (int i = 0; i < n; ++i)
                    scR[(size_t) i] = scInR[i];
                sr = scR.data();
            }
        }
        for (int i = 0; i < n; ++i)
        {
            const int p = (delayPos + i) & delayMask;
            delay[0][(size_t) p] = inL[(size_t) i];
            delay[1][(size_t) p] = st ? inR[(size_t) i] : inL[(size_t) i];
        }

        double* pl = bufL.data();
        double* pr = st ? bufR.data() : nullptr;
        const double* il = inL.data();
        const double* ir = st ? inR.data() : nullptr;
        switch (curMode)
        {
            case Mode::ZeroLatency:
                zl.setParams(eff.data(), g.gainQ, g.dyn);
                zl.process(pl, pr, il, ir, sl, sr, n);
                break;
            case Mode::NaturalPhase:
                np.core.setParams(eff.data(), g.gainQ, g.dyn);
                np.process(pl, pr, il, ir, sl, sr, n);
                break;
            case Mode::LinearPhase:
                runLinearPhase(n, eff.data(), g, sl, sr);
                break;
        }

        if (curSpectral)
            spectral.process(pl, pr, sl, sr, n, eff.data(), g.dyn);

        // Señal seca alineada con lo procesado (reusa los buffers de entrada).
        for (int i = 0; i < n; ++i)
        {
            const int p = (delayPos + i - curLatency) & delayMask;
            inL[(size_t) i] = delay[0][(size_t) p];
            inR[(size_t) i] = delay[1][(size_t) p];
        }
        delayPos = (delayPos + n) & delayMask;

        solo.process(pl, pr, inL.data(), inR.data(), n, g.soloBand, eff.data());

        if (g.character != Character::Clean && solo.currentBand() < 0)
            for (int i = 0; i < n; ++i)
            {
                pl[i] = inL[(size_t) i] + characterShape(g.character, pl[i] - inL[(size_t) i]);
                if (st)
                    pr[i] = inR[(size_t) i] + characterShape(g.character, pr[i] - inR[(size_t) i]);
            }

        // Auto gain: se recalcula solo si cambió la curva.
        bool curveChanged = !lastAutoValid || g.gainQ != lastAutoGainQ;
        for (int i = 0; i < maxBands && !curveChanged; ++i)
        {
            const auto& a = eff[(size_t) i];
            const auto& b = lastAuto[(size_t) i];
            curveChanged = a.used != b.used || a.enabled != b.enabled || a.freq != b.freq || a.gainDb != b.gainDb
                           || a.q != b.q || a.shape != b.shape || a.slope != b.slope || a.place != b.place;
        }
        if (curveChanged)
        {
            autoGainTargetDb = autoGainCompensationDb(eff.data(), g.gainQ);
            lastAuto = eff;
            lastAutoGainQ = g.gainQ;
            lastAutoValid = true;
            autoGainShown.store((float) autoGainTargetDb);
        }

        // Salida: ganancia (con auto gain), balance, fase y bypass, todo suavizado.
        const double outDb = g.outputDb + (g.autoGain ? autoGainTargetDb : 0.0);
        const double tOut = std::pow(10.0, outDb / 20.0) * (g.phaseInvert ? -1.0 : 1.0);
        const double tL = g.pan > 0.0 ? 1.0 - g.pan : 1.0;
        const double tR = g.pan < 0.0 ? 1.0 + g.pan : 1.0;
        const double tByp = g.bypass ? 1.0 : 0.0;
        const double k = 1.0 - std::exp(-1.0 / (0.02 * fs));
        const double fadeStep = 1.0 / (0.005 * fs);
        for (int i = 0; i < n; ++i)
        {
            smOut += (tOut - smOut) * k;
            smL += (tL - smL) * k;
            smR += (tR - smR) * k;
            smBypass += (tByp - smBypass) * k;
            if (switching)
                fadeGain = std::max(0.0, fadeGain - fadeStep);
            else if (fadeGain < 1.0)
                fadeGain = std::min(1.0, fadeGain + fadeStep);

            double yl = pl[i] * smOut * (st ? smL : 1.0);
            yl = yl + smBypass * (inL[(size_t) i] - yl);
            L[i] = (float) (yl * fadeGain);
            if (st)
            {
                double yr = pr[i] * smOut * smR;
                yr = yr + smBypass * (inR[(size_t) i] - yr);
                R[i] = (float) (yr * fadeGain);
            }
        }

        if (switching && fadeGain <= 0.0)
        {
            applyConfig(wantMode, wantRes, anySpectral, eff.data(), g);
            switching = false;
        }
    }

    double fs = 48000.0;
    int maxBlock = 512;
    bool stereo = true;
    bool offline = false;

    IirCore zl;
    NaturalPhaseEngine np;
    DynamicsBank lpDyn;
    KernelExchange kx;
    LinearPhaseConvolver conv;
    LinearPhaseWorker worker;
    LinearPhaseDesigner syncDesigner;
    SpectralDynamics spectral;
    SoloStage solo;

    int maxParts = 1;
    int activeKernel = -1;
    uint32_t lpVersion = 1, builtVersion = 0;
    int builtRes = -1;
    std::array<float, maxBands> builtDyn {};
    std::array<BandParams, maxBands> lastLpBands {};
    bool lastLpGainQ = false;
    LpSnapshot snapshot;

    std::array<BandParams, maxBands> effBands {}, lastAuto {};
    bool lastAutoValid = false, lastAutoGainQ = false;
    double autoGainTargetDb = 0.0, smAutoGain = 0.0;
    std::atomic<float> autoGainShown { 0.0f };

    Mode curMode = Mode::ZeroLatency;
    int curRes = 2;
    bool curSpectral = false;
    int curLatency = 0;
    std::atomic<int> latencyNow { 0 };
    bool first = true, switching = false;
    double fadeGain = 1.0;

    std::vector<double> bufL, bufR, inL, inR, scL, scR, scratchA, scratchB;
    std::vector<double> delay[2];
    int delayMask = 0, delayPos = 0;

    double smOut = 1.0, smL = 1.0, smR = 1.0, smBypass = 0.0;
};
} // namespace cincel
