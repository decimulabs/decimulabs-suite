#include "PluginEditor.h"
#include "BinaryData.h"
#include "dsp/CincelFit.h"

namespace
{
    juce::String tr(bool es, const char* en, const char* spa) { return juce::String(juce::CharPointer_UTF8(es ? spa : en)); }

    void styleLabel(juce::Label& l, float size, juce::Colour c = DecimuColours::textMuted)
    {
        l.setFont(juce::Font(juce::FontOptions(size)));
        l.setColour(juce::Label::textColourId, c);
        l.setJustificationType(juce::Justification::centredLeft);
    }

    void panelBackground(juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour(juce::Colour(0xf20b0d16));
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(DecimuColours::accentPrimary.withAlpha(0.6f));
        g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.2f);
    }

    void setupSmallKnob(juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 15);
    }
}

//==============================================================================
MatchPanel::MatchPanel(DecimulCincelProcessor& p, DecimuTooltips& tips) : proc(p)
{
    for (auto* c : { (juce::Component*) &source, (juce::Component*) &learn, (juce::Component*) &applyButton,
                     (juce::Component*) &clearButton, (juce::Component*) &closeButton, (juce::Component*) &bandsKnob,
                     (juce::Component*) &amountKnob, (juce::Component*) &bandsLabel, (juce::Component*) &amountLabel,
                     (juce::Component*) &status, (juce::Component*) &title })
        addAndMakeVisible(*c);
    learn.setClickingTogglesState(true);
    setupSmallKnob(bandsKnob);
    bandsKnob.setRange(1.0, 16.0, 1.0);
    bandsKnob.setValue(8.0, juce::dontSendNotification);
    setupSmallKnob(amountKnob);
    amountKnob.setRange(0.0, 100.0, 1.0);
    amountKnob.setValue(100.0, juce::dontSendNotification);
    amountKnob.setTextValueSuffix(" %");
    styleLabel(title, 15.0f, DecimuColours::accentPrimary);
    styleLabel(status, 12.0f);
    styleLabel(bandsLabel, 12.0f, DecimuColours::text);
    styleLabel(amountLabel, 12.0f, DecimuColours::text);
    bandsLabel.setJustificationType(juce::Justification::centred);
    amountLabel.setJustificationType(juce::Justification::centred);
    title.setText("EQ MATCH", juce::dontSendNotification);

    learn.onClick = [this] { if (learn.getToggleState() && frames == 0) clearLearning(); };
    clearButton.onClick = [this] { clearLearning(); };
    applyButton.onClick = [this] { apply(); };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    source.onChange = [this] { clearLearning(); };

    tips.add(source, "Reference: what this track should sound like. The sidechain input, or another track that has Cincel.",
             "Referencia: cómo debería sonar esta pista. La entrada de sidechain, u otra pista que tenga Cincel.");
    tips.add(learn, "Learn: press it and play both (the reference and this track) for a while, 10 seconds or more. Press again to stop.",
             "Aprender: apretalo y hacé sonar las dos (la referencia y esta pista) un rato, 10 segundos o más. Apretá de nuevo para parar.");
    tips.add(bandsKnob, "How many bands it can use to get closer to the reference.", "Cuántas bandas puede usar para acercarse a la referencia.");
    tips.add(amountKnob, "How much of the difference to correct. 50 % gets halfway there, often the most natural.",
             "Cuánto de la diferencia corregir. Con 50 % llega a mitad de camino, que suele ser lo más natural.");
    tips.add(applyButton, "Create the bands (they replace the current ones; you can undo).", "Arma las bandas (reemplazan a las que hay; se puede deshacer).");
    tips.add(clearButton, "Forget what was learned and start again.", "Olvida lo aprendido y empieza de nuevo.");
}

void MatchPanel::refreshSources(bool es)
{
    spanish = es;
    bandsLabel.setText(tr(es, "BANDS", "BANDAS"), juce::dontSendNotification);
    amountLabel.setText(tr(es, "AMOUNT", "CANTIDAD"), juce::dontSendNotification);
    learn.setButtonText(tr(es, "LEARN", "APRENDER"));
    applyButton.setButtonText(tr(es, "APPLY", "APLICAR"));
    clearButton.setButtonText(tr(es, "CLEAR", "BORRAR"));

    std::vector<int> ids { 0 };
    juce::StringArray names { "Sidechain" };
    for (const auto& e : InstanceRegistry::get().others(proc.instanceId))
    {
        ids.push_back(e.id);
        names.add(e.name);
    }
    if (ids != sourceIds || source.getNumItems() != names.size())
    {
        const int prevId = source.getSelectedItemIndex() >= 0 && source.getSelectedItemIndex() < (int) sourceIds.size()
                               ? sourceIds[(size_t) source.getSelectedItemIndex()] : 0;
        sourceIds = ids;
        source.clear(juce::dontSendNotification);
        source.addItemList(names, 1);
        int idx = 0;
        for (size_t i = 0; i < ids.size(); ++i)
            if (ids[i] == prevId)
                idx = (int) i;
        source.setSelectedItemIndex(idx, juce::dontSendNotification);
    }
}

void MatchPanel::clearLearning()
{
    sumRef.fill(0.0);
    sumCur.fill(0.0);
    frames = 0;
}

