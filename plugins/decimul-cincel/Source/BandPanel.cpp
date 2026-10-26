#include "BandPanel.h"
#include "EqDisplay.h"

using cincel::BandParams;

BandPanel::BandPanel(DecimulCincelProcessor& p, DecimuTooltips& t) : proc(p), tips(t)
{
    for (auto* b : { (juce::Button*) &prevButton, (juce::Button*) &nextButton, (juce::Button*) &soloPill, (juce::Button*) &deletePill })
        addAndMakeVisible(*b);
    for (auto* b : { (juce::Button*) &dynPill, (juce::Button*) &autoPill, (juce::Button*) &scPill, (juce::Button*) &specPill, (juce::Button*) &onPill })
    {
        b->setClickingTogglesState(true);
        addAndMakeVisible(*b);
    }
    soloPill.setClickingTogglesState(false);

    // Solo como en el Pro-Q: se escucha mientras está apretado; arrastrando a los costados se
    // barre la frecuencia de la banda; al soltar vuelve al EQ normal.
    soloPill.onPress = [this]
    {
        if (band < 0)
            return;
        proc.undoManager.beginNewTransaction();
        soloStartFreq = proc.readBand(band).freq;
        soloHeld = true;
        proc.soloBand.store(band);
    };
    soloPill.onDragged = [this](int dx)
    {
        if (band < 0 || !soloHeld)
            return;
        auto b = proc.readBand(band);
        b.freq = (float) juce::jlimit(10.0, 30000.0, soloStartFreq * std::pow(2.0, dx / 90.0));
        proc.writeBand(band, b);
    };
    soloPill.onRelease = [this]
    {
        if (soloHeld)
            proc.soloBand.store(-1);
        soloHeld = false;
    };

    title.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(title);

    shapeBox.addItemList({ "Bell", "Low Shelf", "Low Cut", "High Shelf", "High Cut", "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt" }, 1);
    slopeBox.addItemList({ "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct", "Brickwall" }, 1);
    placeBox.addItemList({ "Stereo", "Left", "Right", "Mid", "Side" }, 1);
    for (auto* c : { &shapeBox, &slopeBox, &placeBox })
        addAndMakeVisible(*c);

    setupKnob(freq, "FREQ");
    setupKnob(gain, "GAIN");
    setupKnob(q, "Q");
    setupKnob(range, "RANGE");
    setupKnob(thr, "THRESH");

    // Edición relativa de varias bandas a la vez.
    freq.slider.onDragStart = [this] { beginRelative(freq); };
    gain.slider.onDragStart = [this] { beginRelative(gain); };
    q.slider.onDragStart = [this] { beginRelative(q); };
    range.slider.onDragStart = [this] { beginRelative(range); };
    thr.slider.onDragStart = [this] { beginRelative(thr); };
    freq.slider.onValueChange = [this] { applyRelative(freq, CincelIDs::freq, true); };
    gain.slider.onValueChange = [this] { applyRelative(gain, CincelIDs::gain, false); };
    q.slider.onValueChange = [this] { applyRelative(q, CincelIDs::q, true); };
    range.slider.onValueChange = [this] { applyRelative(range, CincelIDs::range, false); };
    thr.slider.onValueChange = [this] { applyRelative(thr, CincelIDs::thr, false); };
    for (auto* k : { &freq, &gain, &q, &range, &thr })
        k->slider.onDragEnd = [this] { relativeStart.clear(); };

    // Lo que el usuario cambia acá se copia a las otras bandas elegidas (no lo que llega de un
    // deshacer, un preset o una automatización: por eso se mira de dónde vino el cambio).
    shapeBox.onChange = [this] { if (shapeBox.consumeUserChange()) applyToOthers(CincelIDs::shape, (float) shapeBox.getSelectedItemIndex()); };
    slopeBox.onChange = [this] { if (slopeBox.consumeUserChange()) applyToOthers(CincelIDs::slope, (float) slopeBox.getSelectedItemIndex()); };
    placeBox.onChange = [this] { if (placeBox.consumeUserChange()) applyToOthers(CincelIDs::place, (float) placeBox.getSelectedItemIndex()); };
    auto pillToOthers = [this](juce::Button& pill, CincelIDs::BandKey key)
    {
        pill.onClick = [this, &pill, key]
        {
            if (userMouse)
                applyToOthers(key, pill.getToggleState() ? 1.0f : 0.0f);
        };
    };
    pillToOthers(dynPill, CincelIDs::dyn);
    pillToOthers(autoPill, CincelIDs::autoThr);
    pillToOthers(scPill, CincelIDs::sc);
    pillToOthers(specPill, CincelIDs::spec);
    pillToOthers(onPill, CincelIDs::on);
    addMouseListener(this, true);

    deletePill.onClick = [this]
    {
        if (onDelete)
            onDelete();
    };
    prevButton.onClick = [this] { if (onNavigate) onNavigate(-1); };
    nextButton.onClick = [this] { if (onNavigate) onNavigate(1); };

    registerTips();
}

