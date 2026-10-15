#pragma once

#include "PluginProcessor.h"
#include "HitScope.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLadderMeter.h>
#include <DecimuVuArc.h>
#include <DecimuPillButton.h>
#include <juce_gui_basics/juce_gui_basics.h>

/** Botón de una muestra del reproductor: número + nombre, con un destello cuando suena. */
class TriggerSlotButton : public juce::Button
{
public:
    explicit TriggerSlotButton(int slotIndex) : juce::Button("sample"), index(slotIndex) {}

    void setState(const juce::String& newName, bool isLoaded, bool isLoading, bool isError, float newFlash)
    {
        if (newName != name || isLoaded != loaded || isLoading != loading || isError != hasError
            || std::abs(newFlash - flash) > 0.02f)
        {
            name = newName;
            loaded = isLoaded;
            loading = isLoading;
            hasError = isError;
            flash = newFlash;
            repaint();
        }
    }

    std::function<void()> onContextMenu;

    void paintButton(juce::Graphics&, bool isMouseOver, bool isDown) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    int index;
    juce::String name;
    bool loaded = false, loading = false, hasError = false;
    float flash = 0.0f;
};

/**
    UI del trigger de batería (nombre de trabajo). Mismo chasis que el resto de la
    suite (shared/), con acento coral. De arriba a abajo: cabecera, monitor de golpes
    con la lectura del último, fila de knobs (detección, velocity, MIDI) y el
    reproductor propio con sus 8 muestras.
*/
class DecimulTriggerEditor : public juce::AudioProcessorEditor,
                              public juce::FileDragAndDropTarget,
                              private juce::Timer
{
public:
    explicit DecimulTriggerEditor(DecimulTriggerProcessor&);
    ~DecimulTriggerEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void setParam(const juce::String& id, float plainValue);
    void addKnob(Knob& k, const juce::String& paramID, const juce::String& caption);
    static void placeKnob(Knob& k, juce::Rectangle<int> cell);
    void openFileChooser(int slot);
    void showSlotMenu(int slot);
    void commitVelocityRange(int slot);
    void commitSlotGain(int slot);
    void commitSlotTune(int slot);
    void refreshCaptureStatus();

    DecimulTriggerProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel { DecimuAccentTheme::coralRed() };

    juce::Image wordmarkImage, dogImage;
    juce::Rectangle<int> topbarRect, scopeRect, detectRect, velocityRect, midiRect, samplerRect, outputRect;
    juce::Rectangle<int> printRect, captureRect;
    juce::Rectangle<int> headerLogoArea;

    struct SlotCaption
    {
        juce::Rectangle<int> area;
        juce::String text;
    };
    std::vector<SlotCaption> slotCaptions;

    HitScope scope;
    HitReadout readout;
    FingerprintView fingerprintView;

    Knob thresholdKnob, maxLevelKnob, scanKnob, retriggerKnob, lowCutKnob, highCutKnob, onsetKnob;
    Knob minVelKnob, maxVelKnob, curveKnob;
    Knob noteKnob, channelKnob, noteLenKnob;
    Knob sampleLevelKnob, origLevelKnob, velSensKnob, offsetKnob;
    Knob matchKnob, fpWindowKnob;
    Knob capLengthKnob, capLayersKnob, capRRKnob;

    DecimuPillButton fixedVelPill { "VELOCITY FIJA" }, listenPill { "OIR DETECCION" };
    DecimuPillButton calibratePill { "CALIBRAR" };
    DecimuPillButton polarityPill { "INVERTIR FASE" }, midiInPill { "MIDI IN: NO" };
    DecimuPillButton exportMidiPill { "EXPORTAR MIDI" };
    DecimuPillButton samplerOffPill { "SOLO MIDI" }, samplerReinforcePill { "REFORZAR" }, samplerReplacePill { "REEMPLAZAR" };
    DecimuPillButton alignPill { "ALINEAR ORIGINAL" }, chokePill { "CORTAR ANTERIOR" };
    DecimuPillButton learnPill { "APRENDER" }, clearPrintPill { "BORRAR HUELLA" }, rejectPill { "RECHAZAR BLEED" };
    DecimuPillButton recPill { "GRABAR GOLPES" }, makePill { "CREAR MUESTRAS" },
                     clearTakesPill { "BORRAR TODO" }, openFolderPill { "ABRIR CARPETA" };
    juce::TextEditor captureName;
    juce::Label captureNameLabel, captureStatus, printStatus, calibrateStatus;
    juce::Label detectLabel, velocityLabel, midiLabel, samplerLabel, hintLabel, outputLabel;
    juce::Label printLabel, captureLabel;
    juce::File lastExportTarget;    // carpeta de muestras o archivo .mid de la última exportación
    juce::String exportMessage;

    std::array<std::unique_ptr<TriggerSlotButton>, DecimulTriggerProcessor::numSlots> slotButtons;
    std::array<juce::Label, DecimulTriggerProcessor::numSlots> loLabels, hiLabels, gainLabels, tuneLabels;
    std::array<float, DecimulTriggerProcessor::numSlots> slotFlash {};

    juce::Slider outputSlider;
    juce::ToggleButton bypassButton;
    DecimuLadderMeter outputMeter;
    DecimuVuArc vuArc;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> outputAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach, fixedAttach, listenAttach, alignAttach, chokeAttach, rejectAttach,
        polarityAttach;

    std::unique_ptr<juce::FileChooser> chooser;
    int lastHitCount = 0;
    int lastVoiceCount = 0;
    float hitFlash = 0.0f;

    // Sin esto los tooltips no aparecen dentro de un host.
    juce::TooltipWindow tooltipWindow { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulTriggerEditor)
};
