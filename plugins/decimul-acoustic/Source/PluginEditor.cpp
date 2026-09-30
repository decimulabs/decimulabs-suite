#include "PluginEditor.h"
#include "BinaryData.h"

void DecimulAcousticEditor::setupFader(juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
    addAndMakeVisible(slider);

    // Estilo "[ NAV ]" del sitio (ver [ESTUDIO] [MÚSICA] [VISUAL] [CONTACTO] en el header real).
    label.setText("[ " + text.toUpperCase() + " ]", juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, DecimuColours::text);
    addAndMakeVisible(label);
}

void DecimulAcousticEditor::setupKnob(juce::Slider& knob)
{
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible(knob);
}

void DecimulAcousticEditor::setupSelector(DecimuLedStackSelector& sel, const juce::StringArray& items,
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

    setupKnob(spaceAPreDelayKnob);
    setupKnob(spaceBPreDelayKnob);
    setupKnob(echoFeedbackKnob);

    auto setupKnobLabel = [this](juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(juce::FontOptions(10.0f)));
        l.setColour(juce::Label::textColourId, DecimuColours::textMuted);
        addAndMakeVisible(l);
    };
    setupKnobLabel(spaceAKnobLabel, "PRE-DELAY");
    setupKnobLabel(spaceBKnobLabel, "PRE-DELAY");
    setupKnobLabel(echoKnobLabel, "FEEDBACK");

    setupSelector(lowModeSel, { "Warm", "Deep", "Air-Cut" }, ParamIDs::lowMode);
    setupSelector(highModeSel, { "Silk", "Sheen", "Crisp" }, ParamIDs::highMode);
    setupSelector(compModeSel, { "Glue", "Snap", "Squash" }, ParamIDs::compMode);
    setupSelector(spaceATypeSel, { "Room", "Hall", "Cathedral" }, ParamIDs::spaceAType);
    setupSelector(spaceBTypeSel, { "Close", "Wide", "Vast" }, ParamIDs::spaceBType);
    setupSelector(echoDivisionSel, { "Slap", "8th", "Quarter" }, ParamIDs::echoDivision);

    // LEDs clickeables: on/off de todo el módulo (generalizado desde el testing de Guitars).
    for (auto* led : { &lowLedBtn, &highLedBtn, &compLedBtn, &spaceALedBtn, &spaceBLedBtn, &echoLedBtn })
        addAndMakeVisible(led);

    addAndMakeVisible(bypassButton);
    addAndMakeVisible(tapTempoButton);
    tapTempoButton.onClick = [this] { processorRef.tapTempo(); };

    // BPM editable: doble click para escribir a mano (alternativa al TAP, pedido en
    // testing — "no solo el tap"). Mismo alcance que TAP: fallback sin host, el host
    // (cuando da un tempo válido) manda por encima de esto.
    bpmLabel.setJustificationType(juce::Justification::centred);
    bpmLabel.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    bpmLabel.setEditable(false, true, false);
    bpmLabel.setTooltip("Tempo manual (fallback sin host): doble click para escribirlo, o TAP");
    bpmLabel.onEditorShow = [this]
    {
        if (auto* ed = bpmLabel.getCurrentTextEditor())
            ed->setInputRestrictions(3, "0123456789");
    };
    bpmLabel.onTextChange = [this]
    {
        if (auto bpm = bpmLabel.getText().getFloatValue(); bpm > 0.0f)
            processorRef.setManualBpm(bpm);
    };
    addAndMakeVisible(bpmLabel);

    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outputMeter);
    addAndMakeVisible(vuArc);

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
    spaceAPreDelayAttach = std::make_unique<SliderAttachment>(apvts, ParamIDs::spaceAPreDelay, spaceAPreDelayKnob);
    spaceBPreDelayAttach = std::make_unique<SliderAttachment>(apvts, ParamIDs::spaceBPreDelay, spaceBPreDelayKnob);
    echoFeedbackAttach   = std::make_unique<SliderAttachment>(apvts, ParamIDs::echoFeedback, echoFeedbackKnob);

    bypassAttach        = std::make_unique<ButtonAttachment>(apvts, ParamIDs::bypass, bypassButton);
    lowEnabledAttach     = std::make_unique<ButtonAttachment>(apvts, ParamIDs::lowEnabled, lowLedBtn);
    highEnabledAttach    = std::make_unique<ButtonAttachment>(apvts, ParamIDs::highEnabled, highLedBtn);
    compEnabledAttach    = std::make_unique<ButtonAttachment>(apvts, ParamIDs::compEnabled, compLedBtn);
    spaceAEnabledAttach  = std::make_unique<ButtonAttachment>(apvts, ParamIDs::spaceAEnabled, spaceALedBtn);
    spaceBEnabledAttach  = std::make_unique<ButtonAttachment>(apvts, ParamIDs::spaceBEnabled, spaceBLedBtn);
    echoEnabledAttach    = std::make_unique<ButtonAttachment>(apvts, ParamIDs::echoEnabled, echoLedBtn);

    setSize(1180, 560);
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
    auto full = getLocalBounds().toFloat();
    DecimuChrome::paintChassisBackground(g, full, 14.0f);
    DecimuChrome::paintRackHandles(g, full);
    DecimuChrome::paintCornerScrews(g, full, 12.0f);

    DecimuChrome::paintCompartment(g, topbarRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, inputCompRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, mainCompRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, spaceACompRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, spaceBCompRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, blendCompRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, outputCompRect.toFloat(), true, 8.0f);

    // Los LEDs de cada módulo ahora son DecimuLedButton (se pintan solos).

    // Copia local: headerLogoArea es un miembro (calculado una vez en resized()) — si
    // le hiciéramos removeFromLeft() directo se iría achicando en cada repintado.
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
    g.drawText("DECIMULABS - ACOUSTIC - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulAcousticEditor::resized()
{
    auto content = getLocalBounds().reduced(22, 18);

    topbarRect = content.removeFromTop(54);
    content.removeFromTop(10);
    content.removeFromBottom(8);

    // --- Header: logo + VU en arco + bypass/REC ---
    auto header = topbarRect.reduced(10, 8);
    bypassButton.setBounds(header.removeFromRight(110));
    vuArc.setBounds(header.removeFromRight(96).reduced(2));
    header.removeFromRight(10);
    headerLogoArea = header;

    // --- Compartimentos: Input | banco principal (6 módulos) | Blend | Output ---
    constexpr int sideW = 108;
    inputCompRect = content.removeFromLeft(sideW);
    content.removeFromLeft(8);
    outputCompRect = juce::Rectangle<int>(content.getRight() - sideW, content.getY(), sideW, content.getHeight());
    content.removeFromRight(sideW + 8);
    blendCompRect = juce::Rectangle<int>(content.getRight() - sideW, content.getY(), sideW, content.getHeight());
    content.removeFromRight(sideW + 8);
    mainCompRect = content;

    // --- Input ---
    {
        auto col = inputCompRect.reduced(8);
        auto labelArea = col.removeFromBottom(20);
        inputLabel.setBounds(labelArea);
        auto meterArea = col.removeFromLeft(16);
        inputMeter.setBounds(meterArea);
        inputSlider.setBounds(col);
    }

    // --- Banco principal: 6 columnas ---
    auto mainArea = mainCompRect.reduced(8);
    const int colW = mainArea.getWidth() / 6;

    // Low/High/Dynamics: LED + selector + fader (sin knob).
    auto layoutSimpleModule = [&](juce::Rectangle<int> col, DecimuLedButton& led, DecimuLedStackSelector& sel,
                                   juce::Slider& slider, juce::Label& label)
    {
        col = col.reduced(4);
        auto labelArea = col.removeFromBottom(20);
        label.setBounds(labelArea);

        auto ledArea = col.removeFromTop(10);
        led.setBounds(ledArea.withSizeKeepingCentre(8, 8));
        col.removeFromTop(4);

        auto selArea = col.removeFromTop(48);
        sel.setBounds(selArea);
        slider.setBounds(col);
    };

    // Space A/Space B/Echo: LED + knob + selector + fader.
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

    layoutSimpleModule(mainArea.removeFromLeft(colW), lowLedBtn, lowModeSel, lowSlider, lowLabel);
    layoutSimpleModule(mainArea.removeFromLeft(colW), highLedBtn, highModeSel, highSlider, highLabel);
    layoutSimpleModule(mainArea.removeFromLeft(colW), compLedBtn, compModeSel, compSlider, compLabel);

    spaceACompRect = mainArea.removeFromLeft(colW);
    layoutKnobModule(spaceACompRect, spaceALedBtn, spaceAPreDelayKnob, spaceAKnobLabel,
                      spaceATypeSel, spaceASlider, spaceALabel);

    spaceBCompRect = mainArea.removeFromLeft(colW);
    layoutKnobModule(spaceBCompRect, spaceBLedBtn, spaceBPreDelayKnob, spaceBKnobLabel,
                      spaceBTypeSel, spaceBSlider, spaceBLabel);

    layoutKnobModule(mainArea, echoLedBtn, echoFeedbackKnob, echoKnobLabel,
                      echoDivisionSel, echoSlider, echoLabel);

    // --- Blend: comparte columna con el botón de Tap Tempo + BPM ---
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

    // --- Output ---
    {
        auto col = outputCompRect.reduced(8);
        auto labelArea = col.removeFromBottom(20);
        outputLabel.setBounds(labelArea);
        auto meterArea = col.removeFromLeft(16);
        outputMeter.setBounds(meterArea);
        outputSlider.setBounds(col);
    }
}

void DecimulAcousticEditor::timerCallback()
{
    inputMeter.setLevelDb(processorRef.inputPeakDb.load());
    inputMeter.repaint();

    outputMeter.setLevelDb(processorRef.outputPeakDb.load());
    outputMeter.repaint();

    vuArc.setLevelDb(processorRef.outputPeakDb.load());
    vuArc.repaint();

    bypassButton.repaint(); // anima el pulso del punto "REC" (ver DecimuLookAndFeel)

    // No pisar el texto mientras se está editando (si no, cada tick del timer borra
    // lo que el usuario está tipeando antes de que termine de escribir el número).
    if (!bpmLabel.isBeingEdited())
        bpmLabel.setText(juce::String((int) processorRef.manualBpm.load()) + " BPM",
                          juce::dontSendNotification);

    // Sincroniza los selectores tipo LED con el valor real del parámetro (por si
    // cambió por automatización de host o al cargar un preset, no solo por click).
    auto& apvts = processorRef.apvts;
    lowModeSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::lowMode)->load());
    highModeSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::highMode)->load());
    compModeSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::compMode)->load());
    spaceATypeSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::spaceAType)->load());
    spaceBTypeSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::spaceBType)->load());
    echoDivisionSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::echoDivision)->load());
}
