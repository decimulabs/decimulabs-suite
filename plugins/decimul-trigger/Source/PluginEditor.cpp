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
                                       : "click o arrastra";

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
    const auto labelCell = cell;       // el rótulo se ubica pegado al knob, abajo
    cell.removeFromTop(16);

    const int side = std::max(30, std::min(cell.getWidth(), cell.getHeight() - 18));
    const int width = std::max(side, 52);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, width, 16);
    const auto sliderBounds = cell.withSizeKeepingCentre(width, side + 18);
    k.slider.setBounds(sliderBounds);
    k.label.setBounds(labelCell.getX(), sliderBounds.getY() - 16, labelCell.getWidth(), 16);
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
    addKnob(onsetKnob, ParamIDs::onset, "ATAQUE");
    addKnob(minVelKnob, ParamIDs::minVel, "VEL MIN");
    addKnob(maxVelKnob, ParamIDs::maxVel, "VEL MAX");
    addKnob(curveKnob, ParamIDs::curve, "CURVA");
    addKnob(noteKnob, ParamIDs::note, "NOTA");
    addKnob(channelKnob, ParamIDs::channel, "CANAL");
    addKnob(noteLenKnob, ParamIDs::noteLen, "LARGO NOTA");
    addKnob(sampleLevelKnob, ParamIDs::sampleLevel, "MUESTRAS");
    addKnob(origLevelKnob, ParamIDs::origLevel, "ORIGINAL");
    addKnob(velSensKnob, ParamIDs::velSens, "VEL>VOL");
    addKnob(offsetKnob, ParamIDs::sampleOffset, "DESFASE");
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
    setupSection(captureLabel, "CAPTURA: MUESTRAS Y MIDI");

    // ---- Botones ----
    addAndMakeVisible(fixedVelPill);
    addAndMakeVisible(listenPill);
    addAndMakeVisible(alignPill);
    addAndMakeVisible(chokePill);
    addAndMakeVisible(rejectPill);


    addAndMakeVisible(calibratePill);
    addAndMakeVisible(calibrateStatus);
    addAndMakeVisible(polarityPill);
    addAndMakeVisible(midiInPill);
    addAndMakeVisible(exportMidiPill);

    calibratePill.setClickingTogglesState(false);
    calibratePill.onClick = [this]
    {
        if (processorRef.isCalibrating()) processorRef.cancelCalibration();
        else                              processorRef.startCalibration();
    };

    calibrateStatus.setFont(juce::Font(juce::FontOptions(10.5f)));
    calibrateStatus.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    calibrateStatus.setJustificationType(juce::Justification::centredLeft);
    calibrateStatus.setBorderSize(juce::BorderSize<int>(0, 4, 0, 0));


    midiInPill.setClickingTogglesState(false);
    midiInPill.onClick = [this]
    {
        const int mode = (int) std::lround(processorRef.apvts.getRawParameterValue(ParamIDs::midiIn)->load());
        setParam(ParamIDs::midiIn, (float) ((mode + 1) % 3));
    };

    exportMidiPill.setClickingTogglesState(false);
    exportMidiPill.onClick = [this]
    {
        const auto result = processorRef.exportRecordedMidi(captureName.getText());
        exportMessage = result.message;
        if (result.file != juce::File())
            lastExportTarget = result.file;
    };

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
    recPill.onClick = [this] { processorRef.setCapturing(!processorRef.isCapturing()); };
    addAndMakeVisible(recPill);

    makePill.setClickingTogglesState(false);
    makePill.onClick = [this]
    {
        const auto result = processorRef.exportTakes(captureName.getText(),
                                                     (int) std::lround(processorRef.apvts.getRawParameterValue(ParamIDs::capLayers)->load()),
                                                     (int) std::lround(processorRef.apvts.getRawParameterValue(ParamIDs::capRR)->load()));
        exportMessage = result.message;
        if (result.folder != juce::File())
            lastExportTarget = result.folder;
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
        if (lastExportTarget.exists())
            lastExportTarget.revealToUser();
    };
    addAndMakeVisible(openFolderPill);

    captureName.setText("Golpe", juce::dontSendNotification);
    captureName.setColour(juce::TextEditor::backgroundColourId, DecimuColours::background);
    captureName.setColour(juce::TextEditor::textColourId, DecimuColours::text);
    captureName.setColour(juce::TextEditor::outlineColourId, DecimuColours::textMuted.withAlpha(0.5f));
    addAndMakeVisible(captureName);

    captureNameLabel.setText("NOMBRE", juce::dontSendNotification);
    captureNameLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    captureNameLabel.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    addAndMakeVisible(captureNameLabel);

    captureStatus.setFont(juce::Font(juce::FontOptions(11.0f)));
    captureStatus.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    captureStatus.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(captureStatus);

    // ---- Modo común / avanzado ----
    modePill.setClickingTogglesState(false);
    modePill.onClick = [this] { setAdvanced(!advanced); };
    addAndMakeVisible(modePill);

    warnPill.setClickingTogglesState(false);
    warnPill.setToggleState(true, juce::dontSendNotification);
    warnPill.onClick = [this] { setAdvanced(true); };
    addChildComponent(warnPill);

    warnLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    warnLabel.setColour(juce::Label::textColourId, DecimuColours::accentPrimary);
    warnLabel.setJustificationType(juce::Justification::centredLeft);
    warnLabel.setMinimumHorizontalScale(0.8f);
    addChildComponent(warnLabel);

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

        for (auto* l : { &gainLabels[(size_t) i], &tuneLabels[(size_t) i] })
        {
            l->setJustificationType(juce::Justification::centred);
            l->setColour(juce::Label::textColourId, DecimuColours::text);
            l->setColour(juce::Label::backgroundColourId, DecimuColours::background);
            l->setColour(juce::Label::outlineColourId, DecimuColours::textMuted.withAlpha(0.5f));
            l->setEditable(false, true, false);
            l->onEditorShow = [l] { if (auto* ed = l->getCurrentTextEditor()) ed->setInputRestrictions(6, "0123456789.-+"); };
            addAndMakeVisible(*l);
        }
        gainLabels[(size_t) i].onTextChange = [this, i] { commitSlotGain(i); };
        tuneLabels[(size_t) i].onTextChange = [this, i] { commitSlotTune(i); };
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
    polarityAttach = std::make_unique<ButtonAttachment>(apvts, ParamIDs::polarity, polarityPill);

    // El monitor arranca limpio: descarta lo que quedó en la cola mientras no había editor.
    {
        DecimulTriggerProcessor::ScopeFrame discard[256];
        while (processorRef.readScopeFrames(discard, 256) > 0) {}
    }
    lastHitCount = processorRef.hitCount.load();
    lastVoiceCount = processorRef.voiceCount.load();
    lastLowCutSeen = apvts.getRawParameterValue(ParamIDs::lowCut)->load();
    lastHighCutSeen = apvts.getRawParameterValue(ParamIDs::highCut)->load();

    // ---- Tooltips: inglés por defecto; el botón EN/ES de abajo los pasa a español ----
    addAndMakeVisible(tips.getSwitch());

    tips.add(scope, "Monitor of the last 4 seconds: the level of the detection signal and every hit with its velocity (a grey cross is a hit rejected as bleed). Drag the dashed Threshold and Max Level lines to set them by eye.",
             "Monitor de los últimos 4 segundos: el nivel de la señal de detección y cada golpe con su velocity (una cruz gris es un golpe rechazado como bleed). Arrastrá las líneas punteadas de Threshold y Max Level para ajustarlas a ojo.");
    tips.add(readout, "Last hit: its velocity and note, and the counters. Similarity and rejected count appear when Reject Bleed is on.",
             "Último golpe: su velocity y nota, y los contadores. El parecido y los rechazados aparecen cuando Rechazar bleed está activo.");

    // Detección
    tips.add(thresholdKnob.slider, "Threshold, -60 to 0 dB: the minimum peak that counts as a hit. It is also the level that gives Vel Min.",
             "Threshold, -60 a 0 dB: el pico mínimo para que cuente como golpe. Es también el nivel que da la Vel Min.");
    tips.add(maxLevelKnob.slider, "Max Level, -40 to 0 dB: the peak that gives the maximum velocity. Harder hits are capped there.",
             "Max Level, -40 a 0 dB: el pico que da la velocity máxima. Los golpes más fuertes quedan topeados ahí.");
    tips.add(scanKnob.slider, "Scan, 0.5 to 10 ms: how long after the attack the peak is measured to get the velocity. Longer is more accurate with slow attacks, but the MIDI comes out later.",
             "Scan, 0.5 a 10 ms: cuánto tiempo después del ataque se mide el pico para sacar la velocity. Más largo es más preciso con ataques lentos, pero el MIDI sale más tarde.");
    tips.add(retriggerKnob.slider, "Retrigger, 5 to 250 ms: dead time after a hit, and how fast the previous hit's level fades as the bar a new hit has to beat. Stops ringing and bleed from retriggering.",
             "Retrigger, 5 a 250 ms: tiempo muerto después de un golpe y qué tan rápido decae el nivel del golpe anterior, que es lo que tiene que superar uno nuevo. Evita que el ring o el bleed retriggueen.");
    tips.add(lowCutKnob.slider, "Low cut of the detection signal, 20 Hz to 5 kHz, 24 dB/oct. It only changes what the DETECTOR hears (not your audio): raise it to ignore a kick when detecting a snare or hi-hat. A curve shows the filter while you move it.",
             "Corte de graves de la señal de detección, 20 Hz a 5 kHz, 24 dB/oct. Solo cambia lo que oye el DETECTOR (no tu audio): subilo para ignorar un bombo al detectar una caja o un hi-hat. Una curva muestra el filtro mientras lo movés.");
    tips.add(highCutKnob.slider, "High cut of the detection signal, 100 Hz to 20 kHz, 24 dB/oct. It only changes what the DETECTOR hears (not your audio): lower it to ignore cymbals when detecting a kick or toms.",
             "Corte de agudos de la señal de detección, 100 Hz a 20 kHz, 24 dB/oct. Solo cambia lo que oye el DETECTOR (no tu audio): bajalo para ignorar los platos al detectar un bombo o toms.");
    tips.add(onsetKnob.slider, "Attack, 1 to 12 dB: how much the level has to jump to count as a new hit. Lower detects softer or closely packed hits (with more false ones); higher accepts only sharp attacks.",
             "Ataque, 1 a 12 dB: cuánto tiene que subir de golpe el nivel para contar como golpe nuevo. Más bajo detecta golpes más suaves o pegados (con más falsos); más alto acepta solo ataques secos.");
    tips.add(listenPill, "Listen to the detection signal instead of your audio: mono, after the Low / High cut. Use it to set those two by ear; turn it off to hear your audio again.",
             "Escuchás la señal de detección en vez de tu audio: en mono, después del Low / High Cut. Sirve para ajustar esos dos de oído; apagalo para volver a oír tu audio.");
    tips.add(calibratePill, "Listens for about 6 seconds and sets Threshold and Max Level for you: Max Level is the loudest hit it heard, and the threshold sits above the noise floor. Play from soft to loud, with pauses between hits.",
             "Escucha unos 6 segundos y ajusta Threshold y Max Level solos: el Max Level es el golpe más fuerte que oyó y el umbral queda sobre el ruido de fondo. Tocá de suave a fuerte, con pausas entre golpes.");

    // Velocity y MIDI
    tips.add(minVelKnob.slider, "Vel Min, 1 to 127: the velocity of a hit right at the Threshold.",
             "Vel Min, 1 a 127: la velocity de un golpe justo en el Threshold.");
    tips.add(maxVelKnob.slider, "Vel Max, 1 to 127: the velocity of a hit at Max Level. With Fixed Velocity every hit uses this value.",
             "Vel Max, 1 a 127: la velocity de un golpe en el Max Level. Con Velocity fija todos los golpes usan este valor.");
    tips.add(curveKnob.slider, "Curve, -1 to +1: negative makes soft hits sound louder (compresses), positive gives more dynamics (soft hits stay soft).",
             "Curva, -1 a +1: negativa hace que los golpes suaves suenen más fuertes (comprime), positiva da más dinámica (los suaves quedan suaves).");
    tips.add(fixedVelPill, "Fixed velocity: every hit goes out with the maximum velocity, without measuring its strength.",
             "Velocity fija: todos los golpes salen con la velocity máxima, sin medir la intensidad.");
    tips.add(noteKnob.slider, "MIDI note the hits go out on (38 = snare by default). It is also the note that MIDI IN: NOTE listens to.",
             "Nota MIDI con la que salen los golpes (38 = caja por defecto). Es también la nota que escucha MIDI IN: NOTA.");
    tips.add(channelKnob.slider, "MIDI channel of the output notes (10 = drums by default).",
             "Canal MIDI de las notas de salida (10 = batería por defecto).");
    tips.add(noteLenKnob.slider, "Length of each output note, 5 to 500 ms.", "Largo de cada nota de salida, 5 a 500 ms.");

    // Huella
    tips.add(learnPill, "Learn: play only this instrument (8 to 12 hits) and press again to close. It closes by itself at 24 hits.",
             "Aprender: tocá solo este instrumento (unos 8 a 12 golpes) y apretá de nuevo para cerrar. Cierra sola a los 24.");
    tips.add(clearPrintPill, "Deletes the learned fingerprint.", "Borra la huella aprendida.");
    tips.add(rejectPill, "Reject bleed: only hits that look like the learned fingerprint pass; the others show as grey crosses on the monitor.",
             "Rechazar bleed: solo pasan los golpes que se parecen a la huella aprendida; los otros se ven como cruces grises en el monitor.");
    tips.add(matchKnob.slider, "Minimum similarity to the fingerprint for a hit to pass, 0.30 to 0.99. Higher is stricter.",
             "Parecido mínimo con la huella para que un golpe pase, 0.30 a 0.99. Más alto es más estricto.");
    tips.add(fpWindowKnob.slider, "Fingerprint window, 4 to 30 ms: how much of the hit after the attack is analysed. If you change it, learn the fingerprint again. With Align Original the latency equals this window.",
             "Ventana de la huella, 4 a 30 ms: cuánto del golpe, después del ataque, se analiza. Si la cambiás, volvé a aprender la huella. Con Alinear original la latencia es igual a esta ventana.");
    tips.add(fingerprintView, "Fingerprint: the bars are the learned spectral shape (low to high frequency); the ticks are the last hit.",
             "Huella: las barras son la forma espectral aprendida (de graves a agudos); las marcas son el último golpe.");

    // Captura
    tips.add(recPill, "Record hits: stores the audio of every detected hit (to build samples) and its position (to export MIDI).",
             "Grabar golpes: guarda el audio de cada golpe detectado (para armar muestras) y su posición (para exportar MIDI).");
    tips.add(makePill, "Create samples: splits the recorded hits into velocity layers and round-robin variations, saves them as WAV files and loads them into the player.",
             "Crear muestras: reparte los golpes grabados en capas por velocity y alternancias, los guarda como WAV y los carga en el reproductor.");
    tips.add(exportMidiPill, "Export MIDI: saves the recorded hits as a .mid file, at the position they had in the project (960 ppq). The message says which bar to drop it on.",
             "Exportar MIDI: guarda los golpes grabados como archivo .mid, en la posición que tenían en el proyecto (960 ppq). El mensaje dice en qué compás soltarlo.");
    tips.add(clearTakesPill, "Deletes the recorded audio takes and MIDI hits.", "Borra las tomas de audio y los golpes MIDI grabados.");
    tips.add(openFolderPill, "Opens the folder (or the file) of the last export.", "Abre la carpeta (o el archivo) de la última exportación.");
    tips.add(captureName, "Name prefix of the files that get created (for example Snare).",
             "Prefijo de los archivos que se crean (por ejemplo Caja).");
    tips.add(capLengthKnob.slider, "Length of each recorded hit, 100 to 1000 ms. It is cut short where the next hit starts or the sound dies away.",
             "Largo de cada golpe grabado, 100 a 1000 ms. Se corta antes donde empieza el golpe siguiente o el sonido se apaga.");
    tips.add(capLayersKnob.slider, "Velocity layers to build when creating samples, 1 to 4 (layers x round-robin: up to 8 samples).",
             "Capas de velocity al crear las muestras, 1 a 4 (capas x alternancias: hasta 8 muestras).");
    tips.add(capRRKnob.slider, "Round-robin variations per layer, 1 to 4 (layers x round-robin: up to 8 samples).",
             "Alternancias (round-robin) por capa, 1 a 4 (capas x alternancias: hasta 8 muestras).");

    // Reproductor propio
    tips.add(samplerOffPill, "Only MIDI: the audio passes untouched and the plugin sends MIDI notes with velocity to whatever follows in the chain.",
             "Solo MIDI: el audio pasa como está y el plugin manda notas con velocity a lo que sigue en la cadena.");
    tips.add(samplerReinforcePill, "Reinforce: the original plays together with the sample of the layer that matches the hit strength.",
             "Reforzar: el original suena junto con la muestra de la capa que corresponde a la fuerza del golpe.");
    tips.add(samplerReplacePill, "Replace: the original is silenced and only the sample plays.",
             "Reemplazar: se silencia el original y suena solo la muestra.");
    tips.add(sampleLevelKnob.slider, "Level of the built-in samples, -24 to +12 dB.", "Nivel de las muestras propias, -24 a +12 dB.");
    tips.add(origLevelKnob.slider, "Level of the original audio in Reinforce mode, -60 to 0 dB.",
             "Nivel del audio original en modo Reforzar, -60 a 0 dB.");
    tips.add(velSensKnob.slider, "Velocity to volume, 0 to 100 %: how much the sample volume follows the hit velocity (0 % = always full volume).",
             "Velocity a volumen, 0 a 100 %: cuánto sigue el volumen de la muestra a la velocity del golpe (0 % = siempre a volumen completo).");
    tips.add(offsetKnob.slider, "Offset, -10 to +10 ms: moves the samples against the original. Positive = later. Negative = skips the start of the sample (recorded ones carry 3 ms of pre-roll: try -3).",
             "Desfase, -10 a +10 ms: mueve las muestras contra el audio original. Positivo = más tarde. Negativo = se salta el comienzo de la muestra (las grabadas traen 3 ms de pre-roll: probá -3).");
    tips.add(alignPill, "Align original: delays the original audio by the measuring time so it lines up with the MIDI and the sample. That latency is reported to the host.",
             "Alinear original: retrasa el audio original lo que dura la medición, para que coincida con el MIDI y la muestra. Esa latencia se le informa al host.");
    tips.add(chokePill, "Choke: every new hit cuts the samples that were still ringing.",
             "Cortar anterior: cada golpe nuevo corta las muestras que seguían sonando.");
    tips.add(polarityPill, "Invert phase of the samples. If reinforcing makes the original and the sample cancel out (it sounds hollow), try this.",
             "Invierte la fase de las muestras. Si al reforzar el original y la muestra se anulan entre sí (suena hueco), probá esto.");
    tips.add(midiInPill, "MIDI IN: plays the built-in samples with the MIDI notes that reach the plugin. NO ignores MIDI, NOTE listens only to the configured note, ALL takes any note. Needs Reinforce or Replace; the notes it plays are consumed, the rest pass through.",
             "MIDI IN: toca las muestras propias con las notas MIDI que llegan al plugin. NO ignora el MIDI, NOTA escucha solo la nota configurada, TODAS toma cualquiera. Necesita Reforzar o Reemplazar; las notas que toca se consumen, el resto pasa de largo.");

    for (int i = 0; i < DecimulTriggerProcessor::numSlots; ++i)
    {
        tips.add(*slotButtons[(size_t) i], "Sample slot " + juce::String(i + 1) + ": click to choose a file, or drop audio files on the window. Right-click for more options.",
                 "Slot de muestra " + juce::String(i + 1) + ": click para elegir un archivo, o soltá archivos de audio en la ventana. Click derecho para más opciones.");
        tips.add(loLabels[(size_t) i], "Lowest velocity this sample responds to (double-click to type it). Samples with the same range take turns (round-robin).",
                 "Velocity mínima a la que responde esta muestra (doble click para escribirla). Las muestras con el mismo rango se turnan (round-robin).");
        tips.add(hiLabels[(size_t) i], "Highest velocity this sample responds to (double-click to type it). Samples with the same range take turns (round-robin).",
                 "Velocity máxima a la que responde esta muestra (doble click para escribirla). Las muestras con el mismo rango se turnan (round-robin).");
        tips.add(gainLabels[(size_t) i], "Gain of this sample in dB, -24 to +12 (double-click to type it).",
                 "Ganancia de esta muestra en dB, de -24 a +12 (doble click para escribirla).");
        tips.add(tuneLabels[(size_t) i], "Tuning of this sample in semitones, -12 to +12 (double-click to type it).",
                 "Afinación de esta muestra en semitonos, de -12 a +12 (doble click para escribirla).");
    }

    tips.add(modePill, "Switch between the common mode (only the essentials) and the advanced mode (every control: filters, velocity, fingerprint, capture and all 8 samples). Hidden settings keep working.",
             "Cambia entre el modo común (solo lo esencial) y el avanzado (todos los controles: filtros, velocity, huella, captura y las 8 muestras). Lo que queda oculto sigue funcionando.");
    tips.add(warnPill, "Opens the advanced mode, where those settings can be seen and changed.",
             "Abre el modo avanzado, donde esos ajustes se ven y se pueden cambiar.");
    tips.add(outputSlider, "Output trim, -24 to +12 dB.", "Ganancia de salida, -24 a +12 dB.");
    tips.add(bypassButton, "Bypass: the audio passes through untouched (the dot goes grey).",
             "Bypass: el audio pasa sin tocarse (el punto se pone gris).");

    // Lo que solo se ve en el modo avanzado (las muestras 1 y 2 y todo lo demás del modo común quedan siempre).
    for (auto* k : { &scanKnob, &lowCutKnob, &highCutKnob, &onsetKnob, &minVelKnob, &maxVelKnob, &curveKnob, &channelKnob,
                     &noteLenKnob, &velSensKnob, &offsetKnob, &matchKnob, &fpWindowKnob, &capLengthKnob, &capLayersKnob,
                     &capRRKnob })
    {
        advancedOnly.push_back(&k->slider);
        advancedOnly.push_back(&k->label);
    }
    for (auto* c : std::initializer_list<juce::Component*> {
             &velocityLabel, &printLabel, &captureLabel, &fixedVelPill, &listenPill, &alignPill, &chokePill, &rejectPill,
             &polarityPill, &midiInPill, &learnPill, &clearPrintPill, &fingerprintView, &printStatus, &makePill,
             &clearTakesPill, &openFolderPill, &captureName, &captureNameLabel })
        advancedOnly.push_back(c);
    for (int i = 0; i < DecimulTriggerProcessor::numSlots; ++i)
    {
        if (i >= 2)
            advancedOnly.push_back(slotButtons[(size_t) i].get());
        for (auto* l : { &loLabels[(size_t) i], &hiLabels[(size_t) i], &gainLabels[(size_t) i], &tuneLabels[(size_t) i] })
            advancedOnly.push_back(l);
    }

    advanced = processorRef.uiAdvanced.load();
    setResizable(true, true);
    applyMode();
    startTimerHz(30);
}

