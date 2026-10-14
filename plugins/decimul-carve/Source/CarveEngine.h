#pragma once

#include <array>
#include <cmath>
#include <algorithm>

/**
    Motor de DecimuLabs Carve: "ducking espectral". Lo que entra por el sidechain se
    mide en 32 bandas (de ~25 Hz a ~18 kHz, repartidas en escala logaritmica) y, en
    cada banda, una campana de EQ en la pista principal baja justo lo que el sidechain
    esta ocupando. Es DSP puro C++ (sin JUCE) para poder probarlo aparte.

    Sin latencia: el sidechain se analiza con filtros pasa-banda IIR y el recorte se
    aplica con una cascada de 32 filtros "peaking" de fase minima.
*/
namespace carve
{
    constexpr int numBands = 32;
    constexpr float minBandHz = 25.0f;
    constexpr float maxBandHz = 18000.0f;
    constexpr float maxCutDb = 24.0f;       // recorte maximo con Amount al 100 %
    constexpr float floorDb = -60.0f;       // por debajo de esto el sidechain "no cuenta"
    constexpr float ceilingDb = -6.0f;      // en esto la banda recorta todo lo que puede
    // Las campanas vecinas se suman: con un sidechain de banda ancha el recorte medido es
    // ~2.2 veces el de cada campana. Cada una aplica 1/overlap para que "Amount 100 %"
    // signifique, a grandes rasgos, hasta maxCutDb de recorte real.
    constexpr float bellOverlap = 2.2f;
    constexpr int controlInterval = 32;     // cada cuantas muestras se recalculan los filtros

    struct Params
    {
        float amount = 0.6f;        // 0..1
        float scGainDb = 0.0f;      // ganancia del sidechain antes de medirlo
        float attackMs = 8.0f;
        float releaseMs = 150.0f;
        float lowHz = 20.0f;        // solo recorta dentro de [lowHz, highHz]
        float highHz = 20000.0f;
        float smooth = 0.25f;       // 0..1: cuanto se "esparce" el recorte a bandas vecinas
        float mix = 1.0f;           // 0..1
    };

    inline float bandCentreHz(int index)
    {
        const float t = (float) index / (float) (numBands - 1);
        return minBandHz * std::pow(maxBandHz / minBandHz, t);
    }

    /** Recorte (dB, positivo = baja) que corresponde a un nivel de sidechain en una banda. */
    inline float cutForLevel(float levelDb, float amount)
    {
        const float x = std::clamp((levelDb - floorDb) / (ceilingDb - floorDb), 0.0f, 1.0f);
        return amount * maxCutDb * x * x * x;
    }

    class Engine
    {
    public:
        void prepare(double newSampleRate)
        {
            sampleRate = newSampleRate > 1000.0 ? newSampleRate : 44100.0;
            // Ancho de banda: bandas a razon ~1.235 (unos 0.3 octava).
            for (int b = 0; b < numBands; ++b)
                setBandpass(analysis[(size_t) b], (double) bandCentreHz(b), analysisQ);
            reset();
        }

        void reset()
        {
            for (auto& s : analysisState) s = {};
            for (auto& s : analysisState2) s = {};
            for (auto& ch : cutState)
                for (auto& s : ch) s = {};
            env.fill(0.0f);
            for (auto& c : peak) c = Coeffs {};
            levelDb.fill(-120.0f);
            cutDb.fill(0.0f);
            counter = 0;
        }

        /** Procesa `n` muestras in-place. `scL/scR` nulos = sin sidechain (no recorta nada). */
        void process(float* l, float* r, const float* scL, const float* scR, int n, const Params& p)
        {
            const double atkCoef = 1.0 - std::exp(-1.0 / (std::max(0.05, (double) p.attackMs) * 0.001 * sampleRate));
            const double relCoef = 1.0 - std::exp(-1.0 / (std::max(1.0, (double) p.releaseMs) * 0.001 * sampleRate));
            const float scGain = std::pow(10.0f, p.scGainDb / 20.0f);
            const float mix = std::clamp(p.mix, 0.0f, 1.0f);
            const bool haveSc = scL != nullptr;

            for (int i = 0; i < n; ++i)
            {
                if (haveSc)
                {
                    const float in = 0.5f * (scL[i] + (scR != nullptr ? scR[i] : scL[i])) * scGain;
                    for (int b = 0; b < numBands; ++b)
                    {
                        // Dos pasa-banda en cascada: faldas de 12 dB/oct para que un tono no "manche" bandas lejanas.
                        const double stage = analysisState[(size_t) b].run(analysis[(size_t) b], (double) in);
                        const float band = (float) analysisState2[(size_t) b].run(analysis[(size_t) b], stage);
                        const float rect = std::fabs(band);
                        float& e = env[(size_t) b];
                        e += (rect > e ? (float) atkCoef : (float) relCoef) * (rect - e);
                    }
                }
                else
                {
                    for (auto& e : env) e *= (float) (1.0 - relCoef);
                }

                if (counter == 0)
                    updateTargets(p);
                counter = (counter + 1) % controlInterval;

                const float dryL = l[i], dryR = r[i];
                double wl = (double) dryL, wr = (double) dryR;
                for (int b = 0; b < numBands; ++b)
                {
                    wl = cutState[0][(size_t) b].run(peak[(size_t) b], wl);
                    wr = cutState[1][(size_t) b].run(peak[(size_t) b], wr);
                }
                l[i] = dryL + mix * ((float) wl - dryL);
                r[i] = dryR + mix * ((float) wr - dryR);
            }
        }

