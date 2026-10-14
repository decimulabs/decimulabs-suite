#pragma once

#include <array>
#include <cmath>
#include "HitDetector.h"

/**
    "Huella" espectral de un golpe: cómo se reparte su energía entre 12 bandas de
    frecuencia (90 Hz - 11 kHz, espaciadas geométricamente), medida sobre la ventana
    de análisis que sigue al ataque.

    Sirve para distinguir un golpe del instrumento buscado del bleed de otro: la caja
    tiene energía en los medios y un "chasquido" en agudos; el hi-hat, casi solo agudos;
    el bombo, casi solo graves. Dos golpes del mismo instrumento tienen una forma
    espectral muy parecida aunque suenen a distinto volumen, porque el vector se
    normaliza (solo cuenta la forma, no la intensidad).

    La comparación es el coseno entre vectores: 1 = misma forma, ~0 = nada que ver.
    Se usa amplitud^0.5 por banda (cuarta raíz de la energía) para que las bandas
    débiles pesen más que con energía pura, que quedaría dominada por los graves.
*/
class FingerprintBank
{
public:
    static constexpr int numBands = 12;
    using Vector = std::array<float, numBands>;

    void prepare(double sampleRate)
    {
        constexpr double lo = 90.0, hi = 11000.0;
        const double ratio = std::pow(hi / lo, 1.0 / (double) (numBands - 1));
        for (int k = 0; k < numBands; ++k)
            bands[(size_t) k].setBandpass(sampleRate, lo * std::pow(ratio, (double) k), 2.2);
        reset();
    }

    void reset()
    {
        for (auto& b : bands)
            b.reset();
        acc.fill(0.0);
        active = false;
    }

    /** Arranca a acumular energía (en el instante del ataque). */
    void begin()
    {
        acc.fill(0.0);
        active = true;
    }

    void end() { active = false; }
    bool isActive() const { return active; }

    /** Hay que llamarlo con cada muestra (los filtros corren siempre). */
    void process(float x)
    {
        for (size_t k = 0; k < (size_t) numBands; ++k)
        {
            const double y = (double) bands[k].process(x);
            if (active)
                acc[k] += y * y;
        }
    }

    /** Vector unitario con la forma espectral acumulada. false si casi no hubo energía. */
    bool getVector(Vector& v) const
    {
        double sum = 0.0;
        for (size_t k = 0; k < (size_t) numBands; ++k)
        {
            const double a = std::pow(std::max(acc[k], 0.0), 0.25);
            v[k] = (float) a;
            sum += a * a;
        }

        if (sum < 1.0e-12)
            return false;

        const float inv = (float) (1.0 / std::sqrt(sum));
        for (auto& e : v)
            e *= inv;
        return true;
    }

    static float similarity(const Vector& a, const Vector& b)
    {
        float dot = 0.0f;
        for (size_t k = 0; k < (size_t) numBands; ++k)
            dot += a[k] * b[k];
        return std::clamp(dot, 0.0f, 1.0f);
    }

private:
    std::array<DetectionBiquad, numBands> bands;
    std::array<double, numBands> acc {};
    bool active = false;
};
