#pragma once

#include "PluginProcessor.h"
#include "SpectrumView.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLadderMeter.h>
#include <DecimuPillButton.h>
#include <DecimuTooltips.h>
#include <DecimuVuArc.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
    UI de DecimuLabs Sarro (nombre de trabajo). Mismo chasis que el resto de la suite (shared/) con
    acento verde azulado oscuro. Arriba la pantalla del espectro (entrada, resultado y reducción por
    frecuencia); abajo nueve perillas (DEPTH, SHARPNESS, SENSITIVITY, ATTACK, RELEASE, LOW y HIGH
    FOCUS, LINK, MIX) y el botón DELTA; a la derecha TRIM y la salida.
*/
class DecimulSarroEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit DecimulSarroEditor(DecimulSarroProcessor&);
    ~DecimulSarroEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    void addKnob(Knob& k, const juce::String& paramID, const juce::String& caption, const juce::String& suffix,
                 const char* en, const char* es);
    static void placeKnob(Knob& k, juce::Rectangle<int> cell, int maxSide);

    DecimulSarroProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel { DecimuAccentTheme::tealMint() };

    juce::Image wordmarkImage, dogImage;
    juce::Rectangle<int> topbarRect, viewRect, knobsRect, outputRect, headerLogoArea;

    SpectrumView view;
    Knob depthKnob, sharpnessKnob, sensitivityKnob, attackKnob, releaseKnob, lowKnob, highKnob, linkKnob, mixKnob;
    DecimuPillButton deltaPill { "DELTA" };
    std::unique_ptr<ButtonAttachment> deltaAttach;
    juce::Label outputLabel, latencyLabel;

    juce::Slider trimSlider;
    juce::ToggleButton bypassButton;
    DecimuLadderMeter outputMeter;
    DecimuVuArc vuArc;

    std::unique_ptr<SliderAttachment> trimAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach;

    DecimuTooltips tips { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulSarroEditor)
};
