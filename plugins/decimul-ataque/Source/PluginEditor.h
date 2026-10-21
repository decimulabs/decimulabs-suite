#pragma once

#include "PluginProcessor.h"
#include "TransientView.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLadderMeter.h>
#include <DecimuVuArc.h>
#include <DecimuTooltips.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
    UI de DecimuLabs Ataque (nombre de trabajo). Mismo chasis que el resto de la suite (shared/)
    con acento índigo. A la izquierda la pantalla (nivel y ganancia aplicada de los últimos
    segundos); a la derecha los dos knobs grandes (ATTACK y SUSTAIN) y SPEED; a la derecha del
    todo la salida.
*/
class DecimulAtaqueEditor : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit DecimulAtaqueEditor(DecimulAtaqueProcessor&);
    ~DecimulAtaqueEditor() override;

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
    void addKnob(Knob& k, const juce::String& paramID, const juce::String& caption);
    static void placeKnob(Knob& k, juce::Rectangle<int> cell, int maxSide);

    DecimulAtaqueProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel { DecimuAccentTheme::indigo() };

    juce::Image wordmarkImage, dogImage;
    juce::Rectangle<int> topbarRect, viewRect, knobsRect, outputRect, headerLogoArea;

    AtaqueView view;
    Knob attackKnob, sustainKnob, speedKnob;
    juce::Label hintLabel, outputLabel;

    juce::Slider outputSlider;
    juce::ToggleButton bypassButton;
    DecimuLadderMeter outputMeter;
    DecimuVuArc vuArc;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> outputAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach;

    // Textos de ayuda al pasar el puntero (inglés por defecto, botón EN/ES abajo).
    DecimuTooltips tips { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulAtaqueEditor)
};
