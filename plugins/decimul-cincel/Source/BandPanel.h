#pragma once

#include "PluginProcessor.h"
#include <DecimuLookAndFeel.h>
#include <DecimuPillButton.h>
#include <DecimuTooltips.h>
#include "BandIconButton.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

/**
    Panel de la banda elegida (flota abajo de la pantalla, como en el Pro-Q): forma, pendiente,
    ubicación estéreo, frecuencia / ganancia / Q, la dinámica (rango, umbral, auto, sidechain,
    espectral), solo, prendida y borrar. Si hay varias bandas elegidas, lo que se mueve acá se
    aplica a todas (frecuencia y Q en proporción, ganancia sumando).
*/
class BandPanel : public juce::Component
{
public:
    /** ComboBox que avisa si el cambio lo hizo el usuario desde su menú (y no una automatización,
        un deshacer o un preset): solo esos cambios se copian a las otras bandas elegidas. */
    class UserComboBox : public juce::ComboBox
    {
    public:
        void showPopup() override
        {
            userPending = true;
            pendingSince = juce::Time::getMillisecondCounter();
            juce::ComboBox::showPopup();
        }
        bool consumeUserChange()
        {
            const bool was = userPending;
            userPending = false;
            return was;
        }
        void expire()
        {
            if (userPending && !isPopupActive() && juce::Time::getMillisecondCounter() - pendingSince > 500)
                userPending = false;
        }

    private:
        bool userPending = false;
        juce::uint32 pendingSince = 0;
    };

    BandPanel(DecimulCincelProcessor&, DecimuTooltips&);
    ~BandPanel() override;

    void setBand(int index);
    int getBand() const { return band; }
    BandIconButton& getSoloButton() { return soloPill; }
    BandIconButton& getOnButton() { return onPill; }
    void refresh();                // estado de solo, habilitados, colores (timer del editor)
    void setSpanish(bool es);

    std::function<std::vector<int>()> getSelection;
    std::function<void(int)> onNavigate;   // pide elegir otra banda (flechas)
    std::function<void()> onDelete;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override { userMouse = true; }
    void mouseUp(const juce::MouseEvent&) override { userMouse = false; }

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
        double dragStart = 0.0;
    };

    void registerTips();
    void setupKnob(Knob& k, const juce::String& caption);
    void placeKnob(Knob& k, juce::Rectangle<int> cell);
    void beginRelative(Knob& k);
    void applyRelative(Knob& k, CincelIDs::BandKey key, bool ratio);
    void applyToOthers(CincelIDs::BandKey key, float plainValue);

    DecimulCincelProcessor& proc;
    DecimuTooltips& tips;
    int band = -1;
    bool spanish = false;

    DecimuPillButton prevButton { "<" }, nextButton { ">" };
    juce::Label title;
    UserComboBox shapeBox, slopeBox, placeBox;
    bool userMouse = false;   // hay un clic del usuario en curso (dentro del panel)
    Knob freq, gain, q, range, thr;
    DecimuPillButton dynPill { "DYN" }, autoPill { "AUTO" }, scPill { "SC" }, specPill { "SPEC" };
    BandIconButton soloPill { BandIconButton::Icon::Headphones }, onPill { BandIconButton::Icon::Power };
    DecimuPillButton deletePill { "X" };
    float soloStartFreq = 1000.0f;
    bool soloHeld = false;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> shapeAttach, slopeAttach, placeAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> dynAttach, autoAttach, scAttach, specAttach, onAttach;
    std::vector<std::pair<int, cincel::BandParams>> relativeStart;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandPanel)
};
