#include "PluginEditor.h"
#include "BinaryData.h"

void DecimulEmeryEditor::addKnob(Knob& k, const juce::String& paramID, const juce::String& caption, const juce::String& suffix)
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
}

void DecimulEmeryEditor::addColumn(Column& c, const juce::String& on, const juce::String& freq, const juce::String& thresh,
                                   const juce::String& amount, const juce::String& wide, const juce::String& listen,
                                   const juce::String& freqCaption, const juce::String& freqSuffix)
{
    addKnob(c.freq, freq, freqCaption, freqSuffix);
    addKnob(c.thresh, thresh, "THRESHOLD", " dB");
    addKnob(c.amount, amount, "AMOUNT", " dB");

    for (auto* pill : { &c.onPill, &c.widePill, &c.listenPill })
    {
        pill->setClickingTogglesState(true);
        addAndMakeVisible(*pill);
    }
    c.onAttach = std::make_unique<ButtonAttachment>(processorRef.apvts, on, c.onPill);
    c.wideAttach = std::make_unique<ButtonAttachment>(processorRef.apvts, wide, c.widePill);
    c.listenAttach = std::make_unique<ButtonAttachment>(processorRef.apvts, listen, c.listenPill);
}

// Rotary cuadrado, con un tamaño máximo; el rótulo va pegado al knob.
void DecimulEmeryEditor::placeKnob(Knob& k, juce::Rectangle<int> cell, int maxSide)
{
    const auto labelCell = cell;
    cell.removeFromTop(18);

    const int side = std::clamp(std::min(cell.getWidth(), cell.getHeight() - 18), 40, maxSide);
    const int width = std::max(side, 60);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, width, 16);
    const auto sliderBounds = cell.withSizeKeepingCentre(width, side + 18);
    k.slider.setBounds(sliderBounds);
    k.label.setBounds(labelCell.getX(), sliderBounds.getY() - 18, labelCell.getWidth(), 18);
}

void DecimulEmeryEditor::layoutColumn(Column& c)
{
    auto area = c.bounds.reduced(10, 8);
    area.removeFromTop(26);                                   // título de la sección
    auto row1 = area.removeFromTop(area.getHeight() * 52 / 100);
    const int half = row1.getWidth() / 2;
    placeKnob(c.freq, row1.removeFromLeft(half).reduced(2), 120);
    placeKnob(c.thresh, row1.reduced(2), 120);

    auto row2 = area;
    auto knobCell = row2.removeFromLeft(row2.getWidth() / 2).reduced(2);
    placeKnob(c.amount, knobCell, 120);

    auto pills = row2.reduced(6, 10);
    const int h = std::min(34, pills.getHeight() / 3 - 6);
    c.onPill.setBounds(pills.removeFromTop(h));
    pills.removeFromTop(8);
    c.widePill.setBounds(pills.removeFromTop(h));
    pills.removeFromTop(8);
    c.listenPill.setBounds(pills.removeFromTop(h));
}

