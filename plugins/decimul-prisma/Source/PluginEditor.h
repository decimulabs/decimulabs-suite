#pragma once

#include "PluginProcessor.h"
#include "EqDisplay.h"
#include "BandPanel.h"
#include <DecimuLookAndFeel.h>
#include <DecimuChrome.h>
#include <DecimuLadderMeter.h>
#include <DecimuVuArc.h>
#include <DecimuPillButton.h>
#include <DecimuTooltips.h>
#include <juce_gui_basics/juce_gui_basics.h>

/** EQ Match: aprende cómo suena una referencia (el sidechain u otra pista con Prisma) y cómo
    suena esta pista, y arma las bandas que acercan una a la otra. */
class MatchPanel : public juce::Component
{
public:
    MatchPanel(DecimulPrismaProcessor&, DecimuTooltips&);
    void refreshSources(bool spanish);
    void tick();                    // timer del editor: acumula mientras aprende
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose, onApplied;

private:
    void apply();
    void clearLearning();

    DecimulPrismaProcessor& proc;
    juce::ComboBox source;
    DecimuPillButton learn { "LEARN" }, applyButton { "APPLY" }, clearButton { "CLEAR" }, closeButton { "X" };
    juce::Slider bandsKnob, amountKnob;
    juce::Label bandsLabel, amountLabel, status, title;
    std::vector<int> sourceIds;     // 0 = sidechain; si no, id de instancia
    std::array<double, SpectrumAnalyzer::points> sumRef {}, sumCur {};
    int frames = 0;
    bool spanish = false;
};

/** Lista de las otras instancias de Prisma: elegir una muestra su espectro y su curva encima, y
    marca en rojo dónde chocan las dos pistas. */
class InstancesPanel : public juce::Component
{
public:
    InstancesPanel(DecimulPrismaProcessor&, DecimuTooltips&);
    void refreshList(bool spanish);
    void paint(juce::Graphics&) override;
    void resized() override;
    int selected = 0;
    std::function<void()> onChange, onClose;

private:
    DecimulPrismaProcessor& proc;
    juce::OwnedArray<DecimuPillButton> buttons;
    std::vector<int> ids;
    DecimuPillButton noneButton { "-" }, collisions { "COLLISIONS" }, closeButton { "X" };
    juce::Label title, empty;
    bool spanish = false;
};

/**
    UI de DecimuLabs Prisma (nombre de trabajo). Mismo chasis que el resto de la suite (shared/),
    acento rosa. Arriba: presets, A/B, deshacer/rehacer y bypass. En el medio la pantalla (curva
    + analizador) con el panel de la banda elegida flotando abajo. Abajo: modo de proceso,
    carácter, analizador, EQ Match, Sketch, instancias, piano, auto gain, dinámica y salida.
*/
class DecimulPrismaEditor : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit DecimulPrismaEditor(DecimulPrismaProcessor&);
    ~DecimulPrismaEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    /** Para las capturas de los tests. */
    EqDisplay& getDisplay() { return display; }
    void openMatchPanel(bool open);

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void addKnob(Knob& k, const juce::String& paramID, const juce::String& caption);
    static void placeKnob(Knob& k, juce::Rectangle<int> cell);
    void showPresetMenu();
    void updateLanguage();
    void layoutOverlays();

    DecimulPrismaProcessor& processorRef;
    DecimuLookAndFeel lookAndFeel { DecimuAccentTheme::rose() };
    DecimuTooltips tips { *this };

    juce::Image wordmarkImage, dogImage;
    juce::Rectangle<int> topbarRect, displayRect, bottomRect, headerLogoArea;

    EqDisplay display;
    BandPanel bandPanel;
    MatchPanel matchPanel;
    InstancesPanel instancesPanel;

    // Arriba
    DecimuPillButton presetButton { "Init" }, abA { "A" }, abB { "B" }, abCopy { "A>B" }, undoButton { "UNDO" }, redoButton { "REDO" };
    juce::ToggleButton bypassButton;
    DecimuVuArc vuArc;

    // Abajo
    juce::ComboBox modeBox, resBox, charBox, rangeBox;
    juce::Label modeLabel, charLabel, rangeLabel, latencyLabel, autoGainLabel;
    DecimuPillButton analyzerButton { "ANALYZER" }, matchPill { "MATCH" }, sketchPill { "SKETCH" }, instancesPill { "INSTANCES" },
        pianoPill { "PIANO" }, gainQPill { "GAIN-Q" }, autoGainPill { "AUTO GAIN" }, phasePill { "PHASE" }, helpPill { "?" };
    Knob scaleKnob, attackKnob, releaseKnob, outputKnob, panKnob;
    DecimuLadderMeter outputMeter;

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ComboAttachment> modeAttach, resAttach, charAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach, gainQAttach, autoGainAttach, phaseAttach;

    bool spanish = false;
    bool panelAtTop = false;
    int instanceRefreshCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulPrismaEditor)
};
