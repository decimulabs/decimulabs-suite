#include "BandPanel.h"
#include "EqDisplay.h"

using prisma::BandParams;

BandPanel::BandPanel(DecimulPrismaProcessor& p, DecimuTooltips& t) : proc(p), tips(t)
{
    for (auto* b : { &prevButton, &nextButton, &soloPill, &deletePill })
        addAndMakeVisible(*b);
    for (auto* b : { &dynPill, &autoPill, &scPill, &specPill, &onPill })
    {
        b->setClickingTogglesState(true);
        addAndMakeVisible(*b);
    }
    soloPill.setClickingTogglesState(false);

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
    freq.slider.onValueChange = [this] { applyRelative(freq, PrismaIDs::freq, true); };
    gain.slider.onValueChange = [this] { applyRelative(gain, PrismaIDs::gain, false); };
    q.slider.onValueChange = [this] { applyRelative(q, PrismaIDs::q, true); };
    range.slider.onValueChange = [this] { applyRelative(range, PrismaIDs::range, false); };
    thr.slider.onValueChange = [this] { applyRelative(thr, PrismaIDs::thr, false); };
    for (auto* k : { &freq, &gain, &q, &range, &thr })
        k->slider.onDragEnd = [this] { relativeStart.clear(); };

    // Lo que el usuario cambia acá se copia a las otras bandas elegidas (no lo que llega de un
    // deshacer, un preset o una automatización: por eso se mira de dónde vino el cambio).
    shapeBox.onChange = [this] { if (shapeBox.consumeUserChange()) applyToOthers(PrismaIDs::shape, (float) shapeBox.getSelectedItemIndex()); };
    slopeBox.onChange = [this] { if (slopeBox.consumeUserChange()) applyToOthers(PrismaIDs::slope, (float) slopeBox.getSelectedItemIndex()); };
    placeBox.onChange = [this] { if (placeBox.consumeUserChange()) applyToOthers(PrismaIDs::place, (float) placeBox.getSelectedItemIndex()); };
    auto pillToOthers = [this](DecimuPillButton& pill, PrismaIDs::BandKey key)
    {
        pill.onClick = [this, &pill, key]
        {
            if (userMouse)
                applyToOthers(key, pill.getToggleState() ? 1.0f : 0.0f);
        };
    };
    pillToOthers(dynPill, PrismaIDs::dyn);
    pillToOthers(autoPill, PrismaIDs::autoThr);
    pillToOthers(scPill, PrismaIDs::sc);
    pillToOthers(specPill, PrismaIDs::spec);
    pillToOthers(onPill, PrismaIDs::on);
    addMouseListener(this, true);

    soloPill.onClick = [this]
    {
        if (band >= 0)
            proc.soloBand.store(proc.soloBand.load() == band ? -1 : band);
    };
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
    tips.add(specPill, "Spectral dynamics: inside the band each frequency is compressed on its own, so a resonance that comes and goes is tamed without touching its neighbours. Adds latency.",
             "Dinámica espectral: dentro de la banda cada frecuencia se comprime por su cuenta, así una resonancia que aparece y desaparece se baja sin tocar a sus vecinas. Suma latencia.");
    tips.add(soloPill, "Solo: listen only to this band's zone. In cuts and notch you hear what is being removed. Tip: hold Alt while dragging a band.",
             "Solo: se escucha solo la zona de esta banda. En cortes y notch se escucha lo que se está sacando. Tip: mantené Alt mientras arrastrás una banda.");
    tips.add(onPill, "Turns the band on or off (also: double-click its dot).", "Prende o apaga la banda (también: doble clic en su punto).");
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
    if (band < 0)
        return;

    auto& s = proc.apvts;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    freq.attach = std::make_unique<SA>(s, PrismaIDs::band(band, PrismaIDs::freq), freq.slider);
    gain.attach = std::make_unique<SA>(s, PrismaIDs::band(band, PrismaIDs::gain), gain.slider);
    q.attach = std::make_unique<SA>(s, PrismaIDs::band(band, PrismaIDs::q), q.slider);
    range.attach = std::make_unique<SA>(s, PrismaIDs::band(band, PrismaIDs::range), range.slider);
    thr.attach = std::make_unique<SA>(s, PrismaIDs::band(band, PrismaIDs::thr), thr.slider);
    shapeAttach = std::make_unique<CA>(s, PrismaIDs::band(band, PrismaIDs::shape), shapeBox);
    slopeAttach = std::make_unique<CA>(s, PrismaIDs::band(band, PrismaIDs::slope), slopeBox);
    placeAttach = std::make_unique<CA>(s, PrismaIDs::band(band, PrismaIDs::place), placeBox);
    dynAttach = std::make_unique<BA>(s, PrismaIDs::band(band, PrismaIDs::dyn), dynPill);
    autoAttach = std::make_unique<BA>(s, PrismaIDs::band(band, PrismaIDs::autoThr), autoPill);
    scAttach = std::make_unique<BA>(s, PrismaIDs::band(band, PrismaIDs::sc), scPill);
    specAttach = std::make_unique<BA>(s, PrismaIDs::band(band, PrismaIDs::spec), specPill);
    onAttach = std::make_unique<BA>(s, PrismaIDs::band(band, PrismaIDs::on), onPill);

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
    const bool hasGain = prisma::shapeHasGain(b.shape);
    gain.slider.setEnabled(hasGain);
    slopeBox.setEnabled(prisma::shapeHasSlope(b.shape));
    q.slider.setEnabled(prisma::shapeHasQ(b.shape));
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

void BandPanel::applyRelative(Knob& k, PrismaIDs::BandKey key, bool ratio)
{
    if (relativeStart.empty() || !k.slider.isMouseButtonDown())
        return;
    const double now = k.slider.getValue();
    for (auto& [i, start] : relativeStart)
    {
        auto b = proc.readBand(i);
        switch (key)
        {
            case PrismaIDs::freq: b.freq = (float) juce::jlimit(10.0, 30000.0, start.freq * now / std::max(1.0e-6, k.dragStart)); break;
            case PrismaIDs::q: b.q = (float) juce::jlimit(0.025, 40.0, start.q * now / std::max(1.0e-6, k.dragStart)); break;
            case PrismaIDs::gain: b.gainDb = (float) juce::jlimit(-30.0, 30.0, start.gainDb + now - k.dragStart); break;
            case PrismaIDs::range: b.rangeDb = (float) juce::jlimit(-30.0, 30.0, start.rangeDb + now - k.dragStart); break;
            case PrismaIDs::thr: b.thresholdDb = (float) juce::jlimit(-80.0, 0.0, start.thresholdDb + now - k.dragStart); break;
            default: break;
        }
        juce::ignoreUnused(ratio);
        proc.writeBand(i, b);
    }
}

void BandPanel::applyToOthers(PrismaIDs::BandKey key, float plainValue)
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
    k.label.setBounds(cell.removeFromTop(14));
    const int side = std::min(cell.getWidth(), cell.getHeight() - 16);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, std::max(60, cell.getWidth()), 15);
    k.slider.setBounds(cell.withSizeKeepingCentre(std::max(side, 60), side + 16));
}

