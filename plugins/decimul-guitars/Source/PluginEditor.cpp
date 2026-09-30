#include "PluginEditor.h"
#include "BinaryData.h"

void DecimulGuitarsEditor::setupFader(juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
    addAndMakeVisible(slider);

    label.setText("[ " + text.toUpperCase() + " ]", juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, DecimuColours::text);
    addAndMakeVisible(label);
}

void DecimulGuitarsEditor::setupKnob(juce::Slider& knob)
{
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible(knob);
}

void DecimulGuitarsEditor::setupSelector(DecimuLedStackSelector& sel, const juce::StringArray& items,
                                          const juce::String& paramID)
{
    sel.setOptions(items);
    sel.setSelectedIndex((int) processorRef.apvts.getRawParameterValue(paramID)->load());
    addAndMakeVisible(sel);

    sel.onChange = [this, paramID, count = items.size()](int idx)
    {
        if (auto* param = processorRef.apvts.getParameter(paramID))
            param->setValueNotifyingHost(idx / (float) juce::jmax(1, count - 1));
    };
}

DecimulGuitarsEditor::DecimulGuitarsEditor(DecimulGuitarsProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    setupFader(inputSlider, inputLabel, "Input");
    setupFader(gateSlider, gateLabel, "Gate");
    setupFader(driveSlider, driveLabel, "Drive");
    setupFader(toneSlider, toneLabel, "Tone");
    setupFader(modSlider, modLabel, "Mod");
    setupFader(spaceSlider, spaceLabel, "Space");
    setupFader(echoSlider, echoLabel, "Echo");
    setupFader(blendSlider, blendLabel, "Blend");
    setupFader(outputSlider, outputLabel, "Output");

    // Cab no tiene fader (es on/off, ver PluginProcessor.h) — solo el label bracket.
    cabLabel.setText("[ CAB ]", juce::dontSendNotification);
    cabLabel.setJustificationType(juce::Justification::centred);
    cabLabel.setColour(juce::Label::textColourId, DecimuColours::text);
    addAndMakeVisible(cabLabel);

    setupKnob(toneMidKnob);
    setupKnob(modRateKnob);
    setupKnob(spacePreDelayKnob);
    setupKnob(echoFeedbackKnob);

    auto setupKnobLabel = [this](juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(juce::FontOptions(10.0f)));
        l.setColour(juce::Label::textColourId, DecimuColours::textMuted);
        addAndMakeVisible(l);
    };
    setupKnobLabel(toneKnobLabel, "MID FREQ");
    setupKnobLabel(modKnobLabel, "RATE");
    setupKnobLabel(spaceKnobLabel, "PRE-DELAY");
    setupKnobLabel(echoKnobLabel, "FEEDBACK");

    setupSelector(gateModeSel, { "Fast", "Med", "Slow" }, ParamIDs::gateMode);
    setupSelector(driveModeSel, { "Tube", "Fuzz", "Boost" }, ParamIDs::driveMode);
    setupSelector(toneModeSel, { "Scoop", "Flat", "Bright" }, ParamIDs::toneMode);
    setupSelector(cabModeSel, { "Combo", "Stack", "DI" }, ParamIDs::cabMode);
    setupSelector(modModeSel, { "Chorus", "Flanger", "Phaser" }, ParamIDs::modMode);
    setupSelector(spaceModeSel, { "Room", "Plate", "Hall" }, ParamIDs::spaceMode);
    setupSelector(echoDivisionSel, { "Slap", "8th", "Quarter" }, ParamIDs::echoDivision);

    // LEDs clickeables: on/off de todo el módulo (feedback directo de testing real).
    for (auto* led : { &toneLedBtn, &modLedBtn, &spaceLedBtn, &echoLedBtn, &cabLedBtn })
        addAndMakeVisible(led);

    addAndMakeVisible(bypassButton);
    addAndMakeVisible(tapTempoButton);
    tapTempoButton.onClick = [this] { processorRef.tapTempo(); };

    bpmLabel.setJustificationType(juce::Justification::centred);
    bpmLabel.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    addAndMakeVisible(bpmLabel);

    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outputMeter);
    addAndMakeVisible(vuArc);

    auto& apvts = processorRef.apvts;
    inputAttach  = std::make_unique<SliderAttachment>(apvts, ParamIDs::inputTrim, inputSlider);
    gateAttach   = std::make_unique<SliderAttachment>(apvts, ParamIDs::gateAmount, gateSlider);
    driveAttach  = std::make_unique<SliderAttachment>(apvts, ParamIDs::driveAmount, driveSlider);
    toneAttach   = std::make_unique<SliderAttachment>(apvts, ParamIDs::toneTilt, toneSlider);
    modAttach    = std::make_unique<SliderAttachment>(apvts, ParamIDs::modAmount, modSlider);
    spaceAttach  = std::make_unique<SliderAttachment>(apvts, ParamIDs::spaceAmount, spaceSlider);
    echoAttach   = std::make_unique<SliderAttachment>(apvts, ParamIDs::echoAmount, echoSlider);
    blendAttach  = std::make_unique<SliderAttachment>(apvts, ParamIDs::blend, blendSlider);
    outputAttach = std::make_unique<SliderAttachment>(apvts, ParamIDs::outputTrim, outputSlider);

    toneMidAttach        = std::make_unique<SliderAttachment>(apvts, ParamIDs::toneMidFreq, toneMidKnob);
    modRateAttach        = std::make_unique<SliderAttachment>(apvts, ParamIDs::modRate, modRateKnob);
    spacePreDelayAttach  = std::make_unique<SliderAttachment>(apvts, ParamIDs::spacePreDelay, spacePreDelayKnob);
    echoFeedbackAttach   = std::make_unique<SliderAttachment>(apvts, ParamIDs::echoFeedback, echoFeedbackKnob);

    bypassAttach       = std::make_unique<ButtonAttachment>(apvts, ParamIDs::bypass, bypassButton);
    toneEnabledAttach  = std::make_unique<ButtonAttachment>(apvts, ParamIDs::toneEnabled, toneLedBtn);
    modEnabledAttach   = std::make_unique<ButtonAttachment>(apvts, ParamIDs::modEnabled, modLedBtn);
    spaceEnabledAttach = std::make_unique<ButtonAttachment>(apvts, ParamIDs::spaceEnabled, spaceLedBtn);
    echoEnabledAttach  = std::make_unique<ButtonAttachment>(apvts, ParamIDs::echoEnabled, echoLedBtn);
    cabEnabledAttach   = std::make_unique<ButtonAttachment>(apvts, ParamIDs::cabEnabled, cabLedBtn);

    setSize(1180, 560);
    setResizable(true, true);
    startTimerHz(30);
}

