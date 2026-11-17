#pragma once

#include "PluginProcessor.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLadderMeter.h>
#include <DecimuPillButton.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

/** UI de DecimuLabs Navaja: cuatro perillas, seis interruptores, medidor de correlación y salida. */
class DecimulNavajaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DecimulNavajaEditor(DecimulNavajaProcessor&);
    ~DecimulNavajaEditor() override;
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
    void addKnob(Knob& k, const juce::String& id, const juce::String& caption);
    static void placeKnob(Knob& k, juce::Rectangle<int> cell);

    DecimulNavajaProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel { DecimuAccentTheme::tealMint() };
    juce::TooltipWindow tooltips { this, 600 };
    juce::Image wordmarkImage, dogImage;
    juce::Rectangle<int> topbarRect, knobsRect, switchRect, meterRect, headerLogoArea, corrBar;

    Knob gain, pan, width, bassHz;
    std::array<DecimuPillButton, 6> toggles { DecimuPillButton { "FASE L" }, DecimuPillButton { "FASE R" }, DecimuPillButton { "SWAP L/R" },
                                              DecimuPillButton { "MONO" }, DecimuPillButton { "GRAVES MONO" }, DecimuPillButton { "FILTRO DC" } };
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 6> toggleAttach;
    DecimuLadderMeter meterL, meterR;
    float shownCorr = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulNavajaEditor)
};
