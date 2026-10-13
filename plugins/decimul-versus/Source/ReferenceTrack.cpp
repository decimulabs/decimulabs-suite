#include "ReferenceTrack.h"
#include "LoudnessAnalyzer.h"
#include "TruePeakMeter.h"
#include <algorithm>
#include <cmath>

float ReferenceTrack::shortTermAt(double seconds) const
{
    if (shortTermLufs.empty())
        return -100.0f;

    const auto index = (size_t) std::clamp((int) std::floor(seconds * 10.0), 0, (int) shortTermLufs.size() - 1);
    return shortTermLufs[index];
}

const float* ReferenceTrack::frameAt(double seconds) const
{
    if (frameCount <= 0 || sampleRate <= 0.0)
        return nullptr;

    // El frame j cubre [j*hop, j*hop + window): el que está centrado en `seconds`.
    const double centreSample = seconds * sampleRate;
    const int j = std::clamp((int) std::floor((centreSample - frameWindow * 0.5) / frameHop + 0.5),
                             0, frameCount - 1);
    return frameDb.data() + (size_t) j * VersusBands::numBands;
}

std::shared_ptr<ReferenceTrack> ReferenceTrack::makeLite() const
{
    auto lite = std::make_shared<ReferenceTrack>();
    lite->file = file;
    lite->displayName = displayName;
    lite->sampleRate = sampleRate;
    lite->lengthSeconds = lengthSeconds;
    lite->truncated = truncated;
    lite->integratedLufs = integratedLufs;
    lite->loudnessRange = loudnessRange;
    lite->peakDb = peakDb;
    lite->truePeakDb = truePeakDb;
    lite->widthPct = widthPct;
    lite->correlation = correlation;
    lite->shortTermLufs = shortTermLufs;
    lite->bandDb = bandDb;
    lite->wavePeaks = wavePeaks;
    lite->frameDb = frameDb;
    lite->frameCount = frameCount;
    return lite;   // audio vacío a propósito
}

