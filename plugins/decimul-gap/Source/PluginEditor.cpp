#include "PluginEditor.h"
#include "BinaryData.h"

void DecimulGapEditor::setParam(const juce::String& id, float plainValue)
{
    if (auto* param = processorRef.apvts.getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1(plainValue));
}

void DecimulGapEditor::addKnob(Knob& k, const juce::String& paramID, const juce::String& caption)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    addAndMakeVisible(k.slider);

    k.label.setText(caption, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setFont(juce::Font(juce::FontOptions(11.0f)));
    k.label.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    addAndMakeVisible(k.label);

    k.attachment = std::make_unique<SliderAttachment>(processorRef.apvts, paramID, k.slider);
}

DecimulGapEditor::DecimulGapEditor(DecimulGapProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    // ---- Curva ----
    curveEditor.setCurve(processorRef.getCurve());
    curveEditor.onCurveChanged = [this](const GapCurve& c)
    {
        processorRef.setCurve(c, -1);
        lastCurveVersion = processorRef.getCurveVersion();
        updatingPresetCombo = true;
        presetCombo.setSelectedId(1, juce::dontSendNotification);
        updatingPresetCombo = false;
    };
    addAndMakeVisible(curveEditor);

    presetCombo.addItem("Custom", 1);
    for (int i = 0; i < GapCurve::numPresets(); ++i)
        presetCombo.addItem(GapCurve::presetName(i), i + 2);
    presetCombo.onChange = [this]
    {
        if (updatingPresetCombo)
            return;
        const int id = presetCombo.getSelectedId();
        if (id >= 2)
            applyPreset(id - 2);
    };
    addAndMakeVisible(presetCombo);

    auto stepPreset = [this](int delta)
    {
        const int count = GapCurve::numPresets();
        int current = processorRef.getPresetIndex();
        current = current < 0 ? (delta > 0 ? -1 : 0) : current;
        applyPreset((current + delta + count) % count);
    };
    prevPresetButton.onClick = [stepPreset] { stepPreset(-1); };
    nextPresetButton.onClick = [stepPreset] { stepPreset(1); };
    addAndMakeVisible(prevPresetButton);
    addAndMakeVisible(nextPresetButton);

    presetLabel.setText("CURVA", juce::dontSendNotification);
    rateLabel.setText("RATE", juce::dontSendNotification);
    for (auto* l : { &presetLabel, &rateLabel })
    {
        l->setColour(juce::Label::textColourId, DecimuColours::textMuted);
        l->setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(*l);
    }

    for (int i = 0; i < DecimulGapProcessor::numRates(); ++i)
        rateCombo.addItem(DecimulGapProcessor::rateName(i), i + 1);
    addAndMakeVisible(rateCombo);
    rateAttach = std::make_unique<ComboAttachment>(processorRef.apvts, ParamIDs::rate, rateCombo);

    // ---- Trigger y banda ----
    auto wirePill = [this](DecimuPillButton& pill, const juce::String& id, float value)
    {
        pill.setClickingTogglesState(false);
        pill.onClick = [this, id, value] { setParam(id, value); };
        addAndMakeVisible(pill);
    };
    wirePill(trigSyncPill, ParamIDs::trigger, 0.0f);
    wirePill(trigMidiPill, ParamIDs::trigger, 1.0f);
    wirePill(trigAudioPill, ParamIDs::trigger, 2.0f);
    wirePill(bandFullPill, ParamIDs::band, 0.0f);
    wirePill(bandLowPill, ParamIDs::band, 1.0f);
    wirePill(bandHighPill, ParamIDs::band, 2.0f);

    // ---- Knobs ----
    addKnob(depthKnob, ParamIDs::depth, "DEPTH");
    addKnob(mixKnob, ParamIDs::mix, "MIX");
    addKnob(smoothKnob, ParamIDs::smooth, "SMOOTH");
    addKnob(nudgeKnob, ParamIDs::nudge, "NUDGE");
    addKnob(spreadKnob, ParamIDs::spread, "ST SPREAD");
    addKnob(tempoKnob, ParamIDs::bpm, "TEMPO (sin host)");
    addKnob(noteKnob, ParamIDs::midiNote, "NOTA MIDI");
    addKnob(thresholdKnob, ParamIDs::threshold, "UMBRAL AUDIO");

    // Knob y no slider horizontal: el LookAndFeel compartido solo dibuja bien los verticales.
    addKnob(crossoverKnob, ParamIDs::crossover, "CROSSOVER");

    triggerLabel.setText("[ DISPARO ]", juce::dontSendNotification);
    bandLabel.setText("[ BANDA QUE BOMBEA ]", juce::dontSendNotification);
    for (auto* l : { &triggerLabel, &bandLabel })
    {
        l->setJustificationType(juce::Justification::centredLeft);
        l->setColour(juce::Label::textColourId, DecimuColours::text);
        addAndMakeVisible(*l);
    }

    hintLabel.setJustificationType(juce::Justification::topLeft);
    hintLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    hintLabel.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    addAndMakeVisible(hintLabel);

    // ---- Salida ----
    outputSlider.setSliderStyle(juce::Slider::LinearVertical);
    outputSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
    addAndMakeVisible(outputSlider);
    outputLabel.setText("[ OUTPUT ]", juce::dontSendNotification);
    outputLabel.setJustificationType(juce::Justification::centred);
    outputLabel.setColour(juce::Label::textColourId, DecimuColours::text);
    addAndMakeVisible(outputLabel);

    addAndMakeVisible(outputMeter);
    addAndMakeVisible(vuArc);
    addAndMakeVisible(bypassButton);

    outputAttach = std::make_unique<SliderAttachment>(processorRef.apvts, ParamIDs::outputTrim, outputSlider);
    bypassAttach = std::make_unique<ButtonAttachment>(processorRef.apvts, ParamIDs::bypass, bypassButton);

    setSize(1100, 640);
    setResizable(true, true);
    setResizeLimits(960, 600, 1800, 1100);
    startTimerHz(30);
}

