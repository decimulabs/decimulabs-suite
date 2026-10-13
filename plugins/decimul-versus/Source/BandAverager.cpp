#include "BandAverager.h"
#include <algorithm>
#include <cmath>

BandAverager::BandAverager()
{
    ring.assign((size_t) fftSize, 0.0f);
    fftData.assign((size_t) fftSize * 2, 0.0f);
    binToBand.assign((size_t) fftSize / 2 + 1, -1);
    extraBin.fill(-1);
    prepare(48000.0);
}

void BandAverager::prepare(double sampleRate)
{
    binToBand.assign((size_t) fftSize / 2 + 1, -1);
    extraBin.fill(-1);

    const double sixthOctave = std::pow(2.0, 1.0 / 6.0);
    std::array<int, VersusBands::numBands> binsInBand {};

    for (int k = 1; k <= fftSize / 2; ++k)
    {
        const double f = (double) k * sampleRate / (double) fftSize;
        for (int b = 0; b < VersusBands::numBands; ++b)
        {
            const double fc = (double) VersusBands::centres[(size_t) b];
            if (f >= fc / sixthOctave && f < fc * sixthOctave)
            {
                binToBand[(size_t) k] = b;
                ++binsInBand[(size_t) b];
                break;
            }
        }
    }

    // Bandas graves que caen entre dos bins: usan el bin más cercano.
    for (int b = 0; b < VersusBands::numBands; ++b)
    {
        if (binsInBand[(size_t) b] == 0)
        {
            const double fc = (double) VersusBands::centres[(size_t) b];
            extraBin[(size_t) b] = std::clamp((int) std::lround(fc * (double) fftSize / sampleRate), 1, fftSize / 2);
        }
    }

    reset();
}

void BandAverager::reset()
{
    std::fill(ring.begin(), ring.end(), 0.0f);
    writePos = 0;
    samplesSinceFrame = 0;
    samplesPushed = 0;
    sumPower.fill(0.0);
    frames = 0;
}

void BandAverager::push(const float* mono, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        ring[(size_t) writePos] = mono[i];
        writePos = (writePos + 1) % fftSize;

        if (samplesPushed < fftSize)
            ++samplesPushed;

        if (++samplesSinceFrame >= hopSize && samplesPushed >= fftSize)
        {
            samplesSinceFrame = 0;
            processFrame();
        }
    }
}

void BandAverager::processFrame()
{
    // Ring -> fftData en orden temporal (el más viejo primero).
    double meanSquare = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        const float s = ring[(size_t) ((writePos + i) % fftSize)];
        fftData[(size_t) i] = s;
        meanSquare += (double) s * (double) s;
    }
    meanSquare /= (double) fftSize;

    if (meanSquare < 1.0e-7) // por debajo de ~-70 dBFS: silencio, no cuenta
        return;

    std::fill(fftData.begin() + fftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable(fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform(fftData.data());

    // Hann: ganancia coherente 0.5 -> amplitud = mag * 4 / N. Potencia = amp^2 / 2.
    const double norm = 4.0 / (double) fftSize;
    std::array<double, VersusBands::numBands> framePower {};

    for (int k = 1; k <= fftSize / 2; ++k)
    {
        const int b = binToBand[(size_t) k];
        if (b >= 0)
        {
            const double amp = (double) fftData[(size_t) k] * norm;
            framePower[(size_t) b] += 0.5 * amp * amp;
        }
    }

    for (int b = 0; b < VersusBands::numBands; ++b)
    {
        const int k = extraBin[(size_t) b];
        if (k > 0)
        {
            const double amp = (double) fftData[(size_t) k] * norm;
            framePower[(size_t) b] += 0.5 * amp * amp;
        }
        sumPower[(size_t) b] += framePower[(size_t) b];
    }

    ++frames;
}

bool BandAverager::getAverageDb(std::array<float, VersusBands::numBands>& out) const
{
    if (frames == 0)
        return false;

    for (int b = 0; b < VersusBands::numBands; ++b)
    {
        const double mean = sumPower[(size_t) b] / (double) frames;
        out[(size_t) b] = (float) (10.0 * std::log10(std::max(mean, 1.0e-12)));
    }
    return true;
}
