#include "PluginEditor.h"

void DecimulAcousticEditor::SimpleMeter::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    constexpr float minDb = -60.0f, maxDb = 0.0f;
    const float norm = juce::jlimit(0.0f, 1.0f, (levelDb - minDb) / (maxDb - minDb));

    auto bounds = getLocalBounds().toFloat();
    auto filled = bounds.removeFromBottom(bounds.getHeight() * norm);
    g.setColour(juce::Colours::limegreen);
    g.fillRect(filled);

    g.setColour(juce::Colours::grey);
    g.drawRect(getLocalBounds());
}

void DecimulAcousticEditor::setupFader(juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colours::white);
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
    titleLabel.setText("DecimuLabs Acoustic", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

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
    bpmLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    bpmLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
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

    setSize(980, 480);
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
    g.setColour(juce::Colours::grey.withAlpha(0.5f));
    g.drawLine(0.0f, 46.0f, (float) getWidth(), 46.0f);
}

void DecimulAcousticEditor::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    // --- Header: título + bypass ---
    auto header = bounds.removeFromTop(36);
    bypassButton.setBounds(header.removeFromRight(90));
    titleLabel.setBounds(header);

    bounds.removeFromTop(10);

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
