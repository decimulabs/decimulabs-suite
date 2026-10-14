#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    bool isAudioFile(const juce::File& f)
    {
        static const juce::StringArray exts { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg" };
        return exts.contains(f.getFileExtension().toLowerCase());
    }

    juce::Font hudFont(juce::Component& c, float height)
    {
        if (auto* laf = dynamic_cast<DecimuLookAndFeel*>(&c.getLookAndFeel()))
            return laf->monoFont(height);
        return juce::Font(juce::FontOptions(height));
    }
}

// ============================ TriggerSlotButton ============================

void TriggerSlotButton::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onContextMenu)
            onContextMenu();
        return;
    }
    juce::Button::mouseDown(e);
}

void TriggerSlotButton::paintButton(juce::Graphics& g, bool isMouseOver, bool)
{
    auto b = getLocalBounds().toFloat().reduced(1.5f);
    const auto accent = DecimuColours::accentPrimary;

    g.setColour(isMouseOver ? DecimuColours::panel.brighter(0.15f) : DecimuColours::panel);
    g.fillRoundedRectangle(b, 6.0f);

    if (flash > 0.0f)
    {
        g.setColour(DecimuColours::accentRec.withAlpha(0.45f * flash));
        g.fillRoundedRectangle(b, 6.0f);
    }

    g.setColour(hasError ? DecimuColours::accentRec : (loaded ? accent.withAlpha(0.8f) : DecimuColours::textMuted.withAlpha(0.5f)));
    g.drawRoundedRectangle(b, 6.0f, 1.0f);

    auto numberArea = b.removeFromLeft(30.0f);
    g.setColour(loaded ? accent : DecimuColours::text);
    g.setFont(hudFont(*this, 24.0f));
    g.drawText(juce::String(index + 1), numberArea, juce::Justification::centred);

    const juce::String line = hasError ? "ERROR AL CARGAR"
                            : loading  ? "CARGANDO..."
                            : loaded   ? name
                                       : "vacio - click o arrastra";

    g.setColour(loaded ? DecimuColours::text : DecimuColours::textMuted);
    g.setFont(hudFont(*this, 15.0f));
    g.drawFittedText(line, b.reduced(4.0f, 2.0f).toNearestInt(), juce::Justification::centredLeft, 1);
}

// ============================= DecimulTriggerEditor =============================

void DecimulTriggerEditor::setParam(const juce::String& id, float plainValue)
{
    if (auto* param = processorRef.apvts.getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1(plainValue));
}

void DecimulTriggerEditor::addKnob(Knob& k, const juce::String& paramID, const juce::String& caption)
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

// Rotary cuadrado (si el slider es más ancho que alto el knob sale ovalado).
void DecimulTriggerEditor::placeKnob(Knob& k, juce::Rectangle<int> cell)
{
    k.label.setBounds(cell.removeFromTop(16));

    const int side = std::max(30, std::min(cell.getWidth(), cell.getHeight() - 18));
    const int width = std::max(side, 52);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, width, 16);
    k.slider.setBounds(cell.withSizeKeepingCentre(width, side + 18));
}