// ------------------------------ Modo común / avanzado ------------------------------

void DecimulTriggerEditor::setAdvanced(bool shouldBeAdvanced)
{
    if (advanced == shouldBeAdvanced)
        return;
    advanced = shouldBeAdvanced;
    processorRef.uiAdvanced.store(advanced);
    applyMode();
}

void DecimulTriggerEditor::applyMode()
{
    for (auto* c : advancedOnly)
        c->setVisible(advanced);

    modePill.setButtonText(advanced ? "MODO COMUN" : "AVANZADO");
    modePill.setToggleState(advanced, juce::dontSendNotification);
    if (advanced)
    {
        warnLabel.setVisible(false);
        warnPill.setVisible(false);
        setResizeLimits(1100, 840, 1800, 1200);
        setSize(1180, 880);
    }
    else
    {
        setResizeLimits(900, 660, 1500, 1000);
        setSize(980, 720);
    }
    resized();
    repaint();
}

// Ajustes del modo avanzado que están fuera de su valor normal (siguen afectando el sonido).
void DecimulTriggerEditor::updateHiddenWarning()
{
    juce::StringArray names;
    auto& apvts = processorRef.apvts;
    struct Item { const juce::String* id; const char* name; };
    const Item items[] = {
        { &ParamIDs::listen, "Oir deteccion" },   { &ParamIDs::lowCut, "Low Cut" },       { &ParamIDs::highCut, "High Cut" },
        { &ParamIDs::scan, "Scan" },              { &ParamIDs::onset, "Ataque" },         { &ParamIDs::minVel, "Vel Min" },
        { &ParamIDs::maxVel, "Vel Max" },         { &ParamIDs::curve, "Curva" },          { &ParamIDs::fixedVel, "Velocity fija" },
        { &ParamIDs::channel, "Canal" },          { &ParamIDs::noteLen, "Largo nota" },   { &ParamIDs::fingerprint, "Rechazar bleed" },
        { &ParamIDs::velSens, "Vel>Vol" },        { &ParamIDs::sampleOffset, "Desfase" }, { &ParamIDs::polarity, "Invertir fase" },
        { &ParamIDs::midiIn, "MIDI IN" },         { &ParamIDs::align, "Alinear original" }, { &ParamIDs::choke, "Cortar anterior" } };
    for (const auto& item : items)
        if (auto* p = apvts.getParameter(*item.id))
            if (std::abs(p->getValue() - p->getDefaultValue()) > 0.001f)
                names.add(item.name);

    int extraSamples = 0;
    bool slotTweaks = false;
    for (int i = 0; i < DecimulTriggerProcessor::numSlots; ++i)
    {
        const auto info = processorRef.getSlotInfo(i);
        if (i >= 2 && (info.loaded || info.loading))
            ++extraSamples;
        if (i < 2 && (std::abs(processorRef.getSlotGain(i)) > 0.04f || std::abs(processorRef.getSlotTune(i)) > 0.04f))
            slotTweaks = true;
    }
    if (extraSamples > 0)
        names.add(juce::String(extraSamples) + (extraSamples == 1 ? " muestra mas (3 a 8)" : " muestras mas (3 a 8)"));
    if (slotTweaks)
        names.add("Gain / afinacion de muestras");

    const int count = names.size();
    const bool show = !advanced && count > 0;
    warnLabel.setText("AVANZADO ACTIVO (" + juce::String(count) + "): " + names.joinIntoString(", "), juce::dontSendNotification);
    warnLabel.setVisible(show);
    warnPill.setVisible(show);
    if (count != hiddenCount)
    {
        const bool layoutChanges = (count > 0) != (hiddenCount > 0);
        hiddenCount = count;
        if (layoutChanges)
        {
            resized();
            repaint();
        }
    }
}

