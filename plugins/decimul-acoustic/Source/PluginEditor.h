#pragma once

#include "PluginProcessor.h"
#include "DecimuLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

/**
    UI de DecimuLabs Acoustic: híbrido entre el layout de banco de faders del Waves
    CLA Unplugged (faders verticales en fila, selector de carácter/tipo arriba de cada
    uno, knobs de pre-delay, meters a los costados) y la identidad visual real del
    sitio decimulabs.com (logo, paleta, tipografías Chakra Petch + VT323 — ver
    DecimuLookAndFeel.h y Assets/README.md). No copia el diseño gráfico ni el branding
    de Waves — mismo esqueleto funcional, estética propia.
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

    // Meter segmentado tipo VU/LED-ladder (varios segmentos que se van encendiendo,
    // verdes abajo, amarillo cerca del techo, rojo en el segmento de clip).
    struct SimpleMeter : public juce::Component
    {
        void paint(juce::Graphics& g) override;
        float levelDb = -100.0f;
    };

    // Selector de 3 opciones tipo "stack de LEDs" (como Warm/Deep/Air-Cut apiladas en
    // la referencia de Waves): un punto de color + texto por fila, click para elegir.
    // Se sincroniza a mano con el AudioParameterChoice correspondiente (no hay una
    // Attachment de JUCE para esto) — ver setupSelector()/timerCallback().
    struct LedStackSelector : public juce::Component
    {
        void paint(juce::Graphics& g) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void setSelectedIndex(int idx);

        juce::StringArray options;
        int selected = 0;
        std::function<void(int)> onChange;
    };

    void setupFader(juce::Slider& slider, juce::Label& label, const juce::String& text);
    void setupKnob(juce::Slider& knob);
    void setupSelector(LedStackSelector& sel, const juce::StringArray& items, const juce::String& paramID);

    DecimulAcousticProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel;

    juce::Image wordmarkImage, dogImage, studioBgImage;
    juce::Rectangle<int> headerLogoArea;

    // Rectángulos de agrupación (paneles tipo "módulo de hardware"), calculados en
    // resized() y pintados en paint().
    juce::Rectangle<int> inputPanel, mainPanel, blendPanel, outputPanel;

    juce::Slider inputSlider, lowSlider, highSlider, compSlider,
                 spaceASlider, spaceBSlider, echoSlider, blendSlider, outputSlider;
    juce::Label inputLabel, lowLabel, highLabel, compLabel,
                spaceALabel, spaceBLabel, echoLabel, blendLabel, outputLabel;

    juce::Slider spaceAPreDelayKnob, spaceBPreDelayKnob;

    LedStackSelector lowModeSel, highModeSel, compModeSel,
                      spaceATypeSel, spaceBTypeSel, echoDivisionSel;

    // Se dibuja como el indicador "REC" del sitio (ver DecimuLookAndFeel::drawToggleButton).
    juce::ToggleButton bypassButton;
    juce::TextButton tapTempoButton { "TAP" };
    juce::Label bpmLabel;

    SimpleMeter inputMeter, outputMeter;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> inputAttach, lowAttach, highAttach, compAttach,
                                       spaceAAttach, spaceBAttach, echoAttach, blendAttach, outputAttach,
                                       spaceAPreDelayAttach, spaceBPreDelayAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulAcousticEditor)
};
