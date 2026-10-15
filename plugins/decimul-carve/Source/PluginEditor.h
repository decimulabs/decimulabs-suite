#pragma once

#include "PluginProcessor.h"
#include "SpectrumDisplay.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLadderMeter.h>
#include <DecimuVuArc.h>
#include <DecimuTooltips.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
    UI de DecimuLabs Carve. Mismo chasis que el resto de la suite (shared/): la pieza
    central es la pantalla de 32 bandas (sidechain abajo, recorte arriba), a la derecha
    los controles y a la izquierda del todo la salida.
*/
class DecimulCarveEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit DecimulCarveEditor(DecimulCarveProcessor&);
    ~DecimulCarveEditor() override;

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

    DecimulCarveProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel { DecimuAccentTheme::violetPink() };

    juce::Image wordmarkImage, dogImage;
    juce::Rectangle<int> topbarRect, displayRect, knobsRect, outputRect, headerLogoArea;

    CarveSpectrumDisplay display;
    Knob amountKnob, scGainKnob, attackKnob, releaseKnob, smoothKnob, mixKnob;
    Knob lowKnob, highKnob;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulCarveEditor)
};
