#pragma once

#include "DecimuLookAndFeel.h"

/**
    Funciones de dibujo reutilizables para el "chasis" de hardware de la suite
    decimulabs: fondo de metal cepillado, tornillos Phillips en las esquinas,
    manijas de rack en los costados, compartimentos con bisel (con o sin tinte de
    color), y el LED que va arriba de cada knob. Todo esto es apariencia pura — no
    tienen estado ni lógica, son funciones libres que cualquier PluginEditor de la
    suite llama desde su paint().

    Por qué funciones libres y no Componentes: cada plugin (Acoustic, y a futuro
    Guitars/Bass/Drums) tiene su propia cantidad de módulos y su propio layout —
    lo único que se comparte de verdad es CÓMO se ve cada pieza, no la composición.
    Ver plugins/decimul-acoustic/Source/PluginEditor.cpp para un ejemplo de uso.
*/
namespace DecimuChrome
{
    // Fondo de metal cepillado con un brillo diagonal sutil, para el panel principal.
    void paintChassisBackground(juce::Graphics&, juce::Rectangle<float> bounds, float cornerRadius = 14.0f);

    // 4 tornillos Phillips (con cruz real) en las esquinas de `bounds`, a `inset` px del borde.
    void paintCornerScrews(juce::Graphics&, juce::Rectangle<float> bounds, float inset = 12.0f);

    // 2 manijas de rack (barras oscuras con textura) apenas salidas de los costados de `bounds`.
    void paintRackHandles(juce::Graphics&, juce::Rectangle<float> bounds);

    // Compartimento con bisel (sombra interior + borde superior más claro), como un
    // módulo de hardware atornillado al panel. `tinted` = variante con tinte de color
    // de acento (usar para agrupar módulos relacionados, p. ej. los dos canales de
    // reverb, o Input/Output).
    void paintCompartment(juce::Graphics&, juce::Rectangle<float> bounds, bool tinted = false, float cornerRadius = 8.0f);

    // LED cuadrado con brillo especular, para poner arriba de un knob o selector.
    void paintLed(juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour colour = DecimuColours::accentCyan);
}
