#pragma once

#include "PluginProcessor.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLadderMeter.h>
#include <DecimuPillButton.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

/** UI de DecimuLabs Estela: el eco arriba, el agachado abajo, y la salida. */
class DecimulEstelaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DecimulEstelaEditor(DecimulEstelaProcessor&);
    ~DecimulEstelaEditor() override;
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
    void addKnob(Knob& k, const juce::String& id, const juce::String& caption, const char* tip);
    static void placeKnob(Knob& k, juce::Rectangle<int> cell);

    DecimulEstelaProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel { DecimuAccentTheme::cyanBlue() };
    juce::TooltipWindow tooltips { this, 600 };
    juce::Image wordmarkImage, dogImage;
    juce::Rectangle<int> topbarRect, echoRect, duckRect, meterRect, headerLogoArea, duckBar;

    Knob time, feedback, lowCut, highCut, wobble, duck, threshold, release, mix;
    juce::ComboBox division;
    DecimuPillButton syncPill { "SYNC" }, pingPill { "PING-PONG" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> divisionAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttach, pingAttach;
    DecimuLadderMeter meterL, meterR;
    float shownDuck = 1.0f, shownTime = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulEstelaEditor)
};
