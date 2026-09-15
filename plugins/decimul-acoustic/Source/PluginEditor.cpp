#include "PluginEditor.h"
#include "BinaryData.h"

void DecimulAcousticEditor::SimpleMeter::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    constexpr float minDb = -60.0f, maxDb = 0.0f;
    const float norm = juce::jlimit(0.0f, 1.0f, (levelDb - minDb) / (maxDb - minDb));

    auto bounds = getLocalBounds().toFloat();
    auto filled = bounds.removeFromBottom(bounds.getHeight() * norm);
    g.setColour(DecimuColours::accentCyan);
    g.fillRect(filled);

    g.setColour(DecimuColours::textMuted);
    g.drawRect(getLocalBounds());
}

void DecimulAcousticEditor::setupFader(juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, DecimuColours::text);
    addAndMakeVisible(label);
}

void DecimulAcousticEditor::setupCombo(juce::ComboBox& box, const juce::StringArray& items)
{
    box.addItemList(items, 1);
    box.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(box);
}

DecimulAcousticEditor::DecimulAcousticEditor(DecimulAcousticProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    setupFader(inputSlider, inputLabel, "Input");
    setupFader(lowSlider, lowLabel, "Low");
    setupFader(highSlider, highLabel, "High");
    setupFader(compSlider, compLabel, "Dynamics");
    setupFader(spaceASlider, spaceALabel, "Space A");
    setupFader(spaceBSlider, spaceBLabel, "Space B");
    setupFader(echoSlider, echoLabel, "Echo");
    setupFader(blendSlider, blendLabel, "Blend");
    setupFader(outputSlider, outputLabel, "Output");

    setupCombo(lowModeBox, { "Warm", "Deep", "Air-Cut" });
    setupCombo(highModeBox, { "Silk", "Sheen", "Crisp" });
    setupCombo(compModeBox, { "Glue", "Snap", "Squash" });
    setupCombo(spaceATypeBox, { "Room", "Hall", "Cathedral" });
    setupCombo(spaceBTypeBox, { "Close", "Wide", "Vast" });
    setupCombo(echoDivisionBox, { "Slap", "8th", "Quarter" });

    addAndMakeVisible(bypassButton);
    addAndMakeVisible(tapTempoButton);
    tapTempoButton.onClick = [this] { processorRef.tapTempo(); };

    bpmLabel.setJustificationType(juce::Justification::centred);
    bpmLabel.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    addAndMakeVisible(bpmLabel);

    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outputMeter);

    auto& apvts = processorRef.apvts;
    inputAttach   = std::make_unique<SliderAttachment>(apvts, ParamIDs::inputTrim, inputSlider);
    lowAttach     = std::make_unique<SliderAttachment>(apvts, ParamIDs::lowGain, lowSlider);
    highAttach    = std::make_unique<SliderAttachment>(apvts, ParamIDs::highGain, highSlider);
    compAttach    = std::make_unique<SliderAttachment>(apvts, ParamIDs::compAmount, compSlider);
    spaceAAttach  = std::make_unique<SliderAttachment>(apvts, ParamIDs::spaceAMix, spaceASlider);
    spaceBAttach  = std::make_unique<SliderAttachment>(apvts, ParamIDs::spaceBMix, spaceBSlider);
    echoAttach    = std::make_unique<SliderAttachment>(apvts, ParamIDs::echoMix, echoSlider);
    blendAttach   = std::make_unique<SliderAttachment>(apvts, ParamIDs::blend, blendSlider);
    outputAttach  = std::make_unique<SliderAttachment>(apvts, ParamIDs::outputTrim, outputSlider);

    lowModeAttach     = std::make_unique<ComboAttachment>(apvts, ParamIDs::lowMode, lowModeBox);
    highModeAttach    = std::make_unique<ComboAttachment>(apvts, ParamIDs::highMode, highModeBox);
    compModeAttach    = std::make_unique<ComboAttachment>(apvts, ParamIDs::compMode, compModeBox);
    spaceATypeAttach  = std::make_unique<ComboAttachment>(apvts, ParamIDs::spaceAType, spaceATypeBox);
    spaceBTypeAttach  = std::make_unique<ComboAttachment>(apvts, ParamIDs::spaceBType, spaceBTypeBox);
    echoDivisionAttach = std::make_unique<ComboAttachment>(apvts, ParamIDs::echoDivision, echoDivisionBox);

    bypassAttach = std::make_unique<ButtonAttachment>(apvts, ParamIDs::bypass, bypassButton);

    setSize(980, 500);
    setResizable(true, true);
    startTimerHz(30);
}

