#include "PluginEditor.h"
#include "BinaryData.h"

void DecimulViseEditor::addKnob(Knob& k, const juce::String& paramID, const juce::String& caption, const juce::String& suffix,
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

void DecimulViseEditor::addPill(DecimuPillButton& pill, std::unique_ptr<ButtonAttachment>& att, const juce::String& paramID,
                                const char* en, const char* es)
{
    pill.setClickingTogglesState(true);
    addAndMakeVisible(pill);
    att = std::make_unique<ButtonAttachment>(processorRef.apvts, paramID, pill);
    tips.add(pill, en, es);
}

// Rotary cuadrado, con un tamaño máximo; el rótulo va pegado al knob.
void DecimulViseEditor::placeKnob(Knob& k, juce::Rectangle<int> cell, int maxSide)
{
    const auto labelCell = cell;
    cell.removeFromTop(18);

    const int side = std::clamp(std::min(cell.getWidth(), cell.getHeight() - 18), 40, maxSide);
    const int width = std::max(side, 64);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, width, 16);
    const auto sliderBounds = cell.withSizeKeepingCentre(width, side + 18);
    k.slider.setBounds(sliderBounds);
    k.label.setBounds(labelCell.getX(), sliderBounds.getY() - 18, labelCell.getWidth(), 18);
}

DecimulViseEditor::DecimulViseEditor(DecimulViseProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    addAndMakeVisible(view);

    addKnob(thresholdKnob, ParamIDs::threshold, "THRESHOLD", " dB",
            "Threshold: the level above which the compressor starts turning the signal down. Watch the screen: the grey level should only cross the yellow line on the loud parts.",
            "Threshold: el nivel a partir del cual el compresor empieza a bajar la señal. Mirá la pantalla: lo gris solo tiene que cruzar la línea amarilla en las partes fuertes.");
    addKnob(ratioKnob, ParamIDs::ratio, "RATIO", " :1",
            "Ratio: how hard it turns down what goes over the threshold. 2:1 is gentle glue, 4:1 is firm, above 10:1 it acts almost like a limiter.",
            "Ratio: qué tan fuerte baja lo que pasa del umbral. 2:1 es pegamento suave, 4:1 es firme, arriba de 10:1 actúa casi como un limitador.");
    addKnob(attackKnob, ParamIDs::attack, "ATTACK", " ms",
            "Attack: how fast it reacts when the signal goes over the threshold. Slow (20-30 ms) lets the punch of drums through; fast (under 3 ms) squashes it.",
            "Attack: qué tan rápido reacciona cuando la señal pasa el umbral. Lento (20-30 ms) deja pasar la pegada de la batería; rápido (menos de 3 ms) la aplasta.");
    addKnob(releaseKnob, ParamIDs::release, "RELEASE", " ms",
            "Release: how fast it lets go once the signal drops. Short is lively and pumping, long is smooth. With AUTO REL on, this knob is ignored.",
            "Release: qué tan rápido suelta cuando la señal baja. Corto es vivo y bombea, largo es suave. Con AUTO REL prendido esta perilla no se usa.");
    addKnob(kneeKnob, ParamIDs::knee, "KNEE", " dB",
            "Knee: how gradually the compression starts around the threshold. 0 is abrupt, 12 dB is very soft and transparent.",
            "Knee: qué tan de a poco empieza la compresión alrededor del umbral. 0 es abrupto, 12 dB es muy suave y transparente.");
    addKnob(makeupKnob, ParamIDs::makeup, "MAKEUP", " dB",
            "Makeup gain: brings the level back up after compressing. With AUTO GAIN on, it is set for you from the threshold and the ratio.",
            "Makeup: devuelve el nivel después de comprimir. Con AUTO GAIN prendido se calcula solo a partir del umbral y el ratio.");
    addKnob(mixKnob, ParamIDs::mix, "MIX", " %",
            "Mix: the amount of compressed signal blended with the original. Under 100 % is parallel compression: it keeps the punch and adds body.",
            "Mix: cuánta señal comprimida se mezcla con la original. Menos de 100 % es compresión paralela: conserva la pegada y suma cuerpo.");
    addKnob(scHpKnob, ParamIDs::scHp, "SC HPF", " Hz",
            "Sidechain high-pass: the compressor stops listening to the lows below this frequency, so a big kick does not pump the whole mix. At the minimum it is off.",
            "Pasa-altos del sidechain: el compresor deja de escuchar los graves por debajo de esta frecuencia, así un bombo grande no hace bombear toda la mezcla. En el mínimo está apagado.");
    addKnob(linkKnob, ParamIDs::link, "LINK", " %",
            "Stereo link: 100 % turns both channels down together (the stereo image stays put); 0 % compresses each channel on its own.",
            "Enlace estéreo: 100 % baja los dos canales juntos (la imagen estéreo no se mueve); 0 % comprime cada canal por su cuenta.");

    addPill(autoRelPill, autoRelAttach, ParamIDs::autoRelease,
            "Auto release: the release adapts to the material, short for small peaks and longer when compressing hard.",
            "Auto release: la caída se adapta al material, corta para picos chicos y más larga cuando comprime fuerte.");
    addPill(autoGainPill, autoGainAttach, ParamIDs::autoMakeup,
            "Auto gain: sets the makeup for you from the threshold and the ratio.",
            "Auto gain: calcula el makeup solo a partir del umbral y el ratio.");
    addPill(rmsPill, rmsAttach, ParamIDs::rms,
            "Detector: PEAK (off) reacts to the loudest instants; RMS (on) follows the average level, which is smoother and more musical on full mixes.",
            "Detector: PEAK (apagado) reacciona a los instantes más fuertes; RMS (prendido) sigue el nivel promedio, más suave y musical en mezclas completas.");

    tips.add(view, "Last few seconds. Red bars hanging from the top: how many dB are being turned down. Grey: the input level. Yellow line: the threshold.",
             "Últimos segundos. Barras rojas que cuelgan de arriba: cuántos dB se están bajando. Gris: el nivel de entrada. Línea amarilla: el umbral.");

    outputSlider.setSliderStyle(juce::Slider::LinearVertical);
    outputSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
    addAndMakeVisible(outputSlider);
    outputLabel.setText("[ OUTPUT ]", juce::dontSendNotification);
    outputLabel.setJustificationType(juce::Justification::centred);
    outputLabel.setColour(juce::Label::textColourId, DecimuColours::text);
    addAndMakeVisible(outputLabel);

    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outputMeter);
    addAndMakeVisible(vuArc);
    addAndMakeVisible(bypassButton);
    addAndMakeVisible(tips.getSwitch());

    outputAttach = std::make_unique<SliderAttachment>(processorRef.apvts, ParamIDs::outputTrim, outputSlider);
    bypassAttach = std::make_unique<ButtonAttachment>(processorRef.apvts, ParamIDs::bypass, bypassButton);
    tips.add(outputSlider, "Output trim, -24 to +12 dB, after everything.", "Ganancia de salida, -24 a +12 dB, después de todo.");
    tips.add(bypassButton, "Bypass: the audio passes through untouched (the dot goes grey).",
             "Bypass: el audio pasa sin tocarse (el punto se pone gris).");

    setSize(1160, 620);
    setResizable(true, true);
    setResizeLimits(980, 560, 1700, 1000);
    startTimerHz(30);
}