void MatchPanel::tick()
{
    const int sel = source.getSelectedItemIndex();
    const int id = sel >= 0 && sel < (int) sourceIds.size() ? sourceIds[(size_t) sel] : 0;
    juce::String refState;
    if (learn.getToggleState())
    {
        const float* ref = nullptr;
        InstanceRegistry::Entry e;
        if (id == 0)
        {
            if (proc.sidechainConnected.load() && proc.scAnalyzer.level > -90.0f)
                ref = proc.scAnalyzer.db.data();
        }
        else if (InstanceRegistry::get().find(id, e) && e.hasSpectrum)
            ref = e.spectrum.data();
        if (ref != nullptr && proc.preAnalyzer.level > -90.0f)
        {
            for (int i = 0; i < SpectrumAnalyzer::points; ++i)
            {
                sumRef[(size_t) i] += std::pow(10.0, ref[i] / 10.0);
                sumCur[(size_t) i] += std::pow(10.0, proc.preAnalyzer.db[(size_t) i] / 10.0);
            }
            ++frames;
        }
    }
    const double secs = frames / 30.0;
    if (frames == 0)
        refState = learn.getToggleState() ? tr(spanish, "Waiting for sound in both...", "Esperando sonido en las dos...")
                                          : tr(spanish, "Press LEARN and play both.", "Apretá APRENDER y hacé sonar las dos.");
    else
        refState = tr(spanish, "Learned: ", "Aprendido: ") + juce::String(secs, 1) + " s" + (learn.getToggleState() ? " ..." : "");
    status.setText(refState, juce::dontSendNotification);
    applyButton.setEnabled(frames >= 15);
}

void MatchPanel::apply()
{
    if (frames < 15)
        return;
    std::vector<double> anaHz, diff;
    for (int i = 0; i < SpectrumAnalyzer::points; ++i)
    {
        anaHz.push_back(SpectrumAnalyzer::pointHz(i));
        diff.push_back(10.0 * std::log10(sumRef[(size_t) i] / frames + 1.0e-20) - 10.0 * std::log10(sumCur[(size_t) i] / frames + 1.0e-20));
    }
    diff = cincel::smoothOctave(anaHz, diff, 1.0 / 3.0);

    cincel::FitGrid grid;
    grid.build(200);
    double sumW = 0.0, sumD = 0.0;
    for (int i = 0; i < grid.size(); ++i)
    {
        const double hz = grid.hz[(size_t) i];
        const double pos = std::log(hz / SpectrumAnalyzer::loHz) / std::log(SpectrumAnalyzer::hiHz / SpectrumAnalyzer::loHz) * (SpectrumAnalyzer::points - 1);
        const int k = juce::jlimit(0, SpectrumAnalyzer::points - 2, (int) pos);
        const double t = pos - k;
        grid.target[(size_t) i] = diff[(size_t) k] + t * (diff[(size_t) k + 1] - diff[(size_t) k]);
        const double refDb = 10.0 * std::log10(sumRef[(size_t) k] / frames + 1.0e-20);
        const double curDb = 10.0 * std::log10(sumCur[(size_t) k] / frames + 1.0e-20);
        grid.weight[(size_t) i] = (refDb > -95.0 && curDb > -95.0) ? 1.0 : 0.1;
        if (hz > 60.0 && hz < 10000.0)
        {
            sumW += grid.weight[(size_t) i];
            sumD += grid.weight[(size_t) i] * grid.target[(size_t) i];
        }
    }
    // No importa el volumen general (eso es la perilla de salida): solo la forma.
    const double mean = sumW > 0.0 ? sumD / sumW : 0.0;
    const double amount = amountKnob.getValue() * 0.01;
    for (auto& t : grid.target)
        t = juce::jlimit(-18.0, 18.0, t - mean) * amount;

    const auto fitted = cincel::fitBandsToCurve(grid, (int) bandsKnob.getValue());
    proc.undoManager.beginNewTransaction("EQ Match");
    proc.resetAll(false);
    int idx = 0;
    for (const auto& b : fitted)
        proc.writeBand(idx++, b);
    learn.setToggleState(false, juce::dontSendNotification);
    if (onApplied)
        onApplied();
}

void MatchPanel::paint(juce::Graphics& g) { panelBackground(g, getLocalBounds().toFloat()); }

void MatchPanel::resized()
{
    auto r = getLocalBounds().reduced(10, 8);
    auto top = r.removeFromTop(22);
    closeButton.setBounds(top.removeFromRight(26));
    title.setBounds(top);
    r.removeFromTop(4);
    source.setBounds(r.removeFromTop(24));
    r.removeFromTop(6);
    auto row = r.removeFromTop(26);
    learn.setBounds(row.removeFromLeft(row.getWidth() / 2 - 3));
    row.removeFromLeft(6);
    clearButton.setBounds(row);
    r.removeFromTop(4);
    status.setBounds(r.removeFromTop(18));
    auto knobs = r.removeFromTop(78);
    auto k1 = knobs.removeFromLeft(knobs.getWidth() / 2);
    bandsLabel.setBounds(k1.removeFromTop(14));
    bandsKnob.setBounds(k1);
    amountLabel.setBounds(knobs.removeFromTop(14));
    amountKnob.setBounds(knobs);
    r.removeFromTop(4);
    applyButton.setBounds(r.removeFromTop(28));
}

