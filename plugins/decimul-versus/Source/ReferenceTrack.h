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
    todas las mediciones que el comparador necesita (loudness integrado, en el tiempo y
    rango, pico y true peak, ancho estéreo, correlación, espectro promedio, espectro
    por frame para el analizador en tiempo real y la alineación de secciones, forma de
    onda).

    Se construye en un hilo de fondo (`load`) y después es inmutable: el audio thread
    la lee sin tocarla. Para ahorrar memoria, el audio decodificado se puede soltar
    (`makeLite`) conservando todo el análisis; se vuelve a cargar cuando el slot se usa.
*/
struct ReferenceTrack
{
    static constexpr int waveColumns = 1200;
    static constexpr double maxSeconds = 15.0 * 60.0;
    static constexpr int frameHop = BandAverager::hopSize;      // samples entre frames de espectro
    static constexpr int frameWindow = BandAverager::fftSize;

    juce::File file;
    juce::String displayName;

    juce::AudioBuffer<float> audio;     // 2 canales; vacío si el audio no está residente
    double sampleRate = 0.0;
    double lengthSeconds = 0.0;
    bool truncated = false;

    float integratedLufs = -100.0f;
    float loudnessRange = 0.0f;         // LRA, LU
    float peakDb = -100.0f;             // pico de muestras
    float truePeakDb = -100.0f;         // dBTP
    float widthPct = 0.0f;              // % de energía lateral (0 = mono)
    float correlation = 1.0f;

    std::vector<float> shortTermLufs;   // 1 valor cada 100ms
    std::array<float, VersusBands::numBands> bandDb {};
    std::vector<float> wavePeaks;       // (min, max) por columna

    // Espectro por frame (frameHop samples), dB por banda; -120 = silencio.
    std::vector<float> frameDb;
    int frameCount = 0;

    bool hasAudio() const { return audio.getNumSamples() > 0; }

    float shortTermAt(double seconds) const;

    /** dB por banda del frame que cubre `seconds`, o nullptr si no hay frames. */
    const float* frameAt(double seconds) const;

    /** Copia de todo el análisis sin el audio decodificado. */
    std::shared_ptr<ReferenceTrack> makeLite() const;

    /** Decodifica, resamplea a targetSampleRate y analiza. Devuelve nullptr (y llena
        `error`) si falla o si shouldCancel() devuelve true. Bloqueante: llamar desde un
        hilo de fondo. */
    static std::shared_ptr<ReferenceTrack> load(const juce::File& file,
                                                double targetSampleRate,
                                                juce::String& error,
                                                const std::function<bool()>& shouldCancel);
};
