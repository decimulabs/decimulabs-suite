#pragma once

#include "PluginProcessor.h"

/**
    Editor placeholder. Todavía sin diseño visual — solo confirma que el plugin
    carga y muestra los nombres de los parámetros, para validar que la cadena de
    build (CMake + JUCE) funciona antes de invertir tiempo en la UI final.

    Próximo paso (ver SPEC.md "Próximo paso"): reemplazar este editor por el
    layout de los 9 módulos con la paleta/branding de decimulabs.
*/
class DecimulAcousticEditor : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit DecimulAcousticEditor(DecimulAcousticProcessor&);
    ~DecimulAcousticEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    DecimulAcousticProcessor& processor;
    juce::Label placeholderLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimulAcousticEditor)
};