DecimulGapEditor::~DecimulGapEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

// Rotary cuadrado (si el slider es más ancho que alto, el knob sale ovalado) con el
// título arriba y el valor abajo.
void DecimulGapEditor::placeKnob(Knob& k, juce::Rectangle<int> cell)
{
    k.label.setBounds(cell.removeFromTop(16));

    const int side = std::max(40, std::min(cell.getWidth(), cell.getHeight() - 18));
    const int width = std::max(side, 56);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, width, 16);
    k.slider.setBounds(cell.withSizeKeepingCentre(width, side + 18));
}

void DecimulGapEditor::applyPreset(int presetIndex)
{
    const auto c = GapCurve::preset(presetIndex);
    processorRef.setCurve(c, presetIndex);
    curveEditor.setCurve(c);
    lastCurveVersion = processorRef.getCurveVersion();

    updatingPresetCombo = true;
    presetCombo.setSelectedId(presetIndex + 2, juce::dontSendNotification);
    updatingPresetCombo = false;
    shownPreset = presetIndex;
}

void DecimulGapEditor::refreshCurveFromProcessor()
{
    curveEditor.setCurve(processorRef.getCurve());
    lastCurveVersion = processorRef.getCurveVersion();

    const int preset = processorRef.getPresetIndex();
    updatingPresetCombo = true;
    presetCombo.setSelectedId(preset >= 0 ? preset + 2 : 1, juce::dontSendNotification);
    updatingPresetCombo = false;
    shownPreset = preset;
}

// ------------------------------- Dibujo -------------------------------