//==============================================================================
InstancesPanel::InstancesPanel(DecimulCincelProcessor& p, DecimuTooltips& tips) : proc(p)
{
    for (auto* c : { (juce::Component*) &noneButton, (juce::Component*) &collisions, (juce::Component*) &closeButton,
                     (juce::Component*) &title, (juce::Component*) &empty })
        addAndMakeVisible(*c);
    collisions.setClickingTogglesState(true);
    collisions.setToggleState((bool) proc.uiState().getProperty("collisions", true), juce::dontSendNotification);
    collisions.onClick = [this] { proc.uiState().setProperty("collisions", collisions.getToggleState(), nullptr); };
    noneButton.onClick = [this]
    {
        selected = 0;
        if (onChange) onChange();
    };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    styleLabel(title, 15.0f, DecimuColours::accentPrimary);
    styleLabel(empty, 12.0f);
    empty.setJustificationType(juce::Justification::topLeft);
    tips.add(collisions, "Mark in red where this track and the chosen one are both loud in the same frequencies (they mask each other).",
             "Marca en rojo dónde esta pista y la elegida suenan fuerte en las mismas frecuencias (se tapan una a la otra).");
}

void InstancesPanel::refreshList(bool es)
{
    spanish = es;
    title.setText(tr(es, "INSTANCES", "INSTANCIAS"), juce::dontSendNotification);
    collisions.setButtonText(tr(es, "COLLISIONS", "CHOQUES"));
    noneButton.setButtonText(tr(es, "NONE", "NINGUNA"));
    const auto others = InstanceRegistry::get().others(proc.instanceId);
    std::vector<int> newIds;
    for (const auto& e : others)
        newIds.push_back(e.id);
    if (newIds != ids)
    {
        ids = newIds;
        buttons.clear();
        for (const auto& e : others)
        {
            auto* b = buttons.add(new DecimuPillButton(e.name));
            const int id = e.id;
            b->onClick = [this, id]
            {
                selected = id;
                if (onChange) onChange();
            };
            addAndMakeVisible(b);
        }
        resized();
    }
    for (size_t i = 0; i < others.size() && i < (size_t) buttons.size(); ++i)
    {
        buttons[(int) i]->setButtonText(others[i].name);
        buttons[(int) i]->setToggleState(others[i].id == selected, juce::dontSendNotification);
    }
    noneButton.setToggleState(selected == 0, juce::dontSendNotification);
    empty.setText(others.empty() ? tr(es, "No other Cincel open. Put it on other tracks to compare them here.",
                                      "No hay otro Cincel abierto. Ponelo en otras pistas para compararlas acá.")
                                 : juce::String(),
                  juce::dontSendNotification);
}

void InstancesPanel::paint(juce::Graphics& g) { panelBackground(g, getLocalBounds().toFloat()); }

void InstancesPanel::resized()
{
    auto r = getLocalBounds().reduced(10, 8);
    auto top = r.removeFromTop(22);
    closeButton.setBounds(top.removeFromRight(26));
    title.setBounds(top);
    r.removeFromTop(6);
    collisions.setBounds(r.removeFromBottom(26));
    r.removeFromBottom(6);
    noneButton.setBounds(r.removeFromTop(24));
    r.removeFromTop(4);
    empty.setBounds(r);
    for (auto* b : buttons)
    {
        if (r.getHeight() < 24)
        {
            b->setBounds({});
            continue;
        }
        b->setBounds(r.removeFromTop(24));
        r.removeFromTop(4);
    }
}

