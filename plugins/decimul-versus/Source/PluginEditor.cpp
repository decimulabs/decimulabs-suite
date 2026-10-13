#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    juce::String mmss(double seconds)
    {
        const int total = (int) std::max(0.0, seconds);
        return juce::String::formatted("%d:%02d", total / 60, total % 60);
    }

    bool isAudioFile(const juce::File& f)
    {
        static const juce::StringArray exts { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg", ".m4a", ".wma" };
        return exts.contains(f.getFileExtension().toLowerCase());
    }
}

void DecimulVersusEditor::setupSelector(DecimuLedStackSelector& sel, const juce::StringArray& items,
                                        const juce::String& paramID)
{
    sel.setOptions(items);
    sel.setSelectedIndex((int) processorRef.apvts.getRawParameterValue(paramID)->load());
    addAndMakeVisible(sel);

    sel.onChange = [this, paramID](int idx) { setParam(paramID, (float) idx); };
}

void DecimulVersusEditor::setupBracketLabel(juce::Label& label, const juce::String& text)
{
    label.setText("[ " + text + " ]", juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, DecimuColours::text);
    addAndMakeVisible(label);
}

void DecimulVersusEditor::setParam(const juce::String& id, float plainValue)
{
    if (auto* param = processorRef.apvts.getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1(plainValue));
}

DecimulVersusEditor::DecimulVersusEditor(DecimulVersusProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);
    setWantsKeyboardFocus(true);

    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    // ---- Slots ----
    for (int i = 0; i < DecimulVersusProcessor::numSlots; ++i)
    {
        auto button = std::make_unique<VersusSlotButton>(i);
        button->onClick = [this, i]
        {
            selectSlot(i);
            const auto info = processorRef.getSlotInfo(i);
            if (!info.loaded && !info.loading)
                openFileChooser(i);
        };
        button->onContextMenu = [this, i] { showSlotMenu(i); };
        addAndMakeVisible(*button);
        slotButtons[(size_t) i] = std::move(button);
    }

    // ---- Transporte ----
    playPill.setClickingTogglesState(false);
    playPill.onClick = [this] { processorRef.setUserPlaying(!processorRef.isUserPlaying()); };
    addAndMakeVisible(playPill);
    addAndMakeVisible(hostPill);
    addAndMakeVisible(loopPill);

    timeLabel.setJustificationType(juce::Justification::centredRight);
    timeLabel.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    addAndMakeVisible(timeLabel);

    // ---- Forma de onda ----
    waveform.onSeek = [this](double seconds) { processorRef.seekSeconds(seconds); };
    waveform.onLoop = [this](double start, double end)
    {
        processorRef.setLoopRegion(processorRef.getSelectedSlot(), start, end);
        setParam(ParamIDs::loopEnabled, end > start ? 1.0f : 0.0f);
    };
    addAndMakeVisible(waveform);

    // ---- Análisis ----
    diffPill.setClickingTogglesState(true);
    diffPill.onClick = [this] { spectrum.setShowDiff(diffPill.getToggleState()); };
    addAndMakeVisible(diffPill);

    resetPill.setClickingTogglesState(false);
    resetPill.onClick = [this] { processorRef.resetMixAnalysis(); };
    addAndMakeVisible(resetPill);

    addAndMakeVisible(spectrum);
    addAndMakeVisible(metrics);

    // ---- A / B ----
    aPill.setEmphasis(true);
    bPill.setEmphasis(true);
    aPill.setClickingTogglesState(false);
    bPill.setClickingTogglesState(false);
    aPill.onClick = [this] { setParam(ParamIDs::refActive, 0.0f); };
    bPill.onClick = [this] { setParam(ParamIDs::refActive, 1.0f); };
    addAndMakeVisible(aPill);
    addAndMakeVisible(bPill);
    setupBracketLabel(abLabel, "ESCUCHAR  (teclas A / B, 1-8 = slot)");

    // ---- Controles ----
    setupSelector(matchSel, { "Auto LUFS", "Off" }, ParamIDs::matchMode);
    setupSelector(monitorSel, { "Stereo", "Mono", "Side" }, ParamIDs::monitor);
    setupSelector(bandSel, { "Full", "Low", "Mid", "High" }, ParamIDs::band);
    setupBracketLabel(matchLabel, "MATCH");
    setupBracketLabel(monitorLabel, "MONITOR");
    setupBracketLabel(bandLabel, "BANDA");
    setupBracketLabel(trimLabel, "TRIM REF");
    setupBracketLabel(outputLabel, "OUTPUT");

    trimKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    trimKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
    addAndMakeVisible(trimKnob);

    outputSlider.setSliderStyle(juce::Slider::LinearVertical);
    outputSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
    addAndMakeVisible(outputSlider);

    addAndMakeVisible(outputMeter);
    addAndMakeVisible(vuArc);
    addAndMakeVisible(bypassButton);

    auto& apvts = processorRef.apvts;
    trimAttach = std::make_unique<SliderAttachment>(apvts, ParamIDs::trim, trimKnob);
    outputAttach = std::make_unique<SliderAttachment>(apvts, ParamIDs::outputTrim, outputSlider);
    bypassAttach = std::make_unique<ButtonAttachment>(apvts, ParamIDs::bypass, bypassButton);
    hostAttach = std::make_unique<ButtonAttachment>(apvts, ParamIDs::followHost, hostPill);
    loopAttach = std::make_unique<ButtonAttachment>(apvts, ParamIDs::loopEnabled, loopPill);

    setSize(1120, 740);
    setResizable(true, true);
    setResizeLimits(960, 680, 1800, 1100);
    startTimerHz(30);
}

