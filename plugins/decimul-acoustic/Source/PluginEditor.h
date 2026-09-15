#pragma once

#include "PluginProcessor.h"
#include "DecimuLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

/**
    UI de DecimuLabs Acoustic con la identidad visual real del sitio decimulabs.com:
    logo (wordmark + "perro"), paleta oscura con acentos rojo/cian/azul, tipografías
    Chakra Petch + VT323 (ver DecimuLookAndFeel.h y Assets/README.md). El layout
    funcional sigue inspirado en el banco de faders del Waves CLA Unplugged (sliders
    verticales en fila, selector de carácter/tipo arriba de cada uno, meters a los
    costados) — no se copia su diseño gráfico ni su branding, ver conversación previa.
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
    // llena según el nivel en dB.
    struct SimpleMeter : public juce::Component
    {
        void paint(juce::Graphics& g) override;
        float levelDb = -100.0f;
    };

    void setupFader(juce::Slider& slider, juce::Label& label, const juce::String& text);
    void setupCombo(juce::ComboBox& box, const juce::StringArray& items);

    DecimulAcousticProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel;

    juce::Image wordmarkImage, dogImage;
    juce::Rectangle<int> headerLogoArea;

    // Orden igual al banco de faders de referencia: Input | Low High Dynamics Space A
    // Space B Echo | Blend | Output — con meters en las puntas (Input/Output) como en
    // la referencia, y un botón de Tap Tempo junto al fader de Blend.
    juce::Slider inputSlider, lowSlider, highSlider, compSlider,
                 spaceASlider, spaceBSlider, echoSlider, blendSlider, outputSlider;
    juce::Label inputLabel, lowLabel, highLabel, compLabel,
                spaceALabel, spaceBLabel, echoLabel, blendLabel, outputLabel;

    juce::ComboBox lowModeBox, highModeBox, compModeBox,
                   spaceATypeBox, spaceBTypeBox, echoDivisionBox;

    // Se dibuja como el indicador "REC" del sitio (ver DecimuLookAndFeel::drawToggleButton).
    juce::ToggleButton bypassButton;
    juce::TextButton tapTempoButton { "TAP" };
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