DecimulTriggerEditor::DecimulTriggerEditor(DecimulTriggerProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    // ---- Monitor ----
    scope.onThresholdDragged = [this](float db) { setParam(ParamIDs::threshold, db); };
    scope.onMaxLevelDragged = [this](float db) { setParam(ParamIDs::maxLevel, db); };
    addAndMakeVisible(scope);
    addAndMakeVisible(readout);

    // ---- Knobs ----
    addKnob(thresholdKnob, ParamIDs::threshold, "THRESHOLD");
    addKnob(maxLevelKnob, ParamIDs::maxLevel, "MAX LEVEL");
    addKnob(scanKnob, ParamIDs::scan, "SCAN");
    addKnob(retriggerKnob, ParamIDs::retrigger, "RETRIGGER");
    addKnob(lowCutKnob, ParamIDs::lowCut, "LOW CUT");
    addKnob(highCutKnob, ParamIDs::highCut, "HIGH CUT");
    addKnob(minVelKnob, ParamIDs::minVel, "VEL MIN");
    addKnob(maxVelKnob, ParamIDs::maxVel, "VEL MAX");
    addKnob(curveKnob, ParamIDs::curve, "CURVA");
    addKnob(noteKnob, ParamIDs::note, "NOTA");
    addKnob(channelKnob, ParamIDs::channel, "CANAL");
    addKnob(noteLenKnob, ParamIDs::noteLen, "LARGO NOTA");
    addKnob(sampleLevelKnob, ParamIDs::sampleLevel, "MUESTRAS");
    addKnob(origLevelKnob, ParamIDs::origLevel, "ORIGINAL");
    addKnob(velSensKnob, ParamIDs::velSens, "VEL > VOLUMEN");
    addKnob(matchKnob, ParamIDs::match, "PARECIDO MIN");
    addKnob(fpWindowKnob, ParamIDs::fpWindow, "VENTANA");
    addKnob(capLengthKnob, ParamIDs::capLength, "LARGO");
    addKnob(capLayersKnob, ParamIDs::capLayers, "CAPAS");
    addKnob(capRRKnob, ParamIDs::capRR, "ALTERNAN.");

    auto setupSection = [this](juce::Label& l, const juce::String& text)
    {
        l.setText("[ " + text + " ]", juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centredLeft);
        l.setColour(juce::Label::textColourId, DecimuColours::text);
        addAndMakeVisible(l);
    };
    setupSection(detectLabel, "DETECCION");
    setupSection(velocityLabel, "VELOCITY");
    setupSection(midiLabel, "MIDI DE SALIDA");
    setupSection(samplerLabel, "REPRODUCTOR PROPIO");
    setupSection(printLabel, "HUELLA DEL GOLPE");
    setupSection(captureLabel, "CAPTURA DE MUESTRAS");

    // ---- Botones ----
    addAndMakeVisible(fixedVelPill);
    addAndMakeVisible(listenPill);
    addAndMakeVisible(alignPill);
    addAndMakeVisible(chokePill);
    addAndMakeVisible(rejectPill);
    fixedVelPill.setTooltip("Todos los golpes salen con la velocity maxima, sin medir la intensidad.");
    listenPill.setTooltip("Escucha la senal que usa el detector (despues de los filtros), para ajustar Low/High Cut.");
    alignPill.setTooltip("Retrasa el audio original lo que dura la medicion, para que coincida con el MIDI y el sample.");
    chokePill.setTooltip("Cada golpe nuevo corta las muestras que seguian sonando.");
    rejectPill.setTooltip("Solo deja pasar los golpes que se parecen a la huella aprendida (rechaza el bleed de otros instrumentos).");

    auto wireSampler = [this](DecimuPillButton& pill, float value)
    {
        pill.setClickingTogglesState(false);
        pill.onClick = [this, value] { setParam(ParamIDs::sampler, value); };
        addAndMakeVisible(pill);
    };
    wireSampler(samplerOffPill, 0.0f);
    wireSampler(samplerReinforcePill, 1.0f);
    wireSampler(samplerReplacePill, 2.0f);

    hintLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    hintLabel.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    hintLabel.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(hintLabel);

    // ---- Huella ----
    learnPill.setClickingTogglesState(false);
    learnPill.setTooltip("Toca solo el instrumento (unos 8 a 12 golpes) y apreta de nuevo para cerrar.");
    learnPill.onClick = [this]
    {
        if (processorRef.isLearning()) processorRef.stopLearning();
        else                           processorRef.startLearning();
    };
    addAndMakeVisible(learnPill);

    clearPrintPill.setClickingTogglesState(false);
    clearPrintPill.onClick = [this] { processorRef.clearFingerprint(); };
    addAndMakeVisible(clearPrintPill);

    addAndMakeVisible(fingerprintView);

    printStatus.setFont(juce::Font(juce::FontOptions(11.0f)));
    printStatus.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    printStatus.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(printStatus);

    // ---- Captura ----
    recPill.setClickingTogglesState(false);
    recPill.setTooltip("Graba cada golpe detectado (con un poco de lo que habia antes del ataque).");
    recPill.onClick = [this] { processorRef.setCapturing(!processorRef.isCapturing()); };
    addAndMakeVisible(recPill);

    makePill.setClickingTogglesState(false);
    makePill.setTooltip("Reparte los golpes grabados en capas por velocity y alternancias, los guarda como "
                        "WAV y los carga en el reproductor.");
    makePill.onClick = [this]
    {
        const auto result = processorRef.exportTakes(captureName.getText(),
                                                     (int) std::lround(processorRef.apvts.getRawParameterValue(ParamIDs::capLayers)->load()),
                                                     (int) std::lround(processorRef.apvts.getRawParameterValue(ParamIDs::capRR)->load()));
        exportMessage = result.message;
        if (result.folder != juce::File())
            lastExportFolder = result.folder;
        if (result.files > 0)
            setParam(ParamIDs::sampler, 2.0f);
    };
    addAndMakeVisible(makePill);

    clearTakesPill.setClickingTogglesState(false);
    clearTakesPill.onClick = [this]
    {
        processorRef.clearTakes();
        exportMessage = {};
    };
    addAndMakeVisible(clearTakesPill);

    openFolderPill.setClickingTogglesState(false);
    openFolderPill.onClick = [this]
    {
        if (lastExportFolder.isDirectory())
            lastExportFolder.revealToUser();
    };
    addAndMakeVisible(openFolderPill);

    captureName.setText("Golpe", juce::dontSendNotification);
    captureName.setColour(juce::TextEditor::backgroundColourId, DecimuColours::background);
    captureName.setColour(juce::TextEditor::textColourId, DecimuColours::text);
    captureName.setColour(juce::TextEditor::outlineColourId, DecimuColours::textMuted.withAlpha(0.5f));
    captureName.setTooltip("Prefijo de los archivos que se crean (ej. Caja)");
    addAndMakeVisible(captureName);

    captureNameLabel.setText("NOMBRE", juce::dontSendNotification);
    captureNameLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    captureNameLabel.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    addAndMakeVisible(captureNameLabel);

    captureStatus.setFont(juce::Font(juce::FontOptions(11.0f)));
    captureStatus.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    captureStatus.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(captureStatus);

    // ---- Muestras ----
    for (int i = 0; i < DecimulTriggerProcessor::numSlots; ++i)
    {
        auto button = std::make_unique<TriggerSlotButton>(i);
        button->onClick = [this, i] { openFileChooser(i); };
        button->onContextMenu = [this, i] { showSlotMenu(i); };
        addAndMakeVisible(*button);
        slotButtons[(size_t) i] = std::move(button);

        for (auto* l : { &loLabels[(size_t) i], &hiLabels[(size_t) i] })
        {
            l->setJustificationType(juce::Justification::centred);
            l->setColour(juce::Label::textColourId, DecimuColours::text);
            l->setColour(juce::Label::backgroundColourId, DecimuColours::background);
            l->setColour(juce::Label::outlineColourId, DecimuColours::textMuted.withAlpha(0.5f));
            l->setEditable(false, true, false);
            l->setTooltip("Rango de velocity de esta muestra (doble click para escribirlo)");
            l->onEditorShow = [l] { if (auto* ed = l->getCurrentTextEditor()) ed->setInputRestrictions(3, "0123456789"); };
            l->onTextChange = [this, i] { commitVelocityRange(i); };
            addAndMakeVisible(*l);
        }
    }

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

    auto& apvts = processorRef.apvts;
    outputAttach = std::make_unique<SliderAttachment>(apvts, ParamIDs::outputTrim, outputSlider);
    bypassAttach = std::make_unique<ButtonAttachment>(apvts, ParamIDs::bypass, bypassButton);
    fixedAttach = std::make_unique<ButtonAttachment>(apvts, ParamIDs::fixedVel, fixedVelPill);
    listenAttach = std::make_unique<ButtonAttachment>(apvts, ParamIDs::listen, listenPill);
    alignAttach = std::make_unique<ButtonAttachment>(apvts, ParamIDs::align, alignPill);
    chokeAttach = std::make_unique<ButtonAttachment>(apvts, ParamIDs::choke, chokePill);
    rejectAttach = std::make_unique<ButtonAttachment>(apvts, ParamIDs::fingerprint, rejectPill);

    // El monitor arranca limpio: descarta lo que quedó en la cola mientras no había editor.
    {
        DecimulTriggerProcessor::ScopeFrame discard[256];
        while (processorRef.readScopeFrames(discard, 256) > 0) {}
    }
    lastHitCount = processorRef.hitCount.load();

    setSize(1100, 850);
    setResizable(true, true);
    setResizeLimits(980, 790, 1800, 1200);
    startTimerHz(30);
}

