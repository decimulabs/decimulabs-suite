// Test del plugin sin DAW: carga cada sonido de fábrica, toca un Do y mide que suene,
// que se apague al soltar y que dos copias no se pisen. cmake -DDECIMU_FM_TESTS=ON
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/PluginProcessor.h"
#include "../Source/FactoryPresets.h"
#include <cstdio>
#include <cmath>

static float peakOf(const juce::AudioBuffer<float>& b) { return b.getMagnitude(0, 0, b.getNumSamples()); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    int fails = 0;
    auto check = [&](bool ok, const char* what, double v) { std::printf("%s %s (%.4f)\n", ok ? "OK   " : "FALLA", what, v); if (!ok) ++fails; };

    DecimulFMProcessor a, b;
    const double sr = 48000.0; const int bs = 512;
    a.prepareToPlay(sr, bs); b.prepareToPlay(sr, bs);
    juce::AudioBuffer<float> buf(2, bs);

    for (int pr = 0; pr < a.getNumPrograms(); ++pr)
    {
        a.setCurrentProgram(pr); a.prepareToPlay(sr, bs);
        float on = 0, off = 0;
        for (int blk = 0; blk < 900; ++blk)
        {
            juce::MidiBuffer m;
            if (blk == 0) m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 10);
            if (blk == 20) m.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            buf.clear(); a.processBlock(buf, m);
            if (blk < 20) on = std::max(on, peakOf(buf));
            if (blk >= 890) off = std::max(off, peakOf(buf));
        }
        std::printf("%-18s ", a.getProgramName(pr).toRawUTF8());
        check(on > 0.05f && off < 0.001f, "suena y se apaga", on);
    }

    // dos copias: b en silencio mientras a toca
    a.setCurrentProgram(0); a.prepareToPlay(sr, bs);
    float pa = 0, pb = 0;
    for (int blk = 0; blk < 20; ++blk)
    {
        juce::MidiBuffer m, none;
        if (blk == 0) m.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8) 100), 0);
        buf.clear(); a.processBlock(buf, m); pa = std::max(pa, peakOf(buf));
        buf.clear(); b.processBlock(buf, none); pb = std::max(pb, peakOf(buf));
    }
    check(pa > 0.05f && pb == 0.0f, "dos copias independientes", pb);

    // estado: guardar y recuperar
    juce::MemoryBlock st; a.setCurrentProgram(3); a.getStateInformation(st);
    b.setStateInformation(st.getData(), (int) st.getSize());
    check(b.getCurrentProgram() == 3 && b.apvts.getRawParameterValue("op1_mul")->load() == (float) factory::presets[3].ops[0][7], "guarda y recupera el sonido", b.apvts.getRawParameterValue("op1_mul")->load());

    std::printf(fails ? "HAY FALLAS\n" : "TODO OK\n");
    return fails ? 1 : 0;
}
