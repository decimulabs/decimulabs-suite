#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

/**
    Piezas de DSP de Flank, sin dependencias de JUCE (header-only: el test de motor las
    usa igual que el plugin).

    - `DelayLine`: retardo con lectura fraccionaria (Hermite de 4 puntos).
    - `MicroShifter`: detune estático. Dos lecturas del retardo con un barrido en diente
      de sierra y ventana Hann: es el pitch shifter de línea de retardo de toda la vida,
      pensado para desvíos chicos (hasta ~50 centésimos).
    - `DriftDelay`: detune "móvil". Un retardo modulado por dos LFO lentos e
      inconmensurables, así la desafinación va y viene alrededor del tono original
      (como un chorus / doble de cinta) en vez de quedarse fija.
    - `EarlyReflections`: unas pocas reflexiones tempranas, difusas y amortiguadas.
    - `Biquad`: pasa-altos / pasa-bajos RBJ sin asignaciones de memoria.
*/
namespace flank
{
    constexpr float kPi = 3.14159265358979f;
    constexpr float kTwoPi = 6.28318530717959f;

    inline float centsToRatio(float cents) { return std::exp2(cents / 1200.0f); }

    // ---------------------------------------------------------------- Biquad

    class Biquad
    {
    public:
        void setHighpass(double sampleRate, double freq, double q = 0.7071) { design(sampleRate, freq, q, true); }
        void setLowpass(double sampleRate, double freq, double q = 0.7071) { design(sampleRate, freq, q, false); }
        void reset() { z1 = z2 = 0.0; }

        float process(float x)
        {
            const double in = (double) x;
            const double out = b0 * in + z1;
            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;
            return (float) out;
        }

    private:
        void design(double sampleRate, double freq, double q, bool highpass)
        {
            const double pi = 3.14159265358979323846;
            const double f = std::clamp(freq, 1.0, sampleRate * 0.45);
            const double w0 = 2.0 * pi * f / sampleRate;
            const double cosw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;

            if (highpass)
            {
                b0 = (1.0 + cosw) * 0.5 / a0;
                b1 = -(1.0 + cosw) / a0;
                b2 = b0;
            }
            else
            {
                b0 = (1.0 - cosw) * 0.5 / a0;
                b1 = (1.0 - cosw) / a0;
                b2 = b0;
            }
            a1 = -2.0 * cosw / a0;
            a2 = (1.0 - alpha) / a0;
        }

        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
    };

    // ------------------------------------------------------------ DelayLine

    class DelayLine
    {
    public:
        void prepare(int minimumSize)
        {
            int n = 16;
            while (n < minimumSize)
                n <<= 1;
            buffer.assign((size_t) n, 0.0f);
            mask = n - 1;
            write = 0;
        }

        void reset()
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            write = 0;
        }

        void push(float x)
        {
            buffer[(size_t) write] = x;
            write = (write + 1) & mask;
        }

        int capacity() const { return mask + 1; }

        /** La muestra de hace `delay` muestras (0 = la última que entró; mínimo útil: 2). */
        float read(float delay) const
        {
            delay = std::clamp(delay, 2.0f, (float) (mask - 4));
            const int di = (int) delay;
            const float frac = delay - (float) di;

            const int base = write - 1 - di - 1;     // la muestra inmediatamente más vieja
            const float xm1 = buffer[(size_t) ((base - 1) & mask)];
            const float x0 = buffer[(size_t) (base & mask)];
            const float x1 = buffer[(size_t) ((base + 1) & mask)];
            const float x2 = buffer[(size_t) ((base + 2) & mask)];
            const float t = 1.0f - frac;

            // Hermite de 4 puntos
            const float c = (x1 - xm1) * 0.5f;
            const float v = x0 - x1;
            const float w = c + v;
            const float a = w + v + (x2 - x0) * 0.5f;
            const float b = w + a;
            return ((a * t - b) * t + c) * t + x0;
        }