// En modo común las muestras 1 y 2 son dos capas: suave (1-80) y fuerte (81-127). Se acomodan solas
// solo cuando se carga o se quita una muestra desde este modo (nunca pisan lo hecho en el avanzado).
void DecimulTriggerEditor::normalizeCommonRanges()
{
    for (int i = 2; i < DecimulTriggerProcessor::numSlots; ++i)
    {
        const auto info = processorRef.getSlotInfo(i);
        if (info.loaded || info.loading)
            return;
    }
    const auto a = processorRef.getSlotInfo(0), b = processorRef.getSlotInfo(1);
    if (a.loading || b.loading)
        return;

    int lo0 = 1, hi0 = 127, lo1 = 1, hi1 = 127;
    processorRef.getVelocityRange(0, lo0, hi0);
    processorRef.getVelocityRange(1, lo1, hi1);

    if (a.loaded && b.loaded)
    {
        if (lo0 == 1 && hi0 == 127 && lo1 == 1 && hi1 == 127)
        {
            processorRef.setVelocityRange(0, 1, 80);
            processorRef.setVelocityRange(1, 81, 127);
        }
    }
    else if (a.loaded && lo0 == 1 && hi0 == 80)
        processorRef.setVelocityRange(0, 1, 127);
    else if (b.loaded && lo1 == 81 && hi1 == 127)
        processorRef.setVelocityRange(1, 1, 127);
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
            pendingNormalize = !advanced;
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
    menu.addItem(4, "Ganancia y afinacion a cero");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, slot](int result)
    {
        if (result == 1)
            openFileChooser(slot);
        else if (result == 2)
        {
            processorRef.clearSample(slot);
            pendingNormalize = !advanced;
        }
        else if (result == 3)
            processorRef.setVelocityRange(slot, 1, 127);
        else if (result == 4)
        {
            processorRef.setSlotGain(slot, 0.0f);
            processorRef.setSlotTune(slot, 0.0f);
        }
    });
}

