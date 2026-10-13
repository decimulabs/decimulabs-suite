#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>
#include "BandAverager.h"

/**
    Una canción de referencia ya decodificada y analizada: audio estéreo al sample
    rate del host (resampleado al cargar, así la reproducción es por índice entero) +
    todas las mediciones que el comparador necesita (loudness integrado y en el
    tiempo, pico, ancho estéreo, correlación, espectro promedio, forma de onda).

    Se construye en un hilo de fondo (`load`) y después es inmutable: el audio thread
    la lee sin tocarla.
*/
struct ReferenceTrack
{
    static constexpr int waveColumns = 1200;
    static constexpr double maxSeconds = 15.0 * 60.0;

    juce::File file;
    juce::String displayName;

    juce::AudioBuffer<float> audio;     // 2 canales
    double sampleRate = 0.0;
    double lengthSeconds = 0.0;
    bool truncated = false;

    float integratedLufs = -100.0f;
    float peakDb = -100.0f;
    float widthPct = 0.0f;              // % de energía lateral (0 = mono)
    float correlation = 1.0f;

    std::vector<float> shortTermLufs;   // 1 valor cada 100ms
    std::array<float, VersusBands::numBands> bandDb {};
    std::vector<float> wavePeaks;       // (min, max) por columna

    float shortTermAt(double seconds) const;

    /** Decodifica, resamplea a targetSampleRate y analiza. Devuelve nullptr (y llena
        `error`) si falla o si shouldCancel() devuelve true. Bloqueante: llamar desde un
        hilo de fondo. */
    static std::shared_ptr<ReferenceTrack> load(const juce::File& file,
                                                double targetSampleRate,
                                                juce::String& error,
                                                const std::function<bool()>& shouldCancel);
};
