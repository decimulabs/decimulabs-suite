// Renderiza el editor de Acoustic a un PNG sin DAW ni ventana.
// Uso: AcousticUiShot.exe ruta.png
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    DecimulAcousticProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    // Deja correr el timer del editor para que dibuje su primer cuadro.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(400);
    auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);

    juce::File out(argc > 1 ? argv[1] : "acoustic.png");
    out.deleteFile();
    juce::FileOutputStream stream(out);
    juce::PNGImageFormat png;
    return stream.openedOk() && png.writeImageToStream(image, stream) ? 0 : 1;
}