BandPanel::~BandPanel()
{
    setBand(-1);
}

void BandPanel::setupKnob(Knob& k, const juce::String& caption)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 15);
    addAndMakeVisible(k.slider);
    k.label.setText(caption, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setFont(juce::Font(juce::FontOptions(12.0f)));
    k.label.setColour(juce::Label::textColourId, DecimuColours::text);
    addAndMakeVisible(k.label);
}

void BandPanel::setSpanish(bool es)
{
    spanish = es;
    refresh();
}

void BandPanel::registerTips()
{
    tips.add(shapeBox, "Filter shape: bell, shelves, cuts, notch, band pass, tilt shelf (one side up, the other down) or flat tilt (a straight slope across the whole spectrum).",
             "Forma del filtro: campana, shelves, cortes, notch, pasabanda, tilt shelf (un lado sube y el otro baja) o tilt plano (una pendiente recta en todo el espectro).");
    tips.add(slopeBox, "Slope of cuts, shelves, notch and band pass: 6 to 96 dB per octave, or brickwall (a wall).",
             "Pendiente de cortes, shelves, notch y pasabanda: de 6 a 96 dB por octava, o brickwall (una pared).");
    tips.add(placeBox, "Where the band acts: both channels, only left, only right, only the centre (Mid) or only the sides (Side).",
             "Dónde actúa la banda: los dos canales, solo el izquierdo, solo el derecho, solo el centro (Mid) o solo los costados (Side).");
    tips.add(freq.slider, "Frequency of the band, 10 Hz to 30 kHz.", "Frecuencia de la banda, de 10 Hz a 30 kHz.");
    tips.add(gain.slider, "Gain, -30 to +30 dB.", "Ganancia, de -30 a +30 dB.");
    tips.add(q.slider, "Q: how narrow the band is. In cuts and shelves, values above 0.7 add a resonant bump at the frequency.",
             "Q: qué tan angosta es la banda. En cortes y shelves, por encima de 0.7 suma un pico resonante en la frecuencia.");
    tips.add(range.slider, "Dynamic range: how much the gain can move when the band's zone passes the threshold (negative = turns down, positive = turns up).",
             "Rango dinámico: cuánto se puede mover la ganancia cuando la zona de la banda pasa el umbral (negativo = baja, positivo = sube).");
    tips.add(thr.slider, "Threshold of the dynamic EQ: the level of the band's zone above which it starts acting.",
             "Umbral del EQ dinámico: el nivel de la zona de la banda a partir del cual empieza a actuar.");
    tips.add(dynPill, "Dynamic EQ: the band only acts when its zone gets loud (like a compressor, but just there).",
             "EQ dinámico: la banda actúa solo cuando su zona se pone fuerte (como un compresor, pero solo ahí).");
    tips.add(autoPill, "Automatic threshold: follows the average level of the zone, so only the peaks above it trigger the band.",
             "Umbral automático: sigue el nivel promedio de la zona, así solo los picos por encima hacen actuar a la banda.");
    tips.add(scPill, "The dynamics listen to the external sidechain instead of this track (route a sidechain to the plugin in your DAW).",
             "La dinámica escucha el sidechain externo en vez de esta pista (hay que rutear un sidechain al plugin en el DAW).");
    tips.add(specPill, "Spectral dynamics: inside the band each frequency is compressed on its own, so a resonance that comes and goes is tamed without touching its neighbours. Adds latency (about 43 ms): if switching it during playback causes a jump, reserve that latency in the analyzer menu (right-click on the graph).",
             "Dinámica espectral: dentro de la banda cada frecuencia se comprime por su cuenta, así una resonancia que aparece y desaparece se baja sin tocar a sus vecinas. Suma latencia (unos 43 ms): si al prenderlo con la música sonando hay un salto, reservá esa latencia en el menú del analizador (clic derecho en el gráfico).");
    tips.add(soloPill, "Solo (hold it, like in Pro-Q): while pressed you hear only this band's zone; drag sideways to sweep its frequency; release to go back. In cuts and notch you hear what is being removed. Also: Alt while dragging a dot, or right-click > Solo to leave it on.",
             "Solo (se mantiene apretado, como en el Pro-Q): mientras está apretado se escucha solo la zona de esta banda; arrastrando a los costados se barre su frecuencia; al soltar vuelve. En cortes y notch se escucha lo que se está sacando. También: Alt mientras arrastrás un punto, o clic derecho > Solo para dejarlo fijo.");
    tips.add(onPill, "Band on / off. When it is off the icon turns red and the dot goes hollow (also: double-click the dot).",
             "Banda prendida / apagada. Apagada, el ícono se pone rojo y el punto queda hueco (también: doble clic en el punto).");
    tips.add(deletePill, "Delete the band (also: Delete key).", "Borra la banda (también: tecla Supr).");
    tips.add(prevButton, "Previous band.", "Banda anterior.");
    tips.add(nextButton, "Next band.", "Banda siguiente.");
}

