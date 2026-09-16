#include "PluginEditor.h"
#include "BinaryData.h"

// ---------------------------------------------------------------------------
// SimpleMeter: ladder de segmentos tipo VU (verde/amarillo/rojo), sin
// ballistics/peak-hold real — a propósito simple.
// ---------------------------------------------------------------------------
void DecimulAcousticEditor::SimpleMeter::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    constexpr int numSegments = 16;
    constexpr float minDb = -48.0f, maxDb = 0.0f;
    const float norm = juce::jlimit(0.0f, 1.0f, (levelDb - minDb) / (maxDb - minDb));
    const int litSegments = juce::roundToInt(norm * (float) numSegments);

    auto bounds = getLocalBounds();
    constexpr int segGap = 1;
    const int segHeight = juce::jmax(1, (bounds.getHeight() - segGap * (numSegments - 1)) / numSegments);

    for (int i = 0; i < numSegments; ++i)
    {
        // i = 0 es el segmento de más abajo.
        auto segRect = juce::Rectangle<int>(bounds.getX(),
                                             bounds.getBottom() - (i + 1) * (segHeight + segGap) + segGap,
                                             bounds.getWidth(), segHeight);
        juce::Colour c = DecimuColours::accentCyan;
        if (i >= numSegments - 2)      c = DecimuColours::accentRec;   // últimos 2: zona de clip
        else if (i >= numSegments - 5) c = juce::Colours::yellow;      // siguientes 3

        g.setColour(i < litSegments ? c : DecimuColours::textMuted.withAlpha(0.15f));
        g.fillRect(segRect);
    }
}

// ---------------------------------------------------------------------------
// LedStackSelector: N opciones apiladas verticalmente, un punto + texto cada
// una, tipo Warm/Deep/Air-Cut de la referencia.
// ---------------------------------------------------------------------------
void DecimulAcousticEditor::LedStackSelector::paint(juce::Graphics& g)
{
    const int n = options.size();
    if (n == 0)
        return;

    const int rowH = getHeight() / n;
    for (int i = 0; i < n; ++i)
    {
        auto row = juce::Rectangle<int>(0, i * rowH, getWidth(), rowH);
        const bool isSel = (i == selected);

        auto dotArea = row.removeFromLeft(rowH).toFloat().reduced((float) rowH * 0.3f);
        g.setColour(isSel ? DecimuColours::accentCyan : DecimuColours::textMuted.withAlpha(0.4f));
        g.fillEllipse(dotArea);

        g.setColour(isSel ? DecimuColours::text : DecimuColours::textMuted);
        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        g.drawText(options[i], row.reduced(2, 0), juce::Justification::centredLeft);
    }
}

void DecimulAcousticEditor::LedStackSelector::mouseUp(const juce::MouseEvent& e)
{
    const int n = options.size();
    if (n == 0)
        return;

    const int rowH = juce::jmax(1, getHeight() / n);
    const int idx = juce::jlimit(0, n - 1, e.y / rowH);
    setSelectedIndex(idx);
    if (onChange != nullptr)
        onChange(idx);
}

void DecimulAcousticEditor::LedStackSelector::setSelectedIndex(int idx)
{
    if (idx != selected)
    {
        selected = idx;
        repaint();
    }
}

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------
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

void DecimulAcousticEditor::setupKnob(juce::Slider& knob)
{
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible(knob);
}