//==============================================================================
void DecimulCincelEditor::addKnob(Knob& k, const juce::String& paramID, const juce::String& caption)
{
    setupSmallKnob(k.slider);
    addAndMakeVisible(k.slider);
    k.label.setText(caption, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setFont(juce::Font(juce::FontOptions(12.0f)));
    k.label.setColour(juce::Label::textColourId, DecimuColours::text);
    addAndMakeVisible(k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.apvts, paramID, k.slider);
}

void DecimulCincelEditor::placeKnob(Knob& k, juce::Rectangle<int> cell)
{
    k.label.setBounds(cell.removeFromTop(14));
    const int side = std::min(cell.getWidth(), cell.getHeight() - 16);
    k.slider.setBounds(cell.withSizeKeepingCentre(std::max(side, 56), side + 16));
}

DecimulCincelEditor::DecimulCincelEditor(DecimulCincelProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p), display(p), bandPanel(p, tips), matchPanel(p, tips), instancesPanel(p, tips)
{
    setLookAndFeel(&lookAndFeel);
    processorRef.editorOpen.store(true);

    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    addAndMakeVisible(display);
    addChildComponent(bandPanel);
    addChildComponent(matchPanel);
    addChildComponent(instancesPanel);

    display.onSelectionChanged = [this]
    {
        const int b = display.getPrimary();
        bandPanel.setBand(b);
        bandPanel.setVisible(b >= 0);
    };
    bandPanel.getSelection = [this] { return display.getSelection(); };
    bandPanel.onDelete = [this] { display.deleteSelection(); };
    bandPanel.onNavigate = [this](int dir)
    {
        std::array<cincel::BandParams, cincel::maxBands> all;
        processorRef.readAllBands(all);
        int i = display.getPrimary();
        for (int step = 0; step < cincel::maxBands; ++step)
        {
            i = (i + dir + cincel::maxBands) % cincel::maxBands;
            if (all[(size_t) i].used)
            {
                display.selectOnly(i);
                return;
            }
        }
    };
    display.onSketchFinished = [this] { sketchPill.setToggleState(false, juce::sendNotification); };
    matchPanel.onClose = [this] { matchPill.setToggleState(false, juce::sendNotification); };
    matchPanel.onApplied = [this] { display.selectOnly(-1); };
    instancesPanel.onClose = [this] { instancesPill.setToggleState(false, juce::sendNotification); };
    instancesPanel.onChange = [this] { display.overlayInstance = instancesPanel.selected; };

    // ---- Arriba ----
    for (auto* b : { &presetButton, &abA, &abB, &abCopy, &undoButton, &redoButton })
        addAndMakeVisible(*b);
    presetButton.onClick = [this] { showPresetMenu(); };
    abA.onClick = [this] { if (processorRef.currentAB() != 0) processorRef.switchAB(); };
    abB.onClick = [this] { if (processorRef.currentAB() != 1) processorRef.switchAB(); };
    abCopy.onClick = [this] { processorRef.copyCurrentToOther(); };
    undoButton.onClick = [this] { processorRef.undoManager.undo(); };
    redoButton.onClick = [this] { processorRef.undoManager.redo(); };
    addAndMakeVisible(bypassButton);
    addAndMakeVisible(vuArc);
    bypassAttach = std::make_unique<ButtonAttachment>(processorRef.apvts, CincelIDs::bypass, bypassButton);

    // ---- Abajo ----
    modeBox.addItemList({ "Zero Latency", "Natural Phase", "Linear Phase" }, 1);
    resBox.addItemList({ "Low", "Medium", "High", "Very High", "Maximum" }, 1);
    charBox.addItemList({ "Clean", "Subtle", "Warm" }, 1);
    rangeBox.addItemList({ "+/- 3 dB", "+/- 6 dB", "+/- 12 dB", "+/- 30 dB" }, 1);
    for (auto* c : { &modeBox, &resBox, &charBox, &rangeBox })
        addAndMakeVisible(*c);
    modeAttach = std::make_unique<ComboAttachment>(processorRef.apvts, CincelIDs::mode, modeBox);
    resAttach = std::make_unique<ComboAttachment>(processorRef.apvts, CincelIDs::lpRes, resBox);
    charAttach = std::make_unique<ComboAttachment>(processorRef.apvts, CincelIDs::character, charBox);
    rangeBox.setSelectedItemIndex((int) processorRef.uiState().getProperty("range", 2), juce::dontSendNotification);
    rangeBox.onChange = [this] { processorRef.uiState().setProperty("range", rangeBox.getSelectedItemIndex(), nullptr); };

    for (auto* l : { &modeLabel, &charLabel, &rangeLabel, &latencyLabel, &autoGainLabel })
    {
        styleLabel(*l, 12.0f);
        addAndMakeVisible(*l);
    }

    for (auto* b : { &analyzerButton, &matchPill, &sketchPill, &instancesPill, &pianoPill, &gainQPill, &autoGainPill, &phasePill, &helpPill })
        addAndMakeVisible(*b);
    for (auto* b : { &matchPill, &sketchPill, &instancesPill, &pianoPill, &gainQPill, &autoGainPill, &phasePill })
        b->setClickingTogglesState(true);
    gainQAttach = std::make_unique<ButtonAttachment>(processorRef.apvts, CincelIDs::gainQ, gainQPill);
    autoGainAttach = std::make_unique<ButtonAttachment>(processorRef.apvts, CincelIDs::autoGain, autoGainPill);
    phaseAttach = std::make_unique<ButtonAttachment>(processorRef.apvts, CincelIDs::phase, phasePill);
    analyzerButton.onClick = [this] { display.showBackgroundMenu(); };
    pianoPill.setToggleState((bool) processorRef.uiState().getProperty("piano", false), juce::dontSendNotification);
    pianoPill.onClick = [this]
    {
        processorRef.uiState().setProperty("piano", pianoPill.getToggleState(), nullptr);
        display.resized();
    };
    sketchPill.onClick = [this]
    {
        display.sketchMode = sketchPill.getToggleState();
        if (display.sketchMode)
            display.selectOnly(-1);
    };
    matchPill.onClick = [this] { openMatchPanel(matchPill.getToggleState()); };
    instancesPill.onClick = [this]
    {
        instancesPanel.setVisible(instancesPill.getToggleState());
        instancesPanel.refreshList(spanish);
        layoutOverlays();
    };

    addKnob(scaleKnob, CincelIDs::gainScale, "SCALE");
    addKnob(attackKnob, CincelIDs::attack, "ATTACK");
    addKnob(releaseKnob, CincelIDs::release, "RELEASE");
    addKnob(outputKnob, CincelIDs::output, "OUTPUT");
    addKnob(panKnob, CincelIDs::pan, "PAN");
    scaleKnob.slider.textFromValueFunction = [](double v) { return juce::String((int) std::lround(v)) + " %"; };
    attackKnob.slider.textFromValueFunction = [](double v) { return juce::String(v, v < 10.0 ? 1 : 0) + " ms"; };
    releaseKnob.slider.textFromValueFunction = [](double v) { return juce::String((int) std::lround(v)) + " ms"; };
    outputKnob.slider.textFromValueFunction = [](double v) { return (v > 0 ? "+" : "") + juce::String(v, 1) + " dB"; };
    panKnob.slider.textFromValueFunction = [](double v)
    {
        if (std::abs(v) < 0.5) return juce::String("C");
        return (v < 0 ? "L" : "R") + juce::String((int) std::lround(std::abs(v)));
    };
    for (auto* k : { &scaleKnob, &attackKnob, &releaseKnob, &outputKnob, &panKnob })
        k->slider.updateText();
    addAndMakeVisible(outputMeter);

    // ---- Textos de ayuda (inglés por defecto; el botón EN/ES de abajo los pasa a español) ----
    addAndMakeVisible(tips.getSwitch());
    tips.add(presetButton, "Presets: factory starting points, your own saved presets, and Save.",
             "Presets: puntos de partida de fábrica, los tuyos guardados, y Guardar.");
    tips.add(abA, "Setting A. Switch between A and B to compare two EQs.", "Ajuste A. Pasá de A a B para comparar dos EQ.");
    tips.add(abB, "Setting B.", "Ajuste B.");
    tips.add(abCopy, "Copy the current setting to the other slot.", "Copia el ajuste actual al otro lugar.");
    tips.add(undoButton, "Undo (Ctrl+Z).", "Deshacer (Ctrl+Z).");
    tips.add(redoButton, "Redo (Ctrl+Shift+Z).", "Rehacer (Ctrl+Shift+Z).");
    tips.add(bypassButton, "Bypass: the audio passes untouched (with the same delay, so nothing jumps).",
             "Bypass: el audio pasa sin tocar (con la misma demora, así nada salta).");
    tips.add(modeBox, "Zero Latency: no delay, analog-matched curves (ideal for tracking and most mixing). Natural Phase: analog magnitude AND phase also near 20 kHz, ~1.3 ms delay. Linear Phase: no phase shift at all (good for mastering and parallel signals), more delay.",
             "Latencia cero: sin demora, curvas iguales a las analógicas (ideal para grabar y para casi toda la mezcla). Fase natural: magnitud Y fase analógicas también cerca de los 20 kHz, ~1.3 ms de demora. Fase lineal: no corre la fase de nada (para mastering y señales en paralelo), más demora.");
    tips.add(resBox, "Linear phase resolution: higher = more precise in the lows, but more delay and some pre-ringing.",
             "Resolución de fase lineal: más alta = más precisa en los graves, pero más demora y algo de pre-eco.");
    tips.add(charBox, "Character: Clean (none), Subtle or Warm. Adds a little analog-style saturation only to what the EQ changes; with a flat curve it colours nothing.",
             "Carácter: Clean (nada), Subtle o Warm. Suma un poco de saturación tipo analógica solo a lo que el EQ cambia; con la curva plana no colorea nada.");
    tips.add(rangeBox, "Vertical zoom of the curve: +/-3, 6, 12 or 30 dB.", "Zoom vertical de la curva: +/-3, 6, 12 o 30 dB.");
    tips.add(analyzerButton, "Analyzer options: input/output/sidechain, resolution, speed, tilt, range, freeze, piano.",
             "Opciones del analizador: entrada/salida/sidechain, resolución, velocidad, inclinación, rango, congelar, piano.");
    tips.add(matchPill, "EQ Match: makes this track sound like a reference (the sidechain or another track with Cincel).",
             "EQ Match: hace que esta pista suene como una referencia (el sidechain u otra pista con Cincel).");
    tips.add(sketchPill, "EQ Sketch: draw the curve you want with the mouse and the bands are created for you.",
             "EQ Sketch: dibujá con el mouse la curva que querés y las bandas se arman solas.");
    tips.add(instancesPill, "Other tracks with Cincel: show one of them on top of this one and see where they collide.",
             "Otras pistas con Cincel: mostrar una encima de esta y ver dónde chocan.");
    tips.add(pianoPill, "Piano keyboard under the curve, to find notes.", "Teclado de piano debajo de la curva, para ubicar notas.");
    tips.add(gainQPill, "Gain-Q: the more gain a bell has, the narrower it gets (like many analog EQs).",
             "Gain-Q: cuanta más ganancia tiene una campana, más angosta se vuelve (como muchos EQ analógicos).");
    tips.add(autoGainPill, "Auto gain: compensates the volume the curve adds or removes, so you judge the tone and not the level.",
             "Auto gain: compensa el volumen que la curva suma o quita, así juzgás el color y no el nivel.");
    tips.add(scaleKnob.slider, "Gain scale: scales all the bands at once (100 % = as drawn, 50 % = half, negative = inverted).",
             "Escala de ganancia: agranda o achica todas las bandas a la vez (100 % = como están, 50 % = la mitad, negativo = invertidas).");
    tips.add(attackKnob.slider, "Attack of the dynamic bands: how fast they react.", "Ataque de las bandas dinámicas: qué tan rápido reaccionan.");
    tips.add(releaseKnob.slider, "Release of the dynamic bands: how fast they let go.", "Release de las bandas dinámicas: qué tan rápido sueltan.");
    tips.add(outputKnob.slider, "Output gain, -36 to +36 dB.", "Ganancia de salida, de -36 a +36 dB.");
    tips.add(panKnob.slider, "Output balance between left and right.", "Balance de salida entre izquierda y derecha.");
    tips.add(phasePill, "Inverts the polarity of the output.", "Invierte la polaridad de la salida.");
    tips.add(helpPill,
             "Mouse: double-click = new band (near the edges, a cut). Drag a dot = frequency and gain (Ctrl = fine; on cuts, vertical = Q). Wheel = Q; Alt+wheel = dynamic range. Alt while dragging = listen to the band alone. Double-click a dot = on/off. Right-click = band menu. Drag on empty space = select several; Shift+click = add. Delete key = delete. Grab a peak of the spectrum = new band right there.",
             "Mouse: doble clic = banda nueva (cerca de los bordes, un corte). Arrastrar un punto = frecuencia y ganancia (Ctrl = fino; en los cortes, vertical = Q). Rueda = Q; Alt+rueda = rango dinámico. Alt mientras arrastrás = escuchar la banda sola. Doble clic en un punto = prender/apagar. Clic derecho = menú de la banda. Arrastrar en el vacío = elegir varias; Shift+clic = sumar. Supr = borrar. Agarrar un pico del espectro = banda nueva justo ahí.");

    setWantsKeyboardFocus(true);
    setSize(1280, 780);
    setResizable(true, true);
    setResizeLimits(1040, 640, 2400, 1500);
    updateLanguage();
    startTimerHz(30);
}

DecimulCincelEditor::~DecimulCincelEditor()
{
    stopTimer();
    InstanceRegistry::get().setWatching(processorRef.instanceId, false);
    processorRef.editorOpen.store(false);
    processorRef.soloBand.store(-1);
    setLookAndFeel(nullptr);
}

void DecimulCincelEditor::openMatchPanel(bool open)
{
    matchPill.setToggleState(open, juce::dontSendNotification);
    matchPanel.setVisible(open);
    matchPanel.refreshSources(spanish);
    layoutOverlays();
}

void DecimulCincelEditor::showPresetMenu()
{
    juce::PopupMenu m, factory, user;
    const auto names = processorRef.factoryPresetNames();
    for (int i = 0; i < names.size(); ++i)
        factory.addItem(100 + i, names[i]);
    const auto files = processorRef.userPresetFolder().findChildFiles(juce::File::findFiles, false, "*.xml");
    juce::Array<juce::File> sorted(files);
    std::sort(sorted.begin(), sorted.end(), [](const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });
    for (int i = 0; i < sorted.size(); ++i)
        user.addItem(1000 + i, sorted[i].getFileNameWithoutExtension());
    m.addSubMenu(tr(spanish, "Factory", "De fábrica"), factory);
    m.addSubMenu(tr(spanish, "My presets", "Mis presets"), user, sorted.size() > 0);
    m.addSeparator();
    m.addItem(1, tr(spanish, "Save preset...", "Guardar preset..."));
    m.addItem(2, tr(spanish, "Open presets folder", "Abrir la carpeta de presets"));

    juce::Component::SafePointer<DecimulCincelEditor> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetButton), [safe, sorted](int r)
    {
        if (safe == nullptr || r == 0)
            return;
        auto& self = *safe;
        if (r >= 100 && r < 1000)
        {
            self.processorRef.loadFactoryPreset(r - 100);
            self.display.selectOnly(-1);
        }
        else if (r >= 1000)
        {
            self.processorRef.undoManager.beginNewTransaction("Preset");
            self.processorRef.loadPresetFile(sorted[r - 1000]);
            self.display.selectOnly(-1);
        }
        else if (r == 1)
        {
            auto chooser = std::make_shared<juce::FileChooser>(tr(self.spanish, "Save preset", "Guardar preset"),
                                                               self.processorRef.userPresetFolder(), "*.xml");
            chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                     | juce::FileBrowserComponent::warnAboutOverwriting,
                                 [safe, chooser](const juce::FileChooser& fc)
                                 {
                                     if (safe == nullptr)
                                         return;
                                     auto f = fc.getResult();
                                     if (f == juce::File())
                                         return;
                                     if (!f.hasFileExtension("xml"))
                                         f = f.withFileExtension("xml");
                                     safe->processorRef.saveUserPreset(f);
                                 });
        }
        else if (r == 2)
            self.processorRef.userPresetFolder().startAsProcess();
    });
}