void DecimulTriggerEditor::commitVelocityRange(int slot)
{
    const int lo = loLabels[(size_t) slot].getText().getIntValue();
    const int hi = hiLabels[(size_t) slot].getText().getIntValue();
    processorRef.setVelocityRange(slot, lo > 0 ? lo : 1, hi > 0 ? hi : 127);
}

void DecimulTriggerEditor::commitSlotGain(int slot)
{
    processorRef.setSlotGain(slot, gainLabels[(size_t) slot].getText().getFloatValue());
    gainLabels[(size_t) slot].setText(juce::String(processorRef.getSlotGain(slot), 1), juce::dontSendNotification);
}

void DecimulTriggerEditor::commitSlotTune(int slot)
{
    processorRef.setSlotTune(slot, tuneLabels[(size_t) slot].getText().getFloatValue());
    tuneLabels[(size_t) slot].setText(juce::String(processorRef.getSlotTune(slot), 1), juce::dontSendNotification);
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
    const int usable = usableSlots();
    int start = -1;
    for (int i = 0; i < usable; ++i)
        if (slotButtons[(size_t) i]->getBounds().contains(x, y))
            start = i;

    if (start < 0)
    {
        start = 0;
        for (int i = 0; i < usable; ++i)
            if (!processorRef.getSlotInfo(i).loaded && !processorRef.getSlotInfo(i).loading)
            {
                start = i;
                break;
            }
    }

    int fileIndex = 0;
    for (int n = 0; n < usable && fileIndex < audio.size(); ++n)
        processorRef.loadSample((start + n) % usable, audio[fileIndex++]);
    pendingNormalize = !advanced;
}

