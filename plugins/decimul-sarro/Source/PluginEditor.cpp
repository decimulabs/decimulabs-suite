#include "PluginEditor.h"
#include "BinaryData.h"

void DecimulSarroEditor::addKnob(Knob& k, const juce::String& paramID, const juce::String& caption, const juce::String& suffix,
                                 const char* en, const char* es)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    k.slider.setTextValueSuffix(suffix);
    addAndMakeVisible(k.slider);

    k.label.setText(caption, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setFont(juce::Font(juce::FontOptions(13.0f)));
    k.label.setColour(juce::Label::textColourId, DecimuColours::text);
    addAndMakeVisible(k.label);

    k.attachment = std::make_unique<SliderAttachment>(processorRef.apvts, paramID, k.slider);
    tips.add(k.slider, en, es);
}

void DecimulSarroEditor::placeKnob(Knob& k, juce::Rectangle<int> cell, int maxSide)
{
    const auto labelCell = cell;
    cell.removeFromTop(18);

    const int side = std::clamp(std::min(cell.getWidth(), cell.getHeight() - 18), 36, maxSide);
    const int width = std::max(side, 64);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, width, 16);
    const auto sliderBounds = cell.withSizeKeepingCentre(width, side + 18);
    k.slider.setBounds(sliderBounds);
    k.label.setBounds(labelCell.getX(), sliderBounds.getY() - 18, labelCell.getWidth(), 18);
}

DecimulSarroEditor::DecimulSarroEditor(DecimulSarroProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    addAndMakeVisible(view);
    tips.add(view, "The spectrum of the input (grey), what is left after the peaks are turned down (coloured line) and, hanging in red from the top, how many dB are being removed at each frequency. The darker sides are outside the focus range.",
             "El espectro de la entrada (gris), lo que queda después de bajar los picos (línea de color) y, colgando en rojo desde arriba, cuántos dB se están sacando en cada frecuencia. Los costados más oscuros están fuera de la zona de trabajo.");

    addKnob(depthKnob, ParamIDs::depth, "DEPTH", " %",
            "Depth: how much of each resonance is removed. 100 % flattens the peak down to the level around it; 50 % removes about half of it. Start low and listen with DELTA.",
            "Depth: cuánto de cada resonancia se saca. 100 % aplana el pico hasta el nivel de su alrededor; 50 % saca más o menos la mitad. Empezá bajo y escuchá con DELTA.");
    addKnob(sharpnessKnob, ParamIDs::sharpness, "SHARPNESS", " %",
            "Sharpness: how narrow a peak has to be to be treated. High only catches thin, ringing peaks and leaves broad tonal shape alone; low also treats wider bumps.",
            "Sharpness: qué tan angosto tiene que ser un pico para tratarlo. Alto solo agarra los picos finos que suenan a ring y deja en paz la forma tonal ancha; bajo trata también bultos más anchos.");
    addKnob(sensitivityKnob, ParamIDs::sensitivity, "SENSITIVITY", " %",
            "Sensitivity: how much a peak has to stand out from its surroundings to be treated. High catches small peaks too (and may touch the natural sound); low only treats the obvious ones.",
            "Sensitivity: cuánto tiene que sobresalir un pico de su alrededor para tratarlo. Alto agarra también los chicos (y puede tocar el sonido natural); bajo solo trata los evidentes.");
    addKnob(attackKnob, ParamIDs::attack, "ATTACK", " ms",
            "Attack: how fast it starts turning a peak down when it shows up. Fast catches short whistles; slow lets the start of notes through.",
            "Attack: qué tan rápido empieza a bajar un pico cuando aparece. Rápido agarra silbidos cortos; lento deja pasar el comienzo de las notas.");
    addKnob(releaseKnob, ParamIDs::release, "RELEASE", " ms",
            "Release: how fast it lets go once the peak is gone. Short is lively, long is smooth.",
            "Release: qué tan rápido suelta cuando el pico se va. Corto es vivo, largo es suave.");
    addKnob(lowKnob, ParamIDs::lowFocus, "LOW FOCUS", " Hz",
            "Low focus: it does not work below this frequency. Raise it to leave the low end alone.",
            "Low focus: no actúa por debajo de esta frecuencia. Subila para dejar los graves en paz.");
    addKnob(highKnob, ParamIDs::highFocus, "HIGH FOCUS", " Hz",
            "High focus: it does not work above this frequency. Lower it to leave the air and the top end alone.",
            "High focus: no actúa por encima de esta frecuencia. Bajala para dejar el aire y los agudos en paz.");
    addKnob(linkKnob, ParamIDs::link, "LINK", " %",
            "Stereo link: 100 % applies the same reduction to both channels, so the stereo image stays put; 0 % treats each channel on its own.",
            "Enlace estéreo: 100 % aplica la misma reducción a los dos canales, así la imagen estéreo no se mueve; 0 % trata cada canal por su cuenta.");
    addKnob(mixKnob, ParamIDs::mix, "MIX", " %",
            "Mix: blend between the original and the processed sound. The original is delayed by the same latency, so they line up.",
            "Mix: mezcla entre el sonido original y el procesado. El original se retrasa lo mismo que el procesamiento, así que quedan alineados.");

    deltaPill.setClickingTogglesState(true);
    addAndMakeVisible(deltaPill);
    deltaAttach = std::make_unique<ButtonAttachment>(processorRef.apvts, ParamIDs::delta, deltaPill);
    tips.add(deltaPill, "Delta: you hear only what is being removed. Use it to check that you are taking out the harshness and not the body of the sound.",
             "Delta: se oye solo lo que se está sacando. Sirve para comprobar que sacás la aspereza y no el cuerpo del sonido.");

    trimSlider.setSliderStyle(juce::Slider::LinearVertical);
    trimSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
    addAndMakeVisible(trimSlider);
    outputLabel.setText("[ TRIM ]", juce::dontSendNotification);
    outputLabel.setJustificationType(juce::Justification::centred);
    outputLabel.setColour(juce::Label::textColourId, DecimuColours::text);
    addAndMakeVisible(outputLabel);

    latencyLabel.setJustificationType(juce::Justification::centredRight);
    latencyLabel.setFont(lookAndFeel.monoFont(13.0f));
    latencyLabel.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    addAndMakeVisible(latencyLabel);
    tips.add(latencyLabel, "Latency reported to the DAW: this plugin analyses the sound in blocks, so it delays the signal by this much. The host compensates it.",
             "Latencia que se le informa al DAW: este plugin analiza el sonido en bloques, así que retrasa la señal esto. El host la compensa.");

    addAndMakeVisible(outputMeter);
    addAndMakeVisible(vuArc);
    addAndMakeVisible(bypassButton);
    addAndMakeVisible(tips.getSwitch());

    trimAttach = std::make_unique<SliderAttachment>(processorRef.apvts, ParamIDs::trim, trimSlider);
    bypassAttach = std::make_unique<ButtonAttachment>(processorRef.apvts, ParamIDs::bypass, bypassButton);
    tips.add(trimSlider, "Output trim, -24 to +12 dB.", "Ganancia de salida, -24 a +12 dB.");
    tips.add(bypassButton, "Bypass: you hear the original (with the same delay, so switching is seamless).",
             "Bypass: se oye el original (con el mismo retardo, así que el cambio no se nota).");

    setSize(1100, 640);
    setResizable(true, true);
    setResizeLimits(940, 580, 1700, 1000);
    startTimerHz(30);
}

