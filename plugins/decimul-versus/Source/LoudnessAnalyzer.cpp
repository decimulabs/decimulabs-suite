#include "LoudnessAnalyzer.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    double energyToLufs(double meanSquare)
    {
        return meanSquare > 1.0e-12 ? -0.691 + 10.0 * std::log10(meanSquare) : -100.0;
    }
}

void LoudnessAnalyzer::prepare(double sampleRate)
{
    // Etapa 1: shelf de agudos (modela la cabeza). Coeficientes de BS.1770 para
    // cualquier sample rate (misma derivación que libebur128).
    {
        const double f0 = 1681.974450955533;
        const double G = 3.999843853973347;
        const double Q = 0.7071752369554196;
        const double K = std::tan(kPi * f0 / sampleRate);
        const double Vh = std::pow(10.0, G / 20.0);
        const double Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;

        for (auto& s : shelf)
        {
            s.b0 = (Vh + Vb * K / Q + K * K) / a0;
            s.b1 = 2.0 * (K * K - Vh) / a0;
            s.b2 = (Vh - Vb * K / Q + K * K) / a0;
            s.a1 = 2.0 * (K * K - 1.0) / a0;
            s.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }

    // Etapa 2: pasa-altos RLB.
    {
        const double f0 = 38.13547087602444;
        const double Q = 0.5003270373238773;
        const double K = std::tan(kPi * f0 / sampleRate);
        const double a0 = 1.0 + K / Q + K * K;

        for (auto& h : highpass)
        {
            h.b0 = 1.0;
            h.b1 = -2.0;
            h.b2 = 1.0;
            h.a1 = 2.0 * (K * K - 1.0) / a0;
            h.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }

    blockSize = std::max(1, (int) std::lround(sampleRate * 0.1));
    reset();
}

void LoudnessAnalyzer::reset()
{
    for (auto& s : shelf) s.clear();
    for (auto& h : highpass) h.clear();

    blockCount = 0;
    blockSumSq = 0.0;
    ring.fill(0.0);
    ringWrite = 0;
    totalBlocks = 0;
    binEnergy.fill(0.0);
    binCount.fill(0);
    momentaryLufs = shortTermLufs = -100.0f;

    if (shortTermLog != nullptr)
        shortTermLog->clear();
}

void LoudnessAnalyzer::process(const float* left, const float* right, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        double l = highpass[0].process(shelf[0].process((double) left[i]));
        double r = highpass[1].process(shelf[1].process((double) right[i]));

        blockSumSq += l * l + r * r;

        if (++blockCount >= blockSize)
            finishBlock();
    }
}

void LoudnessAnalyzer::finishBlock()
{
    const double energy = blockSumSq / (double) blockCount;
    blockSumSq = 0.0;
    blockCount = 0;

    ring[(size_t) ringWrite] = energy;
    ringWrite = (ringWrite + 1) % shortTermBlocks;
    ++totalBlocks;

    auto meanOfLast = [this](int n)
    {
        double sum = 0.0;
        for (int k = 1; k <= n; ++k)
            sum += ring[(size_t) ((ringWrite - k + 2 * shortTermBlocks) % shortTermBlocks)];
        return sum / (double) n;
    };

    if (totalBlocks >= momentaryBlocks)
    {
        const double window = meanOfLast(momentaryBlocks);
        momentaryLufs = (float) energyToLufs(window);

        const double lufs = energyToLufs(window);
        if (lufs > -70.0)
        {
            const int bin = std::clamp((int) std::floor((lufs + 70.0) * 10.0), 0, numBins - 1);
            binEnergy[(size_t) bin] += window;
            ++binCount[(size_t) bin];
        }

        const int n = std::min(totalBlocks, shortTermBlocks);
        shortTermLufs = (float) energyToLufs(meanOfLast(n));
    }

    if (shortTermLog != nullptr)
        shortTermLog->push_back(shortTermLufs);
}

float LoudnessAnalyzer::getIntegratedLufs() const
{
    double sumE = 0.0;
    std::uint64_t n = 0;
    for (int i = 0; i < numBins; ++i)
    {
        sumE += binEnergy[(size_t) i];
        n += binCount[(size_t) i];
    }
    if (n == 0)
        return -100.0f;

    // Gate relativo: 10 LU por debajo del promedio de lo que pasó el gate absoluto.
    const double relativeThreshold = energyToLufs(sumE / (double) n) - 10.0;
    const int startBin = std::clamp((int) std::ceil((relativeThreshold + 70.0) * 10.0), 0, numBins - 1);

    double gatedE = 0.0;
    std::uint64_t gatedN = 0;
    for (int i = startBin; i < numBins; ++i)
    {
        gatedE += binEnergy[(size_t) i];
        gatedN += binCount[(size_t) i];
    }

    return gatedN > 0 ? (float) energyToLufs(gatedE / (double) gatedN) : -100.0f;
}