void BandPanel::setBand(int index)
{
    if (index == band && index >= 0)
        return;
    shapeAttach.reset();
    slopeAttach.reset();
    placeAttach.reset();
    dynAttach.reset();
    autoAttach.reset();
    scAttach.reset();
    specAttach.reset();
    onAttach.reset();
    for (auto* k : { &freq, &gain, &q, &range, &thr })
        k->attach.reset();
    band = index;
    for (int i = 0; i < getNumChildComponents(); ++i)
        getChildComponent(i)->setVisible(band >= 0);
    repaint();
    if (band < 0)
        return;

    auto& s = proc.apvts;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    freq.attach = std::make_unique<SA>(s, CincelIDs::band(band, CincelIDs::freq), freq.slider);
    gain.attach = std::make_unique<SA>(s, CincelIDs::band(band, CincelIDs::gain), gain.slider);
    q.attach = std::make_unique<SA>(s, CincelIDs::band(band, CincelIDs::q), q.slider);
    range.attach = std::make_unique<SA>(s, CincelIDs::band(band, CincelIDs::range), range.slider);
    thr.attach = std::make_unique<SA>(s, CincelIDs::band(band, CincelIDs::thr), thr.slider);
    shapeAttach = std::make_unique<CA>(s, CincelIDs::band(band, CincelIDs::shape), shapeBox);
    slopeAttach = std::make_unique<CA>(s, CincelIDs::band(band, CincelIDs::slope), slopeBox);
    placeAttach = std::make_unique<CA>(s, CincelIDs::band(band, CincelIDs::place), placeBox);
    dynAttach = std::make_unique<BA>(s, CincelIDs::band(band, CincelIDs::dyn), dynPill);
    autoAttach = std::make_unique<BA>(s, CincelIDs::band(band, CincelIDs::autoThr), autoPill);
    scAttach = std::make_unique<BA>(s, CincelIDs::band(band, CincelIDs::sc), scPill);
    specAttach = std::make_unique<BA>(s, CincelIDs::band(band, CincelIDs::spec), specPill);
    onAttach = std::make_unique<BA>(s, CincelIDs::band(band, CincelIDs::on), onPill);

    freq.slider.textFromValueFunction = [](double v) { return EqDisplay::formatHz(v); };
    gain.slider.textFromValueFunction = [](double v) { return (v > 0 ? "+" : "") + juce::String(v, 1) + " dB"; };
    q.slider.textFromValueFunction = [](double v) { return juce::String(v, v < 10.0 ? 2 : 1); };
    range.slider.textFromValueFunction = [](double v) { return (v > 0 ? "+" : "") + juce::String(v, 1) + " dB"; };
    thr.slider.textFromValueFunction = [](double v) { return juce::String(v, 1) + " dB"; };
    for (auto* k : { &freq, &gain, &q, &range, &thr })
        k->slider.updateText();

    title.setText((spanish ? "BANDA " : "BAND ") + juce::String(band + 1), juce::dontSendNotification);
    refresh();
    repaint();
}