DecimulSarroEditor::~DecimulSarroEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DecimulSarroEditor::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    DecimuChrome::paintChassisBackground(g, full, 14.0f);
    DecimuChrome::paintRackHandles(g, full);
    DecimuChrome::paintCornerScrews(g, full, 12.0f);

    DecimuChrome::paintCompartment(g, topbarRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, viewRect.toFloat(), false, 8.0f);
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
    g.drawText("SARRO", logoArea, juce::Justification::centredLeft);

    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(13.0f));
    auto footer = getLocalBounds().removeFromBottom(22).reduced(16, 3);
    g.drawText("DECIMULABS - SARRO - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulSarroEditor::resized()
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
    latencyLabel.setBounds(header.removeFromRight(200));
    headerLogoArea = header;

    constexpr int gap = 8;
    outputRect = content.removeFromRight(112);
    content.removeFromRight(gap);
    knobsRect = content.removeFromBottom(std::clamp(content.getHeight() * 36 / 100, 190, 260));
    content.removeFromBottom(gap);
    viewRect = content;
    view.setBounds(viewRect.reduced(8));

    {
        auto area = knobsRect.reduced(12, 8);
        auto pills = area.removeFromRight(110);
        const int cols = 9;
        const int cellW = area.getWidth() / cols;
        Knob* order[] = { &depthKnob, &sharpnessKnob, &sensitivityKnob, &attackKnob, &releaseKnob, &lowKnob, &highKnob, &linkKnob, &mixKnob };
        for (int i = 0; i < cols; ++i)
            placeKnob(*order[i], juce::Rectangle<int>(area.getX() + i * cellW, area.getY(), cellW, area.getHeight()).reduced(2), i < 3 ? 84 : 62);
        deltaPill.setBounds(pills.withSizeKeepingCentre(pills.getWidth() - 12, 40));
    }

    {
        auto col = outputRect.reduced(10);
        outputLabel.setBounds(col.removeFromBottom(20));
        outputMeter.setBounds(col.removeFromLeft(16));
        trimSlider.setBounds(col);
    }
}

void DecimulSarroEditor::timerCallback()
{
    float spec[SpectrumView::kPoints], red[SpectrumView::kPoints];
    processorRef.getDisplay(spec, red);
    view.update(spec, red);
    auto get = [this](const juce::String& id) { return processorRef.apvts.getRawParameterValue(id)->load(); };
    view.setFocus(get(ParamIDs::lowFocus), get(ParamIDs::highFocus));
    view.repaint();

    const double ms = 1000.0 * processorRef.getLatencyForUi() / processorRef.getSampleRateForUi();
    latencyLabel.setText("LATENCIA " + juce::String(ms, 1) + " ms", juce::dontSendNotification);

    const float outDb = processorRef.outputPeakDb.load();
    outputMeter.setLevelDb(outDb);
    outputMeter.repaint();
    vuArc.setLevelDb(outDb);
    vuArc.repaint();
    bypassButton.repaint();
}
