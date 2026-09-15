#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

/**
    UI "mega básica" de DecimuLabs Acoustic — layout inspirado en el banco de faders
    del Waves CLA Unplugged (sliders verticales en fila, selector de carácter/tipo
    arriba de cada uno, meters a los costados, bypass cerca del output) para que la
    interacción resulte familiar a cualquiera que conozca ese tipo de plugin. Estética
    propia: fondo oscuro liso, sin la textura dorada/paisley, sin el logo "Chris
    Lord-Alge", sin el esquema de colores rojo/azul/verde por opción — acá cada modo es
    simplemente texto en un combo box. Cero diseño gráfico custom a propósito: es un
    hito para poder tocar los parámetros con el mouse, no la UI final (ver SPEC.md).
*/
class DecimulAcousticEditor : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit DecimulAcousticEditor(DecimulAcousticProcessor&);
    ~DecimulAcousticEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // Barra vertical simple (sin ballistics/peak-hold real): un rectángulo que se
    // llena según el nivel en dB. A propósito lo más básico posible.
    struct SimpleMeter : public juce::Component
    {
        void paint(juce::Graphics& g) override;
        float levelDb = -100.0f;
    };

    void setupFader(juce::Slider& slider, juce::Label& label, const juce::String& text);
    void setupCombo(juce::ComboBox& box, const juce::StringArray& items);

    DecimulAcousticProcessor& processorRef;

    juce::Label titleLabel;

    // Orden igual al banco de faders de referencia: Input | Low High Dynamics Space A
    // Space B Echo | Blend | Output — con meters en las puntas (Input/Output) como en
    // la referencia, y un botón de Tap Tempo junto al fader de Blend.
    juce::Slider inputSlider, lowSlider, highSlider, compSlider,
                 spaceASlider, spaceBSlider, echoSlider, blendSlider, outputSlider;
    juce::Label inputLabel, lowLabel, highLabel, compLabel,
                spaceALabel, spaceBLabel, echoLabel, blendLabel, outputLabel;

    juce::ComboBox lowModeBox, highModeBox, compModeBox,
                   spaceATypeBox, spaceBTypeBox, echoDivisionBox;

    juce::ToggleButton bypassButton { "Bypass" };
    juce::TextButton tapTempoButton { "Tap" };
    juce::Label bpmLabel;

    SimpleMeter inputMeter, outputMeter;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> inputAttach, lowAttach, highAttach, compAttach,
                                       spaceAAttach, spaceBAttach, echoAttach, blendAttach, outputAttach;
    std::unique_ptr<ComboAttachment> lowModeAttach, highModeAttach, compModeAttach,
                                      spaceATypeAttach, spaceBTypeAttach, echoDivisionAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulAcousticEditor)
};