// ------------------------------- Dibujo -------------------------------

void DecimulTriggerEditor::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    DecimuChrome::paintChassisBackground(g, full, 14.0f);
    DecimuChrome::paintRackHandles(g, full);
    DecimuChrome::paintCornerScrews(g, full, 12.0f);

    auto compartment = [&g](juce::Rectangle<int> r, bool recessed)
    {
        if (!r.isEmpty())
            DecimuChrome::paintCompartment(g, r.toFloat(), recessed, 8.0f);
    };
    compartment(topbarRect, false);
    compartment(scopeRect, false);
    compartment(detectRect, true);
    compartment(velocityRect, false);
    compartment(midiRect, true);
    compartment(printRect, false);
    compartment(captureRect, true);
    compartment(samplerRect, false);
    compartment(outputRect, true);

    if (!warnRect.isEmpty())
    {
        g.setColour(DecimuColours::accentPrimary.withAlpha(0.12f));
        g.fillRoundedRectangle(warnRect.toFloat(), 6.0f);
        g.setColour(DecimuColours::accentPrimary.withAlpha(0.7f));
        g.drawRoundedRectangle(warnRect.toFloat().reduced(0.5f), 6.0f, 1.2f);
    }

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

    g.setColour(DecimuColours::textMuted);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    for (const auto& caption : slotCaptions)
        g.drawText(caption.text, caption.area, juce::Justification::centred);

    if (!advanced)
        for (int i = 0; i < 2; ++i)
        {
            int lo = 1, hi = 127;
            processorRef.getVelocityRange(i, lo, hi);
            g.drawText(juce::String(i == 0 ? "MUESTRA 1: GOLPES SUAVES" : "MUESTRA 2: GOLPES FUERTES")
                           + "   (velocity " + juce::String(lo) + " a " + juce::String(hi) + ")",
                       commonCaptionAreas[(size_t) i], juce::Justification::centredLeft);
        }
}

