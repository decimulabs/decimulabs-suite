#pragma once

#include <array>
#include <cstdint>
#include <vector>

/**
    Medidor de loudness ITU-R BS.1770-4 / EBU R128 (estéreo): ponderación K (shelf de
    agudos + pasa-altos), bloques de 400ms con paso de 100ms, y gating absoluto (-70
    LUFS) + relativo (-10 LU) para el valor integrado. Se usa en los dos lados del
    comparador: en vivo sobre la mezcla (audio thread) y offline sobre cada referencia
    al cargarla, así el volumen igualado es instantáneo y justo.

    No es thread-safe: cada instancia la usa un solo hilo.
*/
class LoudnessAnalyzer
{
public:
    void prepare(double sampleRate);
    void reset();

    void process(const float* left, const float* right, int numSamples);

    /** Integrado con gating; -100 si todavía no hay bloques válidos. */
    float getIntegratedLufs() const;
    float getMomentaryLufs() const { return momentaryLufs; }   // ventana 400ms
    float getShortTermLufs() const { return shortTermLufs; }   // ventana 3s

    /** Si no es nullptr, en cada bloque de 100ms se agrega el short-term acá
        (para armar la curva de loudness en el tiempo de una referencia). */
    std::vector<float>* shortTermLog = nullptr;

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        double process(double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() { z1 = z2 = 0.0; }
    };

    void finishBlock();

    static constexpr int shortTermBlocks = 30;      // 3s / 100ms
    static constexpr int momentaryBlocks = 4;       // 400ms / 100ms
    static constexpr int numBins = 900;             // -70 .. +20 LUFS, 0.1 LU por bin

    Biquad shelf[2], highpass[2];
    int blockSize = 4800;
    int blockCount = 0;
    double blockSumSq = 0.0;

    std::array<double, shortTermBlocks> ring {};
    int ringWrite = 0;
    int totalBlocks = 0;

    std::array<double, numBins> binEnergy {};
    std::array<std::uint32_t, numBins> binCount {};

    float momentaryLufs = -100.0f;
    float shortTermLufs = -100.0f;
};