DecimulAcousticEditor::~DecimulAcousticEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DecimulAcousticEditor::paint(juce::Graphics& g)
{
    g.fillAll(DecimuColours::background);

    // Marco tipo "visor de cámara" (referencia al HUD del sitio: REC / timecode /
    // marco de encuadre), sin reproducir su contenido, solo el gesto de un borde fino.
    g.setColour(DecimuColours::textMuted.withAlpha(0.35f));
    g.drawRect(getLocalBounds(), 1);
    g.drawLine(0.0f, 54.0f, (float) getWidth(), 54.0f, 1.0f);
    g.drawLine(0.0f, (float) getHeight() - 22.0f, (float) getWidth(), (float) getHeight() - 22.0f, 1.0f);

    // Copia local: headerLogoArea es un miembro (calculado una vez en resized()) y acá
    // solo lo consultamos — si le hiciéramos removeFromLeft() directo se iría achicando
    // en cada repintado sucesivo del editor.
    auto logoArea = headerLogoArea;
    if (dogImage.isValid())
    {
        auto dogArea = logoArea.removeFromLeft(logoArea.getHeight());
        g.drawImage(dogImage, dogArea.toFloat(), juce::RectanglePlacement::centred);
        logoArea.removeFromLeft(8);
    }
    if (wordmarkImage.isValid())
    {
        auto wordmarkArea = logoArea.withSizeKeepingCentre(
            juce::jmin(logoArea.getWidth(), 220), logoArea.getHeight() - 12);
        g.drawImage(wordmarkImage, wordmarkArea.toFloat(), juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
    }

    // Pie de página tipo HUD ("AV-1 ... PRESS PLAY" del sitio, reinterpretado).
    g.setColour(DecimuColours::textMuted);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    auto footer = getLocalBounds().removeFromBottom(22).reduced(10, 3);
    g.drawText("DECIMULABS · ACOUSTIC · GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulAcousticEditor::resized()
{
    auto bounds = getLocalBounds();

    // --- Header: logo (dibujado en paint()) + bypass/REC ---
    auto header = bounds.removeFromTop(54).reduced(10, 8);
    bypassButton.setBounds(header.removeFromRight(110));
    headerLogoArea = header;

    bounds.removeFromBottom(22); // footer HUD, dibujado en paint()
    bounds = bounds.reduced(10, 10);

    // --- Banco de faders (orden tipo CLA Unplugged: input | 6 módulos | blend | output) ---
    auto faderArea = bounds;
    const int numColumns = 9;
    const int columnWidth = faderArea.getWidth() / numColumns;

    auto layoutSimpleColumn = [&](juce::Rectangle<int> col, SimpleMeter* meter,
                                   juce::Slider& slider, juce::Label& label)
    {
        col = col.reduced(4);
        auto labelArea = col.removeFromBottom(20);
        label.setBounds(labelArea);

        if (meter != nullptr)
        {
            auto meterArea = col.removeFromLeft(16);
            meter->setBounds(meterArea);
        }
        slider.setBounds(col);
    };

    auto layoutComboColumn = [&](juce::Rectangle<int> col, juce::ComboBox& box,
                                  juce::Slider& slider, juce::Label& label)
    {
        col = col.reduced(4);
        auto comboArea = col.removeFromTop(24);
        box.setBounds(comboArea);
        auto labelArea = col.removeFromBottom(20);
        label.setBounds(labelArea);
        slider.setBounds(col);
    };

    layoutSimpleColumn(faderArea.removeFromLeft(columnWidth), &inputMeter, inputSlider, inputLabel);
    layoutComboColumn(faderArea.removeFromLeft(columnWidth), lowModeBox, lowSlider, lowLabel);
    layoutComboColumn(faderArea.removeFromLeft(columnWidth), highModeBox, highSlider, highLabel);
    layoutComboColumn(faderArea.removeFromLeft(columnWidth), compModeBox, compSlider, compLabel);
    layoutComboColumn(faderArea.removeFromLeft(columnWidth), spaceATypeBox, spaceASlider, spaceALabel);
    layoutComboColumn(faderArea.removeFromLeft(columnWidth), spaceBTypeBox, spaceBSlider, spaceBLabel);
    layoutComboColumn(faderArea.removeFromLeft(columnWidth), echoDivisionBox, echoSlider, echoLabel);

    // --- Blend: comparte columna con el botón de Tap Tempo + BPM ---
    {
        auto col = faderArea.removeFromLeft(columnWidth).reduced(4);
        auto tapArea = col.removeFromTop(24);
        tapTempoButton.setBounds(tapArea);
        auto bpmArea = col.removeFromTop(16);
        bpmLabel.setBounds(bpmArea);
        auto labelArea = col.removeFromBottom(20);
        blendLabel.setBounds(labelArea);
        blendSlider.setBounds(col);
    }

    layoutSimpleColumn(faderArea, &outputMeter, outputSlider, outputLabel);
}

void DecimulAcousticEditor::timerCallback()
{
    inputMeter.levelDb = processorRef.inputPeakDb.load();
    inputMeter.repaint();

    outputMeter.levelDb = processorRef.outputPeakDb.load();
    outputMeter.repaint();

    bpmLabel.setText(juce::String((int) processorRef.manualBpm.load()) + " BPM",
                      juce::dontSendNotification);
}
