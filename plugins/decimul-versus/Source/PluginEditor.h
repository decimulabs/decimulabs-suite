#pragma once

#include "PluginProcessor.h"
#include "VersusComponents.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLedStackSelector.h>
#include <DecimuLadderMeter.h>
#include <DecimuVuArc.h>
#include <DecimuPillButton.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
    UI de DecimuLabs Versus (AB). Mismo chasis/compartimentos/knobs que el resto de la
    suite (todo sale de shared/), con tema de acento violeta/rosa. De arriba a abajo:
    cabecera, 8 slots de referencia, forma de onda + transporte + alineación de
    secciones, espectro (promedio / tiempo real / diferencia) + tabla de medidas, y la
    fila de controles (A/B/X con test ciego, igualado, trim, monitoreo, banda, salida).
*/
class DecimulVersusEditor : public juce::AudioProcessorEditor,
                             public juce::FileDragAndDropTarget,
                             private juce::Timer
{
public:
    explicit DecimulVersusEditor(DecimulVersusProcessor&);
    ~DecimulVersusEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    enum class SpectrumMode { average, live, diff };

    void timerCallback() override;

    void setParam(const juce::String& id, float plainValue);
    void selectSlot(int slot);
    void openFileChooser(int slot);
    void showSlotMenu(int slot);
    void setSpectrumMode(SpectrumMode mode);
    void setupSelector(DecimuLedStackSelector& sel, const juce::StringArray& items, const juce::String& paramID);
    void setupBracketLabel(juce::Label& label, const juce::String& text);
    bool isAbxOn() const;
    void layoutAbBox();

    DecimulVersusProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel { DecimuAccentTheme::violetPink() };

    juce::Image wordmarkImage, dogImage;

    juce::Rectangle<int> topbarRect, slotsRect, waveRect, analysisRect, headerLogoArea;
    juce::Rectangle<int> abRect, matchRect, trimRect, monitorRect, bandRect, outputRect;

    std::array<std::unique_ptr<VersusSlotButton>, DecimulVersusProcessor::numSlots> slotButtons;

    // Transporte y alineación
    DecimuPillButton playPill { "PLAY" }, hostPill { "HOST SYNC" }, loopPill { "LOOP" };
    DecimuPillButton alignPill { "ALINEAR SECCION" };
    juce::Label alignLabel, timeLabel;

    VersusWaveformView waveform;
    VersusSpectrumView spectrum;
    VersusMetricsView metrics;

    // Espectro: modos
    DecimuPillButton avgPill { "PROMEDIO" }, livePill { "TIEMPO REAL" }, diffPill { "DIFERENCIA" };
    DecimuPillButton resetPill { "RESET PROMEDIO" };
    SpectrumMode spectrumMode = SpectrumMode::average;
    std::array<float, VersusBands::numBands> liveMixSmooth {}, liveRefSmooth {};

    // A / B / X
    DecimuPillButton aPill { "A" }, bPill { "B" }, xPill { "X" };
    DecimuPillButton abxPill { "TEST CIEGO ABX" };
    DecimuPillButton ansAPill { "X ES A" }, ansBPill { "X ES B" }, abxResetPill { "RESET" };
    juce::Label abxStatsLabel, sourceALabel;
    juce::ComboBox sourceACombo;
    juce::StringArray lastSourceItems;
    bool lastAbxOn = false;

    juce::Slider trimKnob, outputSlider;
    juce::Label abLabel, matchLabel, trimLabel, monitorLabel, bandLabel, outputLabel;
    DecimuLedStackSelector matchSel, monitorSel, bandSel;

    juce::ToggleButton bypassButton;
    DecimuLadderMeter outputMeter;
    DecimuVuArc vuArc;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SliderAttachment> trimAttach, outputAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach, hostAttach, loopAttach, abxAttach;
    std::unique_ptr<ComboAttachment> sourceAAttach;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulVersusEditor)
};