        const std::array<float, numBands>& levelsDb() const { return levelDb; }
        const std::array<float, numBands>& cutsDb() const { return cutDb; }

    private:
        static constexpr double analysisQ = 3.0;
        static constexpr double cutQ = 3.0;

        struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
        struct State
        {
            double z1 = 0, z2 = 0;
            double run(const Coeffs& c, double x)
            {
                const double y = c.b0 * x + z1;
                z1 = c.b1 * x - c.a1 * y + z2;
                z2 = c.b2 * x - c.a2 * y;
                return y;
            }
        };

        void setBandpass(Coeffs& c, double hz, double q) const
        {
            const double w0 = 2.0 * 3.14159265358979323846 * std::min(hz, sampleRate * 0.45) / sampleRate;
            const double alpha = std::sin(w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            c.b0 = alpha / a0;
            c.b1 = 0.0;
            c.b2 = -alpha / a0;
            c.a1 = -2.0 * std::cos(w0) / a0;
            c.a2 = (1.0 - alpha) / a0;
        }

        void setPeaking(Coeffs& c, double hz, double q, double gainDb) const
        {
            const double a = std::pow(10.0, gainDb / 40.0);
            const double w0 = 2.0 * 3.14159265358979323846 * std::min(hz, sampleRate * 0.45) / sampleRate;
            const double alpha = std::sin(w0) / (2.0 * q);
            const double cs = std::cos(w0);
            const double a0 = 1.0 + alpha / a;
            c.b0 = (1.0 + alpha * a) / a0;
            c.b1 = (-2.0 * cs) / a0;
            c.b2 = (1.0 - alpha * a) / a0;
            c.a1 = (-2.0 * cs) / a0;
            c.a2 = (1.0 - alpha / a) / a0;
        }

        void updateTargets(const Params& p)
        {
            std::array<float, numBands> raw {};
            for (int b = 0; b < numBands; ++b)
            {
                // Una pasa-banda de pico 1 sobre un seno de amplitud A rectifica a ~0.637 A.
                const float lvl = 20.0f * std::log10(env[(size_t) b] * 1.5708f + 1.0e-9f);
                levelDb[(size_t) b] = lvl;
                const float hz = bandCentreHz(b);
                raw[(size_t) b] = (hz >= p.lowHz && hz <= p.highHz) ? cutForLevel(lvl, p.amount) : 0.0f;
            }

            const float s = std::clamp(p.smooth, 0.0f, 1.0f);
            for (int b = 0; b < numBands; ++b)
            {
                const float prev = raw[(size_t) std::max(0, b - 1)];
                const float next = raw[(size_t) std::min(numBands - 1, b + 1)];
                const float blurred = 0.25f * prev + 0.5f * raw[(size_t) b] + 0.25f * next;
                float g = (1.0f - s) * raw[(size_t) b] + s * blurred;
                // El blur no puede sacar recorte de una banda fuera del rango.
                const float hz = bandCentreHz(b);
                if (hz < p.lowHz || hz > p.highHz)
                    g = 0.0f;
                cutDb[(size_t) b] = g;
                setPeaking(peak[(size_t) b], (double) hz, cutQ, (double) (-g / bellOverlap));
            }
        }

        double sampleRate = 44100.0;
        std::array<Coeffs, numBands> analysis {};
        std::array<State, numBands> analysisState {};
        std::array<State, numBands> analysisState2 {};
        std::array<Coeffs, numBands> peak {};
        std::array<std::array<State, numBands>, 2> cutState {};
        std::array<float, numBands> env {};
        std::array<float, numBands> levelDb {};
        std::array<float, numBands> cutDb {};
        int counter = 0;
    };
}
