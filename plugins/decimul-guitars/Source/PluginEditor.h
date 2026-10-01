#pragma once

#include "PluginProcessor.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLedStackSelector.h>
#include <DecimuLadderMeter.h>
#include <DecimuVuArc.h>
#include <DecimuLedButton.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
    UI de DecimuLabs Guitars — mismo chasis/compartimentos/knobs que Acoustic (todo
    sale de shared/), con tema de acento **ámbar/dorado** en vez de cian/azul (ver
    DecimuAccentTheme::amberGold() en shared/Source/DecimuLookAndFeel.h) para que se
    sienta parte de la misma familia sin ser un clon idéntico. Layout de 7 módulos en
    el banco principal (Gate, Drive, Tone, Cab, Modulation, Space, Echo) en vez de los
    6 de Acoustic — ver SPEC.md.

    Tras el primer testing real (feedback de un guitarrista probando el plugin):
    los LEDs de Tone/Mod/Space/Echo (y ahora también Cab) son botones de verdad
    (DecimuLedButton), no solo un indicador — apagan/prenden el módulo entero sin
    tener que bajarle el fader a cero. Cab perdió su fader (ahora es on/off puro,
    ver PluginProcessor.h para el porqué).
*/
class DecimulGuitarsEditor : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit DecimulGuitarsEditor(DecimulGuitarsProcessor&);
    ~DecimulGuitarsEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void setupFader(juce::Slider& slider, juce::Label& label, const juce::String& text);
    void setupKnob(juce::Slider& knob);
    void setupSelector(DecimuLedStackSelector& sel, const juce::StringArray& items, const juce::String& paramID);

    DecimulGuitarsProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel { DecimuAccentTheme::amberGold() };

    juce::Image wordmarkImage, dogImage;

    juce::Rectangle<int> topbarRect, inputCompRect, mainCompRect, blendCompRect, outputCompRect;
    juce::Rectangle<int> toneCompRect, modCompRect, spaceCompRect, echoCompRect;
    juce::Rectangle<int> headerLogoArea;

    juce::Slider inputSlider, gateSlider, driveSlider, toneSlider,
                 modSlider, spaceSlider, echoSlider, blendSlider, outputSlider;
    juce::Label inputLabel, gateLabel, driveLabel, toneLabel, cabLabel,
                modLabel, spaceLabel, echoLabel, blendLabel, outputLabel;

    juce::Slider toneMidKnob, modRateKnob, spacePreDelayKnob, echoFeedbackKnob;
    juce::Label toneKnobLabel, modKnobLabel, spaceKnobLabel, echoKnobLabel;

    // Botones LED (no solo indicador): apagan/prenden el módulo entero. driveLedBtn
    // sumado en la 2da ronda de testing (Drive era el único módulo del core sin
    // apagador propio).
    DecimuLedButton driveLedBtn, toneLedBtn, modLedBtn, spaceLedBtn, echoLedBtn, cabLedBtn;

    DecimuLedStackSelector gateModeSel, driveModeSel, toneModeSel, cabModeSel,
                            modModeSel, spaceModeSel, echoDivisionSel;

    juce::ToggleButton bypassButton;
    juce::TextButton tapTempoButton { "TAP" };
    juce::Label bpmLabel;

    // Testing real (2da ronda): "el BPM manual no hace nada" — es por diseño cuando el
    // host da un tempo válido (tiene prioridad). Este toggle deja forzar el manual/Tap
    // por encima del host. No es parámetro APVTS (gesto de UI, como manualBpm).
    juce::ToggleButton hostSyncButton { "SYNC" };

    DecimuLadderMeter inputMeter, outputMeter;
    DecimuVuArc vuArc;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> inputAttach, gateAttach, driveAttach, toneAttach,
                                       modAttach, spaceAttach, echoAttach, blendAttach, outputAttach,
                                       toneMidAttach, modRateAttach, spacePreDelayAttach, echoFeedbackAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach, driveEnabledAttach, toneEnabledAttach, modEnabledAttach,
                                       spaceEnabledAttach, echoEnabledAttach, cabEnabledAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulGuitarsEditor)
};
