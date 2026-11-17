// Captura de la ventana sin DAW: UiShot.exe salida.png [programa] [ancho alto]
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/PluginProcessor.h"
#include "../Source/Retro.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out(argc > 1 ? juce::String(argv[1]) : juce::String("fm_ui.png"));
    // modo (argumento 5): normal | shelf | shelfopen | boot
    const juce::String mode = argc > 5 ? juce::String(argv[5]) : juce::String("normal");
    retro::BootScreen::alreadyShown = mode != "boot";
    DecimulFMProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    if (argc > 2) proc.setCurrentProgram(juce::String(argv[2]).getIntValue());
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    if (argc > 4) ed->setSize(juce::String(argv[3]).getIntValue(), juce::String(argv[4]).getIntValue());
    // un par de vueltas del timer para que la UI lea los registros
    juce::MidiBuffer m; juce::AudioBuffer<float> b(2, 512); proc.processBlock(b, m);
    for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
    for (auto* c : ed->getChildren())
        if (auto* sh = dynamic_cast<retro::CartridgeShelf*>(c))
            if (mode.startsWith("shelf")) { sh->setVisible(true); sh->toFront(false); if (mode == "shelfopen") { sh->setCurrent(proc.getCurrentProgram()); sh->openFamily(1); } }
    if (mode == "boot") juce::MessageManager::getInstance()->runDispatchLoopUntil(1000);
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
    if (mode == "menu")
    {
        // dibuja el desplegable de sonidos con la apariencia del sinte, como se vería abierto
        auto& laf = ed->getLookAndFeel();
        const int w = 300, rowH = 24; juce::StringArray items { "Bajo slap", "Bajo sinte", "Bajo picado", "Bajo profundo" };
        juce::Graphics g(img);
        g.setOrigin(600, 70);
        laf.drawPopupMenuBackground(g, w, rowH * 6 + 8);
        laf.drawPopupMenuSectionHeader(g, { 0, 4, w, rowH }, juce::String::fromUTF8("Bajos"));
        for (int i = 0; i < items.size(); ++i)
            laf.drawPopupMenuItem(g, { 0, 4 + rowH * (i + 1), w, rowH }, false, true, i == 2, i == 0, false, items[i], {}, nullptr, nullptr);
        laf.drawPopupMenuSectionHeader(g, { 0, 4 + rowH * 5, w, rowH }, juce::String::fromUTF8("Teclados"));
    }
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);
    std::printf("%s %dx%d\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return 0;
}