void DecimulCincelEditor::updateLanguage()
{
    spanish = tips.getLanguage() == DecimuLang::Lang::es;
    display.spanish = spanish;
    bandPanel.setSpanish(spanish);
    modeLabel.setText(tr(spanish, "PROCESSING", "PROCESO"), juce::dontSendNotification);
    charLabel.setText(tr(spanish, "CHARACTER", "CARÁCTER"), juce::dontSendNotification);
    rangeLabel.setText(tr(spanish, "DISPLAY", "PANTALLA"), juce::dontSendNotification);
    analyzerButton.setButtonText(tr(spanish, "ANALYZER", "ANALIZADOR"));
    instancesPill.setButtonText(tr(spanish, "INSTANCES", "INSTANCIAS"));
    phasePill.setButtonText(tr(spanish, "PHASE", "FASE"));
    undoButton.setButtonText(tr(spanish, "UNDO", "DESHACER"));
    redoButton.setButtonText(tr(spanish, "REDO", "REHACER"));
    scaleKnob.label.setText(tr(spanish, "SCALE", "ESCALA"), juce::dontSendNotification);
    attackKnob.label.setText(tr(spanish, "ATTACK", "ATAQUE"), juce::dontSendNotification);
    releaseKnob.label.setText("RELEASE", juce::dontSendNotification);
    outputKnob.label.setText(tr(spanish, "OUTPUT", "SALIDA"), juce::dontSendNotification);
    panKnob.label.setText("PAN", juce::dontSendNotification);
    matchPanel.refreshSources(spanish);
    instancesPanel.refreshList(spanish);
}

