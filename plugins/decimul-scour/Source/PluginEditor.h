#pragma once

#include "PluginProcessor.h"
#include "ModuleViz.h"
#include "ScopeView.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLadderMeter.h>
#include <DecimuLedButton.h>
#include <DecimuLedStackSelector.h>
#include <DecimuPillButton.h>
#include <DecimuTooltips.h>
#include <DecimuVuArc.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
    UI de DecimuLabs Scour (nombre de trabajo). La distribución sigue la de los efectos lo-fi de
    módulos en fila (arriba un display y la perilla MAGNITUDE; en el medio seis columnas, una por
    módulo; abajo el EQ final) con el chasis, los compartimentos, los LEDs, los selectores y los
    faders de Acoustic y Guitars (todo sale de shared/), en acento cobre.

    Cada columna: LED de encendido + nombre, selector de tipo (si tiene), perillas chicas (con
    FLUX al final) y la perilla grande de cantidad abajo. Entrada a la izquierda; a la derecha
    WIDTH, MIX y la salida.
*/
class DecimulScourEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit DecimulScourEditor(DecimulScourProcessor&);
    ~DecimulScourEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct KnobCell
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct ModuleUi
    {
        DecimuLedButton led;
        std::unique_ptr<ButtonAttachment> ledAttachment;
        juce::Label titleLabel;
        std::unique_ptr<ModuleViz> viz;
        DecimuLedStackSelector selector;
        bool hasSelector = false;
        int typeCount = 0;
        std::vector<std::unique_ptr<KnobCell>> knobs;
        KnobCell big;
        DecimuPillButton pill;
        std::unique_ptr<ButtonAttachment> pillAttachment;
        bool hasPill = false;
        juce::Rectangle<int> bounds;
    };

    struct KnobSpec { const char* id; const char* caption; const char* suffix; const char* en; const char* es; };

    void timerCallback() override;
    void addKnob(KnobCell& k, const juce::String& paramID, const juce::String& caption, const juce::String& suffix,
                 bool textBox, const char* en, const char* es);
    void buildModule(ModuleUi& m, const juce::String& title, const juce::String& onID, const KnobSpec& amount,
                     const juce::String& typeID, const juce::StringArray& types, const char* typeEn, const char* typeEs,
                     const std::vector<KnobSpec>& knobs, const char* ledEn, const char* ledEs);
    void layoutModule(ModuleUi& m);
    static void placeKnob(KnobCell& k, juce::Rectangle<int> cell, int maxSide, bool textBox);
    void setupFader(juce::Slider& slider, juce::Label& label, const juce::String& text);

    DecimulScourProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel { DecimuAccentTheme::copper() };

    juce::Image wordmarkImage, dogImage;
    juce::Rectangle<int> topbarRect, inputRect, outputRect, eqRect, headerLogoArea;

    ScourScope scope;
    std::array<ModuleUi, 6> modules;

    KnobCell magnitudeKnob, widthKnob, mixKnob, eqLowKnob, eqHighKnob, eqToneKnob;
    DecimuLedButton eqLed;
    std::unique_ptr<ButtonAttachment> eqLedAttachment;
    juce::Label eqTitle;

    juce::Slider inputSlider, outputSlider;
    juce::Label inputLabel, outputLabel;
    juce::ToggleButton bypassButton;
    DecimuLadderMeter inputMeter, outputMeter;
    DecimuVuArc vuArc;
    std::unique_ptr<SliderAttachment> inputAttach, outputAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach;

    // Textos de ayuda al pasar el puntero (inglés por defecto, botón EN/ES abajo).
    DecimuTooltips tips { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulScourEditor)
};
