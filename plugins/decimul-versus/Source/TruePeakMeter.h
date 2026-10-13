#pragma once

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>

/**
    Pico real ("true peak", dBTP, BS.1770-4 Anexo 2): sobremuestrea 4x con un filtro FIR
    y toma el máximo. Detecta los picos entre muestras que el pico de muestras no ve y
    que pueden clipear en la conversión D/A o en un codec.

    Estéreo. Se alimenta por bloques de cualquier tamaño (se parte internamente en
    trozos del máximo preparado). No es thread-safe: un hilo por instancia.
*/
class TruePeakMeter
{
public:
    static constexpr int maxChunk = 2048;

    TruePeakMeter()
        : oversampling(2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, false)
    {
        oversampling.initProcessing((size_t) maxChunk);
    }

    void reset() { oversampling.reset(); }

    /** Devuelve el true peak LINEAL máximo del bloque. */
    float process(const float* left, const float* right, int numSamples)
    {
        float peak = 0.0f;

        for (int start = 0; start < numSamples; start += maxChunk)
        {
            const int n = std::min(maxChunk, numSamples - start);
            const float* channels[2] = { left + start, right + start };
            juce::dsp::AudioBlock<const float> input(channels, 2, (size_t) n);

            auto up = oversampling.processSamplesUp(input);
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            {
                const float* data = up.getChannelPointer(ch);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                    peak = std::max(peak, std::abs(data[i]));
            }
        }

        return peak;
    }

private:
    juce::dsp::Oversampling<float> oversampling;
};