void DecimulAcousticEditor::setupSelector(LedStackSelector& sel, const juce::StringArray& items,
                                           const juce::String& paramID)
{
    sel.options = items;
    sel.selected = (int) processorRef.apvts.getRawParameterValue(paramID)->load();
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

    setupSelector(lowModeSel, { "Warm", "Deep", "Air-Cut" }, ParamIDs::lowMode);
    setupSelector(highModeSel, { "Silk", "Sheen", "Crisp" }, ParamIDs::highMode);
    setupSelector(compModeSel, { "Glue", "Snap", "Squash" }, ParamIDs::compMode);
    setupSelector(spaceATypeSel, { "Room", "Hall", "Cathedral" }, ParamIDs::spaceAType);
    setupSelector(spaceBTypeSel, { "Close", "Wide", "Vast" }, ParamIDs::spaceBType);
    setupSelector(echoDivisionSel, { "Slap", "8th", "Quarter" }, ParamIDs::echoDivision);

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
    spaceAPreDelayAttach = std::make_unique<SliderAttachment>(apvts, ParamIDs::spaceAPreDelay, spaceAPreDelayKnob);
    spaceBPreDelayAttach = std::make_unique<SliderAttachment>(apvts, ParamIDs::spaceBPreDelay, spaceBPreDelayKnob);

    bypassAttach = std::make_unique<ButtonAttachment>(apvts, ParamIDs::bypass, bypassButton);

    setSize(1000, 560);
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

    // Paneles agrupados, estilo "módulo" de hardware — se dibujan antes que los
    // controles hijos, que quedan pintados encima automáticamente por JUCE.
    auto drawPanel = [&](const juce::Rectangle<int>& r)
    {
        g.setColour(DecimuColours::panel.withAlpha(0.6f));
        g.fillRoundedRectangle(r.toFloat(), 6.0f);
        g.setColour(DecimuColours::textMuted.withAlpha(0.25f));
        g.drawRoundedRectangle(r.toFloat(), 6.0f, 1.0f);
    };
    drawPanel(inputPanel);
    drawPanel(mainPanel);
    drawPanel(blendPanel);
    drawPanel(outputPanel);

    // Marco general tipo "visor de cámara" + separadores de header/footer.
    g.setColour(DecimuColours::textMuted.withAlpha(0.35f));
    g.drawRect(getLocalBounds(), 1);
    g.drawLine(0.0f, 54.0f, (float) getWidth(), 54.0f, 1.0f);
    g.drawLine(0.0f, (float) getHeight() - 22.0f, (float) getWidth(), (float) getHeight() - 22.0f, 1.0f);

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

    // --- Paneles: Input | banco principal (6 módulos) | Blend | Output ---
    constexpr int sideW = 110;
    inputPanel = bounds.removeFromLeft(sideW);
    bounds.removeFromLeft(8);
    outputPanel = juce::Rectangle<int>(bounds.getRight() - sideW, bounds.getY(), sideW, bounds.getHeight());
    bounds.removeFromRight(sideW + 8);
    blendPanel = juce::Rectangle<int>(bounds.getRight() - sideW, bounds.getY(), sideW, bounds.getHeight());
    bounds.removeFromRight(sideW + 8);
    mainPanel = bounds;

    // --- Input ---
    {
        auto col = inputPanel.reduced(6);
        auto labelArea = col.removeFromBottom(20);
        inputLabel.setBounds(labelArea);
        auto meterArea = col.removeFromLeft(16);
        inputMeter.setBounds(meterArea);
        inputSlider.setBounds(col);
    }

    // --- Banco principal: 6 columnas (Low/High/Dynamics/Space A/Space B/Echo) ---
    auto mainArea = mainPanel.reduced(6);
    const int colW = mainArea.getWidth() / 6;

    auto layoutModuleColumn = [&](juce::Rectangle<int> col, LedStackSelector& sel,
                                   juce::Slider& slider, juce::Label& label, juce::Slider* knob)
    {
        col = col.reduced(4);
        auto labelArea = col.removeFromBottom(20);
        label.setBounds(labelArea);

        if (knob != nullptr)
        {
            auto knobArea = col.removeFromTop(42);
            knob->setBounds(knobArea.withSizeKeepingCentre(38, 38));
        }

        auto selArea = col.removeFromTop(48);
        sel.setBounds(selArea);
        slider.setBounds(col);
    };

    layoutModuleColumn(mainArea.removeFromLeft(colW), lowModeSel, lowSlider, lowLabel, nullptr);
    layoutModuleColumn(mainArea.removeFromLeft(colW), highModeSel, highSlider, highLabel, nullptr);
    layoutModuleColumn(mainArea.removeFromLeft(colW), compModeSel, compSlider, compLabel, nullptr);
    layoutModuleColumn(mainArea.removeFromLeft(colW), spaceATypeSel, spaceASlider, spaceALabel, &spaceAPreDelayKnob);
    layoutModuleColumn(mainArea.removeFromLeft(colW), spaceBTypeSel, spaceBSlider, spaceBLabel, &spaceBPreDelayKnob);
    layoutModuleColumn(mainArea, echoDivisionSel, echoSlider, echoLabel, nullptr);

    // --- Blend: comparte columna con el botón de Tap Tempo + BPM ---
    {
        auto col = blendPanel.reduced(6);
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
        auto col = outputPanel.reduced(6);
        auto labelArea = col.removeFromBottom(20);
        outputLabel.setBounds(labelArea);
        auto meterArea = col.removeFromLeft(16);
        outputMeter.setBounds(meterArea);
        outputSlider.setBounds(col);
    }
}

void DecimulAcousticEditor::timerCallback()
{
    inputMeter.levelDb = processorRef.inputPeakDb.load();
    inputMeter.repaint();

    outputMeter.levelDb = processorRef.outputPeakDb.load();
    outputMeter.repaint();

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