void BandPanel::resized()
{
    auto r = getLocalBounds().reduced(10, 6);
    auto top = r.removeFromTop(24);
    prevButton.setBounds(top.removeFromLeft(24));
    title.setBounds(top.removeFromLeft(78));
    nextButton.setBounds(top.removeFromLeft(24));
    top.removeFromLeft(10);
    deletePill.setBounds(top.removeFromRight(30));
    top.removeFromRight(6);
    onPill.setBounds(top.removeFromRight(44));
    top.removeFromRight(6);
    soloPill.setBounds(top.removeFromRight(56));
    top.removeFromRight(10);
    const int w = top.getWidth() / 3;
    shapeBox.setBounds(top.removeFromLeft(w).reduced(2, 0));
    slopeBox.setBounds(top.removeFromLeft(w).reduced(2, 0));
    placeBox.setBounds(top.reduced(2, 0));

    r.removeFromTop(6);
    const int cell = std::min(76, r.getWidth() / 8);
    placeKnob(freq, r.removeFromLeft(cell));
    placeKnob(gain, r.removeFromLeft(cell));
    placeKnob(q, r.removeFromLeft(cell));
    r.removeFromLeft(14);
    auto dynCol = r.removeFromLeft(56).withSizeKeepingCentre(52, 26);
    dynPill.setBounds(dynCol);
    placeKnob(range, r.removeFromLeft(cell));
    placeKnob(thr, r.removeFromLeft(cell));
    auto pills = r.reduced(4, 6);
    const int ph = std::min(22, pills.getHeight() / 3 - 3);
    autoPill.setBounds(pills.removeFromTop(ph).withWidth(64));
    pills.removeFromTop(3);
    scPill.setBounds(pills.removeFromTop(ph).withWidth(64));
    pills.removeFromTop(3);
    specPill.setBounds(pills.removeFromTop(ph).withWidth(64));
}

void BandPanel::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(juce::Colour(0xf00b0d16));
    g.fillRoundedRectangle(r, 8.0f);
    const auto col = band >= 0 ? EqDisplay::bandColour(band) : DecimuColours::textMuted;
    g.setColour(col.withAlpha(0.7f));
    g.drawRoundedRectangle(r, 8.0f, 1.2f);
    g.fillRoundedRectangle(r.removeFromTop(3.0f).reduced(12.0f, 0.0f), 1.5f);
}
