#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <DecimuLookAndFeel.h>

/**
    Estética "16 bits" de DecimuLabs FM: paleta del chip de video de la Mega Drive (512 colores,
    3 bits por canal), letra 5x7 propia (la del Generador 16 Bit), fondo cuadriculado, pantallas
    tipo tubo, arranque de consola y la góndola de cartuchos. Todo dibujado por código: nada de
    logos, cajas ni tipografías de Sega.
*/
namespace retro
{
    /** Lleva un color al más cercano de los 512 que puede mostrar el chip (niveles medidos 0/52/87/116/144/172/206/255). */
    juce::Colour md9(juce::Colour c);
    juce::Colour md9(uint8_t r, uint8_t g, uint8_t b);

    /** Mayúsculas que también pasan las letras con tilde (á→Á, ñ→Ñ). */
    juce::String upper(const juce::String&);
    /** Dibuja una imagen (logo) de un solo color claro, usando solo su forma. */
    void drawTinted(juce::Graphics&, const juce::Image&, juce::Rectangle<float>, juce::Colour);

    /** Ancho en píxeles de un texto con la letra 5x7 a una escala entera. */
    int textWidth(const juce::String& text, int scale);
    /** Dibuja texto con la letra 5x7 (escala entera, sin suavizar). `shadow` = sombra negra de 1 punto. */
    void drawText(juce::Graphics&, const juce::String& text, juce::Rectangle<int> area, int scale,
                  juce::Colour colour, juce::Justification just = juce::Justification::centredLeft, bool shadow = true);

    /** Igual que drawText pero con degradé vertical por renglón (de top a bottom) y contorno negro, como los títulos de los juegos. */
    void drawTextGradient(juce::Graphics&, const juce::String& text, juce::Rectangle<int> area, int scale,
                          juce::Colour top, juce::Colour bottom, juce::Justification just = juce::Justification::centredLeft);

    /** Ventana de menú de 16 bits: degradé por bandas (los escalones de color del chip) y doble borde claro. */
    void paintWindow(juce::Graphics&, juce::Rectangle<float> r, juce::Colour top, juce::Colour bottom, float corner = 6.0f);

    /** Fondo negro con la grilla blanca fina. */
    void paintGrid(juce::Graphics&, juce::Rectangle<float> bounds, float spacing = 22.0f, float corner = 14.0f);
    /** Líneas de barrido y viñeta de tele de tubo sobre un área. */
    void paintCrt(juce::Graphics&, juce::Rectangle<float> bounds, float corner = 4.0f);

    /** Sprites de 16x16 de cada familia de sonidos. index 0..7 (orden de las categorías). */
    void drawSprite(juce::Graphics&, int index, juce::Rectangle<int> area, juce::Colour accent);
    juce::Colour familyColour(int index);

    /** Apariencia propia del sinte: la de la suite + números, botones y menús en 16 bits. */
    class FMLookAndFeel : public DecimuLookAndFeel
    {
    public:
        using DecimuLookAndFeel::DecimuLookAndFeel;
        void drawLabel(juce::Graphics&, juce::Label&) override;
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
        void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
        void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                               bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
                               const juce::Drawable* icon, const juce::Colour* textColour) override;
        void drawPopupMenuSectionHeader(juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& name) override;
        void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int standardHeight, int& w, int& h) override;
    };

    /** Rótulo con la letra pixelada (reemplaza a juce::Label en los títulos). */
    class PixelLabel : public juce::Component
    {
    public:
        PixelLabel(const juce::String& t = {}, juce::Colour c = juce::Colours::white, int s = 2) : text(t), colour(c), scale(s)
        { setInterceptsMouseClicks(false, false); }
        void set(const juce::String& t, juce::Colour c) { if (t != text || c != colour) { text = t; colour = c; repaint(); } }
        void paint(juce::Graphics& g) override
        {
            int s = scale;
            while (s > 1 && textWidth(text, s) > getWidth()) --s;   // si no entra, más chica
            drawText(g, text, getLocalBounds(), s, colour, just);
        }
        juce::String text; juce::Colour colour; int scale; juce::Justification just = juce::Justification::centred;
    };

    /** Pantalla de arranque de "consola" (una vez por sesión del DAW; un click la saltea). */
    class BootScreen : public juce::Component, private juce::Timer
    {
    public:
        BootScreen(juce::Image dog);
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override { finish(); }
        static bool alreadyShown;
        std::function<void()> onDone;
    private:
        void timerCallback() override;
        void finish();
        juce::Image dog;
        double t0 = 0;
    };

    /** Góndola de cartuchos: una "caja" por familia; al elegir una, la lista de sus sonidos. */
    class CartridgeShelf : public juce::Component, private juce::Timer
    {
    public:
        struct Family { juce::String name; juce::Array<int> programs; juce::StringArray names; };
        CartridgeShelf(juce::Array<Family> families, std::function<void(int)> onPick);
        void paint(juce::Graphics&) override;
        void resized() override;
        void mouseMove(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override { hover = -1; repaint(); }
        void mouseDown(const juce::MouseEvent&) override;
        bool keyPressed(const juce::KeyPress&) override;
        void setCurrent(int program) { current = program; repaint(); }
        void openFamily(int i) { open = i; repaint(); }
        std::function<void()> onClose;
        void visibilityChanged() override { if (isVisible()) startTimerHz(30); else stopTimer(); }
    private:
        void timerCallback() override { if (open < 0 && hover >= 0) repaint(boxRect(hover).expanded(12)); }
        juce::Rectangle<int> boxRect(int i) const;
        juce::Rectangle<int> itemRect(int i) const;
        juce::Rectangle<int> closeRect() const;
        void paintBox(juce::Graphics&, int i, juce::Rectangle<int> r, bool hot) const;
        juce::Array<Family> fams;
        std::function<void(int)> pick;
        int open = -1, hover = -1, current = -1;
    };
}
