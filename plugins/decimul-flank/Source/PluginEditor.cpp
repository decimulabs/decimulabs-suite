#include "PluginEditor.h"
#include "BinaryData.h"

void DecimulFlankEditor::setParam(const juce::String& id, float plainValue)
{
    if (auto* param = processorRef.apvts.getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1(plainValue));
}

void DecimulFlankEditor::addKnob(Knob& k, const juce::String& paramID, const juce::String& caption)
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
void DecimulFlankEditor::placeKnob(Knob& k, juce::Rectangle<int> cell)
{
    k.label.setBounds(cell.removeFromTop(16));

    const int side = std::max(40, std::min(cell.getWidth(), cell.getHeight() - 18));
    const int width = std::max(side, 56);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, width, 16);
    k.slider.setBounds(cell.withSizeKeepingCentre(width, side + 18));
}

DecimulFlankEditor::DecimulFlankEditor(DecimulFlankProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    addAndMakeVisible(field);

    addKnob(detuneKnob, ParamIDs::detune, "DETUNE");
    addKnob(delayKnob, ParamIDs::delay, "DELAY");
    addKnob(offsetKnob, ParamIDs::offset, "OFFSET");
    addKnob(spreadKnob, ParamIDs::spread, "SPREAD");
    addKnob(mixKnob, ParamIDs::mix, "MIX");
    addKnob(lowCutKnob, ParamIDs::lowCut, "LOW CUT");
    addKnob(highCutKnob, ParamIDs::highCut, "HIGH CUT");
    addKnob(spaceKnob, ParamIDs::space, "SPACE");

    modeLabel.setText("[ DETUNE MODE ]", juce::dontSendNotification);
    modeLabel.setJustificationType(juce::Justification::centredLeft);
    modeLabel.setColour(juce::Label::textColourId, DecimuColours::text);
    addAndMakeVisible(modeLabel);

    auto wireMode = [this](DecimuPillButton& pill, float value)
    {
        pill.setClickingTogglesState(false);
        pill.onClick = [this, value] { setParam(ParamIDs::mode, value); };
        addAndMakeVisible(pill);
    };
    wireMode(staticPill, 0.0f);
    wireMode(driftPill, 1.0f);
    addAndMakeVisible(monoPill);

    hintLabel.setJustificationType(juce::Justification::topLeft);
    hintLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    hintLabel.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    addAndMakeVisible(hintLabel);

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
    monoAttach = std::make_unique<ButtonAttachment>(processorRef.apvts, ParamIDs::monoCheck, monoPill);

    // ---- Tooltips: inglés por defecto; el botón EN/ES de abajo los pasa a español ----
    addAndMakeVisible(tips.getSwitch());

    tips.add(field, "Stereo field. Top: goniometer of the output (a mono voice is a vertical line, wider is more open). Middle: the voice and its two copies. Bottom: correlation between channels and how much the level drops in mono.",
             "Campo estéreo. Arriba: goniómetro de la salida (una voz mono es una línea vertical, más abierto es más ancho). En el medio: la voz y sus dos copias. Abajo: correlación entre canales y cuánto baja el nivel en mono.");
    tips.add(detuneKnob.slider, "Detune, 0 to 50 cents: how far the copies drift from the original pitch (one flat, one sharp). Around 8 to 15 is the classic doubling range.",
             "Detune, 0 a 50 centésimos: cuánto se desafinan las copias del tono original (una grave, otra aguda). Entre 8 y 15 es el rango clásico de doble.");
    tips.add(delayKnob.slider, "Delay, 3 to 40 ms: how late the copies come after the voice. Short = thickening, long = a clearly separate double.",
             "Delay, 3 a 40 ms: cuánto llegan tarde las copias respecto de la voz. Corto = engrosa, largo = un doble claramente separado.");
    tips.add(offsetKnob.slider, "Offset, 0 to 25 ms: extra delay on the right copy, so the two sides don't arrive together (Haas effect). It separates left from right.",
             "Offset, 0 a 25 ms: retardo extra de la copia derecha, para que los dos lados no lleguen juntos (efecto Haas). Separa izquierda de derecha.");
    tips.add(spreadKnob.slider, "Spread, 0 to 100 %: how far the copies move to the sides. 0 = both at the centre, 100 = one hard left and one hard right. Subtle is usually better.",
             "Spread, 0 a 100 %: cuánto se corren las copias hacia los costados. 0 = las dos al centro, 100 = una a cada lado. Sutil suele andar mejor.");
    tips.add(mixKnob.slider, "Mix, 0 to 100 %: how much of the copies you hear. The dry voice goes down a little as the copies come up, so the overall level stays similar.",
             "Mix, 0 a 100 %: cuánto se escuchan las copias. La voz seca baja un poco a medida que suben las copias, así el nivel total se mantiene parecido.");
    tips.add(lowCutKnob.slider, "Low cut, 20 to 600 Hz: removes the lows from what feeds the copies and the space, so the low end stays solid in the centre.",
             "Low cut, 20 a 600 Hz: saca los graves de lo que alimenta las copias y el ambiente, así el grave queda firme en el centro.");
    tips.add(highCutKnob.slider, "High cut, 2 to 20 kHz: darkens the copies a little so they sit behind the voice and don't double the sibilance.",
             "High cut, 2 a 20 kHz: oscurece un poco las copias para que queden detrás de la voz y no dupliquen las sibilancias.");
    tips.add(spaceKnob.slider, "Space, 0 to 100 %: a short ambience (early reflections) around the voice. Gives air without sounding like a reverb.",
             "Space, 0 a 100 %: un ambiente corto (reflexiones tempranas) alrededor de la voz. Da aire sin sonar a reverb.");
    tips.add(staticPill, "Static: a fixed detune (delay-line pitch shifter). Precise, with the classic micro-pitch sound.",
             "Static: desafinación fija (pitch shifter de línea de retardo). Precisa, con el sonido clásico de micro-pitch.");
    tips.add(driftPill, "Drift: the detune floats around the original pitch, like a double recorded on tape. Smoother and more natural.",
             "Drift: la desafinación va y viene alrededor del tono original, como un doble grabado en cinta. Más suave y natural.");
    tips.add(monoPill, "Mono check: listen to the output summed to mono, to see if the voice thins out or caves in.",
             "Mono check: escuchás la salida sumada a mono, para ver si la voz se adelgaza o se cae.");
    tips.add(outputSlider, "Output trim, -24 to +12 dB.", "Ganancia de salida, -24 a +12 dB.");
    tips.add(bypassButton, "Bypass: the audio passes through untouched (the dot goes grey).",
             "Bypass: el audio pasa sin tocarse (el punto se pone gris).");

    setSize(1000, 560);
    setResizable(true, true);
    setResizeLimits(900, 520, 1700, 1000);
    startTimerHz(30);
}