DecimulTriggerEditor::~DecimulTriggerEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

// ------------------------------ Acciones ------------------------------

void DecimulTriggerEditor::openFileChooser(int slot)
{
    static juce::File lastDirectory = juce::File::getSpecialLocation(juce::File::userMusicDirectory);

    chooser = std::make_unique<juce::FileChooser>("Elegi una muestra de bateria", lastDirectory,
                                                  "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");

    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, slot](const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file.existsAsFile())
        {
            lastDirectory = file.getParentDirectory();
            processorRef.loadSample(slot, file);
        }
    });
}

void DecimulTriggerEditor::showSlotMenu(int slot)
{
    const auto info = processorRef.getSlotInfo(slot);

    juce::PopupMenu menu;
    menu.addItem(1, info.loaded ? "Reemplazar por otro archivo..." : "Cargar archivo...");
    menu.addItem(2, "Quitar muestra", info.loaded || info.error.isNotEmpty());
    menu.addItem(3, "Rango de velocity completo (1-127)");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, slot](int result)
    {
        if (result == 1)
            openFileChooser(slot);
        else if (result == 2)
            processorRef.clearSample(slot);
        else if (result == 3)
            processorRef.setVelocityRange(slot, 1, 127);
    });
}

void DecimulTriggerEditor::commitVelocityRange(int slot)
{
    const int lo = loLabels[(size_t) slot].getText().getIntValue();
    const int hi = hiLabels[(size_t) slot].getText().getIntValue();
    processorRef.setVelocityRange(slot, lo > 0 ? lo : 1, hi > 0 ? hi : 127);
}

