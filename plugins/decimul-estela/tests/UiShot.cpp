// Renderiza el editor de Estela a un PNG (sin DAW). Uso: EstelaUiShot.exe ruta.png
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    DecimulEstelaProcessor proc;
    proc.duckGain.store(0.18f);
    proc.peakL.store(-9.0f);
    proc.peakR.store(-11.0f);
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(250);
    auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
    juce::File out(argc > 1 ? argv[1] : "estela.png");
    out.deleteFile();
    juce::FileOutputStream stream(out);
    juce::PNGImageFormat png;
    return stream.openedOk() && png.writeImageToStream(image, stream) ? 0 : 1;
}