DecimulFlankEditor::~DecimulFlankEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DecimulFlankEditor::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    DecimuChrome::paintChassisBackground(g, full, 14.0f);
    DecimuChrome::paintRackHandles(g, full);
    DecimuChrome::paintCornerScrews(g, full, 12.0f);

    DecimuChrome::paintCompartment(g, topbarRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, fieldRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, knobsRect.toFloat(), true, 8.0f);
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
    g.drawText("FLANK", logoArea, juce::Justification::centredLeft);

    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(13.0f));
    auto footer = getLocalBounds().removeFromBottom(22).reduced(16, 3);
    g.drawText("DECIMULABS - FLANK - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulFlankEditor::resized()
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

    constexpr int gap = 8;
    outputRect = content.removeFromRight(112);
    content.removeFromRight(gap);
    knobsRect = content.removeFromRight(440);
    content.removeFromRight(gap);
    fieldRect = content;

    field.setBounds(fieldRect.reduced(10));

    // ---- Knobs (4 x 2) + modo + ayuda ----
    {
        auto area = knobsRect.reduced(10);
        auto grid = area.removeFromTop(area.getHeight() * 64 / 100);
        const int rowH = grid.getHeight() / 2;
        const int colW = grid.getWidth() / 4;

        auto place = [&](Knob& k, int row, int col)
        {
            auto cell = juce::Rectangle<int>(grid.getX() + col * colW, grid.getY() + row * rowH, colW, rowH).reduced(2);
            placeKnob(k, cell);
        };
        place(detuneKnob, 0, 0);
        place(delayKnob, 0, 1);
        place(offsetKnob, 0, 2);
        place(spreadKnob, 0, 3);
        place(mixKnob, 1, 0);
        place(lowCutKnob, 1, 1);
        place(highCutKnob, 1, 2);
        place(spaceKnob, 1, 3);

        area.removeFromTop(6);
        modeLabel.setBounds(area.removeFromTop(18));
        auto pills = area.removeFromTop(30);
        const int pw = (pills.getWidth() - 12) / 3;
        staticPill.setBounds(pills.removeFromLeft(pw));
        pills.removeFromLeft(6);
        driftPill.setBounds(pills.removeFromLeft(pw));
        pills.removeFromLeft(6);
        monoPill.setBounds(pills);

        area.removeFromTop(6);
        hintLabel.setBounds(area);
    }

    // ---- Salida ----
    {
        auto col = outputRect.reduced(10);
        outputLabel.setBounds(col.removeFromBottom(20));
        outputMeter.setBounds(col.removeFromLeft(16));
        outputSlider.setBounds(col);
    }
}

