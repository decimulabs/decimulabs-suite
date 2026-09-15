#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Paleta e identidad visual tomadas del sitio real de decimulabs (decimulabs.com):
    fondo casi negro, texto gris-azulado, acentos rojo/naranja (el punto "REC" del
    sitio), cian y azul. Tipografías: Chakra Petch (texto de módulos, igual que el
    body del sitio) + VT323 (números/HUD, igual que el contador "REC 0:00" del sitio).
    Ver Assets/README.md para el origen y la licencia de cada asset.
*/
namespace DecimuColours
{
    const juce::Colour background     { 0xff06070D };
    const juce::Colour panel          { 0xff0D0F18 };
    const juce::Colour text           { 0xffC9D2E0 };
    const juce::Colour textMuted      { 0xff7A87A3 };
    const juce::Colour accentRec      { 0xffFF5A36 };
    const juce::Colour accentCyan     { 0xffA6F0FF };
    const juce::Colour accentBlue     { 0xff3D7BFF };
    const juce::Colour accentBlueSoft { 0xff7FA8FF };
    const juce::Colour trackFill      { 0xff11131A };
}

class DecimuLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DecimuLookAndFeel();

    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;

    // El toggle de Bypass se dibuja como el indicador "REC" del sitio: punto
    // encendido (rojo, acentRec) + texto "REC" cuando el plugin está procesando
    // (no bypassed); punto apagado + "BYPASS" en gris cuando está en bypass.
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    juce::Typeface::Ptr chakraRegular, chakraBold, vt323;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimuLookAndFeel)
};
