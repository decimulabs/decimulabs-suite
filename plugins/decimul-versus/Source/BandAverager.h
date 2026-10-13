#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

namespace VersusBands
{
    constexpr int numBands = 30;

    // Centros de 1/3 de octava (ISO), 25Hz - 20kHz.
    inline constexpr std::array<float, numBands> centres {
        25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f, 125.0f, 160.0f, 200.0f,
        250.0f, 315.0f, 400.0f, 500.0f, 630.0f, 800.0f, 1000.0f, 1250.0f, 1600.0f, 2000.0f,
        2500.0f, 3150.0f, 4000.0f, 5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f, 20000.0f
    };
}

/**
    Espectro PROMEDIO de largo plazo en 30 bandas de 1/3 de octava: acumula la potencia
    de cada frame (FFT de 8192, Hann, 50% de solape) y devuelve el promedio en dB. Se
    usa igual en los dos lados del comparador (en vivo sobre la mezcla, offline sobre
    cada referencia), así que las curvas son directamente comparables.

    Los frames casi silenciosos se descartan para que los huecos no tiren el promedio.
    Los valores son dB relativos (no una calibración absoluta de dBFS): lo que importa
    es la diferencia entre mezcla y referencia, que pasa por el mismo camino.

    No es thread-safe: cada instancia la usa un solo hilo.
*/
class BandAverager
{
public:
    static constexpr int fftOrder = 13;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = fftSize / 2;

    BandAverager();

    void prepare(double sampleRate);
    void reset();

    void push(const float* mono, int numSamples);

    /** false si todavía no hubo ningún frame válido. */
    bool getAverageDb(std::array<float, VersusBands::numBands>& out) const;
    int getFrameCount() const { return frames; }

private:
    void processFrame();

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };

    std::vector<float> ring;        // últimos fftSize samples (circular)
    std::vector<float> fftData;     // 2 * fftSize
    int writePos = 0;
    int samplesSinceFrame = 0;
    int samplesPushed = 0;

    std::vector<int> binToBand;                         // por bin; -1 = ninguna
    std::array<int, VersusBands::numBands> extraBin {}; // bandas sin ningún bin propio
    std::array<double, VersusBands::numBands> sumPower {};
    int frames = 0;
};
