// Renderiza el editor de Rake a un PNG (sin DAW). Uso: RakeUiShot.exe ruta.png [acorde] [feedback] [damping]
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdlib>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    DecimulRakeProcessor proc;
    auto set = [&](const juce::String& id, float v) { if (auto* p = proc.apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v)); };
    if (argc > 2) set(ParamIDs::chord, (float) std::atof(argv[2]));
    if (argc > 3) set(ParamIDs::feedback, (float) std::atof(argv[3]));
    if (argc > 4) set(ParamIDs::damping, (float) std::atof(argv[4]));
    proc.peakL.store(-9.0f);
    proc.peakR.store(-11.0f);
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(250);
    auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
    juce::File out(argc > 1 ? argv[1] : "rake.png");
    out.deleteFile();
    juce::FileOutputStream stream(out);
    juce::PNGImageFormat png;
    return stream.openedOk() && png.writeImageToStream(image, stream) ? 0 : 1;
}
