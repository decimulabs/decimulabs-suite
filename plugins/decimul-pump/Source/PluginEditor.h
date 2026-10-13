#pragma once

#include "PluginProcessor.h"
#include "CurveEditor.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLadderMeter.h>
#include <DecimuVuArc.h>
#include <DecimuPillButton.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
    UI de DecimuLabs Pump. Mismo chasis que el resto de la suite (shared/) con acento
    verde lima. La pieza central es el editor de la curva; a la derecha, los controles
    de profundidad/tiempo y, abajo, el modo de disparo (Sync / MIDI / Audio) con sus
    parámetros, y la banda sobre la que actúa el bombeo (Full / Low / High).
*/
class DecimulPumpEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit DecimulPumpEditor(DecimulPumpProcessor&);
    ~DecimulPumpEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void setParam(const juce::String& id, float plainValue);
    void addKnob(Knob& k, const juce::String& paramID, const juce::String& caption);
    static void placeKnob(Knob& k, juce::Rectangle<int> cell);
    void refreshCurveFromProcessor();
    void applyPreset(int presetIndex);

    DecimulPumpProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel { DecimuAccentTheme::limeGreen() };

    juce::Image wordmarkImage, dogImage;
    juce::Rectangle<int> topbarRect, curveRect, knobsRect, triggerRect, outputRect, headerLogoArea;

    PumpCurveEditor curveEditor;
    juce::ComboBox presetCombo, rateCombo;
    juce::Label presetLabel, rateLabel;
    juce::TextButton prevPresetButton { "<" }, nextPresetButton { ">" };

    DecimuPillButton trigSyncPill { "SYNC" }, trigMidiPill { "MIDI" }, trigAudioPill { "AUDIO" };
    DecimuPillButton bandFullPill { "FULL" }, bandLowPill { "LOW" }, bandHighPill { "HIGH" };

    Knob depthKnob, mixKnob, smoothKnob, nudgeKnob, spreadKnob, tempoKnob;
    Knob noteKnob, thresholdKnob, crossoverKnob;
    juce::Label triggerLabel, bandLabel, hintLabel, outputLabel;

    juce::Slider outputSlider;
    juce::ToggleButton bypassButton;
    DecimuLadderMeter outputMeter;
    DecimuVuArc vuArc;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SliderAttachment> outputAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach;
    std::unique_ptr<ComboAttachment> rateAttach;

    int shownPreset = -2;
    int lastCurveVersion = -1;
    bool updatingPresetCombo = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulPumpEditor)
};
