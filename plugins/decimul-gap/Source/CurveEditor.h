#pragma once

#include <DecimuLookAndFeel.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "GapCurve.h"

/**
    Editor de la curva de bombeo.

    - Arrastrar un punto: lo mueve (los extremos solo suben y bajan).
    - Arrastrar el rombo del medio de un tramo: curva ese tramo.
    - Doble clic en un espacio vacío: agrega un punto. Doble clic (o clic derecho) sobre
      un punto: lo borra.
    - Con Shift apretado, los puntos se pegan a una grilla de 1/16 en x y 5 % en y.
*/
class GapCurveEditor : public juce::Component
{
public:
    static constexpr int maxPoints = 24;

    std::function<void(const GapCurve&)> onCurveChanged;

    void setCurve(const GapCurve& newCurve);
    const GapCurve& getCurve() const { return curve; }

    /** `phase` y `gain` actuales del plugin, para dibujar el cabezal. */
    void setPlayState(float phase, float gain);
    void setDepth(float newDepth);
    void setGridDivisions(int divisions);
    void setAccentActive(bool active);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toPixel(float x, float y) const;
    float xFromPixel(float px) const;
    float yFromPixel(float py) const;

    int hitPoint(juce::Point<float> p) const;
    int hitBendHandle(juce::Point<float> p) const;
    juce::Point<float> bendHandlePosition(int segment) const;
    void notifyChanged();

    GapCurve curve;
    float phase = 0.0f, gain = 1.0f, depth = 1.0f;
    int gridDivisions = 4;

    int dragPoint = -1, dragBend = -1;
    int hoverPoint = -1, hoverBend = -1;
};