void DecimulGapEditor::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    DecimuChrome::paintChassisBackground(g, full, 14.0f);
    DecimuChrome::paintRackHandles(g, full);
    DecimuChrome::paintCornerScrews(g, full, 12.0f);

    DecimuChrome::paintCompartment(g, topbarRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, curveRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, knobsRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, triggerRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, outputRect.toFloat(), true, 8.0f);

    auto logoArea = headerLogoArea;
    if (dogImage.isValid())
    {
        auto dogArea = logoArea.removeFromLeft(logoArea.getHeight());
        g.drawImage(dogImage, dogArea.toFloat(), juce::RectanglePlacement::centred);
        logoArea.removeFromLeft(8);
    }
    if (wordmarkImage.isValid())
    {
        auto wordmarkArea = logoArea.removeFromLeft(juce::jmin(logoArea.getWidth(), 200)).reduced(0, 7);
        g.drawImage(wordmarkImage, wordmarkArea.toFloat(),
                    juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
        logoArea.removeFromLeft(18);
    }

    g.setColour(DecimuColours::accentPrimary);
    g.setFont(lookAndFeel.monoFont(22.0f));
    g.drawText("GAP", logoArea, juce::Justification::centredLeft);

    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(13.0f));
    auto footer = getLocalBounds().removeFromBottom(22).reduced(16, 3);
    g.drawText("DECIMULABS - GAP - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulGapEditor::resized()
{
    auto content = getLocalBounds().reduced(22, 18);
    content.removeFromBottom(8);

    topbarRect = content.removeFromTop(54);
    content.removeFromTop(8);

    auto header = topbarRect.reduced(10, 8);
    bypassButton.setBounds(header.removeFromRight(110));
    vuArc.setBounds(header.removeFromRight(96).reduced(2));
    header.removeFromRight(10);
    headerLogoArea = header;

    constexpr int gap = 8;
    outputRect = content.removeFromRight(112);
    content.removeFromRight(gap);

    auto right = content.removeFromRight(372);
    content.removeFromRight(gap);
    curveRect = content;

    knobsRect = right.removeFromTop(236);
    right.removeFromTop(gap);
    triggerRect = right;

    // ---- Curva + barra de presets ----
    {
        auto area = curveRect.reduced(10);
        auto strip = area.removeFromTop(30);
        presetLabel.setBounds(strip.removeFromLeft(52));
        strip.removeFromLeft(6);
        prevPresetButton.setBounds(strip.removeFromLeft(28));
        strip.removeFromLeft(4);
        presetCombo.setBounds(strip.removeFromLeft(200));
        strip.removeFromLeft(4);
        nextPresetButton.setBounds(strip.removeFromLeft(28));
        strip.removeFromLeft(24);
        rateLabel.setBounds(strip.removeFromLeft(50));
        strip.removeFromLeft(6);
        rateCombo.setBounds(strip.removeFromLeft(110));
        area.removeFromTop(8);
        curveEditor.setBounds(area);
    }

    // ---- Knobs principales (3 x 2) ----
    {
        auto area = knobsRect.reduced(10);
        const int rowH = area.getHeight() / 2;
        const int colW = area.getWidth() / 3;

        auto place = [&](Knob& k, int row, int col)
        {
            auto cell = juce::Rectangle<int>(area.getX() + col * colW, area.getY() + row * rowH, colW, rowH).reduced(2);
            placeKnob(k, cell);
        };
        place(depthKnob, 0, 0);
        place(mixKnob, 0, 1);
        place(smoothKnob, 0, 2);
        place(nudgeKnob, 1, 0);
        place(spreadKnob, 1, 1);
        place(tempoKnob, 1, 2);
    }

    // ---- Disparo y banda ----
    {
        auto area = triggerRect.reduced(10);

        // Cada sección: los tres botones apilados a la izquierda y los knobs a la derecha.
        constexpr int pillColW = 104;
        constexpr int rowH = 98;
        auto stackPills = [](juce::Rectangle<int> col, DecimuPillButton& a, DecimuPillButton& b, DecimuPillButton& c)
        {
            const int h = (col.getHeight() - 8) / 3;
            a.setBounds(col.removeFromTop(h));
            col.removeFromTop(4);
            b.setBounds(col.removeFromTop(h));
            col.removeFromTop(4);
            c.setBounds(col.removeFromTop(h));
        };

        triggerLabel.setBounds(area.removeFromTop(16));
        {
            auto row = area.removeFromTop(rowH);
            stackPills(row.removeFromLeft(pillColW), trigSyncPill, trigMidiPill, trigAudioPill);
            row.removeFromLeft(8);
            const int cw = row.getWidth() / 2;
            placeKnob(noteKnob, row.removeFromLeft(cw).reduced(2));
            placeKnob(thresholdKnob, row.reduced(2));
        }

        hintLabel.setBounds(area.removeFromTop(34).reduced(2, 4));
        area.removeFromTop(4);

        bandLabel.setBounds(area.removeFromTop(16));
        {
            auto row = area.removeFromTop(rowH);
            stackPills(row.removeFromLeft(pillColW), bandFullPill, bandLowPill, bandHighPill);
            row.removeFromLeft(8);
            const int cw = row.getWidth() / 2;
            placeKnob(crossoverKnob, row.removeFromLeft(cw).reduced(2));
        }
    }

    // ---- Salida ----
    {
        auto col = outputRect.reduced(10);
        outputLabel.setBounds(col.removeFromBottom(20));
        outputMeter.setBounds(col.removeFromLeft(16));
        outputSlider.setBounds(col);
    }
}

// ------------------------------- Timer -------------------------------

void DecimulGapEditor::timerCallback()
{
    auto& apvts = processorRef.apvts;
    auto get = [&](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };

    // La curva pudo cambiar por fuera (restaurar proyecto, preset del host).
    if (processorRef.getCurveVersion() != lastCurveVersion)
        refreshCurveFromProcessor();
    else if (processorRef.getPresetIndex() != shownPreset)
    {
        shownPreset = processorRef.getPresetIndex();
        updatingPresetCombo = true;
        presetCombo.setSelectedId(shownPreset >= 0 ? shownPreset + 2 : 1, juce::dontSendNotification);
        updatingPresetCombo = false;
    }

    const int trig = (int) get(ParamIDs::trigger);
    const int band = (int) get(ParamIDs::band);
    const int rate = (int) get(ParamIDs::rate);

    // Cabezal: en modo Sync siempre; en MIDI/Audio, quieto al final hasta el próximo disparo.
    curveEditor.setPlayState(processorRef.uiPhase.load(), processorRef.uiGain.load());
    curveEditor.setDepth(get(ParamIDs::depth) * 0.01f);

    const double beats = DecimulGapProcessor::rateBeats(rate);
    curveEditor.setGridDivisions(beats >= 1.0 ? (int) std::lround(std::min(beats, 8.0)) : 4);

    trigSyncPill.setToggleState(trig == 0, juce::dontSendNotification);
    trigMidiPill.setToggleState(trig == 1, juce::dontSendNotification);
    trigAudioPill.setToggleState(trig == 2, juce::dontSendNotification);
    bandFullPill.setToggleState(band == 0, juce::dontSendNotification);
    bandLowPill.setToggleState(band == 1, juce::dontSendNotification);
    bandHighPill.setToggleState(band == 2, juce::dontSendNotification);

    // Lo que no aplica al modo actual se apaga para que no confunda.
    noteKnob.slider.setEnabled(trig == 1);
    noteKnob.label.setAlpha(trig == 1 ? 1.0f : 0.4f);
    noteKnob.slider.setAlpha(trig == 1 ? 1.0f : 0.4f);
    thresholdKnob.slider.setEnabled(trig == 2);
    thresholdKnob.label.setAlpha(trig == 2 ? 1.0f : 0.4f);
    thresholdKnob.slider.setAlpha(trig == 2 ? 1.0f : 0.4f);

    const bool crossoverOn = band != 0;
    crossoverKnob.slider.setEnabled(crossoverOn);
    crossoverKnob.slider.setAlpha(crossoverOn ? 1.0f : 0.4f);
    crossoverKnob.label.setAlpha(crossoverOn ? 1.0f : 0.4f);

    const bool synced = processorRef.hostSynced.load();
    tempoKnob.slider.setAlpha(synced ? 0.4f : 1.0f);
    tempoKnob.label.setAlpha(synced ? 0.4f : 1.0f);
    tempoKnob.label.setText(synced ? "TEMPO (del host)" : "TEMPO (sin host)", juce::dontSendNotification);

    juce::String hint;
    if (trig == 0)
        hint = "SYNC: la curva sigue el tempo y la posicion del DAW.";
    else if (trig == 1)
        hint = "MIDI: cada nota dispara la curva una vez. Mandale las notas del kick.";
    else
        hint = "AUDIO: detecta el kick en el sidechain del plugin (o en la entrada si no esta activo).";
    hintLabel.setText(hint, juce::dontSendNotification);

    const float outDb = processorRef.outputPeakDb.load();
    outputMeter.setLevelDb(outDb);
    outputMeter.repaint();
    vuArc.setLevelDb(outDb);
    vuArc.repaint();
    bypassButton.repaint();
}
