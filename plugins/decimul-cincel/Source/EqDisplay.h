#pragma once

#include "PluginProcessor.h"
#include <DecimuLookAndFeel.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

/**
    La pantalla de Cincel: el analizador de espectro de fondo y encima la curva del EQ con un
    punto por banda. Todo se hace con el mouse, como en el Pro-Q:

      - doble clic en un lugar vacío: banda nueva (pegado a los bordes, un corte);
      - arrastrar un punto: frecuencia y ganancia (Ctrl = fino; en los cortes, vertical = Q);
      - rueda sobre un punto: Q (Alt + rueda: rango dinámico);
      - Alt mientras se arrastra: se escucha la banda sola;
      - doble clic en un punto: prende / apaga la banda; clic derecho: menú de la banda;
      - arrastrar en el vacío: seleccionar varias; Shift + clic: sumar a la selección;
      - Supr: borrar las bandas elegidas;
      - pasar por un pico del espectro y agarrarlo: banda nueva justo ahí ("spectrum grab");
      - modo SKETCH: se dibuja la curva y se arman las bandas solas.
*/
class EqDisplay : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    explicit EqDisplay(DecimulCincelProcessor&);

    /** Lee bandas, dinámica y analizadores; se llama desde el timer del editor. */
    void refresh();

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

    // ---- Selección (la comparte con el panel de banda) ----
    const std::vector<int>& getSelection() const { return selection; }
    int getPrimary() const { return primary; }
    /** Punto de la pantalla para una frecuencia y una ganancia (lo usan los tests). */
    juce::Point<float> pointFor(double hz, double db) const { return { xForHz(hz), yForDb(db) }; }

    /** Altura del punto de la banda elegida, de 0 (arriba) a 1 (abajo); -1 si no hay. */
    float primaryDotHeight() const
    {
        return primary >= 0 && getHeight() > 0 ? dotPosition(primary).y / (float) getHeight() : -1.0f;
    }
    void selectOnly(int band);
    std::function<void()> onSelectionChanged;

    // ---- Opciones ----
    bool sketchMode = false;
    std::function<void()> onSketchFinished;
    int overlayInstance = 0;           // 0 = ninguna; si no, el id de otra instancia de Cincel
    bool spanish = false;

    static juce::Colour bandColour(int index);
    static juce::String noteName(double hz);
    static juce::String formatHz(double hz);

    /** Rango vertical del EQ en dB (3, 6, 12 o 30) y del analizador (60, 90, 120). */
    float eqRangeDb() const;
    float analyzerRangeDb() const;

    /** El menú de opciones de la pantalla y del analizador (también con clic derecho en el vacío). */
    void showBackgroundMenu();
    void deleteSelection();

private:
    // Coordenadas
    float xForHz(double hz) const;
    double hzForX(float x) const;
    float yForDb(double db) const;
    double dbForY(float y) const;
    float yForAnalyzer(double db, double hz) const;
    juce::Point<float> dotPosition(int band) const;
    double displayedGain(int band) const;

    int hitBand(juce::Point<float> p) const;
    int hitRangeHandle(juce::Point<float> p) const;
    int findSpectrumPeak(juce::Point<float> p) const;

    void paintGrid(juce::Graphics&);
    void paintSpectra(juce::Graphics&);
    void paintCollisions(juce::Graphics&);
    void paintCurves(juce::Graphics&);
    void paintSoloShade(juce::Graphics&);
    void paintDots(juce::Graphics&);
    void paintReadout(juce::Graphics&, int band);
    void paintPiano(juce::Graphics&);

    juce::Path curvePath(const curves::Curve& c) const;
    void createBandAt(juce::Point<float> p, bool fromPeak, double peakHz);
    void showBandMenu(int band);
    void setSolo(int band);
    void finishSketch();
    juce::Font mono(float h) const;

    DecimulCincelProcessor& proc;
    juce::Rectangle<float> plot;      // zona del gráfico (sin rótulos)
    juce::Rectangle<float> pianoArea;

    std::array<cincel::BandParams, cincel::maxBands> bands {};
    std::array<curves::Curve, cincel::maxBands> bandCurves {}, staticCurves {};
    std::array<cincel::BandParams, cincel::maxBands> curveKey {};
    std::array<float, cincel::maxBands> curveDynKey {};
    std::array<bool, cincel::maxBands> curveValid {};
    std::array<float, cincel::maxBands> dynOffset {};
    curves::Curve mainCurve {}, leftCurve {}, rightCurve {}, sideCurve {};
    bool anyLR = false, anySide = false;
    double gainScale = 1.0;
    bool gainQ = false, linearPhase = false;

    InstanceRegistry::Entry overlay;
    bool overlayValid = false;

    std::vector<int> selection;
    int primary = -1;
    int hovered = -1;
    int grabPeak = -1;
    juce::Point<float> mousePos;
    bool mouseInside = false;

    enum class Drag { None, Bands, Range, Lasso, Sketch };
    Drag drag = Drag::None;
    int dragBand = -1;
    juce::Point<float> dragStart;
    struct Start
    {
        int band;
        double freq, gain, q, range;
    };
    std::vector<Start> starts;
    bool soloFromDrag = false;
    juce::Rectangle<float> lasso;
    std::vector<juce::Point<float>> sketch;
    juce::int64 lastSignature = 0;
    bool forceRepaint = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqDisplay)
};
