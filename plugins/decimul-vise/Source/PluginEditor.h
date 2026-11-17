#pragma once

#include "PluginProcessor.h"
#include "GrView.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLadderMeter.h>
#include <DecimuPillButton.h>
#include <DecimuTooltips.h>
#include <DecimuVuArc.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
    UI de DecimuLabs Vise (nombre de trabajo). Mismo chasis que el resto de la suite (shared/) con
    acento amarillo. A la izquierda la pantalla (reducción colgando en rojo, nivel de entrada y umbral);
    en el medio nueve perillas y tres botones (AUTO REL, AUTO GAIN, RMS); a la derecha la salida.
*/
class DecimulViseEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit DecimulViseEditor(DecimulViseProcessor&);
    ~DecimulViseEditor() override;

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
    void addPill(DecimuPillButton& pill, std::unique_ptr<ButtonAttachment>& att, const juce::String& paramID, const char* en, const char* es);
    static void placeKnob(Knob& k, juce::Rectangle<int> cell, int maxSide);

    DecimulViseProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel { DecimuAccentTheme::sunYellow() };

    juce::Image wordmarkImage, dogImage;
    juce::Rectangle<int> topbarRect, viewRect, knobsRect, outputRect, headerLogoArea;

    ViseView view;
    Knob thresholdKnob, ratioKnob, attackKnob, releaseKnob, kneeKnob, makeupKnob, mixKnob, scHpKnob, linkKnob;
    DecimuPillButton autoRelPill { "AUTO REL" }, autoGainPill { "AUTO GAIN" }, rmsPill { "PEAK" };
    std::unique_ptr<ButtonAttachment> autoRelAttach, autoGainAttach, rmsAttach;
    juce::Label outputLabel;

    juce::Slider outputSlider;
    juce::ToggleButton bypassButton;
    DecimuLadderMeter inputMeter, outputMeter;
    DecimuVuArc vuArc;

    std::unique_ptr<SliderAttachment> outputAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach;

    // Textos de ayuda al pasar el puntero (inglés por defecto, botón EN/ES abajo).
    DecimuTooltips tips { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulViseEditor)
};