void DecimulFlankEditor::timerCallback()
{
    auto& apvts = processorRef.apvts;
    auto get = [&](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };

    // Goniómetro: los pares nuevos desde la última vez
    float pairs[512 * 2];
    int count;
    while ((count = processorRef.readScope(pairs, 512)) > 0)
    {
        field.pushPoints(pairs, count);
        if (count < 512)
            break;
    }

    FlankStereoField::State s;
    s.spread = get(ParamIDs::spread) * 0.01f;
    s.offsetMs = get(ParamIDs::offset);
    s.space = get(ParamIDs::space) * 0.01f;
    s.correlation = processorRef.correlation.load();
    s.monoLossDb = processorRef.monoLossDb.load();
    s.monoCheck = get(ParamIDs::monoCheck) > 0.5f;
    field.setState(s);
    field.repaint();

    const int mode = (int) std::lround(get(ParamIDs::mode));
    staticPill.setToggleState(mode == 0, juce::dontSendNotification);
    driftPill.setToggleState(mode == 1, juce::dontSendNotification);

    const bool spanish = tips.getLanguage() == DecimuLang::Lang::es;
    juce::String hint;
    if (mode == 0)
        hint = spanish ? "STATIC: la desafinación es fija. Una copia queda grave y la otra aguda, siempre lo mismo."
                       : "STATIC: the detune is fixed. One copy stays flat and the other sharp, always the same.";
    else
        hint = spanish ? "DRIFT: la desafinación va y viene alrededor del tono, como un doble de cinta. DETUNE es el desvío máximo."
                       : "DRIFT: the detune floats around the pitch, like a tape double. DETUNE is the maximum deviation.";
    hint += spanish ? "\nCuidá MONO: si la correlación baja de 0.2 la voz se adelgaza al sumar."
                    : "\nWatch MONO: if the correlation drops below 0.2 the voice thins out when summed.";
    hintLabel.setText(hint, juce::dontSendNotification);

    const float outDb = processorRef.outputPeakDb.load();
    outputMeter.setLevelDb(outDb);
    outputMeter.repaint();
    vuArc.setLevelDb(outDb);
    vuArc.repaint();
    bypassButton.repaint();
}
