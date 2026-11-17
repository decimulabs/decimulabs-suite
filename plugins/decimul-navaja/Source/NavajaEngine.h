#pragma once

#include <algorithm>
#include <cmath>

/**
    Motor de DecimuLabs Navaja: la "navaja suiza" del canal estéreo. Ganancia, paneo, ancho
    estéreo (mid/side), invertir la fase de cada lado, intercambiar L/R, mono, graves en mono
    debajo de una frecuencia y filtro DC. Además mide la correlación entre L y R.
    C++ puro, sin JUCE, sin latencia.
*/
namespace navaja
{
    struct Params
    {
        float gainDb = 0.0f;        // -24..+24
        float pan = 0.0f;           // -1..+1 (balance)
        float width = 1.0f;         // 0 = mono, 1 = original, 2 = el doble de lados
        bool invertL = false, invertR = false;
        bool swap = false;
        bool mono = false;
        bool bassMono = false;
        float bassMonoHz = 120.0f;
        bool dcFilter = false;
    };

    class Engine
    {
    public:
        void prepare(double sampleRate)
        {
            sr = sampleRate > 1000.0 ? sampleRate : 44100.0;
            reset();
        }

        void reset()
        {
            dcxL = dcyL = dcxR = dcyR = 0.0;
            hp1x = hp1y = hp2x = hp2y = 0.0;
            sLR = sLL = sRR = 0.0;
            gainNow = -1.0f;
        }

        void process(float* l, float* r, int n, const Params& p)
        {
            const float target = std::pow(10.0f, p.gainDb / 20.0f);
            if (gainNow < 0.0f) gainNow = target;
            const float panL = p.pan > 0.0f ? 1.0f - p.pan : 1.0f;
            const float panR = p.pan < 0.0f ? 1.0f + p.pan : 1.0f;
            const double dcR = 1.0 - 2.0 * 3.14159265358979 * 10.0 / sr;      // DC: pasa-altos de 10 Hz
            // Coeficiente de los pasa-altos de 1er orden (dos en serie) que limpian los graves del "side".
            const double lpA = std::exp(-2.0 * 3.14159265358979 * std::clamp((double) p.bassMonoHz, 20.0, 1000.0) / sr);
            const double avg = std::exp(-1.0 / (0.3 * sr));                      // correlación: ventana de ~300 ms

            for (int i = 0; i < n; ++i)
            {
                gainNow += 0.001f * (target - gainNow);
                double L = l[i], R = r[i];
                if (p.dcFilter)
                {
                    const double yl = L - dcxL + dcR * dcyL; dcxL = L; dcyL = yl; L = yl;
                    const double yr = R - dcxR + dcR * dcyR; dcxR = R; dcyR = yr; R = yr;
                }
                if (p.invertL) L = -L;
                if (p.invertR) R = -R;
                if (p.swap) std::swap(L, R);

                double mid = 0.5 * (L + R), side = 0.5 * (L - R);
                side *= p.mono ? 0.0 : (double) p.width;
                if (p.bassMono && !p.mono)
                {
                    // Graves del "side" fuera (pasa-altos de 2º orden): lo que queda debajo del corte suena en mono.
                    const double h1 = lpA * (hp1y + side - hp1x); hp1x = side; hp1y = h1;
                    const double h2 = lpA * (hp2y + h1 - hp2x); hp2x = h1; hp2y = h2;
                    side = h2;
                }
                L = (mid + side) * panL * gainNow;
                R = (mid - side) * panR * gainNow;

                sLR = avg * sLR + (1.0 - avg) * L * R;
                sLL = avg * sLL + (1.0 - avg) * L * L;
                sRR = avg * sRR + (1.0 - avg) * R * R;
                l[i] = (float) L;
                r[i] = (float) R;
            }
        }

        /** +1 = mono, 0 = sin relación, -1 = fase invertida (se cancela en mono). */
        float correlation() const
        {
            const double d = std::sqrt(sLL * sRR);
            return d > 1.0e-12 ? (float) std::clamp(sLR / d, -1.0, 1.0) : 1.0f;
        }

    private:
        double sr = 48000.0;
        double dcxL = 0, dcyL = 0, dcxR = 0, dcyR = 0;
        double hp1x = 0, hp1y = 0, hp2x = 0, hp2y = 0;
        double sLR = 0, sLL = 0, sRR = 0;
        float gainNow = -1.0f;
    };
}
