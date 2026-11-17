#pragma once

#include "PluginProcessor.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLadderMeter.h>
#include <juce_gui_basics/juce_gui_basics.h>

/** El rastrillo: cada diente es un pico del filtro. La separación es la nota, el largo la
    resonancia, el filo el damping; con feedback negativo los dientes cuelgan al revés; con un
    acorde se superponen varios rastrillos; con el LFO tiemblan. Arrastrar a los costados afina. */
class RakeDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit RakeDisplay(DecimulRakeProcessor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void setMonoFont(const juce::Font& f) { mono = f; }

private:
    DecimulRakeProcessor& proc;
    juce::Font mono { juce::FontOptions(12.0f) };
    int dragStartNote = 0;
};

class DecimulRakeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DecimulRakeEditor(DecimulRakeProcessor&);
    ~DecimulRakeEditor() override;
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

    DecimulRakeProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel { DecimuAccentTheme::amberGold() };
    juce::TooltipWindow tooltips { this, 600 };
    juce::Image wordmarkImage, dogImage;
    juce::Rectangle<int> topbarRect, displayRect, knobsRect, meterRect, headerLogoArea;

    RakeDisplay display;
    Knob note, fine, feedback, damping, lfoRate, lfoDepth, stereo, mix, output;
    juce::ComboBox chord;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chordAttach;
    DecimuLadderMeter meterL, meterR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulRakeEditor)
};