DecimulEmeryEditor::DecimulEmeryEditor(DecimulEmeryProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    addAndMakeVisible(view);

    addColumn(essCol, ParamIDs::essOn, ParamIDs::essFreq, ParamIDs::essThresh, ParamIDs::essAmount,
              ParamIDs::essWide, ParamIDs::essListen, "FREQ", " Hz");
    addColumn(popCol, ParamIDs::popOn, ParamIDs::popFreq, ParamIDs::popThresh, ParamIDs::popAmount,
              ParamIDs::popWide, ParamIDs::popListen, "CUTOFF", " Hz");

    for (auto* title : { &essTitle, &popTitle })
    {
        title->setJustificationType(juce::Justification::centredLeft);
        title->setFont(lookAndFeel.monoFont(15.0f));
        title->setColour(juce::Label::textColourId, DecimuColours::accentPrimary);
        addAndMakeVisible(*title);
    }

    hintLabel.setJustificationType(juce::Justification::topLeft);
    hintLabel.setFont(juce::Font(juce::FontOptions(11.5f)));
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

    // ---- Tooltips: inglés por defecto; el botón EN/ES de abajo los pasa a español ----
    addAndMakeVisible(tips.getSwitch());

    tips.add(view, "Last few seconds of each section. Grey: the level of the band the detector watches. Accent line: the threshold. Red bars hanging from the top: how many dB are being turned down right now. If the red never shows, lower the threshold.",
             "Últimos segundos de cada sección. Gris: el nivel de la banda que vigila el detector. Línea de color: el umbral. Barras rojas que cuelgan de arriba: cuántos dB se están bajando en ese momento. Si el rojo nunca aparece, bajá el umbral.");

    tips.add(essCol.freq.slider, "Sibilance frequency, 2 to 12 kHz: the centre of the band where the harsh 's' lives. Turn on LISTEN and sweep it until you hear the hissy part; usually 5 to 8 kHz.",
             "Frecuencia de la sibilancia, 2 a 12 kHz: el centro de la banda donde vive la ese áspera. Prendé LISTEN y barré hasta oír la parte que silba; suele estar entre 5 y 8 kHz.");
    tips.add(essCol.thresh.slider, "Sibilance threshold: the level of the band above which it gets turned down. Lower = acts on more. Watch the screen: the grey should only cross the line on the harsh 's'.",
             "Umbral de la sibilancia: el nivel de la banda a partir del cual se baja. Más bajo = actúa más. Mirá la pantalla: lo gris solo tiene que cruzar la línea en las eses ásperas.");
    tips.add(essCol.amount.slider, "Sibilance amount: the most it will ever turn down, in dB. 6 to 10 dB is natural; more starts to lisp.",
             "Cantidad de la sibilancia: lo máximo que va a bajar, en dB. De 6 a 10 dB suena natural; más empieza a ceceo.");
    tips.add(essCol.onPill, "Turns the sibilance section on or off.", "Prende o apaga la sección de sibilancia.");
    tips.add(essCol.widePill, "BAND (off): only the 's' band is turned down, the rest of the voice is untouched. WIDE (on): the whole sound is turned down while an 's' passes; more drastic, useful when the harshness is spread out.",
             "BAND (apagado): se baja solo la banda de la ese y el resto de la voz queda intacto. WIDE (prendido): se baja todo el sonido mientras pasa una ese; es más drástico, sirve cuando la aspereza está repartida.");
    tips.add(essCol.listenPill, "Listen: you hear only what is being removed by the sibilance section. Use it to find the right frequency and to check you are not taking too much.",
             "Listen: se oye solo lo que está quitando la sección de sibilancia. Sirve para encontrar la frecuencia y comprobar que no estás sacando de más.");

    tips.add(popCol.freq.slider, "Pop cutoff, 40 to 300 Hz: the band below this frequency is what the detector watches for the thump of a 'p' or 'b'. 100 to 150 Hz fits most voices; use LISTEN to check.",
             "Corte del golpe, 40 a 300 Hz: el detector vigila lo que está por debajo de esta frecuencia, que es donde pega el aire de una p o una b. De 100 a 150 Hz va bien para casi todas las voces; verificá con LISTEN.");
    tips.add(popCol.thresh.slider, "Pops threshold: the level of the low band above which it gets turned down. A normal voice stays under the line; only the thump crosses it.",
             "Umbral de los golpes: el nivel de la banda grave a partir del cual se baja. Una voz normal queda por debajo de la línea; solo el golpe la cruza.");
    tips.add(popCol.amount.slider, "Pops amount: the most it will turn the thump down, in dB. It cannot fix a pop that overloaded the microphone, but works well on ordinary ones.",
             "Cantidad de los golpes: lo máximo que baja el golpe, en dB. No arregla un golpe que saturó el micrófono, pero anda bien con los normales.");
    tips.add(popCol.onPill, "Turns the pops section on or off.", "Prende o apaga la sección de golpes.");
    tips.add(popCol.widePill, "BAND (off): only the low band is turned down. WIDE (on): the whole sound is turned down when a pop hits.",
             "BAND (apagado): se baja solo la banda grave. WIDE (prendido): se baja todo el sonido cuando llega un golpe.");
    tips.add(popCol.listenPill, "Listen: you hear only what the pops section is removing.",
             "Listen: se oye solo lo que está quitando la sección de golpes.");

    tips.add(outputSlider, "Output trim, -24 to +12 dB.", "Ganancia de salida, -24 a +12 dB.");
    tips.add(bypassButton, "Bypass: the audio passes through untouched (the dot goes grey).",
             "Bypass: el audio pasa sin tocarse (el punto se pone gris).");

    setSize(1000, 580);
    setResizable(true, true);
    setResizeLimits(860, 520, 1700, 1000);
    startTimerHz(30);
}