void BandPanel::refresh()
{
    for (auto* c : { &shapeBox, &slopeBox, &placeBox })
        c->expire();
    if (band < 0)
        return;
    const auto b = proc.readBand(band);
    soloPill.setToggleState(proc.soloBand.load() == band, juce::dontSendNotification);
    soloPill.setOnColour(EqDisplay::bandColour(band));
    onPill.setOnColour(EqDisplay::bandColour(band));
    const bool hasGain = cincel::shapeHasGain(b.shape);
    gain.slider.setEnabled(hasGain);
    slopeBox.setEnabled(cincel::shapeHasSlope(b.shape));
    q.slider.setEnabled(cincel::shapeHasQ(b.shape));
    dynPill.setEnabled(hasGain);
    const bool dyn = hasGain && b.dynamic;
    for (auto* c : { (juce::Component*) &range.slider, (juce::Component*) &autoPill, (juce::Component*) &scPill, (juce::Component*) &specPill })
        c->setEnabled(dyn);
    thr.slider.setEnabled(dyn && !b.autoThreshold);
    title.setColour(juce::Label::textColourId, EqDisplay::bandColour(band));
    title.setText((spanish ? "BANDA " : "BAND ") + juce::String(band + 1), juce::dontSendNotification);
}

void BandPanel::beginRelative(Knob& k)
{
    k.dragStart = k.slider.getValue();
    relativeStart.clear();
    if (!getSelection)
        return;
    proc.undoManager.beginNewTransaction();
    for (int i : getSelection())
        if (i != band)
            relativeStart.push_back({ i, proc.readBand(i) });
}

void BandPanel::applyRelative(Knob& k, CincelIDs::BandKey key, bool ratio)
{
    if (relativeStart.empty() || !k.slider.isMouseButtonDown())
        return;
    const double now = k.slider.getValue();
    for (auto& [i, start] : relativeStart)
    {
        auto b = proc.readBand(i);
        switch (key)
        {
            case CincelIDs::freq: b.freq = (float) juce::jlimit(10.0, 30000.0, start.freq * now / std::max(1.0e-6, k.dragStart)); break;
            case CincelIDs::q: b.q = (float) juce::jlimit(0.025, 40.0, start.q * now / std::max(1.0e-6, k.dragStart)); break;
            case CincelIDs::gain: b.gainDb = (float) juce::jlimit(-30.0, 30.0, start.gainDb + now - k.dragStart); break;
            case CincelIDs::range: b.rangeDb = (float) juce::jlimit(-30.0, 30.0, start.rangeDb + now - k.dragStart); break;
            case CincelIDs::thr: b.thresholdDb = (float) juce::jlimit(-80.0, 0.0, start.thresholdDb + now - k.dragStart); break;
            default: break;
        }
        juce::ignoreUnused(ratio);
        proc.writeBand(i, b);
    }
}

