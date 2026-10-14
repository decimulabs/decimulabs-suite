// Renderiza el editor de Carve a un PNG con datos de ejemplo (sin DAW).
// Uso: CarveUiShot.exe ruta.png [sinSidechain]
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cmath>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const bool noSidechain = argc > 2;

    DecimulCarveProcessor proc;
    for (int b = 0; b < carve::numBands; ++b)
    {
        // Un "bombo": mucha energía abajo, cayendo hacia los agudos.
        const float t = (float) b / (float) (carve::numBands - 1);
        const float lvl = -14.0f - 46.0f * std::pow(t, 0.7f) + 6.0f * std::sin(b * 0.9f);
        proc.uiLevelDb[(size_t) b].store(lvl);
        proc.uiCutDb[(size_t) b].store(carve::cutForLevel(lvl, 0.6f));
    }
    proc.sidechainConnected.store(!noSidechain);
    proc.outputPeakDb.store(-12.0f);

    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    editor->setSize(1000, 560);
    // Deja correr el timer del editor para que tome los datos de ejemplo.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(250);
    auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);

    juce::File out(argc > 1 ? argv[1] : "carve.png");
    out.deleteFile();
    juce::FileOutputStream stream(out);
    juce::PNGImageFormat png;
    return stream.openedOk() && png.writeImageToStream(image, stream) ? 0 : 1;
}