DecimulEmeryEditor::~DecimulEmeryEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DecimulEmeryEditor::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    DecimuChrome::paintChassisBackground(g, full, 14.0f);
    DecimuChrome::paintRackHandles(g, full);
    DecimuChrome::paintCornerScrews(g, full, 12.0f);

    DecimuChrome::paintCompartment(g, topbarRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, viewRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, essCol.bounds.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, popCol.bounds.toFloat(), true, 8.0f);
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
    g.drawText("EMERY", logoArea, juce::Justification::centredLeft);

    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(13.0f));
    auto footer = getLocalBounds().removeFromBottom(22).reduced(16, 3);
    g.drawText("DECIMULABS - EMERY - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulEmeryEditor::resized()
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

    const int colsWidth = std::clamp(content.getWidth() * 62 / 100, 520, 760);
    auto cols = content.removeFromRight(colsWidth);
    content.removeFromRight(gap);
    popCol.bounds = cols.removeFromRight(cols.getWidth() / 2);
    cols.removeFromRight(gap / 2);
    essCol.bounds = cols;
    popCol.bounds.removeFromLeft(gap / 2);
    viewRect = content;

    view.setBounds(viewRect.reduced(8));
    layoutColumn(essCol);
    layoutColumn(popCol);

    essTitle.setBounds(essCol.bounds.getX() + 14, essCol.bounds.getY() + 8, essCol.bounds.getWidth() - 28, 22);
    popTitle.setBounds(popCol.bounds.getX() + 14, popCol.bounds.getY() + 8, popCol.bounds.getWidth() - 28, 22);
    hintLabel.setBounds({});

    {
        auto col = outputRect.reduced(10);
        outputLabel.setBounds(col.removeFromBottom(20));
        outputMeter.setBounds(col.removeFromLeft(16));
        outputSlider.setBounds(col);
    }
}

void DecimulEmeryEditor::timerCallback()
{
    const bool spanish = tips.getLanguage() == DecimuLang::Lang::es;
    const juce::String essName = spanish ? "SIBILANCIA (eses)" : "SIBILANCE (esses)";
    const juce::String popName = spanish ? "GOLPES (p y b)" : "POPS (p and b)";
    essTitle.setText(essName, juce::dontSendNotification);
    popTitle.setText(popName, juce::dontSendNotification);
    view.setTitles(essName, popName);

    auto get = [this](const juce::String& id) { return processorRef.apvts.getRawParameterValue(id)->load(); };
    view.setSection(false, get(ParamIDs::essThresh), get(ParamIDs::essAmount), get(ParamIDs::essOn) > 0.5f);
    view.setSection(true, get(ParamIDs::popThresh), get(ParamIDs::popAmount), get(ParamIDs::popOn) > 0.5f);

    DecimulEmeryProcessor::Frame frames[256];
    float el[256], er[256], pl[256], pr[256];
    int count;
    while ((count = processorRef.readFrames(frames, 256)) > 0)
    {
        for (int i = 0; i < count; ++i)
        {
            el[i] = frames[i].essBandDb;  er[i] = frames[i].essReductionDb;
            pl[i] = frames[i].popBandDb;  pr[i] = frames[i].popReductionDb;
        }
        view.pushFrames(el, er, pl, pr, count);
        if (count < 256)
            break;
    }
    view.repaint();

    const float outDb = processorRef.outputPeakDb.load();
    outputMeter.setLevelDb(outDb);
    outputMeter.repaint();
    vuArc.setLevelDb(outDb);
    vuArc.repaint();
    bypassButton.repaint();
}
