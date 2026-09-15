#include "PluginEditor.h"

DecimulAcousticEditor::DecimulAcousticEditor(DecimulAcousticProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
    placeholderLabel.setText("Decimul Acoustic — build OK, falta UI (ver SPEC.md)",
                              juce::dontSendNotification);
    placeholderLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(placeholderLabel);

    setSize(600, 400);
    setResizable(true, true);
    startTimerHz(30);
}

DecimulAcousticEditor::~DecimulAcousticEditor()
{
    stopTimer();
}

void DecimulAcousticEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1c1e22));
}

void DecimulAcousticEditor::resized()
{
    placeholderLabel.setBounds(getLocalBounds());
}

void DecimulAcousticEditor::timerCallback()
{
    // Placeholder de lectura de meters — se reemplaza cuando existan los widgets reales.
    juce::ignoreUnused(processor.inputPeakDb.load());
}
