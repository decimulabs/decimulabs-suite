#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    juce::String utf8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }
    float param(DecimulRakeProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue(id)->load(); }
}

// ================================ Rastrillo ================================

void RakeDisplay::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff0a0c10));
    g.fillRoundedRectangle(area, 6.0f);
    auto plot = area.reduced(16.0f, 12.0f);
    auto ruler = plot.removeFromBottom(18.0f);

    const float noteVal = param(proc, ParamIDs::note) + param(proc, ParamIDs::fine) * 0.01f;
    const int chord = (int) param(proc, ParamIDs::chord);
    const float fb = param(proc, ParamIDs::feedback) * 0.01f;
    const float damp = param(proc, ParamIDs::damping) * 0.01f;
    const float depth = param(proc, ParamIDs::lfoDepth) * 0.01f;
    const float lfo = std::sin(proc.lfoPhase.load());
    constexpr double fMax = 2400.0;          // eje lineal de 0 a 2.4 kHz: los dientes quedan parejos, como un peine
    const auto accent = DecimuColours::accentPrimary;

    // Regla de frecuencias
    g.setFont(mono);
    for (double hz : { 250.0, 500.0, 1000.0, 1500.0, 2000.0 })
    {
        const float x = plot.getX() + (float) (hz / fMax) * plot.getWidth();
        g.setColour(DecimuColours::textMuted.withAlpha(0.12f));
        g.drawVerticalLine((int) x, plot.getY(), plot.getBottom());
        g.setColour(DecimuColours::textMuted);
        g.drawText(hz >= 1000.0 ? juce::String(hz / 1000.0, 1) + "k" : juce::String((int) hz),
                   juce::Rectangle<float>(x - 24, ruler.getY(), 48, ruler.getHeight()), juce::Justification::centred);
    }

    const int voices = rake::chordSize(chord);
    for (int v = voices - 1; v >= 0; --v)
    {
        const double baseHz = rake::noteToHz(noteVal + rake::chordInterval(chord, v)) * std::pow(2.0, lfo * depth / 12.0);
        const float headY = plot.getY() + 34.0f + (float) v * 16.0f;
        const juce::Colour col = (fb < 0.0f ? DecimuColours::accentRec : accent)
                                     .withRotatedHue(0.06f * (float) v).withAlpha(v == 0 ? 1.0f : 0.55f);

        // Mango (sólo el rastrillo principal) y cabezal
        if (v == 0)
        {
            const float cx = plot.getCentreX();
            juce::ColourGradient wood(juce::Colour(0xff8a6a44), cx - 5, 0, juce::Colour(0xff5a4128), cx + 5, 0, false);
            g.setGradientFill(wood);
            g.fillRoundedRectangle(cx - 5.0f, area.getY(), 10.0f, headY - area.getY(), 3.0f);
        }
        auto head = juce::Rectangle<float>(plot.getX() - 4.0f, headY - 7.0f, plot.getWidth() + 8.0f, 12.0f);
        juce::ColourGradient metal(juce::Colour(0xffc9ced8).withAlpha(col.getFloatAlpha()), 0, head.getY(),
                                   juce::Colour(0xff5c6270).withAlpha(col.getFloatAlpha()), 0, head.getBottom(), false);
        g.setGradientFill(metal);
        g.fillRoundedRectangle(head, 4.0f);

        // Dientes: con feedback positivo en cada armónico (f, 2f, 3f...); con negativo, en los
        // impares de la mitad (f/2, 3f/2...): el rastrillo se corre y suena hueco.
        const float maxLen = plot.getBottom() - headY - 8.0f;
        const float baseLen = maxLen * (0.2f + 0.8f * std::fabs(fb));
        const float wBase = 3.0f + 7.0f * damp;
        for (int k = 1; k < 400; ++k)
        {
            const double hz = fb >= 0.0f ? baseHz * k : baseHz * (k - 0.5);
            if (hz > fMax) break;
            const float x = plot.getX() + (float) (hz / fMax) * plot.getWidth();
            // DAMPING: los dientes de los agudos (armónicos altos) se acortan y se despuntan.
            const float len = baseLen * std::pow(1.0f - 0.85f * damp, (float) hz / 600.0f);
            const float tip = wBase * damp;          // sin damping, filo de aguja; con damping, punta roma
            juce::Path tooth;
            tooth.startNewSubPath(x - wBase * 0.5f, headY);
            tooth.lineTo(x + wBase * 0.5f, headY);
            tooth.lineTo(x + tip * 0.5f, headY + len);
            tooth.lineTo(x - tip * 0.5f, headY + len);
            tooth.closeSubPath();
            g.setColour(col);
            g.fillPath(tooth);
        }
    }

    // Rótulo: nota, frecuencia y acorde
    g.setFont(mono.withHeight(16.0f));
    g.setColour(DecimuColours::text);
    const juce::String noteName = juce::MidiMessage::getMidiNoteName((int) std::lround(noteVal), true, true, 4);
    g.drawText(noteName + "  " + juce::String(rake::noteToHz(noteVal), 1) + " Hz  -  " + DecimulRakeProcessor::chordNames()[chord]
                   + (fb < 0.0f ? "  -  FEEDBACK NEGATIVO (HUECO)" : ""),
               area.reduced(14.0f, 8.0f).removeFromTop(18.0f).withTrimmedLeft(area.getWidth() * 0.5f + 12.0f), juce::Justification::centredLeft);
}