DecimulVersusEditor::~DecimulVersusEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

// ------------------------------ Acciones ------------------------------

void DecimulVersusEditor::selectSlot(int slot)
{
    setParam(ParamIDs::slot, (float) (slot + 1));
}

void DecimulVersusEditor::openFileChooser(int slot)
{
    static juce::File lastDirectory = juce::File::getSpecialLocation(juce::File::userMusicDirectory);

    chooser = std::make_unique<juce::FileChooser>(
        "Elegi una cancion de referencia", lastDirectory,
        "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a;*.wma");

    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, slot](const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file.existsAsFile())
        {
            lastDirectory = file.getParentDirectory();
            processorRef.loadReference(slot, file);
        }
    });
}

void DecimulVersusEditor::showSlotMenu(int slot)
{
    const auto info = processorRef.getSlotInfo(slot);

    juce::PopupMenu menu;
    menu.addItem(1, info.loaded ? "Reemplazar por otro archivo..." : "Cargar archivo...");
    menu.addItem(2, "Quitar referencia", info.loaded || info.loading || info.error.isNotEmpty());

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, slot](int result)
    {
        if (result == 1)
            openFileChooser(slot);
        else if (result == 2)
            processorRef.clearReference(slot);
    });
}

bool DecimulVersusEditor::keyPressed(const juce::KeyPress& key)
{
    const auto c = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());

    if (c == 'a') { setParam(ParamIDs::refActive, 0.0f); return true; }
    if (c == 'b') { setParam(ParamIDs::refActive, 1.0f); return true; }
    if (c >= '1' && c <= '8') { selectSlot((int) (c - '1')); return true; }

    return false;
}

bool DecimulVersusEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& path : files)
        if (isAudioFile(juce::File(path)))
            return true;
    return false;
}

void DecimulVersusEditor::filesDropped(const juce::StringArray& files, int, int)
{
    juce::Array<juce::File> audio;
    for (auto& path : files)
        if (isAudioFile(juce::File(path)))
            audio.add(juce::File(path));

    if (audio.isEmpty())
        return;

    const int selected = processorRef.getSelectedSlot();

    if (audio.size() == 1)
    {
        processorRef.loadReference(selected, audio[0]);
        return;
    }

    // Varios archivos: arranca desde el slot elegido y llena los que estén vacíos.
    int fileIndex = 0;
    for (int n = 0; n < DecimulVersusProcessor::numSlots && fileIndex < audio.size(); ++n)
    {
        const int slot = (selected + n) % DecimulVersusProcessor::numSlots;
        const auto info = processorRef.getSlotInfo(slot);
        if (!info.loaded && !info.loading)
            processorRef.loadReference(slot, audio[fileIndex++]);
    }
}

// ------------------------------- Dibujo -------------------------------

