#include "PluginEditor.h"
#include "BinaryData.h"

void DecimulAtaqueEditor::addKnob(Knob& k, const juce::String& paramID, const juce::String& caption)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    k.slider.setTextValueSuffix(" %");
    addAndMakeVisible(k.slider);

    k.label.setText(caption, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setFont(juce::Font(juce::FontOptions(13.0f)));
    k.label.setColour(juce::Label::textColourId, DecimuColours::text);
    addAndMakeVisible(k.label);

    k.attachment = std::make_unique<SliderAttachment>(processorRef.apvts, paramID, k.slider);
}

// Rotary cuadrado, con un tamaño máximo; el rótulo va pegado al knob.
void DecimulAtaqueEditor::placeKnob(Knob& k, juce::Rectangle<int> cell, int maxSide)
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

DecimulAtaqueEditor::DecimulAtaqueEditor(DecimulAtaqueProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    addAndMakeVisible(view);

    addKnob(attackKnob, ParamIDs::attack, "ATTACK");
    addKnob(sustainKnob, ParamIDs::sustain, "SUSTAIN");
    addKnob(speedKnob, ParamIDs::speed, "SPEED");

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

    tips.add(view, "Last few seconds. Top: the level of the signal. Bottom: the gain being applied, from -12 to +12 dB: up (accent colour) where an attack is boosted or a tail is lengthened, down (red) where it is softened or cut.",
             "Últimos segundos. Arriba: el nivel de la señal. Abajo: la ganancia que se está aplicando, de -12 a +12 dB: hacia arriba (color de acento) donde se acentúa un ataque o se alarga una cola, hacia abajo (rojo) donde se suaviza o se corta.");
    tips.add(attackKnob.slider, "Attack, -100 to +100 %: positive gives each hit more punch, negative rounds it off. -100 % flattens the start of the hit completely. The effect is the same for soft and hard hits.",
             "Attack, -100 a +100 %: positivo le da más pegada a cada golpe, negativo lo redondea. -100 % aplana por completo el comienzo del golpe. El efecto es el mismo con golpes suaves y fuertes.");
    tips.add(sustainKnob.slider, "Sustain, -100 to +100 %: positive lengthens the tail after each hit (more room, more ring), negative dries it out and shortens it. It works best on tails shorter than about half a second.",
             "Sustain, -100 a +100 %: positivo alarga la cola después de cada golpe (más sala, más resonancia), negativo la seca y la acorta. Anda mejor con colas de menos de medio segundo.");
    tips.add(speedKnob.slider, "Speed, 0 to 100 %: how fast it tells attack from tail. Higher reacts faster (short, snappy hits); lower is smoother (bass, slower material). 50 % suits most things.",
             "Speed, 0 a 100 %: qué tan rápido distingue el ataque de la cola. Más alto reacciona más rápido (golpes cortos y secos); más bajo es más suave (bajo, material más lento). 50 % va bien para casi todo.");
    tips.add(outputSlider, "Output trim, -24 to +12 dB.", "Ganancia de salida, -24 a +12 dB.");
    tips.add(bypassButton, "Bypass: the audio passes through untouched (the dot goes grey).",
             "Bypass: el audio pasa sin tocarse (el punto se pone gris).");

    setSize(960, 540);
    setResizable(true, true);
    setResizeLimits(800, 480, 1700, 1000);
    startTimerHz(30);
}

DecimulAtaqueEditor::~DecimulAtaqueEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DecimulAtaqueEditor::paint(juce::Graphics& g)
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
    g.drawText("ATAQUE", logoArea, juce::Justification::centredLeft);

    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(13.0f));
    auto footer = getLocalBounds().removeFromBottom(22).reduced(16, 3);
    g.drawText("DECIMULABS - ATAQUE - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulAtaqueEditor::resized()
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
    knobsRect = content.removeFromRight(std::clamp(content.getWidth() * 40 / 100, 330, 520));
    content.removeFromRight(gap);
    viewRect = content;

    view.setBounds(viewRect.reduced(8));

    {
        auto area = knobsRect.reduced(10);
        auto big = area.removeFromTop(area.getHeight() * 56 / 100);
        const int half = big.getWidth() / 2;
        placeKnob(attackKnob, big.removeFromLeft(half).reduced(2), 160);
        placeKnob(sustainKnob, big.reduced(2), 160);

        area.removeFromTop(6);
        auto small = area.removeFromTop(std::min(area.getHeight() * 55 / 100, 120));
        placeKnob(speedKnob, small.removeFromLeft(std::min(small.getWidth() / 2, 130)).reduced(2), 80);
        small.removeFromLeft(6);
        hintLabel.setBounds(small);
    }

    {
        auto col = outputRect.reduced(10);
        outputLabel.setBounds(col.removeFromBottom(20));
        outputMeter.setBounds(col.removeFromLeft(16));
        outputSlider.setBounds(col);
    }
}

void DecimulAtaqueEditor::timerCallback()
{
    DecimulAtaqueProcessor::Frame frames[256];
    float levels[256], gains[256];
    int count;
    while ((count = processorRef.readFrames(frames, 256)) > 0)
    {
        for (int i = 0; i < count; ++i)
        {
            levels[i] = frames[i].levelDb;
            gains[i] = frames[i].gainDb;
        }
        view.pushFrames(levels, gains, count);
        if (count < 256)
            break;
    }
    view.repaint();

    const bool spanish = tips.getLanguage() == DecimuLang::Lang::es;
    hintLabel.setText(spanish ? "ATTACK +: más pegada. ATTACK -: golpe más suave.\nSUSTAIN +: más cola. SUSTAIN -: más seco.\nSPEED: qué tan rápido detecta."
                              : "ATTACK +: more punch. ATTACK -: softer hit.\nSUSTAIN +: longer tail. SUSTAIN -: drier.\nSPEED: how fast it detects.",
                      juce::dontSendNotification);

    const float outDb = processorRef.outputPeakDb.load();
    outputMeter.setLevelDb(outDb);
    outputMeter.repaint();
    vuArc.setLevelDb(outDb);
    vuArc.repaint();
    bypassButton.repaint();
}