void RakeDisplay::mouseDown(const juce::MouseEvent&)
{
    dragStartNote = (int) param(proc, ParamIDs::note);
    if (auto* prm = proc.apvts.getParameter(ParamIDs::note)) prm->beginChangeGesture();
}

void RakeDisplay::mouseDrag(const juce::MouseEvent& e)
{
    // Arrastrar a los costados afina: un semitono cada 14 px.
    const int note = juce::jlimit(24, 96, dragStartNote + e.getDistanceFromDragStartX() / 14);
    if (auto* prm = proc.apvts.getParameter(ParamIDs::note))
        prm->setValueNotifyingHost(prm->convertTo0to1((float) note));
}

void RakeDisplay::mouseUp(const juce::MouseEvent&)
{
    if (auto* prm = proc.apvts.getParameter(ParamIDs::note)) prm->endChangeGesture();
}

// ================================ Editor ================================

void DecimulRakeEditor::addKnob(Knob& k, const juce::String& id, const juce::String& caption, const char* tip)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    k.slider.setTooltip(utf8(tip));
    addAndMakeVisible(k.slider);
    k.label.setText(caption, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setFont(juce::Font(juce::FontOptions(11.0f)));
    k.label.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    addAndMakeVisible(k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.apvts, id, k.slider);
}

void DecimulRakeEditor::placeKnob(Knob& k, juce::Rectangle<int> cell)
{
    const auto labelCell = cell;
    cell.removeFromTop(16);
    const int side = std::max(36, std::min(cell.getWidth(), cell.getHeight() - 18));
    const int w = std::max(side, 56);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, w, 16);
    const auto sb = cell.withSizeKeepingCentre(w, side + 18);
    k.slider.setBounds(sb);
    k.label.setBounds(labelCell.getX(), sb.getY() - 16, labelCell.getWidth(), 16);
}

DecimulRakeEditor::DecimulRakeEditor(DecimulRakeProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p), display(p)
{
    setLookAndFeel(&lookAndFeel);
    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    display.setMonoFont(lookAndFeel.monoFont(13.0f));
    display.setTooltip(utf8("El rastrillo: cada diente es una frecuencia que resuena. Arrastrá a los costados para afinarlo."));
    addAndMakeVisible(display);

    addKnob(note, ParamIDs::note, "NOTA", "Nota en la que resuena el peine (y sus armónicos).");
    addKnob(fine, ParamIDs::fine, "FINO", "Ajuste fino de la afinación, en cents.");
    addKnob(feedback, ParamIDs::feedback, "FEEDBACK", "Cuánto resuena. Positivo: tubular. Negativo: hueco, como una octava más abajo.");
    addKnob(damping, ParamIDs::damping, "DAMPING", "Apaga los armónicos agudos: más metálico abajo, más blando arriba.");
    addKnob(lfoRate, ParamIDs::lfoRate, "LFO RATE", "Velocidad del vaivén de la afinación.");
    addKnob(lfoDepth, ParamIDs::lfoDepth, "LFO DEPTH", "Cuánto se mueve la afinación (hasta un semitono): de flanger suave a algo más loco.");
    addKnob(stereo, ParamIDs::stereo, utf8("ESTÉREO"), "Desfase del LFO entre izquierda y derecha: abre la imagen.");
    addKnob(mix, ParamIDs::mix, "MIX", "Cuánto peine se mezcla con la señal original.");
    addKnob(output, ParamIDs::output, "OUTPUT", "Volumen de salida.");

    chord.addItemList(DecimulRakeProcessor::chordNames(), 1);
    chord.setTooltip(utf8("Cuántos peines y en qué acorde: el sonido pasa a 'tocar' esas notas."));
    addAndMakeVisible(chord);
    chordAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts, ParamIDs::chord, chord);
    addAndMakeVisible(meterL);
    addAndMakeVisible(meterR);

    setSize(980, 600);
    setResizable(true, true);
    setResizeLimits(860, 540, 1600, 1000);
    startTimerHz(30);
}