void DecimulTriggerEditor::resized()
{
    tips.layout(getLocalBounds());
    if (advanced)
        layoutAdvanced();
    else
        layoutCommon();
}

// Modo común: lo esencial. Detección (Threshold, Max Level, Retrigger, Calibrar), la nota, grabar y
// exportar el MIDI, el modo del reproductor con sus dos niveles y dos muestras (suave y fuerte).
void DecimulTriggerEditor::layoutCommon()
{
    auto content = getLocalBounds().reduced(22, 18);
    content.removeFromBottom(8);

    topbarRect = content.removeFromTop(54);
    content.removeFromTop(8);

    auto header = topbarRect.reduced(10, 8);
    bypassButton.setBounds(header.removeFromRight(110));
    vuArc.setBounds(header.removeFromRight(96).reduced(2));
    header.removeFromRight(10);
    modePill.setBounds(header.removeFromRight(130).reduced(0, 4));
    header.removeFromRight(10);
    headerLogoArea = header;

    constexpr int gap = 8;
    outputRect = content.removeFromRight(112);
    content.removeFromRight(gap);

    velocityRect = printRect = captureRect = juce::Rectangle<int>();
    slotCaptions.clear();

    scopeRect = content.removeFromTop(200);
    {
        auto area = scopeRect.reduced(8);
        readout.setBounds(area.removeFromRight(210));
        area.removeFromRight(8);
        scope.setBounds(area);
    }
    content.removeFromTop(gap);

    warnRect = juce::Rectangle<int>();
    if (hiddenCount > 0)
    {
        warnRect = content.removeFromTop(30);
        content.removeFromTop(gap);
        auto a = warnRect.reduced(8, 3);
        warnPill.setBounds(a.removeFromRight(140));
        a.removeFromRight(8);
        warnLabel.setBounds(a);
    }

    auto knobsRow = content.removeFromTop(146);
    detectRect = knobsRow.removeFromLeft(knobsRow.getWidth() * 11 / 20);
    knobsRow.removeFromLeft(gap);
    midiRect = knobsRow;
    content.removeFromTop(gap);

    {
        auto area = detectRect.reduced(8, 6);
        detectLabel.setBounds(area.removeFromTop(16));
        auto bottom = area.removeFromBottom(26);
        calibratePill.setBounds(bottom.removeFromLeft(std::min(116, bottom.getWidth())).reduced(0, 1));
        bottom.removeFromLeft(8);
        calibrateStatus.setBounds(bottom);
        const int w = area.getWidth() / 3;
        for (auto* k : { &thresholdKnob, &maxLevelKnob, &retriggerKnob })
            placeKnob(*k, area.removeFromLeft(w).reduced(2));
    }
    {
        auto area = midiRect.reduced(8, 6);
        midiLabel.setBounds(area.removeFromTop(16));
        placeKnob(noteKnob, area.removeFromLeft(110).reduced(2));
        area.removeFromLeft(6);
        auto pills = area.removeFromTop(26);
        const int pw = (pills.getWidth() - 6) / 2;
        recPill.setBounds(pills.removeFromLeft(pw));
        pills.removeFromLeft(6);
        exportMidiPill.setBounds(pills);
        area.removeFromTop(6);
        captureStatus.setBounds(area);
    }

    samplerRect = content;
    {
        auto area = samplerRect.reduced(10, 8);
        samplerLabel.setBounds(area.removeFromTop(16));
        area.removeFromTop(4);

        auto left = area.removeFromLeft(330);
        area.removeFromLeft(14);

        auto modes = left.removeFromTop(28);
        const int mw = (modes.getWidth() - 12) / 3;
        samplerOffPill.setBounds(modes.removeFromLeft(mw));
        modes.removeFromLeft(6);
        samplerReinforcePill.setBounds(modes.removeFromLeft(mw));
        modes.removeFromLeft(6);
        samplerReplacePill.setBounds(modes);
        left.removeFromTop(6);

        auto knobs = left.removeFromTop(84);
        placeKnob(sampleLevelKnob, knobs.removeFromLeft(120).reduced(2));
        placeKnob(origLevelKnob, knobs.removeFromLeft(120).reduced(2));
        left.removeFromTop(4);
        hintLabel.setBounds(left);

        const int rowH = std::clamp((area.getHeight() - 2 * 18 - 6) / 2, 28, 56);
        for (int i = 0; i < 2; ++i)
        {
            commonCaptionAreas[(size_t) i] = area.removeFromTop(18);
            slotButtons[(size_t) i]->setBounds(area.removeFromTop(rowH));
            area.removeFromTop(6);
        }
    }

    {
        auto col = outputRect.reduced(10);
        outputLabel.setBounds(col.removeFromBottom(20));
        outputMeter.setBounds(col.removeFromLeft(16));
        outputSlider.setBounds(col);
    }
}

