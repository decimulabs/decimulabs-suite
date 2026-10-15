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
    // driveLedBtn sumado en la 2da ronda (Drive era el único módulo del core sin
    // apagador propio — reportado en testing).
    for (auto* led : { &driveLedBtn, &toneLedBtn, &modLedBtn, &spaceLedBtn, &echoLedBtn, &cabLedBtn })
        addAndMakeVisible(led);

    addAndMakeVisible(bypassButton);
    addAndMakeVisible(tapTempoButton);
    tapTempoButton.onClick = [this] { processorRef.tapTempo(); };

    // Toggle de sync (2da ronda de testing: "el BPM manual no influye en nada" — es
    // porque el host manda cuando da un tempo válido; este switch deja forzar el
    // manual/Tap). No es parámetro APVTS, se guarda directo en el processor.
    hostSyncButton.setToggleState(processorRef.hostSyncEnabled.load(), juce::dontSendNotification);
    hostSyncButton.onClick = [this] { processorRef.hostSyncEnabled.store(hostSyncButton.getToggleState()); };
    addAndMakeVisible(hostSyncButton);

    // BPM editable: doble click para escribir a mano (alternativa al TAP, pedido en
    // testing — "no solo el tap"). Mismo alcance que TAP: fallback sin host.
    bpmLabel.setJustificationType(juce::Justification::centred);
    bpmLabel.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    bpmLabel.setEditable(false, true, false);
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
    driveEnabledAttach = std::make_unique<ButtonAttachment>(apvts, ParamIDs::driveEnabled, driveLedBtn);
    toneEnabledAttach  = std::make_unique<ButtonAttachment>(apvts, ParamIDs::toneEnabled, toneLedBtn);
    modEnabledAttach   = std::make_unique<ButtonAttachment>(apvts, ParamIDs::modEnabled, modLedBtn);
    spaceEnabledAttach = std::make_unique<ButtonAttachment>(apvts, ParamIDs::spaceEnabled, spaceLedBtn);
    echoEnabledAttach  = std::make_unique<ButtonAttachment>(apvts, ParamIDs::echoEnabled, echoLedBtn);
    cabEnabledAttach   = std::make_unique<ButtonAttachment>(apvts, ParamIDs::cabEnabled, cabLedBtn);

    // ---- Tooltips: inglés por defecto; el botón EN/ES de abajo los pasa a español ----
    addAndMakeVisible(tips.getSwitch());

    tips.add(inputSlider, "Input trim, -24 to +24 dB: the level the signal enters the plugin with.",
             "Ganancia de entrada, -24 a +24 dB: el nivel con el que entra la señal al plugin.");

    tips.add(gateSlider, "Gate amount, 0 to 100 %. 0 % = no gate; more % = higher threshold, so it cuts more noise.",
             "Cantidad de gate, 0 a 100 %. 0 % = sin gate; más % = umbral más alto, o sea que corta más ruido.");
    tips.add(gateModeSel, "Gate speed: Fast (3 ms attack, 60 ms release), Med (6 / 150 ms) or Slow (12 / 300 ms).",
             "Velocidad del gate: Fast (3 ms de ataque, 60 ms de release), Med (6 / 150 ms) o Slow (12 / 300 ms).");

    tips.add(driveSlider, "Drive amount, 0 to 100 %: gain into the saturator, up to +40 dB, oversampled 4x.",
             "Cantidad de Drive, 0 a 100 %: ganancia hacia el saturador, hasta +40 dB, con sobremuestreo 4x.");
    tips.add(driveModeSel, "Drive character: Tube, Fuzz or Boost. Different saturation curves, not just more gain.",
             "Carácter del Drive: Tube, Fuzz o Boost. Curvas de saturación distintas, no solo más ganancia.");
    tips.add(driveLedBtn, "Drive on/off. Switches the whole module off without touching its fader.",
             "Drive on/off. Apaga el módulo entero sin tocar su fader.");

    tips.add(toneSlider, "Tone: tilt EQ, +/-12 dB. One way cuts lows and lifts highs; the other way, the opposite.",
             "Tone: EQ de inclinación, +/-12 dB. Para un lado baja graves y sube agudos; para el otro, al revés.");
    tips.add(toneMidKnob, "Mid sweep: frequency of a fixed +4 dB mid peak, 300 to 3000 Hz.",
             "Mid sweep: frecuencia de un pico fijo de +4 dB en los medios, 300 a 3000 Hz.");
    tips.add(toneModeSel, "Where the tilt shelves sit: Scoop (250 Hz / 3 kHz), Flat (400 Hz / 2 kHz) or Bright (500 Hz / 1.5 kHz).",
             "Dónde actúan los shelves del tilt: Scoop (250 Hz / 3 kHz), Flat (400 Hz / 2 kHz) o Bright (500 Hz / 1.5 kHz).");
    tips.add(toneLedBtn, "Tone on/off. Switches the whole module off without touching its fader.",
             "Tone on/off. Apaga el módulo entero sin tocar su fader.");

    tips.add(cabModeSel, "Cabinet: Combo, Stack or DI (no cabinet colour even if the LED is on).",
             "Gabinete: Combo, Stack o DI (sin color de gabinete aunque el LED esté prendido).");
    tips.add(cabLedBtn, "Cab on/off. When on it is 100 % wet: there is no partial blend.",
             "Cab on/off. Prendido es 100 % wet: no hay mezcla parcial.");

    tips.add(modSlider, "Modulation amount, 0 to 100 %.", "Cantidad de modulación, 0 a 100 %.");
    tips.add(modRateKnob, "Modulation rate, 0.1 to 5 Hz.", "Velocidad de la modulación, 0.1 a 5 Hz.");
    tips.add(modModeSel, "Modulation type: Chorus, Flanger or Phaser.", "Tipo de modulación: Chorus, Flanger o Phaser.");
    tips.add(modLedBtn, "Modulation on/off. Switches the whole module off without touching its fader.",
             "Modulation on/off. Apaga el módulo entero sin tocar su fader.");

    tips.add(spaceSlider, "Space: reverb level, 0 to 100 %.", "Space: nivel de la reverb, 0 a 100 %.");
    tips.add(spacePreDelayKnob, "Reverb pre-delay, 0 to 500 ms: time before the reverb starts.",
             "Pre-delay de la reverb, 0 a 500 ms: tiempo antes de que arranque la reverb.");
    tips.add(spaceModeSel, "Reverb type: Room, Plate or Hall.", "Tipo de reverb: Room (sala), Plate (plate) o Hall.");
    tips.add(spaceLedBtn, "Space on/off. Switches the whole module off without touching its fader.",
             "Space on/off. Apaga el módulo entero sin tocar su fader.");

    tips.add(echoSlider, "Echo: level of the tempo-synced delay, 0 to 100 %.",
             "Echo: nivel del delay sincronizado al tempo, 0 a 100 %.");
    tips.add(echoDivisionSel, "Echo time, following the tempo: Slap (short slapback), 8th note or quarter note.",
             "Tiempo del eco, siguiendo el tempo: Slap (rebote corto), corchea o negra.");
    tips.add(echoFeedbackKnob, "Echo feedback, 0 to 100 %: how many repeats. 100 % repeats without dying out.",
             "Feedback del eco, 0 a 100 %: cuántas repeticiones. 100 % repite sin apagarse.");
    tips.add(echoLedBtn, "Echo on/off. Switches the whole module off without touching its fader.",
             "Echo on/off. Apaga el módulo entero sin tocar su fader.");

    tips.add(blendSlider, "Blend (dry/wet): how much of Space, Mod and Echo you hear. Gate, Drive, Tone and Cab are never diluted.",
             "Blend (dry/wet): cuánto se escucha de Space, Mod y Echo. Gate, Drive, Tone y Cab nunca se diluyen.");
    tips.add(outputSlider, "Output trim, -60 to +24 dB.", "Ganancia de salida, -60 a +24 dB.");
    tips.add(bypassButton, "Bypass: the audio passes through untouched (the dot goes grey).",
             "Bypass: el audio pasa sin tocarse (el punto se pone gris).");
    tips.add(tapTempoButton, "Tap the tempo here. Used when the host gives no tempo (or SYNC is off).",
             "Tocá el tempo acá. Se usa cuando el host no da tempo (o SYNC está apagado).");
    tips.add(bpmLabel, "Manual tempo for the Echo (fallback without host): double-click to type it, or use TAP.",
             "Tempo manual del Echo (sin host): doble click para escribirlo, o usá TAP.");
    tips.add(hostSyncButton, "SYNC on: the host tempo wins when it is available (default). Off: forces the manual BPM / TAP even with a host.",
             "SYNC on: manda el tempo del host cuando está disponible (default). Off: fuerza el BPM manual / TAP aunque haya host.");

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
    tips.layout(getLocalBounds());

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

    // Tamaño de LED subido de 8x8 a 18x18 (área clickeable de 22px, antes 10px): en
    // testing real costaba apuntar justo con el mouse en el tamaño anterior.
    constexpr int ledAreaH = 22;
    constexpr int ledSize = 18;

    auto layoutKnobModule = [&](juce::Rectangle<int> col, DecimuLedButton& led,
                                 juce::Slider& knob, juce::Label& knobLabel,
                                 DecimuLedStackSelector& sel, juce::Slider& slider, juce::Label& label)
    {
        col = col.reduced(4);
        auto labelArea = col.removeFromBottom(20);
        label.setBounds(labelArea);

        auto ledArea = col.removeFromTop(ledAreaH);
        led.setBounds(ledArea.withSizeKeepingCentre(ledSize, ledSize));

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

        auto ledArea = col.removeFromTop(ledAreaH);
        led.setBounds(ledArea.withSizeKeepingCentre(ledSize, ledSize));
        col.removeFromTop(8);

        auto selArea = col.removeFromTop(48);
        sel.setBounds(selArea);
    };

    // Drive: como layoutSimpleModule pero con LED arriba — sumado en 2da ronda de
    // testing (Drive era el único módulo "core" sin apagador propio).
    auto layoutSimpleModuleWithLed = [&](juce::Rectangle<int> col, DecimuLedButton& led,
                                          DecimuLedStackSelector& sel, juce::Slider& slider, juce::Label& label)
    {
        col = col.reduced(4);
        auto labelArea = col.removeFromBottom(20);
        label.setBounds(labelArea);

        auto ledArea = col.removeFromTop(ledAreaH);
        led.setBounds(ledArea.withSizeKeepingCentre(ledSize, ledSize));

        auto selArea = col.removeFromTop(48);
        sel.setBounds(selArea);
        slider.setBounds(col);
    };

    layoutSimpleModule(mainArea.removeFromLeft(colW), gateModeSel, gateSlider, gateLabel);
    layoutSimpleModuleWithLed(mainArea.removeFromLeft(colW), driveLedBtn, driveModeSel, driveSlider, driveLabel);

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
        auto syncArea = col.removeFromTop(18);
        hostSyncButton.setBounds(syncArea);
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

    if (!bpmLabel.isBeingEdited())
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
