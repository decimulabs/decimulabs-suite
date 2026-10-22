#pragma once

#include "PluginProcessor.h"
#include "BandView.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLadderMeter.h>
#include <DecimuVuArc.h>
#include <DecimuPillButton.h>
#include <DecimuTooltips.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
    UI de DecimuLabs Emery (nombre de trabajo). Mismo chasis que el resto de la suite (shared/)
    con acento índigo. A la izquierda la pantalla (las dos bandas, el umbral y lo que se baja);
    en el medio dos columnas, SIBILANCIA y GOLPES, cada una con FREQ, THRESHOLD, AMOUNT y los
    botones ON / WIDE / LISTEN; a la derecha la salida.
*/
class DecimulEmeryEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit DecimulEmeryEditor(DecimulEmeryProcessor&);
    ~DecimulEmeryEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Column
    {
        Knob freq, thresh, amount;
        DecimuPillButton onPill { "ON" }, widePill { "WIDE" }, listenPill { "LISTEN" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttach, wideAttach, listenAttach;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;
    void addKnob(Knob& k, const juce::String& paramID, const juce::String& caption, const juce::String& suffix);
    void addColumn(Column& c, const juce::String& on, const juce::String& freq, const juce::String& thresh,
                   const juce::String& amount, const juce::String& wide, const juce::String& listen,
                   const juce::String& freqCaption, const juce::String& freqSuffix);
    void layoutColumn(Column& c);
    static void placeKnob(Knob& k, juce::Rectangle<int> cell, int maxSide);

    DecimulEmeryProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel { DecimuAccentTheme::indigo() };

    juce::Image wordmarkImage, dogImage;
    juce::Rectangle<int> topbarRect, viewRect, outputRect, headerLogoArea;

    EmeryView view;
    Column essCol, popCol;
    juce::Label hintLabel, outputLabel, essTitle, popTitle;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulEmeryEditor)
};