std::shared_ptr<ReferenceTrack> ReferenceTrack::load(const juce::File& file,
                                                     double targetSampleRate,
                                                     juce::String& error,
                                                     const std::function<bool()>& shouldCancel)
{
    error.clear();

    if (!file.existsAsFile())
    {
        error = "No existe el archivo";
        return nullptr;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        error = "Formato no soportado";
        return nullptr;
    }

    auto track = std::make_shared<ReferenceTrack>();
    track->file = file;
    track->displayName = file.getFileNameWithoutExtension();

    const double sourceRate = reader->sampleRate;
    auto sourceLength = (juce::int64) reader->lengthInSamples;
    const auto maxSourceLength = (juce::int64) (maxSeconds * sourceRate);
    if (sourceLength > maxSourceLength)
    {
        sourceLength = maxSourceLength;
        track->truncated = true;
    }

    // --- Decodificación (en trozos, para poder cancelar) ---
    const int padding = 64;
    juce::AudioBuffer<float> source(2, (int) sourceLength + padding);
    source.clear();

    const int chunk = 1 << 18;
    for (juce::int64 pos = 0; pos < sourceLength; pos += chunk)
    {
        if (shouldCancel())
            return nullptr;

        const int n = (int) std::min<juce::int64>(chunk, sourceLength - pos);
        reader->read(&source, (int) pos, n, pos, true, reader->numChannels > 1);
    }

    if (reader->numChannels == 1)
        source.copyFrom(1, 0, source, 0, 0, (int) sourceLength);

    // --- Resampleo al rate del host ---
    if (std::abs(sourceRate - targetSampleRate) > 0.5)
    {
        const double ratio = sourceRate / targetSampleRate;
        const int outLength = (int) std::ceil((double) sourceLength / ratio);
        track->audio.setSize(2, outLength);

        for (int ch = 0; ch < 2; ++ch)
        {
            if (shouldCancel())
                return nullptr;

            juce::LagrangeInterpolator interpolator;
            interpolator.process(ratio, source.getReadPointer(ch), track->audio.getWritePointer(ch), outLength);
        }
    }
    else
    {
        track->audio.setSize(2, (int) sourceLength);
        for (int ch = 0; ch < 2; ++ch)
            track->audio.copyFrom(ch, 0, source, ch, 0, (int) sourceLength);
    }

    track->sampleRate = targetSampleRate;
    const int length = track->audio.getNumSamples();
    track->lengthSeconds = (double) length / targetSampleRate;

    const float* left = track->audio.getReadPointer(0);
    const float* right = track->audio.getReadPointer(1);

    // --- Pico, ancho estéreo, correlación, forma de onda ---
    float peak = 0.0f;
    double sumLL = 0.0, sumRR = 0.0, sumLR = 0.0, sumM = 0.0, sumS = 0.0;

    track->wavePeaks.assign((size_t) waveColumns * 2, 0.0f);
    for (int c = 0; c < waveColumns; ++c)
    {
        const int from = (int) ((juce::int64) c * length / waveColumns);
        const int to = std::max(from + 1, (int) ((juce::int64) (c + 1) * length / waveColumns));
        float lo = 0.0f, hi = 0.0f;
        for (int i = from; i < std::min(to, length); ++i)
        {
            lo = std::min({ lo, left[i], right[i] });
            hi = std::max({ hi, left[i], right[i] });
        }
        track->wavePeaks[(size_t) c * 2] = lo;
        track->wavePeaks[(size_t) c * 2 + 1] = hi;
    }

    for (int i = 0; i < length; ++i)
    {
        if ((i & 0xFFFF) == 0 && shouldCancel())
            return nullptr;

        const float l = left[i], r = right[i];
        peak = std::max({ peak, std::abs(l), std::abs(r) });

        if ((double) l * l + (double) r * r > 1.0e-8)
        {
            sumLL += (double) l * l;
            sumRR += (double) r * r;
            sumLR += (double) l * r;
            const double m = 0.5 * ((double) l + r), s = 0.5 * ((double) l - r);
            sumM += m * m;
            sumS += s * s;
        }
    }

    track->peakDb = juce::Decibels::gainToDecibels(peak, -100.0f);

    // --- True peak (4x, BS.1770-4) ---
    {
        TruePeakMeter truePeak;
        float tp = 0.0f;
        const int block = 8192;
        for (int pos = 0; pos < length; pos += block)
        {
            if (shouldCancel())
                return nullptr;
            tp = std::max(tp, truePeak.process(left + pos, right + pos, std::min(block, length - pos)));
        }
        track->truePeakDb = juce::Decibels::gainToDecibels(std::max(tp, peak), -100.0f);
    }
    track->widthPct = (sumM + sumS) > 0.0 ? (float) (100.0 * sumS / (sumM + sumS)) : 0.0f;
    track->correlation = (sumLL > 0.0 && sumRR > 0.0) ? (float) (sumLR / std::sqrt(sumLL * sumRR)) : 1.0f;

    // --- Loudness (integrado + curva en el tiempo) ---
    {
        LoudnessAnalyzer loudness;
        loudness.prepare(targetSampleRate);
        loudness.shortTermLog = &track->shortTermLufs;

        const int block = 8192;
        for (int pos = 0; pos < length; pos += block)
        {
            if (shouldCancel())
                return nullptr;
            loudness.process(left + pos, right + pos, std::min(block, length - pos));
        }
        track->integratedLufs = loudness.getIntegratedLufs();
        track->loudnessRange = loudness.getLoudnessRange();
    }

    // --- Espectro promedio ---
    {
        BandAverager bands;
        bands.prepare(targetSampleRate);

        // Espectro de cada frame, para el analizador en tiempo real y la alineación.
        bands.onFrame = [&track](const std::array<float, VersusBands::numBands>& db, bool)
        {
            track->frameDb.insert(track->frameDb.end(), db.begin(), db.end());
            ++track->frameCount;
        };

        std::vector<float> mono((size_t) 8192);
        for (int pos = 0; pos < length; pos += 8192)
        {
            if (shouldCancel())
                return nullptr;

            const int n = std::min(8192, length - pos);
            for (int i = 0; i < n; ++i)
                mono[(size_t) i] = 0.5f * (left[pos + i] + right[pos + i]);
            bands.push(mono.data(), n);
        }

        if (!bands.getAverageDb(track->bandDb))
            track->bandDb.fill(-120.0f);
    }

    return track;
}