void DecimulVersusEditor::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    DecimuChrome::paintChassisBackground(g, full, 14.0f);
    DecimuChrome::paintRackHandles(g, full);
    DecimuChrome::paintCornerScrews(g, full, 12.0f);

    DecimuChrome::paintCompartment(g, topbarRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, slotsRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, waveRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, analysisRect.toFloat(), false, 8.0f);

    DecimuChrome::paintCompartment(g, abRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, matchRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, trimRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, monitorRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, bandRect.toFloat(), false, 8.0f);
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
        auto wordmarkArea = logoArea.removeFromLeft(juce::jmin(logoArea.getWidth(), 200))
                                    .reduced(0, 7);
        g.drawImage(wordmarkImage, wordmarkArea.toFloat(),
                    juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
        logoArea.removeFromLeft(18);
    }

    g.setColour(DecimuColours::accentPrimary);
    g.setFont(lookAndFeel.monoFont(22.0f));
    g.drawText("VERSUS  (AB)", logoArea, juce::Justification::centredLeft);

    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(13.0f));
    auto footer = getLocalBounds().removeFromBottom(22).reduced(16, 3);
    g.drawText("DECIMULABS - VERSUS (AB) - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulVersusEditor::resized()
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

    slotsRect = content.removeFromTop(62);
    content.removeFromTop(8);

    auto controls = content.removeFromBottom(160);
    content.removeFromBottom(8);

    waveRect = content.removeFromTop(176);
    content.removeFromTop(8);
    analysisRect = content;

    // ---- Slots ----
    {
        auto row = slotsRect.reduced(8);
        const int gap = 6;
        const int w = (row.getWidth() - gap * (DecimulVersusProcessor::numSlots - 1)) / DecimulVersusProcessor::numSlots;
        for (int i = 0; i < DecimulVersusProcessor::numSlots; ++i)
        {
            slotButtons[(size_t) i]->setBounds(row.removeFromLeft(w));
            row.removeFromLeft(gap);
        }
    }

    // ---- Forma de onda + transporte ----
    {
        auto area = waveRect.reduced(8);
        auto strip = area.removeFromTop(30);
        playPill.setBounds(strip.removeFromLeft(90));
        strip.removeFromLeft(6);
        hostPill.setBounds(strip.removeFromLeft(130));
        strip.removeFromLeft(6);
        loopPill.setBounds(strip.removeFromLeft(80));
        timeLabel.setBounds(strip);
        area.removeFromTop(6);
        waveform.setBounds(area);
    }

    // ---- Análisis ----
    {
        auto area = analysisRect.reduced(8);
        auto metricsArea = area.removeFromRight(330);
        area.removeFromRight(8);
        metrics.setBounds(metricsArea);

        auto strip = area.removeFromTop(28);
        resetPill.setBounds(strip.removeFromRight(170));
        strip.removeFromRight(6);
        diffPill.setBounds(strip.removeFromRight(90));
        area.removeFromTop(6);
        spectrum.setBounds(area);
    }

    // ---- Controles ----
    {
        const int gap = 8;
        const int matchW = 150, trimW = 130, monitorW = 130, bandW = 130, outputW = 130;
        outputRect = controls.removeFromRight(outputW);
        controls.removeFromRight(gap);
        bandRect = controls.removeFromRight(bandW);
        controls.removeFromRight(gap);
        monitorRect = controls.removeFromRight(monitorW);
        controls.removeFromRight(gap);
        trimRect = controls.removeFromRight(trimW);
        controls.removeFromRight(gap);
        matchRect = controls.removeFromRight(matchW);
        controls.removeFromRight(gap);
        abRect = controls;
    }

    {
        auto col = abRect.reduced(10);
        auto label = col.removeFromBottom(20);
        abLabel.setBounds(label);
        col.removeFromBottom(4);
        auto left = col.removeFromLeft(col.getWidth() / 2 - 4);
        col.removeFromLeft(8);
        aPill.setBounds(left);
        bPill.setBounds(col);
    }

    auto layoutSelector = [](juce::Rectangle<int> rect, DecimuLedStackSelector& sel,
                              juce::Label& label, int selectorHeight)
    {
        auto col = rect.reduced(10);
        label.setBounds(col.removeFromBottom(20));
        col.removeFromBottom(4);
        sel.setBounds(col.removeFromTop(selectorHeight));
    };

    layoutSelector(matchRect, matchSel, matchLabel, 48);
    layoutSelector(monitorRect, monitorSel, monitorLabel, 56);
    layoutSelector(bandRect, bandSel, bandLabel, 76);

    {
        auto col = trimRect.reduced(10);
        trimLabel.setBounds(col.removeFromBottom(20));
        trimKnob.setBounds(col);
    }

    {
        auto col = outputRect.reduced(10);
        outputLabel.setBounds(col.removeFromBottom(20));
        outputMeter.setBounds(col.removeFromLeft(16));
        outputSlider.setBounds(col);
    }
}