bool DecimulTriggerEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& path : files)
        if (isAudioFile(juce::File(path)))
            return true;
    return false;
}

void DecimulTriggerEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    juce::Array<juce::File> audio;
    for (auto& path : files)
        if (isAudioFile(juce::File(path)))
            audio.add(juce::File(path));

    if (audio.isEmpty())
        return;

    // Si se suelta sobre una fila, arranca ahí; si no, en la primera vacía.
    int start = -1;
    for (int i = 0; i < DecimulTriggerProcessor::numSlots; ++i)
        if (slotButtons[(size_t) i]->getBounds().contains(x, y))
            start = i;

    if (start < 0)
    {
        start = 0;
        for (int i = 0; i < DecimulTriggerProcessor::numSlots; ++i)
            if (!processorRef.getSlotInfo(i).loaded && !processorRef.getSlotInfo(i).loading)
            {
                start = i;
                break;
            }
    }

    int fileIndex = 0;
    for (int n = 0; n < DecimulTriggerProcessor::numSlots && fileIndex < audio.size(); ++n)
        processorRef.loadSample((start + n) % DecimulTriggerProcessor::numSlots, audio[fileIndex++]);
}

// ------------------------------- Dibujo -------------------------------

void DecimulTriggerEditor::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    DecimuChrome::paintChassisBackground(g, full, 14.0f);
    DecimuChrome::paintRackHandles(g, full);
    DecimuChrome::paintCornerScrews(g, full, 12.0f);

    DecimuChrome::paintCompartment(g, topbarRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, scopeRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, detectRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, velocityRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, midiRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, printRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, captureRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, samplerRect.toFloat(), false, 8.0f);
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
    g.drawText("TRIGGER", logoArea, juce::Justification::centredLeft);

    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(13.0f));
    auto footer = getLocalBounds().removeFromBottom(22).reduced(16, 3);
    g.drawText("DECIMULABS - TRIGGER - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulTriggerEditor::resized()
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

    // ---- Monitor + lectura ----
    scopeRect = content.removeFromTop(200);
    {
        auto area = scopeRect.reduced(8);
        readout.setBounds(area.removeFromRight(210));
        area.removeFromRight(8);
        scope.setBounds(area);
    }
    content.removeFromTop(gap);

    // ---- Fila de knobs: detección | velocity | MIDI ----
    auto knobsRow = content.removeFromTop(146);
    detectRect = knobsRow.removeFromLeft(knobsRow.getWidth() * 6 / 12);
    knobsRow.removeFromLeft(gap);
    velocityRect = knobsRow.removeFromLeft((knobsRow.getWidth() - gap) / 2);
    knobsRow.removeFromLeft(gap);
    midiRect = knobsRow;

    // Título arriba, knobs en el medio y, abajo, una franja para el botón del panel.
    auto layoutKnobs = [this](juce::Rectangle<int> rect, juce::Label& title,
                              std::initializer_list<Knob*> knobs, DecimuPillButton* pill)
    {
        auto area = rect.reduced(8, 6);
        title.setBounds(area.removeFromTop(16));
        auto bottom = area.removeFromBottom(26);
        if (pill != nullptr)
            pill->setBounds(bottom.removeFromLeft(std::min(170, bottom.getWidth())).reduced(0, 1));

        const int w = area.getWidth() / (int) knobs.size();
        for (auto* k : knobs)
            placeKnob(*k, area.removeFromLeft(w).reduced(2));
    };
    layoutKnobs(detectRect, detectLabel,
                { &thresholdKnob, &maxLevelKnob, &scanKnob, &retriggerKnob, &lowCutKnob, &highCutKnob }, &listenPill);
    layoutKnobs(velocityRect, velocityLabel, { &minVelKnob, &maxVelKnob, &curveKnob }, &fixedVelPill);
    layoutKnobs(midiRect, midiLabel, { &noteKnob, &channelKnob, &noteLenKnob }, nullptr);

    content.removeFromTop(gap);

    // ---- Huella | Captura ----
    auto midRow = content.removeFromTop(142);
    printRect = midRow.removeFromLeft(380);
    midRow.removeFromLeft(gap);
    captureRect = midRow;
    content.removeFromTop(gap);

    {
        auto area = printRect.reduced(8, 6);
        printLabel.setBounds(area.removeFromTop(16));
        auto pills = area.removeFromTop(26);
        const int pw = (pills.getWidth() - 12) / 3;
        learnPill.setBounds(pills.removeFromLeft(pw));
        pills.removeFromLeft(6);
        clearPrintPill.setBounds(pills.removeFromLeft(pw));
        pills.removeFromLeft(6);
        rejectPill.setBounds(pills);
        area.removeFromTop(4);

        auto knobs = area.removeFromLeft(138);
        placeKnob(matchKnob, knobs.removeFromLeft(69).reduced(1));
        placeKnob(fpWindowKnob, knobs.reduced(1));
        area.removeFromLeft(6);
        printStatus.setBounds(area.removeFromBottom(30));
        area.removeFromBottom(2);
        fingerprintView.setBounds(area);
    }

    {
        auto area = captureRect.reduced(8, 6);
        captureLabel.setBounds(area.removeFromTop(16));
        auto pills = area.removeFromTop(26);
        const int pw = (pills.getWidth() - 18) / 4;
        recPill.setBounds(pills.removeFromLeft(pw));
        pills.removeFromLeft(6);
        makePill.setBounds(pills.removeFromLeft(pw));
        pills.removeFromLeft(6);
        clearTakesPill.setBounds(pills.removeFromLeft(pw));
        pills.removeFromLeft(6);
        openFolderPill.setBounds(pills);
        area.removeFromTop(4);

        auto nameCell = area.removeFromLeft(130);
        captureNameLabel.setBounds(nameCell.removeFromTop(14));
        captureName.setBounds(nameCell.removeFromTop(26));
        area.removeFromLeft(8);

        auto knobs = area.removeFromLeft(210);
        const int kw = knobs.getWidth() / 3;
        placeKnob(capLengthKnob, knobs.removeFromLeft(kw).reduced(1));
        placeKnob(capLayersKnob, knobs.removeFromLeft(kw).reduced(1));
        placeKnob(capRRKnob, knobs.reduced(1));
        area.removeFromLeft(8);
        captureStatus.setBounds(area);
    }

    // ---- Reproductor propio ----
    samplerRect = content;
    {
        auto area = samplerRect.reduced(10, 8);
        samplerLabel.setBounds(area.removeFromTop(16));
        area.removeFromTop(4);

        // Izquierda: modo, knobs y toggles. Derecha: 8 muestras en 2 columnas.
        auto left = area.removeFromLeft(330);
        area.removeFromLeft(12);

        auto modes = left.removeFromTop(28);
        const int mw = (modes.getWidth() - 12) / 3;
        samplerOffPill.setBounds(modes.removeFromLeft(mw));
        modes.removeFromLeft(6);
        samplerReinforcePill.setBounds(modes.removeFromLeft(mw));
        modes.removeFromLeft(6);
        samplerReplacePill.setBounds(modes);
        left.removeFromTop(6);

        auto knobs = left.removeFromTop(84);
        const int kw = knobs.getWidth() / 3;
        placeKnob(sampleLevelKnob, knobs.removeFromLeft(kw).reduced(2));
        placeKnob(origLevelKnob, knobs.removeFromLeft(kw).reduced(2));
        placeKnob(velSensKnob, knobs.reduced(2));
        left.removeFromTop(4);

        auto toggles = left.removeFromTop(28);
        const int tw = (toggles.getWidth() - 6) / 2;
        alignPill.setBounds(toggles.removeFromLeft(tw));
        toggles.removeFromLeft(6);
        chokePill.setBounds(toggles);
        left.removeFromTop(4);
        hintLabel.setBounds(left);

        const int colW = (area.getWidth() - 12) / 2;
        const int rowH = std::min(46, std::max(30, (area.getHeight() - 18) / 4));
        for (int i = 0; i < DecimulTriggerProcessor::numSlots; ++i)
        {
            const int col = i / 4, row = i % 4;
            auto cell = juce::Rectangle<int>(area.getX() + col * (colW + 12), area.getY() + row * (rowH + 6), colW, rowH);
            hiLabels[(size_t) i].setBounds(cell.removeFromRight(46).reduced(0, 6));
            cell.removeFromRight(4);
            loLabels[(size_t) i].setBounds(cell.removeFromRight(46).reduced(0, 6));
            cell.removeFromRight(6);
            slotButtons[(size_t) i]->setBounds(cell);
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

void DecimulTriggerEditor::refreshCaptureStatus()
{
    std::vector<int> velocities;
    processorRef.getTakeVelocities(velocities);

    juce::String text;
    if (processorRef.isCapturing())
        text = "GRABANDO...\n";
    else
        text = juce::String();

    if (velocities.empty())
    {
        text += "Sin golpes grabados. Apreta GRABAR GOLPES y toca (o pasa tu pista).";
    }
    else
    {
        const auto [lo, hi] = std::minmax_element(velocities.begin(), velocities.end());
        text += juce::String((int) velocities.size()) + " golpes grabados (velocity " + juce::String(*lo) + " a "
              + juce::String(*hi) + ")";
    }

    if (exportMessage.isNotEmpty())
        text += "\n" + exportMessage;

    captureStatus.setText(text, juce::dontSendNotification);
}

void DecimulTriggerEditor::timerCallback()
{
    auto& apvts = processorRef.apvts;
    auto get = [&](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };

    // Monitor de golpes
    DecimulTriggerProcessor::ScopeFrame frames[128];
    for (;;)
    {
        const int n = processorRef.readScopeFrames(frames, 128);
        for (int i = 0; i < n; ++i)
            scope.pushFrame(frames[i].level, frames[i].velocity);
        if (n < 128)
            break;
    }
    scope.setThresholdDb(get(ParamIDs::threshold));
    scope.setMaxLevelDb(get(ParamIDs::maxLevel));
    scope.repaint();

    // Último golpe
    const int hits = processorRef.hitCount.load();
    if (hits != lastHitCount)
    {
        lastHitCount = hits;
        hitFlash = 1.0f;
        const int s = processorRef.lastSlotPlayed.load();
        if (s >= 0 && s < DecimulTriggerProcessor::numSlots)
            slotFlash[(size_t) s] = 1.0f;
    }
    hitFlash = std::max(0.0f, hitFlash - 0.12f);

    const bool fingerprintOn = get(ParamIDs::fingerprint) > 0.5f;
    const int note = (int) std::lround(get(ParamIDs::note));

    HitReadout::State rs;
    rs.velocity = processorRef.lastVelocity.load();
    rs.noteName = juce::MidiMessage::getMidiNoteName(note, true, true, 4);
    rs.hits = hits;
    rs.rejected = processorRef.rejectedCount.load();
    rs.similarity = processorRef.lastSimilarity.load();
    rs.fingerprintOn = fingerprintOn;
    rs.flash = hitFlash;
    readout.setState(rs);

    // Huella
    const bool learning = processorRef.isLearning();
    FingerprintBank::Vector tpl {}, last {};
    processorRef.getFingerprint(tpl);
    const bool lastOk = processorRef.getLastHitVector(last);
    fingerprintView.setData(tpl, processorRef.hasFingerprint(), last, lastOk, learning);

    learnPill.setToggleState(learning, juce::dontSendNotification);
    learnPill.setButtonText(learning ? "CERRAR (" + juce::String(processorRef.getLearnedHits()) + ")"
                                     : juce::String("APRENDER"));

    juce::String printText;
    if (learning)
        printText = "Aprendiendo: toca solo este instrumento (cierra sola a los 24).";
    else if (processorRef.hasFingerprint())
        printText = "Huella de " + juce::String(processorRef.getFingerprintHits()) + " golpes - "
                  + (fingerprintOn ? "rechazando bleed" : "apagada");
    else
        printText = "Sin huella.";
    printStatus.setText(printText, juce::dontSendNotification);

    const bool printUsable = processorRef.hasFingerprint();
    matchKnob.slider.setAlpha(printUsable ? 1.0f : 0.45f);
    matchKnob.label.setAlpha(printUsable ? 1.0f : 0.45f);
    clearPrintPill.setAlpha(printUsable ? 1.0f : 0.5f);

    // Captura
    recPill.setToggleState(processorRef.isCapturing(), juce::dontSendNotification);
    recPill.setButtonText(processorRef.isCapturing() ? "PARAR (" + juce::String(processorRef.getTakeCount()) + ")"
                                                    : juce::String("GRABAR GOLPES"));
    openFolderPill.setAlpha(lastExportFolder.isDirectory() ? 1.0f : 0.45f);
    refreshCaptureStatus();

    // Muestras
    for (int i = 0; i < DecimulTriggerProcessor::numSlots; ++i)
    {
        slotFlash[(size_t) i] = std::max(0.0f, slotFlash[(size_t) i] - 0.1f);
        const auto info = processorRef.getSlotInfo(i);
        slotButtons[(size_t) i]->setState(info.name, info.loaded, info.loading,
                                          info.error.isNotEmpty() && !info.loading, slotFlash[(size_t) i]);

        int lo = 1, hi = 127;
        processorRef.getVelocityRange(i, lo, hi);
        if (!loLabels[(size_t) i].isBeingEdited())
            loLabels[(size_t) i].setText(juce::String(lo), juce::dontSendNotification);
        if (!hiLabels[(size_t) i].isBeingEdited())
            hiLabels[(size_t) i].setText(juce::String(hi), juce::dontSendNotification);
    }

    const int mode = (int) std::lround(get(ParamIDs::sampler));
    samplerOffPill.setToggleState(mode == 0, juce::dontSendNotification);
    samplerReinforcePill.setToggleState(mode == 1, juce::dontSendNotification);
    samplerReplacePill.setToggleState(mode == 2, juce::dontSendNotification);

    // Lo que no aplica al modo actual se apaga.
    const bool samplerOn = mode != 0;
    const bool reinforce = mode == 1;
    for (auto* k : { &sampleLevelKnob, &velSensKnob })
    {
        k->slider.setAlpha(samplerOn ? 1.0f : 0.4f);
        k->label.setAlpha(samplerOn ? 1.0f : 0.4f);
    }
    origLevelKnob.slider.setAlpha(reinforce ? 1.0f : 0.4f);
    origLevelKnob.label.setAlpha(reinforce ? 1.0f : 0.4f);
    chokePill.setAlpha(samplerOn ? 1.0f : 0.5f);

    const bool fixedVel = get(ParamIDs::fixedVel) > 0.5f;
    for (auto* k : { &minVelKnob, &curveKnob, &maxLevelKnob })
    {
        k->slider.setAlpha(fixedVel ? 0.4f : 1.0f);
        k->label.setAlpha(fixedVel ? 0.4f : 1.0f);
    }

    juce::String hint;
    if (mode == 0)
        hint = "Solo MIDI: manda notas con velocity a lo que sigue en la cadena (un sampler). "
               "Para sonar sin otro plugin, elegi Reforzar o Reemplazar y carga muestras.";
    else if (mode == 1)
        hint = "Reforzar: el audio original suena junto con la muestra de la capa que corresponde "
               "a la fuerza del golpe. Con ALINEAR ORIGINAL coinciden en tiempo.";
    else
        hint = "Reemplazar: se silencia el original y suena solo la muestra. Varias muestras con el "
               "mismo rango de velocity se turnan (round-robin).";
    hintLabel.setText(hint, juce::dontSendNotification);

    // Salida
    const float outDb = processorRef.outputPeakDb.load();
    outputMeter.setLevelDb(outDb);
    outputMeter.repaint();
    vuArc.setLevelDb(outDb);
    vuArc.repaint();
    bypassButton.repaint();
}