void BandPanel::applyToOthers(CincelIDs::BandKey key, float plainValue)
{
    if (band < 0 || !getSelection)
        return;
    for (int i : getSelection())
    {
        if (i == band)
            continue;
        auto* p = proc.bandParam(i, key);
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(plainValue));
        p->endChangeGesture();
    }
}

void BandPanel::placeKnob(Knob& k, juce::Rectangle<int> cell)
{
    // Rótulo arriba, perilla chica y el valor abajo, todo en la altura de la franja.
    k.label.setBounds(cell.removeFromTop(13));
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, std::max(58, cell.getWidth() - 4), 14);
    k.slider.setBounds(cell);
}

void BandPanel::resized()
{
    // Una sola fila, siempre en el mismo lugar (abajo del gráfico, nunca encima):
    // < BANDA n > | forma pendiente ubicación | FREQ GAIN Q | DYN RANGE THRESH AUTO SC SPEC | SOLO ON X
    auto r = getLocalBounds().reduced(10, 5);
    const int rowH = r.getHeight();
    auto pill = [&](juce::Component& c, juce::Rectangle<int> cell, int w)
    {
        c.setBounds(cell.withSizeKeepingCentre(w, std::min(24, rowH)));
    };

    pill(prevButton, r.removeFromLeft(24), 24);
    title.setBounds(r.removeFromLeft(74));
    pill(nextButton, r.removeFromLeft(24), 24);
    r.removeFromLeft(10);

    auto right = r.removeFromRight(164);
    pill(soloPill, right.removeFromLeft(60), 48);
    pill(onPill, right.removeFromLeft(56), 48);
    pill(deletePill, right.removeFromLeft(40), 32);

    auto boxes = r.removeFromLeft(std::min(300, r.getWidth() / 3));
    const int bw = boxes.getWidth() / 3;
    pill(shapeBox, boxes.removeFromLeft(bw), bw - 4);
    pill(slopeBox, boxes.removeFromLeft(bw), bw - 4);
    pill(placeBox, boxes, bw - 4);
    r.removeFromLeft(8);

    const int cell = std::min(70, r.getWidth() / 8);
    placeKnob(freq, r.removeFromLeft(cell));
    placeKnob(gain, r.removeFromLeft(cell));
    placeKnob(q, r.removeFromLeft(cell));
    r.removeFromLeft(10);
    pill(dynPill, r.removeFromLeft(56), 50);
    placeKnob(range, r.removeFromLeft(cell));
    placeKnob(thr, r.removeFromLeft(cell));
    r.removeFromLeft(6);
    const int pw = std::min(56, r.getWidth() / 3);
    pill(autoPill, r.removeFromLeft(pw), pw - 4);
    pill(scPill, r.removeFromLeft(pw), pw - 4);
    pill(specPill, r.removeFromLeft(pw), pw - 4);
}

void BandPanel::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    const auto col = band >= 0 ? EqDisplay::bandColour(band) : DecimuColours::textMuted;
    g.setColour(col.withAlpha(band >= 0 ? 0.9f : 0.4f));
    g.fillRoundedRectangle(r.removeFromLeft(4.0f).reduced(0.0f, 6.0f), 2.0f);
    if (band < 0)
    {
        g.setColour(DecimuColours::textMuted);
        if (auto* laf = dynamic_cast<DecimuLookAndFeel*>(&getLookAndFeel()))
            g.setFont(laf->monoFont(15.0f));
        g.drawText(juce::String(juce::CharPointer_UTF8(spanish
                       ? "Elegí una banda (clic en un punto) para ver sus controles acá. Doble clic en el gráfico: banda nueva."
                       : "Click a dot to see its controls here. Double-click the graph: new band.")),
                   getLocalBounds().reduced(16, 0), juce::Justification::centredLeft);
    }
}
