#pragma once

#include "PluginProcessor.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLedStackSelector.h>
#include <DecimuLadderMeter.h>
#include <DecimuVuArc.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
    UI de DecimuLabs Acoustic — implementa el mockup 3 ("realista", ver
    docs/mockups/acoustic-panel-realistic-mockup.html): chasis de metal cepillado,
    compartimentos con bisel y tinte alternado (Input/Space A/Space B/Output en
    celeste, el resto oscuro — mismo patrón que Input/Reverb1/Reverb2/Output en la
    referencia de hardware), knobs con relieve real, tornillos Phillips, manijas de
    rack, VU en arco funcional en el header. Todo el "look" (LookAndFeel, chrome,
    selectores, meters, VU) vive en shared/ — este archivo solo compone el layout
    específico de Acoustic. Ver shared/Source/DecimuChrome.h.
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

    void setupFader(juce::Slider& slider, juce::Label& label, const juce::String& text);
    void setupKnob(juce::Slider& knob);
    void setupSelector(DecimuLedStackSelector& sel, const juce::StringArray& items, const juce::String& paramID);

    DecimulAcousticProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel;

    juce::Image wordmarkImage, dogImage;

    // Rectángulos calculados en resized() y usados en paint() para dibujar el chasis,
    // los compartimentos y los LEDs de los knobs.
    juce::Rectangle<int> topbarRect, inputCompRect, mainCompRect, blendCompRect, outputCompRect;
    juce::Rectangle<int> spaceACompRect, spaceBCompRect;
    juce::Rectangle<int> headerLogoArea;
    juce::Rectangle<float> spaceALedRect, spaceBLedRect, echoLedRect;

    juce::Slider inputSlider, lowSlider, highSlider, compSlider,
                 spaceASlider, spaceBSlider, echoSlider, blendSlider, outputSlider;
    juce::Label inputLabel, lowLabel, highLabel, compLabel,
                spaceALabel, spaceBLabel, echoLabel, blendLabel, outputLabel;

    juce::Slider spaceAPreDelayKnob, spaceBPreDelayKnob, echoFeedbackKnob;
    juce::Label spaceAKnobLabel, spaceBKnobLabel, echoKnobLabel;

    DecimuLedStackSelector lowModeSel, highModeSel, compModeSel,
                            spaceATypeSel, spaceBTypeSel, echoDivisionSel;

    // Se dibuja como el indicador "REC" del sitio (ver DecimuLookAndFeel::drawToggleButton).
    juce::ToggleButton bypassButton;
    juce::TextButton tapTempoButton { "TAP" };
    juce::Label bpmLabel;

    DecimuLadderMeter inputMeter, outputMeter;
    DecimuVuArc vuArc;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> inputAttach, lowAttach, highAttach, compAttach,
                                       spaceAAttach, spaceBAttach, echoAttach, blendAttach, outputAttach,
                                       spaceAPreDelayAttach, spaceBPreDelayAttach, echoFeedbackAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulAcousticEditor)
};
