#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Paleta e identidad visual tomadas del sitio real de decimulabs (decimulabs.com):
    fondo casi negro, texto gris-azulado, acento rojo/naranja fijo (el punto "REC" del
    sitio). Tipografías: Chakra Petch (texto de módulos, igual que el body del sitio) +
    VT323 (números/HUD, igual que el contador "REC 0:00" del sitio). Ver
    shared/Assets/README.md para el origen y la licencia de cada asset.

    Esto vive en shared/ (no en un plugin puntual) porque es la base visual de TODA
    la suite decimulabs — cuando se sumen Guitars/Bass/Drums/etc., linkean esto mismo
    en vez de reimplementar knobs/faders/paleta desde cero. Ver shared/CMakeLists.txt.

    `accentPrimary`/`accentSecondary`/`accentSecondarySoft` son las dos únicas piezas
    de la paleta que varían por plugin (para que cada uno de la suite se sienta parte
    de la familia pero no sea un clon idéntico) — todo lo demás (fondo, texto, el rojo
    de REC) es fijo de marca. Se fijan una vez, al construir el DecimuLookAndFeel de
    cada plugin (ver DecimuAccentTheme más abajo); son variables `inline` (no
    `const`) a propósito, para poder cambiarlas — pero cada plugin es un binario
    separado, así que no hay riesgo de que un plugin le cambie el color a otro en
    tiempo real.
*/
namespace DecimuColours
{
    const juce::Colour background     { 0xff06070D };
    const juce::Colour panel          { 0xff0D0F18 };
    const juce::Colour text           { 0xffC9D2E0 };
    const juce::Colour textMuted      { 0xff7A87A3 };
    const juce::Colour accentRec      { 0xffFF5A36 };
    const juce::Colour trackFill      { 0xff11131A };

    inline juce::Colour accentPrimary      { 0xffA6F0FF }; // cian (default: DecimuLabs Acoustic)
    inline juce::Colour accentSecondary    { 0xff3D7BFF }; // azul
    inline juce::Colour accentSecondarySoft{ 0xff7FA8FF };
}

/** Un par de acentos (primario + secundario + variante suave del secundario) que
    identifica a un plugin de la suite. Agregá una factory acá por cada plugin nuevo. */
struct DecimuAccentTheme
{
    juce::Colour primary, secondary, secondarySoft;

    static DecimuAccentTheme cyanBlue()  { return { juce::Colour(0xffA6F0FF), juce::Colour(0xff3D7BFF), juce::Colour(0xff7FA8FF) }; }
    static DecimuAccentTheme amberGold() { return { juce::Colour(0xffFFC46A), juce::Colour(0xffD9862B), juce::Colour(0xffE8A65C) }; }
};

class DecimuLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit DecimuLookAndFeel(const DecimuAccentTheme& theme = DecimuAccentTheme::cyanBlue());

    // Para que otros helpers de dibujo (ver DecimuChrome.h) usen las mismas
    // tipografías de marca sin recargarlas desde BinaryData.
    juce::Font displayFont(float height, bool bold = false) const;
    juce::Font monoFont(float height) const;

    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;

    // El toggle de Bypass se dibuja como el indicador "REC" del sitio: punto
    // encendido (rojo, acentRec, pulsando) cuando el plugin está procesando (no
    // bypassed); punto apagado + "BYPASS" en gris cuando está en bypass.
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    juce::Typeface::Ptr chakraRegular, chakraBold, vt323;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimuLookAndFeel)
};