DecimulGuitarsEditor::~DecimulGuitarsEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DecimulGuitarsEditor::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    DecimuChrome::paintChassisBackground(g, full, 14.0f);
    DecimuChrome::paintRackHandles(g, full);
    DecimuChrome::paintCornerScrews(g, full, 12.0f);

    DecimuChrome::paintCompartment(g, topbarRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, inputCompRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, mainCompRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, toneCompRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, modCompRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, spaceCompRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, echoCompRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, blendCompRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, outputCompRect.toFloat(), true, 8.0f);

    // Los LEDs de Tone/Mod/Space/Echo/Cab ahora son DecimuLedButton (se pintan solos).

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
            juce::jmin(logoArea.getWidth(), 200), logoArea.getHeight() - 14);
        g.drawImage(wordmarkImage, wordmarkArea.toFloat(), juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
    }

    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(13.0f));
    auto footer = getLocalBounds().removeFromBottom(22).reduced(16, 3);
    g.drawText("DECIMULABS - GUITARS - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulGuitarsEditor::resized()
{
    auto content = getLocalBounds().reduced(22, 18);

    topbarRect = content.removeFromTop(54);
    content.removeFromTop(10);
    content.removeFromBottom(8);

    auto header = topbarRect.reduced(10, 8);
    bypassButton.setBounds(header.removeFromRight(110));
    vuArc.setBounds(header.removeFromRight(96).reduced(2));
    header.removeFromRight(10);
    headerLogoArea = header;

    constexpr int sideW = 100;
    inputCompRect = content.removeFromLeft(sideW);
    content.removeFromLeft(8);
    outputCompRect = juce::Rectangle<int>(content.getRight() - sideW, content.getY(), sideW, content.getHeight());
    content.removeFromRight(sideW + 8);
    blendCompRect = juce::Rectangle<int>(content.getRight() - sideW, content.getY(), sideW, content.getHeight());
    content.removeFromRight(sideW + 8);
    mainCompRect = content;

    {
        auto col = inputCompRect.reduced(8);
        auto labelArea = col.removeFromBottom(20);
        inputLabel.setBounds(labelArea);
        auto meterArea = col.removeFromLeft(16);
        inputMeter.setBounds(meterArea);
        inputSlider.setBounds(col);
    }

    auto mainArea = mainCompRect.reduced(8);
    const int colW = mainArea.getWidth() / 7;

    auto layoutSimpleModule = [&](juce::Rectangle<int> col, DecimuLedStackSelector& sel,
                                   juce::Slider& slider, juce::Label& label)
    {
        col = col.reduced(4);
        auto labelArea = col.removeFromBottom(20);
        label.setBounds(labelArea);
        auto selArea = col.removeFromTop(48);
        sel.setBounds(selArea);
        slider.setBounds(col);
    };

    auto layoutKnobModule = [&](juce::Rectangle<int> col, DecimuLedButton& led,
                                 juce::Slider& knob, juce::Label& knobLabel,
                                 DecimuLedStackSelector& sel, juce::Slider& slider, juce::Label& label)
    {
        col = col.reduced(4);
        auto labelArea = col.removeFromBottom(20);
        label.setBounds(labelArea);

        auto ledArea = col.removeFromTop(10);
        led.setBounds(ledArea.withSizeKeepingCentre(8, 8));

        auto knobArea = col.removeFromTop(52);
        knob.setBounds(knobArea.withSizeKeepingCentre(46, 46));

        knobLabel.setBounds(col.removeFromTop(14));

        auto selArea = col.removeFromTop(48);
        sel.setBounds(selArea);
        slider.setBounds(col);
    };

    // Cab: sin fader ni knob — LED + selector nada más, centrado en la columna.
    auto layoutToggleModule = [&](juce::Rectangle<int> col, DecimuLedButton& led,
                                   DecimuLedStackSelector& sel, juce::Label& label)
    {
        col = col.reduced(4);
        auto labelArea = col.removeFromBottom(20);
        label.setBounds(labelArea);

        auto ledArea = col.removeFromTop(10);
        led.setBounds(ledArea.withSizeKeepingCentre(8, 8));
        col.removeFromTop(8);

        auto selArea = col.removeFromTop(48);
        sel.setBounds(selArea);
    };

    layoutSimpleModule(mainArea.removeFromLeft(colW), gateModeSel, gateSlider, gateLabel);
    layoutSimpleModule(mainArea.removeFromLeft(colW), driveModeSel, driveSlider, driveLabel);

    toneCompRect = mainArea.removeFromLeft(colW);
    layoutKnobModule(toneCompRect, toneLedBtn, toneMidKnob, toneKnobLabel, toneModeSel, toneSlider, toneLabel);

    layoutToggleModule(mainArea.removeFromLeft(colW), cabLedBtn, cabModeSel, cabLabel);

    modCompRect = mainArea.removeFromLeft(colW);
    layoutKnobModule(modCompRect, modLedBtn, modRateKnob, modKnobLabel, modModeSel, modSlider, modLabel);

    spaceCompRect = mainArea.removeFromLeft(colW);
    layoutKnobModule(spaceCompRect, spaceLedBtn, spacePreDelayKnob, spaceKnobLabel, spaceModeSel, spaceSlider, spaceLabel);

    echoCompRect = mainArea;
    layoutKnobModule(echoCompRect, echoLedBtn, echoFeedbackKnob, echoKnobLabel, echoDivisionSel, echoSlider, echoLabel);

    {
        auto col = blendCompRect.reduced(8);
        auto tapArea = col.removeFromTop(24);
        tapTempoButton.setBounds(tapArea);
        auto bpmArea = col.removeFromTop(16);
        bpmLabel.setBounds(bpmArea);
        auto labelArea = col.removeFromBottom(20);
        blendLabel.setBounds(labelArea);
        blendSlider.setBounds(col);
    }

    {
        auto col = outputCompRect.reduced(8);
        auto labelArea = col.removeFromBottom(20);
        outputLabel.setBounds(labelArea);
        auto meterArea = col.removeFromLeft(16);
        outputMeter.setBounds(meterArea);
        outputSlider.setBounds(col);
    }
}

void DecimulGuitarsEditor::timerCallback()
{
    inputMeter.setLevelDb(processorRef.inputPeakDb.load());
    inputMeter.repaint();

    outputMeter.setLevelDb(processorRef.outputPeakDb.load());
    outputMeter.repaint();

    vuArc.setLevelDb(processorRef.outputPeakDb.load());
    vuArc.repaint();

    bypassButton.repaint();

    bpmLabel.setText(juce::String((int) processorRef.manualBpm.load()) + " BPM",
                      juce::dontSendNotification);

    auto& apvts = processorRef.apvts;
    gateModeSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::gateMode)->load());
    driveModeSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::driveMode)->load());
    toneModeSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::toneMode)->load());
    cabModeSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::cabMode)->load());
    modModeSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::modMode)->load());
    spaceModeSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::spaceMode)->load());
    echoDivisionSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::echoDivision)->load());
}