DecimulRakeEditor::~DecimulRakeEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DecimulRakeEditor::resized()
{
    auto content = getLocalBounds().reduced(22, 18);
    content.removeFromBottom(8);
    topbarRect = content.removeFromTop(54);
    content.removeFromTop(8);
    headerLogoArea = topbarRect.reduced(10, 8);
    meterRect = content.removeFromRight(90);
    content.removeFromRight(8);
    knobsRect = content.removeFromBottom(160);
    content.removeFromBottom(8);
    displayRect = content;
    display.setBounds(displayRect.reduced(10));

    auto k = knobsRect.reduced(10);
    auto left = k.removeFromLeft(130);
    left.removeFromTop(30);
    chord.setBounds(left.removeFromTop(30));
    k.removeFromLeft(8);
    const int w = k.getWidth() / 9;
    for (auto* kn : { &note, &fine, &feedback, &damping, &lfoRate, &lfoDepth, &stereo, &mix, &output })
        placeKnob(*kn, k.removeFromLeft(w));

    auto m = meterRect.reduced(14, 12);
    m.removeFromBottom(20);
    meterL.setBounds(m.removeFromLeft(m.getWidth() / 2 - 3));
    m.removeFromLeft(6);
    meterR.setBounds(m);
}

void DecimulRakeEditor::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    DecimuChrome::paintChassisBackground(g, full, 14.0f);
    DecimuChrome::paintRackHandles(g, full);
    DecimuChrome::paintCornerScrews(g, full, 12.0f);
    DecimuChrome::paintCompartment(g, topbarRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, displayRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, knobsRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, meterRect.toFloat(), true, 8.0f);

    auto logo = headerLogoArea;
    if (dogImage.isValid())
    {
        g.drawImage(dogImage, logo.removeFromLeft(logo.getHeight()).toFloat(), juce::RectanglePlacement::centred);
        logo.removeFromLeft(8);
    }
    if (wordmarkImage.isValid())
    {
        g.drawImage(wordmarkImage, logo.removeFromLeft(juce::jmin(logo.getWidth(), 200)).reduced(0, 7).toFloat(),
                    juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
        logo.removeFromLeft(18);
    }
    g.setColour(DecimuColours::accentPrimary);
    g.setFont(lookAndFeel.monoFont(22.0f));
    g.drawText("RAKE", logo, juce::Justification::centredLeft);
    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(15.0f));
    g.drawText("COMB FILTER", logo, juce::Justification::centredRight);
    g.setFont(lookAndFeel.monoFont(14.0f));
    g.drawText("ACORDE", knobsRect.reduced(12, 10).removeFromTop(20), juce::Justification::centredLeft);

    g.setFont(lookAndFeel.monoFont(13.0f));
    g.drawText("L    R", meterRect.reduced(10).removeFromBottom(16), juce::Justification::centred);
    auto footer = getLocalBounds().removeFromBottom(22).reduced(16, 3);
    g.drawText("DECIMULABS - RAKE - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulRakeEditor::timerCallback()
{
    meterL.setLevelDb(processorRef.peakL.load());
    meterR.setLevelDb(processorRef.peakR.load());
    meterL.repaint();
    meterR.repaint();
    display.repaint();
}