// ------------------------------- Timer -------------------------------

void DecimulVersusEditor::timerCallback()
{
    auto& apvts = processorRef.apvts;
    const int slot = processorRef.getSelectedSlot();

    // ---- Slots ----
    for (int i = 0; i < DecimulVersusProcessor::numSlots; ++i)
    {
        const auto info = processorRef.getSlotInfo(i);
        VersusSlotButton::State s;
        s.name = info.name;
        s.loaded = info.loaded;
        s.loading = info.loading;
        s.hasError = info.error.isNotEmpty() && !info.loading;
        s.selected = (i == slot);
        slotButtons[(size_t) i]->setState(s);
    }

    const auto info = processorRef.getSlotInfo(slot);
    auto track = processorRef.getTrack(slot);
    const bool trackOk = track != nullptr && info.loaded;

    // ---- Forma de onda ----
    waveform.setTrack(trackOk ? track : nullptr);
    if (info.loading)
        waveform.setStatusText("CARGANDO Y ANALIZANDO...");
    else if (info.error.isNotEmpty())
        waveform.setStatusText("ERROR: " + info.error.toUpperCase());
    else
        waveform.setStatusText({});

    const double playhead = processorRef.getPlayheadSeconds();
    waveform.setPlayhead(playhead);

    double loopStart = 0.0, loopEnd = 0.0;
    processorRef.getLoopRegion(slot, loopStart, loopEnd);
    waveform.setLoop(loopStart, loopEnd, apvts.getRawParameterValue(ParamIDs::loopEnabled)->load() > 0.5f);

    playPill.setToggleState(processorRef.isUserPlaying(), juce::dontSendNotification);

    if (trackOk)
        timeLabel.setText((processorRef.isReferenceRunning() ? "> " : "  ") + mmss(playhead) + " / "
                              + mmss(track->lengthSeconds) + (track->truncated ? "  (recortado a 15 min)" : ""),
                          juce::dontSendNotification);
    else
        timeLabel.setText({}, juce::dontSendNotification);

    // ---- A / B ----
    const bool b = apvts.getRawParameterValue(ParamIDs::refActive)->load() > 0.5f;
    aPill.setToggleState(!b, juce::dontSendNotification);
    bPill.setToggleState(b, juce::dontSendNotification);

    // ---- Selectores ----
    matchSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::matchMode)->load());
    monitorSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::monitor)->load());
    bandSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::band)->load());

    // ---- Espectro ----
    VersusSpectrumView::Data sd;
    sd.mixValid = processorRef.mixBandsValid.load();
    for (int i = 0; i < VersusBands::numBands; ++i)
        sd.mix[(size_t) i] = processorRef.mixBandDb[(size_t) i].load();
    sd.refValid = trackOk;
    if (trackOk)
        sd.ref = track->bandDb;
    sd.refGainDb = processorRef.appliedRefGainDb.load();
    spectrum.setData(sd);

    // ---- Medidas ----
    VersusMetricsView::Data md;
    md.mixValid = processorRef.mixIntegratedLufs.load() > -99.0f || processorRef.mixBandsValid.load();
    md.mixLufsI = processorRef.mixIntegratedLufs.load();
    md.mixLufsS = processorRef.mixShortTermLufs.load();
    md.mixPeak = processorRef.mixPeakDb.load();
    md.mixWidth = processorRef.mixWidthPct.load();
    md.mixCorr = processorRef.mixCorrelation.load();
    md.refValid = trackOk;
    if (trackOk)
    {
        md.refLufsI = track->integratedLufs;
        md.refLufsS = track->shortTermAt(playhead);
        md.refPeak = track->peakDb;
        md.refWidth = track->widthPct;
        md.refCorr = track->correlation;
        md.refName = track->displayName;
    }
    md.gainAppliedDb = processorRef.appliedRefGainDb.load();
    md.gainLimited = processorRef.refGainLimitedByPeak.load();
    md.matchAuto = (int) apvts.getRawParameterValue(ParamIDs::matchMode)->load() == 0;
    metrics.setData(md);

    // ---- Salida ----
    const float outDb = processorRef.outputPeakDb.load();
    outputMeter.setLevelDb(outDb);
    outputMeter.repaint();
    vuArc.setLevelDb(outDb);
    vuArc.repaint();
    bypassButton.repaint();
}
