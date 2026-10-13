// Test de integración del motor de Versus (sin UI, sin host): genera referencias WAV
// sintéticas, las carga por el camino real (decodificación + resampleo + análisis) y
// verifica loudness, igualado A/B y continuidad del crossfade.
//
// Compilar con: cmake -DDECIMU_VERSUS_TESTS=ON y correr VersusEngineTest.exe.

#include "../Source/PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <iostream>

namespace
{
    int failures = 0;

    void check(bool ok, const juce::String& what, const juce::String& detail = {})
    {
        std::cout << (ok ? "[ OK ] " : "[FAIL] ") << what << (detail.isNotEmpty() ? "  -> " + detail : juce::String()) << std::endl;
        if (!ok)
            ++failures;
    }

    juce::File writeSine(const juce::String& name, double sampleRate, double freq, float peakDb, double seconds,
                         bool mono = false)
    {
        auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
        file.deleteFile();

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(stream.get(), sampleRate, mono ? 1u : 2u, 24, {}, 0));
        stream.release();

        const int n = (int) (seconds * sampleRate);
        juce::AudioBuffer<float> buf(mono ? 1 : 2, n);
        const float amp = juce::Decibels::decibelsToGain(peakDb);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < n; ++i)
                buf.setSample(ch, i, amp * (float) std::sin(2.0 * juce::MathConstants<double>::pi * freq * i / sampleRate));

        writer->writeFromAudioSampleBuffer(buf, 0, n);
        return file;
    }

    bool waitForLoad(DecimulVersusProcessor& p, int slot)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + 30000;
        while (juce::Time::getMillisecondCounter() < deadline)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            const auto info = p.getSlotInfo(slot);
            if (info.loaded)
                return true;
            if (!info.loading && info.error.isNotEmpty())
                return false;
        }
        return false;
    }

    void setParam(DecimulVersusProcessor& p, const juce::String& id, float plain)
    {
        auto* param = p.apvts.getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    }

    // Procesa `seconds` de una senoide como "mezcla"; devuelve el pico del último bloque.
    float run(DecimulVersusProcessor& p, double sampleRate, double freq, float peakDb, double seconds,
              float* maxJump = nullptr)
    {
        constexpr int block = 512;
        const float amp = juce::Decibels::decibelsToGain(peakDb);
        juce::AudioBuffer<float> buf(2, block);
        juce::MidiBuffer midi;
        static double phase = 0.0;
        float lastPeak = 0.0f, prev = 0.0f;
        const int blocks = (int) (seconds * sampleRate / block);

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                const float v = amp * (float) std::sin(phase);
                phase += 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
                buf.setSample(0, i, v);
                buf.setSample(1, i, v);
            }
            p.processBlock(buf, midi);

            if (maxJump != nullptr)
                for (int i = 0; i < block; ++i)
                {
                    *maxJump = std::max(*maxJump, std::abs(buf.getSample(0, i) - prev));
                    prev = buf.getSample(0, i);
                }

            lastPeak = buf.getMagnitude(0, block);
        }
        return juce::Decibels::gainToDecibels(lastPeak, -100.0f);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr double hostRate = 48000.0;

    // Referencia 1: 1kHz a -10 dBFS pico, estéreo en fase, a 44.1k (fuerza el resampleo).
    // BS.1770: pico -10 -> rms -13.01 por canal; dos canales en fase suman +3.01 dB ->
    // -10.0 LUFS (la ponderación K vale ~0 dB a 1 kHz).
    const auto refA = writeSine("versus_test_refA.wav", 44100.0, 1000.0, -10.0f, 12.0);
    const auto refB = writeSine("versus_test_refB.wav", 44100.0, 250.0, -6.0f, 12.0, true);

    DecimulVersusProcessor p;
    p.setRateAndBufferSizeDetails(hostRate, 512);
    p.prepareToPlay(hostRate, 512);

    p.loadReference(0, refA);
    check(waitForLoad(p, 0), "carga de la referencia A (wav 44.1k -> 48k)");

    auto trackA = p.getTrack(0);
    check(trackA != nullptr, "la pista A quedó en el slot");
    if (trackA == nullptr)
        return 1;

    check(std::abs(trackA->integratedLufs - (-10.0f)) < 0.3f, "LUFS integrado de A ~ -10.0",
          juce::String(trackA->integratedLufs, 2));
    check(std::abs(trackA->peakDb - (-10.0f)) < 0.3f, "pico de A ~ -10 dBFS", juce::String(trackA->peakDb, 2));
    check(trackA->widthPct < 1.0f && trackA->correlation > 0.99f, "A es mono-compatible (ancho ~0, corr ~1)",
          juce::String(trackA->widthPct, 2) + " / " + juce::String(trackA->correlation, 3));
    check(std::abs(trackA->lengthSeconds - 12.0) < 0.05, "duración de A ~ 12 s", juce::String(trackA->lengthSeconds, 3));

    int peakBand = 0;
    for (int i = 1; i < VersusBands::numBands; ++i)
        if (trackA->bandDb[(size_t) i] > trackA->bandDb[(size_t) peakBand])
            peakBand = i;
    check(std::abs(VersusBands::centres[(size_t) peakBand] - 1000.0f) < 1.0f, "el espectro de A pica en la banda de 1 kHz",
          juce::String(VersusBands::centres[(size_t) peakBand]));

    p.loadReference(1, refB);
    check(waitForLoad(p, 1), "carga de la referencia B (wav mono 44.1k)");

    // ---- Igualado A/B ----
    // Mezcla: 1kHz a -23 dBFS pico -> -23.0 LUFS. Con match Auto, la referencia debe
    // bajar -13 dB (de -10 a -23 LUFS), o sea pico -23 dBFS.
    setParam(p, ParamIDs::slot, 1.0f);
    setParam(p, ParamIDs::refActive, 0.0f);
    p.setUserPlaying(true);

    const float aPeak = run(p, hostRate, 1000.0, -23.0f, 4.0);
    check(std::abs(aPeak - (-23.0f)) < 0.3f, "A (mezcla) pasa sin cambios", juce::String(aPeak, 2));
    check(std::abs(p.mixIntegratedLufs.load() - (-23.0f)) < 0.4f, "LUFS integrado de la mezcla ~ -23.0",
          juce::String(p.mixIntegratedLufs.load(), 2));

    setParam(p, ParamIDs::refActive, 1.0f);
    float jump = 0.0f;
    run(p, hostRate, 1000.0, -23.0f, 1.0, &jump);          // transición A -> B (y el ramp de ganancia)
    const float bPeak = run(p, hostRate, 1000.0, -23.0f, 3.0);
    check(std::abs(bPeak - (-23.0f)) < 0.7f, "B suena al mismo volumen que la mezcla (igualado por LUFS)",
          juce::String(bPeak, 2) + " dBFS, ganancia aplicada " + juce::String(p.appliedRefGainDb.load(), 2) + " dB");
    check(std::abs(p.appliedRefGainDb.load() - (-13.0f)) < 0.7f, "ganancia aplicada a la referencia ~ -13 dB",
          juce::String(p.appliedRefGainDb.load(), 2));
    check(jump < 0.1f, "el crossfade A->B no tiene saltos bruscos", juce::String(jump, 4));

    // ---- Trim manual ----
    setParam(p, ParamIDs::trim, 6.0f);
    run(p, hostRate, 1000.0, -23.0f, 2.0);
    check(std::abs(p.appliedRefGainDb.load() - (-7.0f)) < 0.7f, "trim +6 dB suma a la ganancia automática",
          juce::String(p.appliedRefGainDb.load(), 2));
    setParam(p, ParamIDs::trim, 0.0f);

    // ---- Snapshot de la UI (para revisar el layout sin abrir un host) ----
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        editor->setSize(1120, 740);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(400);

        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        const auto out = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("versus_ui.png");
        out.deleteFile();
        juce::PNGImageFormat png;
        juce::FileOutputStream stream(out);
        check(stream.openedOk() && png.writeImageToStream(image, stream), "snapshot de la UI", out.getFullPathName());
    }

    // ---- Cambio de slot en caliente (sin clicks) ----
    float slotJump = 0.0f;
    setParam(p, ParamIDs::slot, 2.0f);
    run(p, hostRate, 1000.0, -23.0f, 1.0, &slotJump);
    check(slotJump < 0.12f, "cambiar de slot mientras suena no hace clicks", juce::String(slotJump, 4));

    // ---- Match apagado: la referencia suena a su volumen natural ----
    setParam(p, ParamIDs::slot, 1.0f);
    setParam(p, ParamIDs::matchMode, 1.0f);
    run(p, hostRate, 1000.0, -23.0f, 2.0);
    check(std::abs(p.appliedRefGainDb.load()) < 0.5f, "con match Off la ganancia es 0 dB",
          juce::String(p.appliedRefGainDb.load(), 2));

    // ---- Estado: las rutas de las referencias sobreviven al guardado ----
    juce::MemoryBlock state;
    p.getStateInformation(state);

    DecimulVersusProcessor p2;
    p2.setRateAndBufferSizeDetails(hostRate, 512);
    p2.prepareToPlay(hostRate, 512);
    p2.setStateInformation(state.getData(), (int) state.getSize());
    check(waitForLoad(p2, 0), "al restaurar el estado se recarga la referencia del slot 1");
    check(p2.getSlotInfo(1).file == refB || waitForLoad(p2, 1), "...y la del slot 2");

    std::cout << (failures == 0 ? "\nTODO OK" : "\nHAY FALLAS: " + juce::String(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