DecimulViseEditor::~DecimulViseEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DecimulViseEditor::paint(juce::Graphics& g)
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
    g.drawText("VISE", logoArea, juce::Justification::centredLeft);

    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(13.0f));
    auto footer = getLocalBounds().removeFromBottom(22).reduced(16, 3);
    g.drawText("DECIMULABS - VISE - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulViseEditor::resized()
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
    outputRect = content.removeFromRight(124);
    content.removeFromRight(gap);
    knobsRect = content.removeFromRight(std::clamp(content.getWidth() * 52 / 100, 430, 640));
    content.removeFromRight(gap);
    viewRect = content;

    view.setBounds(viewRect.reduced(8));

    {
        auto area = knobsRect.reduced(12, 10);
        auto pills = area.removeFromBottom(40);
        area.removeFromBottom(6);

        const int rows = 3, cols = 3;
        const int cellW = area.getWidth() / cols, cellH = area.getHeight() / rows;
        Knob* order[] = { &thresholdKnob, &ratioKnob, &attackKnob, &releaseKnob, &kneeKnob, &makeupKnob, &mixKnob, &scHpKnob, &linkKnob };
        for (int i = 0; i < 9; ++i)
        {
            const juce::Rectangle<int> cell(area.getX() + (i % cols) * cellW, area.getY() + (i / cols) * cellH, cellW, cellH);
            placeKnob(*order[i], cell.reduced(3), 78);
        }

        const int pw = pills.getWidth() / 3;
        autoRelPill.setBounds(pills.removeFromLeft(pw).reduced(8, 3));
        autoGainPill.setBounds(pills.removeFromLeft(pw).reduced(8, 3));
        rmsPill.setBounds(pills.reduced(8, 3));
    }

    {
        auto col = outputRect.reduced(10);
        outputLabel.setBounds(col.removeFromBottom(20));
        inputMeter.setBounds(col.removeFromLeft(14));
        col.removeFromLeft(3);
        outputMeter.setBounds(col.removeFromLeft(14));
        col.removeFromLeft(3);
        outputSlider.setBounds(col);
    }
}

void DecimulViseEditor::timerCallback()
{
    DecimulViseProcessor::Frame frames[256];
    float ins[256], grs[256];
    int count;
    while ((count = processorRef.readFrames(frames, 256)) > 0)
    {
        for (int i = 0; i < count; ++i)
        {
            ins[i] = frames[i].inDb;
            grs[i] = frames[i].grDb;
        }
        view.pushFrames(ins, grs, count);
        if (count < 256)
            break;
    }
    view.setThreshold(processorRef.apvts.getRawParameterValue(ParamIDs::threshold)->load());
    view.repaint();

    rmsPill.setButtonText(rmsPill.getToggleState() ? "RMS" : "PEAK");

    inputMeter.setLevelDb(processorRef.inputPeakDb.load());
    inputMeter.repaint();
    const float outDb = processorRef.outputPeakDb.load();
    outputMeter.setLevelDb(outDb);
    outputMeter.repaint();
    vuArc.setLevelDb(outDb);
    vuArc.repaint();
    bypassButton.repaint();
}
