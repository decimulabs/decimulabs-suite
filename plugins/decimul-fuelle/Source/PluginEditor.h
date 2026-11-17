#pragma once

#include "PluginProcessor.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLadderMeter.h>
#include <juce_gui_basics/juce_gui_basics.h>

/** UI de DecimuLabs Fuelle: la sala arriba, el agachado abajo, y la salida. */
class DecimulFuelleEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DecimulFuelleEditor(DecimulFuelleProcessor&);
    ~DecimulFuelleEditor() override;
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

    DecimulFuelleProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel { DecimuAccentTheme::violetPink() };
    juce::TooltipWindow tooltips { this, 600 };
    juce::Image wordmarkImage, dogImage;
    juce::Rectangle<int> topbarRect, roomRect, duckRect, meterRect, headerLogoArea, duckBar;

    Knob size, damping, predelay, lowCut, width, duck, threshold, release, mix;
    DecimuLadderMeter meterL, meterR;
    float shownDuck = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulFuelleEditor)
};
