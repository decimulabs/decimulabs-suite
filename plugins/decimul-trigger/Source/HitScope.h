#pragma once

#include <DecimuLookAndFeel.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <deque>
#include <functional>

/**
    Monitor de golpes: una ventana de unos 4 segundos con el nivel de la señal de
    detección (en dB) y cada golpe detectado marcado con una barra cuya altura es su
    velocity. Las dos líneas horizontales son el umbral (Threshold) y el nivel que da la
    velocity máxima (Max Level): se arrastran con el mouse para ajustarlas viendo el
    audio, que es como se calibra un trigger de verdad.
*/
class HitScope : public juce::Component
{
public:
    static constexpr int historyFrames = 800;      // a 200 cuadros por segundo = 4 s
    static constexpr float floorDb = -66.0f;

    struct Frame
    {
        float level = 0.0f;
        int velocity = 0;
    };

    void pushFrame(float levelLinear, int velocity);

    void setThresholdDb(float db);
    void setMaxLevelDb(float db);

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
};

/** Lectura grande del último golpe: velocity, y un destello cada vez que llega uno. */
class HitReadout : public juce::Component
{
public:
    void setState(int velocity, const juce::String& noteName, int hits, float flash);
    void paint(juce::Graphics&) override;

private:
    int velocity = 0, hits = 0;
    juce::String noteName;
    float flash = 0.0f;
};