    private:
        std::vector<float> buffer;
        int mask = 0;
        int write = 0;
    };

    // --------------------------------------------------------- MicroShifter

    class MicroShifter
    {
    public:
        void prepare(double sampleRate)
        {
            line.prepare((int) (sampleRate * 0.2) + 16);
            phase = 0.5;
        }

        void reset()
        {
            line.reset();
            phase = 0.5;
        }

        /** `ratio` = factor de frecuencia (2^(cents/1200)); `center` = retardo medio en
            muestras; `window` = ancho del barrido en muestras. Con ratio = 1 el retardo
            queda exactamente en `center`. */
        float process(float x, float ratio, float center, float window)
        {
            line.push(x);

            const float w = std::max(window, 2.0f);
            const float p = (float) phase;
            float p2 = p + 0.5f;
            if (p2 >= 1.0f)
                p2 -= 1.0f;

            const float d1 = center + (p - 0.5f) * w;
            const float d2 = center + (p2 - 0.5f) * w;

            const float w1 = 0.5f - 0.5f * std::cos(kTwoPi * p);   // ventana Hann: w1 + w2 = 1
            const float w2 = 1.0f - w1;

            const float y = w1 * line.read(d1) + w2 * line.read(d2);

            // El retardo cambia (1 - ratio) muestras por muestra: eso es lo que desafina.
            phase += (double) (1.0f - ratio) / (double) w;
            phase -= std::floor(phase);
            return y;
        }

    private:
        DelayLine line;
        double phase = 0.5;
    };

    // ----------------------------------------------------------- DriftDelay

    class DriftDelay
    {
    public:
        /** f1/f2: frecuencias de los dos LFO (Hz); ph1/ph2: fases iniciales (rad). */
        void prepare(double sampleRate, float f1, float f2, float ph1, float ph2)
        {
            sr = (float) sampleRate;
            freq1 = f1;
            freq2 = f2;
            inc1 = kTwoPi * f1 / sr;
            inc2 = kTwoPi * f2 / sr;
            startPhase1 = ph1;
            startPhase2 = ph2;
            line.prepare((int) (sampleRate * 0.2) + 16);
            phase1 = ph1;
            phase2 = ph2;
        }

        void reset()
        {
            line.reset();
            phase1 = startPhase1;
            phase2 = startPhase2;
        }

        /** `center`: retardo medio (muestras). `deviation`: desvío máximo de frecuencia
            relativo (2^(cents/1200) - 1): la desafinación llega hasta ese valor. */
        float process(float x, float center, float deviation)
        {
            line.push(x);

            constexpr float a1 = 0.65f, a2 = 0.35f;
            float depth1 = a1 * deviation * sr / (kTwoPi * freq1);
            float depth2 = a2 * deviation * sr / (kTwoPi * freq2);

            // El retardo no puede ser menor que unas pocas muestras: si el barrido no
            // entra, se reduce el desvío (en lugar de leer del futuro).
            const float room = std::max(0.0f, center - 3.0f);
            const float total = depth1 + depth2;
            if (total > room && total > 0.0f)
            {
                const float s = room / total;
                depth1 *= s;
                depth2 *= s;
            }

            const float d = center + depth1 * std::sin(phase1) + depth2 * std::sin(phase2);

            phase1 += inc1;
            if (phase1 > kTwoPi) phase1 -= kTwoPi;
            phase2 += inc2;
            if (phase2 > kTwoPi) phase2 -= kTwoPi;

            return line.read(d);
        }

    private:
        DelayLine line;
        float sr = 48000.0f;
        float freq1 = 1.0f, freq2 = 2.0f, inc1 = 0.0f, inc2 = 0.0f;
        float startPhase1 = 0.0f, startPhase2 = 0.0f, phase1 = 0.0f, phase2 = 0.0f;
    };

    // ----------------------------------------------------- EarlyReflections

    class EarlyReflections
    {
    public:
        static constexpr int numTaps = 6;

        void prepare(double sampleRate)
        {
            const float srf = (float) sampleRate;
            line.prepare((int) (sampleRate * 0.08) + 16);

            static const float msL[numTaps] = { 5.3f, 9.7f, 14.9f, 21.1f, 28.7f, 37.3f };
            static const float msR[numTaps] = { 6.9f, 11.3f, 17.7f, 24.3f, 32.9f, 41.9f };
            static const float gL[numTaps] = { 0.55f, -0.42f, 0.34f, -0.26f, 0.20f, -0.15f };
            static const float gR[numTaps] = { -0.50f, 0.40f, -0.31f, 0.24f, -0.18f, 0.13f };

            for (int i = 0; i < numTaps; ++i)
            {
                tapL[(size_t) i] = msL[i] * 0.001f * srf;
                tapR[(size_t) i] = msR[i] * 0.001f * srf;
                gainL[(size_t) i] = gL[i];
                gainR[(size_t) i] = gR[i];
            }

            apL1.prepare((int) (0.0047f * srf), 0.5f);
            apL2.prepare((int) (0.0023f * srf), 0.5f);
            apR1.prepare((int) (0.0053f * srf), 0.5f);
            apR2.prepare((int) (0.0029f * srf), 0.5f);

            // Amortiguación: pasa-bajos de un polo en ~6 kHz
            dampCoeff = 1.0f - std::exp(-kTwoPi * 6000.0f / srf);
            dampL = dampR = 0.0f;
        }

        void reset()
        {
            line.reset();
            apL1.reset(); apL2.reset(); apR1.reset(); apR2.reset();
            dampL = dampR = 0.0f;
        }

        void process(float x, float& outL, float& outR)
        {
            line.push(x);

            float sl = 0.0f, sr = 0.0f;
            for (size_t i = 0; i < (size_t) numTaps; ++i)
            {
                sl += gainL[i] * line.read(tapL[i]);
                sr += gainR[i] * line.read(tapR[i]);
            }

            sl = apL2.process(apL1.process(sl));
            sr = apR2.process(apR1.process(sr));

            dampL += dampCoeff * (sl - dampL);
            dampR += dampCoeff * (sr - dampR);

            outL = dampL;
            outR = dampR;
        }

    private:
        class Allpass
        {
        public:
            void prepare(int delaySamples, float feedback)
            {
                buf.assign((size_t) std::max(1, delaySamples), 0.0f);
                idx = 0;
                g = feedback;
            }
            void reset() { std::fill(buf.begin(), buf.end(), 0.0f); idx = 0; }

            float process(float x)
            {
                const float delayed = buf[(size_t) idx];
                const float v = x + g * delayed;
                const float y = -g * v + delayed;
                buf[(size_t) idx] = v;
                if (++idx >= (int) buf.size())
                    idx = 0;
                return y;
            }

        private:
            std::vector<float> buf;
            int idx = 0;
            float g = 0.5f;
        };

        DelayLine line;
        std::array<float, numTaps> tapL {}, tapR {}, gainL {}, gainR {};
        Allpass apL1, apL2, apR1, apR2;
        float dampCoeff = 0.5f, dampL = 0.0f, dampR = 0.0f;
    };
}
