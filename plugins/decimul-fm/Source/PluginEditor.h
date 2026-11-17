#pragma once

#include "PluginProcessor.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLedButton.h>
#include <DecimuTooltips.h>
#include "Retro.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>

/** Qué operadores se escuchan (portadores) en cada algoritmo del YM2612. */
namespace fmui
{
    bool isCarrier(int alg, int op);

    /** Los 8 algoritmos dibujados; un click elige. Atado al parámetro "alg". */
    class AlgorithmSelector : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        AlgorithmSelector(juce::RangedAudioParameter& p, const DecimuLookAndFeel& laf);
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        int selected = 0;
    private:
        juce::Rectangle<float> cell(int i) const;
        const DecimuLookAndFeel& lnf;
        juce::ParameterAttachment attachment;
    };

    /** Dibujo de la envolvente de un operador (se lee de los parámetros). */
    class EnvelopeView : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        EnvelopeView(juce::AudioProcessorValueTreeState& s, int opIndex) : apvts(s), op(opIndex) {}
        void paint(juce::Graphics&) override;
        bool carrier = false;
    private:
        juce::AudioProcessorValueTreeState& apvts;
        int op;
    };

    /** Pantallita LCD con los registros del canal 1 tal como los escribe el plugin. */
    class RegisterView : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        RegisterView(DecimulFMProcessor& p, const DecimuLookAndFeel& laf) : proc(p), lnf(laf) {}
        void paint(juce::Graphics&) override;
    private:
        DecimulFMProcessor& proc;
        const DecimuLookAndFeel& lnf;
    };
}

class DecimulFMEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DecimulFMEditor(DecimulFMProcessor&);
    ~DecimulFMEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        retro::PixelLabel label;
        std::unique_ptr<SliderAttachment> attachment;
    };
    struct OpPanel
    {
        std::unique_ptr<fmui::EnvelopeView> env;
        std::array<Knob, 9> knobs;
        DecimuLedButton am;
        juce::Label amLabel;
        retro::PixelLabel roleLabel;
        juce::ComboBox ssg;
        std::unique_ptr<ButtonAttachment> amAttach;
        std::unique_ptr<ComboAttachment> ssgAttach;
        juce::Rectangle<int> area;
    };

    void timerCallback() override;
    void addKnob(Knob& k, const juce::String& paramID, const juce::String& caption);
    void placeKnob(Knob& k, juce::Rectangle<int> cell);
    void refreshPresetBox();

    DecimulFMProcessor& proc;
    // cobre redondeado a los 512 colores del chip de video
    retro::FMLookAndFeel lookAndFeel { DecimuAccentTheme { juce::Colour(0xffFFAC57), juce::Colour(0xffCE5734), juce::Colour(0xffFF7434) } };
    juce::Image wordmarkImage, dogImage;

    juce::Rectangle<int> topbarRect, algRect, lfoRect, regRect, headerLogoArea;

    juce::ComboBox presetBox;
    juce::TextButton shelfButton { "CARTUCHOS" };
    std::unique_ptr<retro::CartridgeShelf> shelf;
    std::unique_ptr<retro::BootScreen> boot;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, ladderButton { "MD1 DAC" };
    std::unique_ptr<ButtonAttachment> ladderAttach;
    Knob outputKnob, bendKnob, fbKnob, amsKnob, pmsKnob;

    std::unique_ptr<fmui::AlgorithmSelector> algSelector;
    retro::PixelLabel algLabel, lfoLabel, regLabel;
    DecimuLedButton lfoOn;
    juce::Label lfoOnLabel;
    juce::ComboBox lfoRate;
    std::unique_ptr<ButtonAttachment> lfoOnAttach;
    std::unique_ptr<ComboAttachment> lfoRateAttach;

    std::unique_ptr<fmui::RegisterView> regView;
    std::array<OpPanel, 4> ops;
    int lastProgram = -1;

    DecimuTooltips tips { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulFMEditor)
};