void DecimulTriggerEditor::layoutAdvanced()
{
    warnRect = juce::Rectangle<int>();
    auto content = getLocalBounds().reduced(22, 18);
    content.removeFromBottom(8);

    topbarRect = content.removeFromTop(54);
    content.removeFromTop(8);

    auto header = topbarRect.reduced(10, 8);
    bypassButton.setBounds(header.removeFromRight(110));
    vuArc.setBounds(header.removeFromRight(96).reduced(2));
    header.removeFromRight(10);
    modePill.setBounds(header.removeFromRight(130).reduced(0, 4));
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
    detectRect = knobsRow.removeFromLeft(knobsRow.getWidth() * 7 / 13);
    knobsRow.removeFromLeft(gap);
    velocityRect = knobsRow.removeFromLeft((knobsRow.getWidth() - gap) / 2);
    knobsRow.removeFromLeft(gap);
    midiRect = knobsRow;

    // Título arriba, knobs en el medio y, abajo, una franja para botones. Devuelve lo que
    // queda de la franja a la derecha del botón.
    auto layoutKnobs = [this](juce::Rectangle<int> rect, juce::Label& title,
                              std::initializer_list<Knob*> knobs, DecimuPillButton* pill,
                              int pillWidth = 170) -> juce::Rectangle<int>
    {
        auto area = rect.reduced(8, 6);
        title.setBounds(area.removeFromTop(16));
        auto bottom = area.removeFromBottom(26);
        if (pill != nullptr)
            pill->setBounds(bottom.removeFromLeft(std::min(pillWidth, bottom.getWidth())).reduced(0, 1));

        const int w = area.getWidth() / (int) knobs.size();
        for (auto* k : knobs)
            placeKnob(*k, area.removeFromLeft(w).reduced(2));
        return bottom;
    };
    {
        auto rest = layoutKnobs(detectRect, detectLabel,
                                { &thresholdKnob, &maxLevelKnob, &scanKnob, &retriggerKnob, &lowCutKnob, &highCutKnob,
                                  &onsetKnob },
                                &listenPill, 148);
        rest.removeFromLeft(6);
        calibratePill.setBounds(rest.removeFromLeft(std::min(116, rest.getWidth())).reduced(0, 1));
        rest.removeFromLeft(8);
        calibrateStatus.setBounds(rest);
    }
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
        const int pw = (pills.getWidth() - 4 * 6) / 5;
        for (auto* pill : { &recPill, &makePill, &exportMidiPill, &clearTakesPill })
        {
            pill->setBounds(pills.removeFromLeft(pw));
            pills.removeFromLeft(6);
        }
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
        auto left = area.removeFromLeft(310);
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
        const int kw = knobs.getWidth() / 4;
        placeKnob(sampleLevelKnob, knobs.removeFromLeft(kw).reduced(2));
        placeKnob(origLevelKnob, knobs.removeFromLeft(kw).reduced(2));
        placeKnob(velSensKnob, knobs.removeFromLeft(kw).reduced(2));
        placeKnob(offsetKnob, knobs.reduced(2));
        left.removeFromTop(4);

        auto toggles = left.removeFromTop(28);
        const int tw = (toggles.getWidth() - 6) / 2;
        alignPill.setBounds(toggles.removeFromLeft(tw));
        toggles.removeFromLeft(6);
        chokePill.setBounds(toggles);
        left.removeFromTop(4);

        auto toggles2 = left.removeFromTop(28);
        polarityPill.setBounds(toggles2.removeFromLeft(tw));
        toggles2.removeFromLeft(6);
        midiInPill.setBounds(toggles2);
        left.removeFromTop(4);
        hintLabel.setBounds(left);

        // Muestras: botón, rango de velocity (lo-hi), ganancia y afinación. Arriba van los títulos.
        slotCaptions.clear();
        const int colW = (area.getWidth() - 12) / 2;
        const auto captionRow = area.removeFromTop(14);
        const int rowH = std::min(46, std::max(30, (area.getHeight() - 18) / 4));
        for (int i = 0; i < DecimulTriggerProcessor::numSlots; ++i)
        {
            const int col = i / 4, row = i % 4;
            auto cell = juce::Rectangle<int>(area.getX() + col * (colW + 12), area.getY() + row * (rowH + 6), colW, rowH);
            tuneLabels[(size_t) i].setBounds(cell.removeFromRight(46).reduced(0, 6));
            cell.removeFromRight(4);
            gainLabels[(size_t) i].setBounds(cell.removeFromRight(50).reduced(0, 6));
            cell.removeFromRight(10);
            hiLabels[(size_t) i].setBounds(cell.removeFromRight(42).reduced(0, 6));
            cell.removeFromRight(4);
            loLabels[(size_t) i].setBounds(cell.removeFromRight(42).reduced(0, 6));
            cell.removeFromRight(6);
            slotButtons[(size_t) i]->setBounds(cell);

            if (row == 0)
            {
                auto caption = [&](const juce::Label& field, const juce::String& text, int widen = 0)
                {
                    slotCaptions.push_back({ { field.getX() - widen, captionRow.getY(), field.getWidth() + 2 * widen, captionRow.getHeight() },
                                             text });
                };
                caption(loLabels[(size_t) i], "VEL LO");
                caption(hiLabels[(size_t) i], "VEL HI");
                caption(gainLabels[(size_t) i], "GAIN dB");
                caption(tuneLabels[(size_t) i], "TUNE st");
            }
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
    const int midiHits = processorRef.getRecordedMidiHits();

    juce::String text;
    if (processorRef.isCapturing())
        text = "GRABANDO...\n";

    if (velocities.empty() && midiHits == 0)
    {
        text += "Sin golpes grabados. Apreta GRABAR GOLPES y toca (o pasa tu pista).";
    }
    else
    {
        if (velocities.empty())
        {
            text += "Sin tomas de audio";
        }
        else
        {
            const auto [lo, hi] = std::minmax_element(velocities.begin(), velocities.end());
            text += juce::String((int) velocities.size()) + " tomas de audio (velocity " + juce::String(*lo) + " a "
                  + juce::String(*hi) + ")";
        }
        text += "\n" + juce::String(midiHits) + " golpes MIDI";
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

    // Filtro de detección: la curva aparece al mover Low / High Cut y mientras se escucha la detección.
    {
        const float lowCut = get(ParamIDs::lowCut), highCut = get(ParamIDs::highCut);
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (std::abs(lowCut - lastLowCutSeen) > 0.01f || std::abs(highCut - lastHighCutSeen) > 0.01f)
        {
            lastLowCutSeen = lowCut;
            lastHighCutSeen = highCut;
            filterShownUntilMs = now + 2500.0;
        }
        const bool listening = get(ParamIDs::listen) > 0.5f;
        scope.setListening(listening);
        scope.setFilterView(lowCut, std::max(highCut, lowCut * 1.3f), listening || now < filterShownUntilMs);
    }

    const bool calibrating = processorRef.isCalibrating();
    scope.setCalibration(calibrating ? processorRef.getCalibrationProgress() : -1.0f);
    scope.repaint();

    // Último golpe
    const int hits = processorRef.hitCount.load();
    if (hits != lastHitCount)
    {
        lastHitCount = hits;
        hitFlash = 1.0f;
    }
    hitFlash = std::max(0.0f, hitFlash - 0.12f);

    // Muestra que sonó (por un golpe o por MIDI de entrada)
    const int voices = processorRef.voiceCount.load();
    if (voices != lastVoiceCount)
    {
        lastVoiceCount = voices;
        const int s = processorRef.lastSlotPlayed.load();
        if (s >= 0 && s < DecimulTriggerProcessor::numSlots)
            slotFlash[(size_t) s] = 1.0f;
    }

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

    // Calibración
    calibratePill.setToggleState(calibrating, juce::dontSendNotification);
    if (calibrating)
    {
        const int secondsLeft = (int) std::ceil((1.0f - processorRef.getCalibrationProgress()) * 6.0f);
        calibratePill.setButtonText("CANCELAR (" + juce::String(secondsLeft) + ")");
        calibrateStatus.setText("Toca de suave a fuerte: va midiendo el piso y el golpe mas fuerte.",
                                juce::dontSendNotification);
    }
    else
    {
        calibratePill.setButtonText("CALIBRAR");
        const auto cal = processorRef.getLastCalibration();
        juce::String text;
        if (!cal.valid)
            text = "Toca unos 6 s (suave a fuerte) y ajusta Threshold y Max Level solo.";
        else if (cal.ok)
            text = "Listo: Threshold " + juce::String(cal.thresholdDb, 1) + " dB, Max Level " + juce::String(cal.maxLevelDb, 1)
                 + " dB (" + juce::String(cal.hits) + " golpes, piso " + juce::String(cal.floorDb, 0) + " dB)";
        else
            text = "No oi golpes: sube el nivel de entrada o toca mas fuerte.";
        calibrateStatus.setText(text, juce::dontSendNotification);
    }

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
    recPill.setButtonText(processorRef.isCapturing() ? "PARAR (" + juce::String(processorRef.getRecordedMidiHits()) + ")"
                                                    : juce::String("GRABAR GOLPES"));
    openFolderPill.setAlpha(lastExportTarget.exists() ? 1.0f : 0.45f);
    exportMidiPill.setAlpha(processorRef.getRecordedMidiHits() > 0 ? 1.0f : 0.5f);
    makePill.setAlpha(processorRef.getTakeCount() > 0 ? 1.0f : 0.5f);
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

        const float gainDb = processorRef.getSlotGain(i);
        const float tuneSt = processorRef.getSlotTune(i);
        if (!gainLabels[(size_t) i].isBeingEdited())
            gainLabels[(size_t) i].setText((gainDb > 0.04f ? "+" : "") + juce::String(gainDb, 1), juce::dontSendNotification);
        if (!tuneLabels[(size_t) i].isBeingEdited())
            tuneLabels[(size_t) i].setText((tuneSt > 0.04f ? "+" : "") + juce::String(tuneSt, 1), juce::dontSendNotification);
    }

    const int mode = (int) std::lround(get(ParamIDs::sampler));
    samplerOffPill.setToggleState(mode == 0, juce::dontSendNotification);
    samplerReinforcePill.setToggleState(mode == 1, juce::dontSendNotification);
    samplerReplacePill.setToggleState(mode == 2, juce::dontSendNotification);

    // MIDI de entrada: el botón rota entre apagado / solo la nota configurada / cualquiera
    const int midiInMode = (int) std::lround(get(ParamIDs::midiIn));
    midiInPill.setToggleState(midiInMode != DecimulTriggerProcessor::midiInOff, juce::dontSendNotification);
    midiInPill.setButtonText(midiInMode == DecimulTriggerProcessor::midiInOff    ? juce::String("MIDI IN: NO")
                             : midiInMode == DecimulTriggerProcessor::midiInNote ? "MIDI IN: NOTA " + juce::String(note)
                                                                                 : juce::String("MIDI IN: TODAS"));

    // Lo que no aplica al modo actual se apaga.
    const bool samplerOn = mode != 0;
    const bool reinforce = mode == 1;
    for (auto* k : { &sampleLevelKnob, &velSensKnob, &offsetKnob })
    {
        k->slider.setAlpha(samplerOn ? 1.0f : 0.4f);
        k->label.setAlpha(samplerOn ? 1.0f : 0.4f);
    }
    origLevelKnob.slider.setAlpha(reinforce ? 1.0f : 0.4f);
    origLevelKnob.label.setAlpha(reinforce ? 1.0f : 0.4f);
    chokePill.setAlpha(samplerOn ? 1.0f : 0.5f);
    polarityPill.setAlpha(samplerOn ? 1.0f : 0.5f);
    midiInPill.setAlpha(samplerOn || midiInMode == DecimulTriggerProcessor::midiInOff ? 1.0f : 0.5f);

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
    if (!advanced)
        hint = juce::String(mode == 0 ? "Solo MIDI: manda notas con velocity a lo que sigue en la cadena."
                            : mode == 1 ? "Reforzar: suena el original junto con la muestra."
                                        : "Reemplazar: suena solo la muestra.")
             + "\nArrastra un archivo a cada muestra: la 1 suena con los golpes suaves y la 2 con los fuertes.";
    hintLabel.setText(hint, juce::dontSendNotification);

    updateHiddenWarning();
    if (pendingNormalize && !advanced)
    {
        const auto a = processorRef.getSlotInfo(0), b = processorRef.getSlotInfo(1);
        if (!a.loading && !b.loading)
        {
            pendingNormalize = false;
            normalizeCommonRanges();
        }
    }

    // Salida
    const float outDb = processorRef.outputPeakDb.load();
    outputMeter.setLevelDb(outDb);
    outputMeter.repaint();
    vuArc.setLevelDb(outDb);
    vuArc.repaint();
    bypassButton.repaint();
}
