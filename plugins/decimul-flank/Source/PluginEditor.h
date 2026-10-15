#pragma once

#include "PluginProcessor.h"
#include "StereoField.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLadderMeter.h>
#include <DecimuVuArc.h>
#include <DecimuPillButton.h>
#include <DecimuTooltips.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
    UI de DecimuLabs Flank (nombre de trabajo). Mismo chasis que el resto de la suite
    (shared/) con acento verde menta. A la izquierda el campo estéreo (goniómetro, las
    posiciones de la voz y las copias y la correlación); a la derecha los knobs
    (detune / retardo / offset / spread, y mix / cortes / space), el modo y el chequeo
    en mono; a la derecha del todo la salida.
*/
class DecimulFlankEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit DecimulFlankEditor(DecimulFlankProcessor&);
    ~DecimulFlankEditor() override;

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

    DecimulFlankProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel { DecimuAccentTheme::tealMint() };

    juce::Image wordmarkImage, dogImage;
    juce::Rectangle<int> topbarRect, fieldRect, knobsRect, outputRect, headerLogoArea;

    FlankStereoField field;
    Knob detuneKnob, delayKnob, offsetKnob, spreadKnob;
    Knob mixKnob, lowCutKnob, highCutKnob, spaceKnob;

    DecimuPillButton staticPill { "STATIC" }, driftPill { "DRIFT" }, monoPill { "MONO CHECK" };
    juce::Label modeLabel, hintLabel, outputLabel;

    juce::Slider outputSlider;
    juce::ToggleButton bypassButton;
    DecimuLadderMeter outputMeter;
    DecimuVuArc vuArc;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> outputAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach, monoAttach;

    // Textos de ayuda al pasar el puntero (inglés por defecto, botón EN/ES abajo).
    DecimuTooltips tips { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulFlankEditor)
};
