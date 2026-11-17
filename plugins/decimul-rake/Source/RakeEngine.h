#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

/**
    Motor de DecimuLabs Rake (Comb Filter): hasta cuatro filtros peine afinados en paralelo
    (uno por nota del acorde). Cada peine resuena en su nota y en todos sus armónicos, así
    cualquier sonido queda "afinado". Feedback positivo (tubular) o negativo (hueco, una octava
    abajo), damping dentro del lazo, un LFO que barre la afinación con desfase entre L y R, y
    una saturación suave a la salida para que el feedback alto no pique. C++ puro, sin latencia.
*/
namespace rake
{
    constexpr double pi = 3.14159265358979323846;
    constexpr int maxVoices = 4;

    /** Intervalos (semitonos) de cada acorde. */
    inline int chordSize(int chord) { static const int n[] = { 1, 2, 3, 3, 3, 4, 3 }; return n[std::clamp(chord, 0, 6)]; }
    inline int chordInterval(int chord, int i)
    {
        static const int iv[7][4] = { { 0 }, { 0, 7 }, { 0, 4, 7 }, { 0, 3, 7 }, { 0, 5, 7 }, { 0, 4, 7, 10 }, { 0, 12, 24 } };
        return iv[std::clamp(chord, 0, 6)][std::clamp(i, 0, 3)];
    }

    struct Params
    {
        float note = 57.0f;         // nota MIDI (con fracción por el ajuste fino): 57 = LA 220 Hz
        int chord = 0;              // 0 unísono, 1 quinta, 2 mayor, 3 menor, 4 sus4, 5 séptima, 6 octavas
        float feedback = 0.85f;     // -0.99..0.99
        float damping = 0.3f;       // 0..1
        float lfoRateHz = 0.5f;
        float lfoDepth = 0.0f;      // 0..1 (hasta ±1 semitono)
        float stereoPhase = 0.25f;  // 0..0.5 de vuelta
        float mix = 0.6f;
        float outputDb = 0.0f;
    };

    inline double noteToHz(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }

    class Engine
    {
    public:
        void prepare(double sampleRate)
        {
            sr = sampleRate > 1000.0 ? sampleRate : 44100.0;
            const size_t len = (size_t) (sr / 25.0) + 8;          // la nota más grave: ~25 Hz
            for (auto& ch : lines) for (auto& v : ch) v.buf.assign(len, 0.0f);
            reset();
        }

        void reset()
        {
            for (auto& ch : lines) for (auto& v : ch) { std::fill(v.buf.begin(), v.buf.end(), 0.0f); v.w = 0; v.lp = 0.0f; }
            phase = 0.0;
            for (auto& d : smoothDelay) d = -1.0;
        }

        void process(float* l, float* r, int n, const Params& p)
        {
            const int voices = chordSize(p.chord);
            const float fb = std::clamp(p.feedback, -0.99f, 0.99f);
            const float damp = std::clamp(p.damping, 0.0f, 1.0f) * 0.9f;
            const float norm = (1.0f - std::fabs(fb)) * 0.9f + 0.1f;          // compensa el pico de la resonancia
            const float out = std::pow(10.0f, p.outputDb / 20.0f);
            const double lfoInc = 2.0 * pi * std::clamp((double) p.lfoRateHz, 0.01, 20.0) / sr;
            float* io[2] = { l, r };

            std::array<double, maxVoices> target {};
            for (int v = 0; v < voices; ++v)
                target[(size_t) v] = sr / noteToHz(std::clamp((double) p.note + chordInterval(p.chord, v), 12.0, 120.0));

            for (int i = 0; i < n; ++i)
            {
                phase += lfoInc;
                if (phase > 2.0 * pi) phase -= 2.0 * pi;
                for (int c = 0; c < 2; ++c)
                {
                    const double lfo = std::sin(phase + (c == 1 ? 2.0 * pi * p.stereoPhase : 0.0));
                    const double bend = std::pow(2.0, -lfo * p.lfoDepth / 12.0);    // ±1 semitono
                    const float x = io[c][i];
                    float wet = 0.0f;
                    for (int v = 0; v < voices; ++v)
                    {
                        auto& sd = smoothDelay[(size_t) (c * maxVoices + v)];
                        if (sd < 0.0) sd = target[(size_t) v];
                        sd += 0.002 * (target[(size_t) v] - sd);                    // cambiar la nota no hace clics
                        auto& line = lines[(size_t) c][(size_t) v];
                        const int len = (int) line.buf.size();
                        const double d = std::clamp(sd * bend, 2.0, (double) len - 3.0);
                        double pos = (double) line.w - d;
                        while (pos < 0) pos += len;
                        const int i0 = (int) pos;
                        const float f = (float) (pos - i0);
                        const float y = line.buf[(size_t) i0] + f * (line.buf[(size_t) ((i0 + 1) % len)] - line.buf[(size_t) i0]);
                        line.lp = (1.0f - damp) * y + damp * line.lp;                // damping dentro del lazo
                        const float s = x + fb * line.lp;
                        line.buf[(size_t) line.w] = s;
                        line.w = (line.w + 1) % len;
                        wet += s;
                    }
                    wet *= norm / (float) voices;
                    wet = std::tanh(wet);                                            // que el feedback alto no pique
                    io[c][i] = out * (x + p.mix * (wet - x));
                }
            }
        }

    private:
        struct Line { std::vector<float> buf; int w = 0; float lp = 0.0f; };
        double sr = 48000.0;
        std::array<std::array<Line, maxVoices>, 2> lines;
        std::array<double, 2 * maxVoices> smoothDelay {};
        double phase = 0.0;
    };
}
