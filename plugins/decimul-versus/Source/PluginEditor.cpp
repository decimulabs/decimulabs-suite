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

    // P(X >= correct) para X ~ Binomial(total, 0.5): probabilidad de acertar tantas o más
    // veces por puro azar. Es el criterio estándar de un test ABX.
    double abxPValue(int correct, int total)
    {
        if (total <= 0)
            return 1.0;

        double p = 0.0;
        for (int k = std::max(0, correct); k <= total; ++k)
        {
            const double logChoose = std::lgamma(total + 1.0) - std::lgamma(k + 1.0) - std::lgamma(total - k + 1.0);
            p += std::exp(logChoose - total * std::log(2.0));
        }
        return std::min(1.0, p);
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

bool DecimulVersusEditor::isAbxOn() const
{
    return processorRef.apvts.getRawParameterValue(ParamIDs::abxMode)->load() > 0.5f;
}

DecimulVersusEditor::DecimulVersusEditor(DecimulVersusProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);
    setWantsKeyboardFocus(true);

    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    liveMixSmooth.fill(-120.0f);
    liveRefSmooth.fill(-120.0f);

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

    alignPill.setClickingTogglesState(false);
    alignPill.onClick = [this]
    {
        juce::String report;
        processorRef.alignToMix(report);
        alignLabel.setText(report, juce::dontSendNotification);
    };
    addAndMakeVisible(alignPill);

    alignLabel.setJustificationType(juce::Justification::centredLeft);
    alignLabel.setColour(juce::Label::textColourId, DecimuColours::accentPrimary);
    addAndMakeVisible(alignLabel);

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
    avgPill.setClickingTogglesState(false);
    livePill.setClickingTogglesState(false);
    diffPill.setClickingTogglesState(false);
    avgPill.onClick = [this] { setSpectrumMode(SpectrumMode::average); };
    livePill.onClick = [this] { setSpectrumMode(SpectrumMode::live); };
    diffPill.onClick = [this] { setSpectrumMode(SpectrumMode::diff); };
    addAndMakeVisible(avgPill);
    addAndMakeVisible(livePill);
    addAndMakeVisible(diffPill);
    avgPill.setToggleState(true, juce::dontSendNotification);

    resetPill.setClickingTogglesState(false);
    resetPill.onClick = [this] { processorRef.resetMixAnalysis(); };
    addAndMakeVisible(resetPill);

    addAndMakeVisible(spectrum);
    addAndMakeVisible(metrics);

    // ---- A / B / X ----
    for (auto* pill : { &aPill, &bPill, &xPill })
    {
        pill->setEmphasis(true);
        pill->setClickingTogglesState(false);
    }
    aPill.onClick = [this]
    {
        if (isAbxOn()) processorRef.abxSetListen(0);
        else           setParam(ParamIDs::refActive, 0.0f);
    };
    bPill.onClick = [this]
    {
        if (isAbxOn()) processorRef.abxSetListen(1);
        else           setParam(ParamIDs::refActive, 1.0f);
    };
    xPill.onClick = [this] { processorRef.abxSetListen(2); };
    addAndMakeVisible(aPill);
    addAndMakeVisible(bPill);
    addChildComponent(xPill);

    addAndMakeVisible(abxPill);

    ansAPill.setClickingTogglesState(false);
    ansBPill.setClickingTogglesState(false);
    abxResetPill.setClickingTogglesState(false);
    ansAPill.onClick = [this] { processorRef.abxAnswer(false); };
    ansBPill.onClick = [this] { processorRef.abxAnswer(true); };
    abxResetPill.onClick = [this] { processorRef.abxReset(); };
    addChildComponent(ansAPill);
    addChildComponent(ansBPill);
    addChildComponent(abxResetPill);

    abxStatsLabel.setJustificationType(juce::Justification::centredLeft);
    abxStatsLabel.setColour(juce::Label::textColourId, DecimuColours::text);
    abxStatsLabel.setFont(juce::Font(juce::FontOptions(14.0f)));
    addChildComponent(abxStatsLabel);

    sourceALabel.setText("A =", juce::dontSendNotification);
    sourceALabel.setJustificationType(juce::Justification::centredRight);
    sourceALabel.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    addAndMakeVisible(sourceALabel);

    sourceACombo.addItem("Mezcla del DAW", 1);
    for (int i = 0; i < DecimulVersusProcessor::numSlots; ++i)
        sourceACombo.addItem("Slot " + juce::String(i + 1), i + 2);
    addAndMakeVisible(sourceACombo);

    setupBracketLabel(abLabel, "ESCUCHAR   teclas A / B / X, 1-8 = slot B");

    // ---- Controles ----
    setupSelector(matchSel, { "Auto LUFS", "Off" }, ParamIDs::matchMode);
    setupSelector(monitorSel, { "Stereo", "Mono", "Side" }, ParamIDs::monitor);
    setupSelector(bandSel, { "Full", "Low", "Mid", "High" }, ParamIDs::band);
    setupBracketLabel(matchLabel, "MATCH");
    setupBracketLabel(monitorLabel, "MONITOR");
    setupBracketLabel(bandLabel, "BANDA");
    setupBracketLabel(trimLabel, "TRIM B");
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
    abxAttach = std::make_unique<ButtonAttachment>(apvts, ParamIDs::abxMode, abxPill);
    sourceAAttach = std::make_unique<ComboAttachment>(apvts, ParamIDs::sourceA, sourceACombo);

    // ---- Tooltips: inglés por defecto; el botón EN/ES de abajo los pasa a español ----
    addAndMakeVisible(tips.getSwitch());

    for (int i = 0; i < DecimulVersusProcessor::numSlots; ++i)
        tips.add(*slotButtons[(size_t) i],
                 "Reference slot " + juce::String(i + 1) + ": click to pick it as B (an empty one asks for a song), "
                 "right-click to replace or remove it. You can also drop audio files on the window. Key " + juce::String(i + 1) + ".",
                 "Slot de referencia " + juce::String(i + 1) + ": click para elegirlo como B (uno vacío pide una canción), "
                 "click derecho para reemplazarlo o quitarlo. También podés soltar archivos de audio en la ventana. Tecla " + juce::String(i + 1) + ".");

    tips.add(playPill, "Play / stop the reference from the plugin itself, without the DAW playing.",
             "Reproduce / detiene la referencia desde el plugin, sin que el DAW esté en play.");
    tips.add(hostPill, "Host sync (default): the reference plays when the DAW plays and follows its transport.",
             "Host sync (default): la referencia suena cuando el DAW está en play y sigue su transporte.");
    tips.add(loopPill, "Loop: repeats the marked region of the waveform. Drag on the waveform to mark one.",
             "Loop: repite la región marcada de la forma de onda. Arrastrá sobre la forma de onda para marcarla.");
    tips.add(alignPill, "Finds the stretch of the reference that best matches (in tonal balance and level) the last seconds of your mix, and jumps there. Play your mix for about 5 seconds first.",
             "Busca en la referencia el tramo que más se parece (en balance tonal y nivel) a los últimos segundos de tu mezcla, y salta ahí. Antes dejá sonar tu mezcla unos 5 segundos.");
    tips.add(waveform, "Waveform of the selected reference. Click to jump, drag to mark a loop, double-click to clear it.",
             "Forma de onda de la referencia elegida. Click = saltar, arrastrar = marcar un loop, doble click = borrarlo.");

    tips.add(avgPill, "Long-term average spectrum: your mix (A) against the reference (B), with the loudness match applied.",
             "Espectro promedio de largo plazo: tu mezcla (A) contra la referencia (B), con el volumen igualado aplicado.");
    tips.add(livePill, "Real-time spectrum, in third-octave bands.", "Espectro en tiempo real, en bandas de tercio de octava.");
    tips.add(diffPill, "Difference per band, +/-12 dB: a bar going up means your mix has more energy in that band.",
             "Diferencia por banda, +/-12 dB: una barra hacia arriba significa que tu mezcla tiene más energía en esa banda.");
    tips.add(resetPill, "Restarts the long-term average of your mix.", "Reinicia el promedio de largo plazo de tu mezcla.");
    tips.add(spectrum, "Spectrum in 30 third-octave bands: your mix against the reference, at the same loudness.",
             "Espectro en 30 bandas de tercio de octava: tu mezcla contra la referencia, al mismo volumen.");
    tips.add(metrics, "Loudness and dynamics: LUFS, true peak, PLR, LRA, stereo width and correlation, for your mix and the reference.",
             "Loudness y dinámica: LUFS, true peak, PLR, LRA, ancho estéreo y correlación, de tu mezcla y de la referencia.");

    tips.add(aPill, "A: listen to your mix (or whatever is chosen in \"A =\"). Key A.",
             "A: escuchás tu mezcla (o lo que esté elegido en \"A =\"). Tecla A.");
    tips.add(bPill, "B: listen to the selected reference, at the same loudness. Key B.",
             "B: escuchás la referencia elegida, al mismo volumen. Tecla B.");
    tips.add(xPill, "X: the hidden one, A or B at random. Key X.", "X: la oculta, A o B al azar. Tecla X.");
    tips.add(abxPill, "Blind test: X is A or B at random. Guess which one it is and see how many you get right.",
             "Test ciego: X es A o B al azar. Adiviná cuál es y mirá cuántas acertás.");
    tips.add(ansAPill, "Answer: X is A.", "Respuesta: X es A.");
    tips.add(ansBPill, "Answer: X is B.", "Respuesta: X es B.");
    tips.add(abxResetPill, "Resets the blind test count.", "Reinicia la cuenta del test ciego.");
    tips.add(sourceACombo, "What plays on side A: your mix, or a slot (to compare two versions of a song with each other).",
             "Qué suena en el lado A: tu mezcla, o un slot (para comparar dos versiones de un tema entre sí).");

    tips.add(matchSel, "Match: Auto LUFS levels the reference to your mix by integrated loudness (default). Off applies no gain.",
             "Match: Auto LUFS iguala la referencia a tu mezcla por loudness integrado (default). Off no aplica ganancia.");
    tips.add(trimKnob, "Trim B: manual gain on the reference, +/-12 dB, on top of the loudness match.",
             "Trim B: ganancia manual sobre la referencia, +/-12 dB, encima del igualado de volumen.");
    tips.add(monitorSel, "Monitor: Stereo, Mono, or Side (only what differs between left and right).",
             "Monitor: Stereo, Mono o Side (solo lo que difiere entre izquierda y derecha).");
    tips.add(bandSel, "Listen to one band only: Full, Low (below 150 Hz), Mid, or High (above 2.5 kHz).",
             "Escuchar una sola banda: Full, Low (menos de 150 Hz), Mid o High (más de 2.5 kHz).");
    tips.add(outputSlider, "Output trim, -24 to +12 dB.", "Ganancia de salida, -24 a +12 dB.");
    tips.add(bypassButton, "Bypass: the audio passes through untouched (the dot goes grey).",
             "Bypass: el audio pasa sin tocarse (el punto se pone gris).");

    setSize(1120, 790);
    setResizable(true, true);
    setResizeLimits(960, 720, 1800, 1100);
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

void DecimulVersusEditor::setSpectrumMode(SpectrumMode mode)
{
    spectrumMode = mode;
    avgPill.setToggleState(mode == SpectrumMode::average, juce::dontSendNotification);
    livePill.setToggleState(mode == SpectrumMode::live, juce::dontSendNotification);
    diffPill.setToggleState(mode == SpectrumMode::diff, juce::dontSendNotification);
    spectrum.setShowDiff(mode == SpectrumMode::diff);
    resetPill.setEnabled(mode != SpectrumMode::live);
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

    if (c == 'a') { aPill.onClick(); return true; }
    if (c == 'b') { bPill.onClick(); return true; }
    if (c == 'x' && isAbxOn()) { processorRef.abxSetListen(2); return true; }
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

void DecimulVersusEditor::layoutAbBox()
{
    const bool abx = isAbxOn();

    auto col = abRect.reduced(10);
    abLabel.setBounds(col.removeFromBottom(20));
    col.removeFromBottom(4);

    auto row1 = col.removeFromTop(62);
    col.removeFromTop(6);
    auto row2 = col.removeFromTop(30);
    col.removeFromTop(6);
    auto row3 = col.removeFromTop(40);

    if (abx)
    {
        const int w = (row1.getWidth() - 16) / 3;
        aPill.setBounds(row1.removeFromLeft(w));
        row1.removeFromLeft(8);
        bPill.setBounds(row1.removeFromLeft(w));
        row1.removeFromLeft(8);
        xPill.setBounds(row1);
    }
    else
    {
        const int w = (row1.getWidth() - 8) / 2;
        aPill.setBounds(row1.removeFromLeft(w));
        row1.removeFromLeft(8);
        bPill.setBounds(row1);
        xPill.setBounds({});
    }

    abxPill.setBounds(row2.removeFromLeft(150));
    row2.removeFromLeft(8);
    sourceALabel.setBounds(row2.removeFromLeft(34));
    sourceACombo.setBounds(row2);

    ansAPill.setBounds(row3.removeFromLeft(64).withTrimmedTop(6).withTrimmedBottom(4));
    row3.removeFromLeft(6);
    ansBPill.setBounds(row3.removeFromLeft(64).withTrimmedTop(6).withTrimmedBottom(4));
    row3.removeFromLeft(8);
    abxResetPill.setBounds(row3.removeFromRight(58).withTrimmedTop(6).withTrimmedBottom(4));
    row3.removeFromRight(6);
    abxStatsLabel.setBounds(row3);
}

void DecimulVersusEditor::resized()
{
    tips.layout(getLocalBounds());

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

    auto controls = content.removeFromBottom(176);
    content.removeFromBottom(8);

    waveRect = content.removeFromTop(150);
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

    // ---- Forma de onda + transporte + alineación ----
    {
        auto area = waveRect.reduced(8);
        auto strip = area.removeFromTop(30);
        playPill.setBounds(strip.removeFromLeft(90));
        strip.removeFromLeft(6);
        hostPill.setBounds(strip.removeFromLeft(130));
        strip.removeFromLeft(6);
        loopPill.setBounds(strip.removeFromLeft(80));
        strip.removeFromLeft(16);
        alignPill.setBounds(strip.removeFromLeft(170));
        strip.removeFromLeft(8);
        timeLabel.setBounds(strip.removeFromRight(230));
        alignLabel.setBounds(strip);
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
        avgPill.setBounds(strip.removeFromLeft(110));
        strip.removeFromLeft(6);
        livePill.setBounds(strip.removeFromLeft(130));
        strip.removeFromLeft(6);
        diffPill.setBounds(strip.removeFromLeft(130));
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

    layoutAbBox();

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
    const int srcA = processorRef.getSourceASlot();
    const bool abx = isAbxOn();

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

    // Nombres de los slots en el selector de la fuente A.
    {
        juce::StringArray items;
        for (int i = 0; i < DecimulVersusProcessor::numSlots; ++i)
        {
            const auto info = processorRef.getSlotInfo(i);
            items.add(juce::String(i + 1) + ": " + (info.name.isNotEmpty() ? info.name : juce::String("(vacio)")));
        }
        if (items != lastSourceItems)
        {
            lastSourceItems = items;
            for (int i = 0; i < items.size(); ++i)
                sourceACombo.changeItemText(i + 2, items[i]);
            sourceACombo.repaint();
        }
    }

    const auto info = processorRef.getSlotInfo(slot);
    auto track = processorRef.getTrack(slot);
    const bool trackOk = track != nullptr && info.loaded;

    std::shared_ptr<const ReferenceTrack> trackA;
    if (srcA >= 0)
    {
        trackA = processorRef.getTrack(srcA);
        if (!processorRef.getSlotInfo(srcA).loaded)
            trackA = nullptr;
    }

    // ---- Forma de onda ----
    waveform.setTrack(trackOk ? track : nullptr);
    if (info.loading)
        waveform.setStatusText("CARGANDO Y ANALIZANDO...");
    else if (info.error.isNotEmpty())
        waveform.setStatusText("ERROR: " + info.error.toUpperCase());
    else if (trackOk && !track->hasAudio())
        waveform.setStatusText("RECARGANDO EL AUDIO...");
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

    // ---- A / B / X ----
    if (abx != lastAbxOn)
    {
        lastAbxOn = abx;
        xPill.setVisible(abx);
        ansAPill.setVisible(abx);
        ansBPill.setVisible(abx);
        abxResetPill.setVisible(abx);
        abxStatsLabel.setVisible(abx);
        layoutAbBox();
    }

    if (abx)
    {
        const int listen = processorRef.abxGetListen();
        aPill.setToggleState(listen == 0, juce::dontSendNotification);
        bPill.setToggleState(listen == 1, juce::dontSendNotification);
        xPill.setToggleState(listen == 2, juce::dontSendNotification);

        const int correct = processorRef.abxGetCorrect();
        const int total = processorRef.abxGetTotal();
        juce::String stats;
        if (total == 0)
        {
            stats = "Escucha A, B y X\ny decidi que es X";
        }
        else
        {
            const double p = abxPValue(correct, total);
            stats = juce::String(correct) + "/" + juce::String(total) + "  p=" + juce::String(p, 3) + "\n";
            if (total < 8)
                stats += "faltan intentos (10+)";
            else if (p < 0.05)
                stats += "SE NOTA (p<0.05)";
            else
                stats += "no se distingue";
        }
        abxStatsLabel.setText(stats, juce::dontSendNotification);
    }
    else
    {
        const bool b = apvts.getRawParameterValue(ParamIDs::refActive)->load() > 0.5f;
        aPill.setToggleState(!b, juce::dontSendNotification);
        bPill.setToggleState(b, juce::dontSendNotification);
    }

    // ---- Selectores ----
    matchSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::matchMode)->load());
    monitorSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::monitor)->load());
    bandSel.setSelectedIndex((int) apvts.getRawParameterValue(ParamIDs::band)->load());

    // ---- Espectro ----
    const bool aIsSlot = srcA >= 0 && trackA != nullptr;
    const float gainBDb = processorRef.appliedRefGainDb.load();
    const float gainADb = aIsSlot ? processorRef.appliedGainADb.load() : 0.0f;

    VersusSpectrumView::Data sd;
    sd.mixGainDb = gainADb;
    sd.refGainDb = gainBDb;
    sd.mixName = aIsSlot ? "A: " + trackA->displayName.substring(0, 22) : juce::String("MEZCLA");
    sd.refName = trackOk ? "B: " + track->displayName.substring(0, 22) + "  (volumen igualado)"
                         : juce::String("REFERENCIA");

    if (spectrumMode == SpectrumMode::live)
    {
        // Lado A: la mezcla en vivo, o el frame del slot A en el cabezal.
        bool aValid = false;
        std::array<float, VersusBands::numBands> a {};
        if (srcA < 0)
        {
            aValid = processorRef.mixLiveActive.load();
            for (int i = 0; i < VersusBands::numBands; ++i)
                a[(size_t) i] = processorRef.mixLiveDb[(size_t) i].load();
        }
        else if (aIsSlot)
        {
            if (auto* frame = trackA->frameAt(playhead))
            {
                aValid = true;
                for (int i = 0; i < VersusBands::numBands; ++i)
                    a[(size_t) i] = frame[i];
            }
        }

        bool bValid = false;
        std::array<float, VersusBands::numBands> bb {};
        if (trackOk)
        {
            if (auto* frame = track->frameAt(playhead))
            {
                bValid = true;
                for (int i = 0; i < VersusBands::numBands; ++i)
                    bb[(size_t) i] = frame[i];
            }
        }

        // Ataque inmediato, caída suave: se lee mejor que el parpadeo crudo.
        auto smooth = [](std::array<float, VersusBands::numBands>& state,
                         const std::array<float, VersusBands::numBands>& v, bool valid)
        {
            for (int i = 0; i < VersusBands::numBands; ++i)
            {
                const float target = valid ? v[(size_t) i] : -120.0f;
                state[(size_t) i] = target > state[(size_t) i]
                                        ? target
                                        : state[(size_t) i] + (target - state[(size_t) i]) * 0.25f;
            }
        };
        smooth(liveMixSmooth, a, aValid);
        smooth(liveRefSmooth, bb, bValid);

        sd.mixValid = aValid;
        sd.refValid = bValid;
        sd.mix = liveMixSmooth;
        sd.ref = liveRefSmooth;
        sd.emptyHint = "REPRODUCI PARA VER EL ESPECTRO EN TIEMPO REAL";
    }
    else
    {
        if (srcA < 0)
        {
            sd.mixValid = processorRef.mixBandsValid.load();
            for (int i = 0; i < VersusBands::numBands; ++i)
                sd.mix[(size_t) i] = processorRef.mixBandDb[(size_t) i].load();
        }
        else if (aIsSlot)
        {
            sd.mixValid = true;
            sd.mix = trackA->bandDb;
        }

        sd.refValid = trackOk;
        if (trackOk)
            sd.ref = track->bandDb;
        sd.emptyHint = "REPRODUCI LA MEZCLA Y/O CARGA UNA REFERENCIA PARA VER EL ESPECTRO PROMEDIO";
    }
    spectrum.setData(sd);

    // ---- Medidas ----
    VersusMetricsView::Data md;
    md.leftName = aIsSlot ? "A" : "MIX";
    md.rightName = aIsSlot ? "B" : "REF";

    if (aIsSlot)
    {
        md.mixValid = true;
        md.mixLufsI = trackA->integratedLufs;
        md.mixLufsS = trackA->shortTermAt(playhead);
        md.mixTruePeak = trackA->truePeakDb;
        md.mixLra = trackA->loudnessRange;
        md.mixWidth = trackA->widthPct;
        md.mixCorr = trackA->correlation;
    }
    else
    {
        md.mixValid = processorRef.mixIntegratedLufs.load() > -99.0f || processorRef.mixBandsValid.load();
        md.mixLufsI = processorRef.mixIntegratedLufs.load();
        md.mixLufsS = processorRef.mixShortTermLufs.load();
        md.mixTruePeak = processorRef.mixTruePeakDb.load();
        md.mixLra = processorRef.mixLoudnessRange.load();
        md.mixWidth = processorRef.mixWidthPct.load();
        md.mixCorr = processorRef.mixCorrelation.load();
    }

    md.refValid = trackOk;
    if (trackOk)
    {
        md.refLufsI = track->integratedLufs;
        md.refLufsS = track->shortTermAt(playhead);
        md.refTruePeak = track->truePeakDb;
        md.refLra = track->loudnessRange;
        md.refWidth = track->widthPct;
        md.refCorr = track->correlation;
    }
    md.gainAppliedDb = gainBDb;
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
