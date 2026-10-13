#pragma once

#include <DecimuLookAndFeel.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include "BandAverager.h"
#include "ReferenceTrack.h"

/** Botón de slot de referencia: número + nombre corto + estado (vacío / cargado /
    cargando / error). Click izquierdo = elegir (o cargar si está vacío); click
    derecho = menú. */
class VersusSlotButton : public juce::Button
{
public:
    struct State
    {
        juce::String name;
        bool loaded = false;
        bool loading = false;
        bool hasError = false;
        bool selected = false;
    };

    explicit VersusSlotButton(int slotIndex) : juce::Button("slot"), index(slotIndex) {}

    void setState(const State& s)
    {
        if (s.name != state.name || s.loaded != state.loaded || s.loading != state.loading
            || s.hasError != state.hasError || s.selected != state.selected)
        {
            state = s;
            repaint();
        }
    }

    std::function<void()> onContextMenu;

    void paintButton(juce::Graphics&, bool isMouseOver, bool isDown) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    int index;
    State state;
};

/** Forma de onda de la referencia elegida, con cursor de reproducción y región de
    loop. Click = saltar; arrastrar = marcar loop; doble click = borrar loop. */
class VersusWaveformView : public juce::Component
{
public:
    void setTrack(std::shared_ptr<const ReferenceTrack> t);
    void setStatusText(const juce::String& text) { if (text != status) { status = text; repaint(); } }
    void setPlayhead(double seconds);
    void setLoop(double startSeconds, double endSeconds, bool enabled);

    std::function<void(double)> onSeek;
    std::function<void(double, double)> onLoop;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    double xToSeconds(float x) const;
    float secondsToX(double seconds) const;

    std::shared_ptr<const ReferenceTrack> track;
    juce::String status;
    double playhead = 0.0;
    double loopStart = 0.0, loopEnd = 0.0;
    bool loopOn = false;
    bool dragging = false;
    float dragStartX = 0.0f, dragCurrentX = 0.0f;
};

/** Espectro por bandas de 1/3 de octava: lado A vs. lado B (con el volumen igualado
    aplicado), o la curva de diferencia por banda. Sirve igual para el promedio de largo
    plazo y para el tiempo real: quien lo usa elige qué arreglos le pasa. */
class VersusSpectrumView : public juce::Component
{
public:
    struct Data
    {
        bool mixValid = false, refValid = false;
        std::array<float, VersusBands::numBands> mix {}, ref {};
        float mixGainDb = 0.0f;     // ganancia aplicada al lado A (0 si es la mezcla)
        float refGainDb = 0.0f;     // ganancia aplicada al lado B
        juce::String mixName { "MEZCLA" }, refName { "REFERENCIA (volumen igualado)" };
        juce::String emptyHint;
    };

    void setData(const Data& d) { data = d; repaint(); }
    void setShowDiff(bool shouldShow) { showDiff = shouldShow; repaint(); }

    void paint(juce::Graphics&) override;

private:
    Data data;
    bool showDiff = false;
};

/** Tabla comparativa MIX / REF / diferencia. */
class VersusMetricsView : public juce::Component
{
public:
    struct Data
    {
        bool mixValid = false, refValid = false;
        float mixLufsI = -100, mixLufsS = -100, refLufsI = -100, refLufsS = -100;
        float mixTruePeak = -100, refTruePeak = -100;
        float mixLra = 0, refLra = 0;
        float mixWidth = 0, refWidth = 0;
        float mixCorr = 1, refCorr = 1;
        float gainAppliedDb = 0;
        bool gainLimited = false;
        bool matchAuto = true;
        juce::String leftName { "MIX" }, rightName { "REF" };
    };

    void setData(const Data& d) { data = d; repaint(); }
    void paint(juce::Graphics&) override;

private:
    Data data;
};