bool DecimulCincelEditor::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z')
    {
        if (key.getModifiers().isShiftDown())
            processorRef.undoManager.redo();
        else
            processorRef.undoManager.undo();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Y')
    {
        processorRef.undoManager.redo();
        return true;
    }
    return false;
}

void DecimulCincelEditor::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    DecimuChrome::paintChassisBackground(g, full, 14.0f);
    DecimuChrome::paintRackHandles(g, full);
    DecimuChrome::paintCornerScrews(g, full, 12.0f);

    DecimuChrome::paintCompartment(g, topbarRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, displayRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, bottomRect.toFloat(), true, 8.0f);

    auto logoArea = headerLogoArea;
    if (dogImage.isValid())
    {
        auto dogArea = logoArea.removeFromLeft(logoArea.getHeight());
        g.drawImage(dogImage, dogArea.toFloat(), juce::RectanglePlacement::centred);
        logoArea.removeFromLeft(8);
    }
    if (wordmarkImage.isValid())
    {
        auto wordmarkArea = logoArea.removeFromLeft(juce::jmin(logoArea.getWidth(), 190)).reduced(0, 7);
        g.drawImage(wordmarkImage, wordmarkArea.toFloat(), juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
        logoArea.removeFromLeft(16);
    }
    g.setColour(DecimuColours::accentPrimary);
    g.setFont(lookAndFeel.monoFont(24.0f));
    g.drawText("CINCEL EQ", logoArea.removeFromLeft(150), juce::Justification::centredLeft);

    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(13.0f));
    auto footer = getLocalBounds().removeFromBottom(22).reduced(16, 3);
    g.drawText("DECIMULABS - CINCEL EQ - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulCincelEditor::layoutOverlays()
{
    auto area = display.getBounds().reduced(10);
    {
        // Abajo, salvo que la banda elegida esté en la mitad de abajo (el panel la taparía).
        const int w = std::min(720, area.getWidth() - 20), h = 128;
        const int y = panelAtTop ? area.getY() + 4 : area.getBottom() - h - 24;
        bandPanel.setBounds(area.getCentreX() - w / 2, y, w, h);
    }
    auto right = area.removeFromRight(250).removeFromTop(330).translated(-26, 4);
    matchPanel.setBounds(right);
    auto right2 = display.getBounds().reduced(10).removeFromRight(250).removeFromTop(280).translated(-26, 4);
    if (matchPanel.isVisible())
        right2 = right2.translated(-260, 0);
    instancesPanel.setBounds(right2);
}

void DecimulCincelEditor::resized()
{
    tips.layout(getLocalBounds());

    auto content = getLocalBounds().reduced(22, 18);
    content.removeFromBottom(8);
    topbarRect = content.removeFromTop(54);
    content.removeFromTop(8);
    bottomRect = content.removeFromBottom(92);
    content.removeFromBottom(8);
    displayRect = content;
    display.setBounds(displayRect.reduced(6));

    // Arriba: logo | preset | A B A>B | deshacer rehacer | VU | bypass
    auto header = topbarRect.reduced(10, 8);
    bypassButton.setBounds(header.removeFromRight(110));
    vuArc.setBounds(header.removeFromRight(96).reduced(2));
    header.removeFromRight(10);
    redoButton.setBounds(header.removeFromRight(86).reduced(0, 3));
    header.removeFromRight(4);
    undoButton.setBounds(header.removeFromRight(96).reduced(0, 3));
    header.removeFromRight(14);
    abCopy.setBounds(header.removeFromRight(52).reduced(0, 3));
    header.removeFromRight(4);
    abB.setBounds(header.removeFromRight(34).reduced(0, 3));
    header.removeFromRight(4);
    abA.setBounds(header.removeFromRight(34).reduced(0, 3));
    header.removeFromRight(14);
    presetButton.setBounds(header.removeFromRight(std::min(260, header.getWidth() / 3)).reduced(0, 3));
    headerLogoArea = header;

    // Abajo, de izquierda a derecha.
    auto bar = bottomRect.reduced(12, 8);
    auto rows = [](juce::Rectangle<int> col, int& y0, int& y1)
    {
        y0 = col.getY();
        y1 = col.getY() + col.getHeight() / 2 + 2;
    };
    int y0 = 0, y1 = 0;
    rows(bar, y0, y1);
    const int rowH = bar.getHeight() / 2 - 4;

    auto colA = bar.removeFromLeft(318);
    modeLabel.setBounds(colA.getX(), y0, 90, rowH);
    modeBox.setBounds(colA.getX() + 92, y0 + 2, 128, rowH - 4);
    resBox.setBounds(colA.getX() + 224, y0 + 2, 92, rowH - 4);
    charLabel.setBounds(colA.getX(), y1, 90, rowH);
    charBox.setBounds(colA.getX() + 92, y1 + 2, 90, rowH - 4);
    latencyLabel.setBounds(colA.getX() + 186, y1, 132, rowH);
    bar.removeFromLeft(12);

    auto colB = bar.removeFromLeft(352);
    const int pw = 84;
    analyzerButton.setBounds(colB.getX(), y0 + 2, 100, rowH - 4);
    rangeLabel.setBounds(colB.getX() + 106, y0, 66, rowH);
    rangeBox.setBounds(colB.getX() + 172, y0 + 2, 104, rowH - 4);
    pianoPill.setBounds(colB.getX() + 282, y0 + 2, 66, rowH - 4);
    matchPill.setBounds(colB.getX(), y1 + 2, pw - 4, rowH - 4);
    sketchPill.setBounds(colB.getX() + pw, y1 + 2, pw - 4, rowH - 4);
    instancesPill.setBounds(colB.getX() + 2 * pw, y1 + 2, pw + 14, rowH - 4);
    helpPill.setBounds(colB.getX() + 3 * pw + 18, y1 + 2, 30, rowH - 4);
    bar.removeFromLeft(10);

    outputMeter.setBounds(bar.removeFromRight(14).reduced(0, 2));
    bar.removeFromRight(6);
    auto phaseCol = bar.removeFromRight(64);
    phasePill.setBounds(phaseCol.withSizeKeepingCentre(60, 26));
    const int kw = std::min(66, bar.getWidth() / 7);
    placeKnob(panKnob, bar.removeFromRight(kw));
    placeKnob(outputKnob, bar.removeFromRight(kw));
    bar.removeFromRight(8);
    auto agCol = bar.removeFromRight(96);
    gainQPill.setBounds(agCol.getX(), y0 + 2, 92, rowH - 4);
    autoGainPill.setBounds(agCol.getX(), y1 + 2, 92, rowH - 4);
    autoGainLabel.setBounds({});
    bar.removeFromRight(4);
    placeKnob(releaseKnob, bar.removeFromRight(kw));
    placeKnob(attackKnob, bar.removeFromRight(kw));
    placeKnob(scaleKnob, bar.removeFromRight(kw));

    layoutOverlays();
}

void DecimulCincelEditor::timerCallback()
{
    const bool es = tips.getLanguage() == DecimuLang::Lang::es;
    if (es != spanish)
        updateLanguage();

    display.refresh();
    if (display.getPrimary() != bandPanel.getBand())
    {
        bandPanel.setBand(display.getPrimary());
        bandPanel.setVisible(display.getPrimary() >= 0);
    }
    bandPanel.refresh();
    {
        const float h = display.primaryDotHeight();
        const bool top = h > 0.55f && !matchPanel.isVisible() && !instancesPanel.isVisible();
        if (top != panelAtTop && !display.isMouseButtonDown())
        {
            panelAtTop = top;
            layoutOverlays();
        }
    }

    const float outDb = std::max(processorRef.outPeakL.load(), processorRef.outPeakR.load());
    outputMeter.setLevelDb(outDb);
    outputMeter.repaint();
    vuArc.setLevelDb(outDb);
    vuArc.repaint();
    bypassButton.repaint();

    const int lat = processorRef.latencySamples();
    const double ms = 1000.0 * lat / processorRef.getSampleRateHz();
    latencyLabel.setText(lat == 0 ? tr(spanish, "no latency", "sin latencia")
                                  : juce::String(lat) + tr(spanish, " smp (", " muestras (") + juce::String(ms, 1) + " ms)",
                         juce::dontSendNotification);
    resBox.setEnabled(processorRef.globalValue(CincelIDs::mode) > 1.5f);
    if (processorRef.globalValue(CincelIDs::autoGain) > 0.5f)
    {
        const float ag = processorRef.autoGainDb();
        autoGainPill.setButtonText("AUTO " + juce::String(ag > 0 ? "+" : "") + juce::String(ag, 1));
    }
    else
        autoGainPill.setButtonText("AUTO GAIN");

    undoButton.setEnabled(processorRef.undoManager.canUndo());
    redoButton.setEnabled(processorRef.undoManager.canRedo());
    presetButton.setButtonText(processorRef.currentPresetName);
    abA.setToggleState(processorRef.currentAB() == 0, juce::dontSendNotification);
    abB.setToggleState(processorRef.currentAB() == 1, juce::dontSendNotification);
    abCopy.setButtonText(processorRef.currentAB() == 0 ? "A>B" : "B>A");
    rangeBox.setSelectedItemIndex((int) processorRef.uiState().getProperty("range", 2), juce::dontSendNotification);
    pianoPill.setToggleState((bool) processorRef.uiState().getProperty("piano", false), juce::dontSendNotification);

    if (matchPanel.isVisible())
        matchPanel.tick();
    if (++instanceRefreshCounter >= 30)
    {
        instanceRefreshCounter = 0;
        instancesPanel.refreshList(spanish);
        if (matchPanel.isVisible())
            matchPanel.refreshSources(spanish);
    }
    const bool watching = instancesPanel.isVisible() || display.overlayInstance != 0 || matchPanel.isVisible();
    InstanceRegistry::get().setWatching(processorRef.instanceId, watching);
}
