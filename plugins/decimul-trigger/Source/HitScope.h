#pragma once

#include <DecimuLookAndFeel.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <deque>
#include <functional>
#include "SpectralFingerprint.h"

/**
    Monitor de golpes: una ventana de unos 4 segundos con el nivel de la señal de
    detección (en dB) y cada golpe detectado marcado con una barra cuya altura es su
    velocity (en gris y tachado si la huella lo rechazó como bleed). Las dos líneas
    horizontales son el umbral (Threshold) y el nivel que da la velocity máxima (Max
    Level): se arrastran con el mouse para ajustarlas viendo el audio, que es como se
    calibra un trigger de verdad.
*/
class HitScope : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    static constexpr int historyFrames = 800;      // a 200 cuadros por segundo = 4 s
    static constexpr float floorDb = -66.0f;

    struct Frame
    {
        float level = 0.0f;
        int velocity = 0;      // > 0 golpe, < 0 rechazado
    };

    void pushFrame(float levelLinear, int velocity);

    void setThresholdDb(float db);
    void setMaxLevelDb(float db);

    /** Menor que 0: nada. De 0 a 1: avance de la calibración (muestra un cartel arriba). */
    void setCalibration(float progress);

    /** Respuesta del filtro de detección (Low / High Cut): se dibuja como curva mientras
        visible (al tocar los knobs o al escuchar la detección). */
    void setFilterView(float lowHz, float highHz, bool visible);

    /** Suena la señal de detección en vez del audio (botón "oír detección"). */
    void setListening(bool isListening);

    std::function<void(float)> onThresholdDragged;
    std::function<void(float)> onMaxLevelDragged;

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plotArea() const;
    float dbToY(float db) const;
    float yToDb(float y) const;
    int hitLine(float y) const;   // 0 = threshold, 1 = max level, -1 = ninguna

    std::deque<Frame> history;
    float thresholdDb = -30.0f, maxLevelDb = -6.0f;
    int hoverLine = -1, dragLine = -1;
    float calibration = -1.0f;
    float filterLowHz = 60.0f, filterHighHz = 14000.0f;
    bool filterVisible = false, listening = false;
};

/** Lectura grande del último golpe: velocity, parecido con la huella y un destello. */
class HitReadout : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    struct State
    {
        int velocity = 0;
        juce::String noteName;
        int hits = 0;
        int rejected = 0;
        float similarity = -1.0f;      // -1 = sin dato
        bool fingerprintOn = false;
        float flash = 0.0f;
    };

    void setState(const State& s);
    void paint(juce::Graphics&) override;

private:
    State state;
};

/** Forma espectral de la huella aprendida (barras) y del último golpe (marcas). */
class FingerprintView : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    void setData(const FingerprintBank::Vector& templateVector, bool templateValid,
                 const FingerprintBank::Vector& lastVector, bool lastValid, bool learning);
    void paint(juce::Graphics&) override;

private:
    FingerprintBank::Vector tpl {}, last {};
    bool tplValid = false, lastOk = false, isLearning = false;
};
