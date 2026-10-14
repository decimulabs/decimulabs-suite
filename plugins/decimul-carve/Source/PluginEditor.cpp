#include "PluginEditor.h"
#include "BinaryData.h"

void DecimulCarveEditor::addKnob(Knob& k, const juce::String& paramID, const juce::String& caption)
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

DecimulCarveEditor::DecimulCarveEditor(DecimulCarveProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    display.setMonoFont(lookAndFeel.monoFont(13.0f));
    addAndMakeVisible(display);

    addKnob(amountKnob, ParamIDs::amount, "AMOUNT");
    addKnob(scGainKnob, ParamIDs::scGain, "SC GAIN");
    addKnob(mixKnob, ParamIDs::mix, "MIX");
    addKnob(attackKnob, ParamIDs::attack, "ATTACK");
    addKnob(releaseKnob, ParamIDs::release, "RELEASE");
    addKnob(smoothKnob, ParamIDs::smooth, "SMOOTH");

    addKnob(lowKnob, ParamIDs::lowFreq, "RECORTA DESDE");
    addKnob(highKnob, ParamIDs::highFreq, "RECORTA HASTA");

    hintLabel.setJustificationType(juce::Justification::topLeft);
    hintLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    hintLabel.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    hintLabel.setText("Mandale la pista que molesta (ej. el bombo) al sidechain de Carve "
                      "y ponelo en la pista que tiene que dejar lugar (ej. el bajo). "
                      "SC GAIN sube o baja cuanto 'escucha' del sidechain.",
                      juce::dontSendNotification);
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

    setSize(1000, 560);
    setResizable(true, true);
    setResizeLimits(880, 520, 1700, 1000);
    startTimerHz(30);
}

DecimulCarveEditor::~DecimulCarveEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DecimulCarveEditor::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    DecimuChrome::paintChassisBackground(g, full, 14.0f);
    DecimuChrome::paintRackHandles(g, full);
    DecimuChrome::paintCornerScrews(g, full, 12.0f);

    DecimuChrome::paintCompartment(g, topbarRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, displayRect.toFloat(), false, 8.0f);
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
    g.drawText("CARVE", logoArea, juce::Justification::centredLeft);

    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(13.0f));
    auto footer = getLocalBounds().removeFromBottom(22).reduced(16, 3);
    g.drawText("DECIMULABS - CARVE - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulCarveEditor::resized()
{
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
    knobsRect = content.removeFromRight(340);
    content.removeFromRight(gap);
    displayRect = content;

    display.setBounds(displayRect.reduced(10));

    // ---- Knobs (3 x 2) + rango + ayuda ----
    {
        auto area = knobsRect.reduced(10);
        auto grid = area.removeFromTop(area.getHeight() * 78 / 100);
        const int rowH = grid.getHeight() / 3;
        const int colW = grid.getWidth() / 3;

        auto place = [&](Knob& k, int row, int col)
        {
            auto cell = juce::Rectangle<int>(grid.getX() + col * colW, grid.getY() + row * rowH, colW, rowH).reduced(2);
            k.label.setBounds(cell.removeFromTop(16));
            k.slider.setBounds(cell);
        };
        place(amountKnob, 0, 0);
        place(scGainKnob, 0, 1);
        place(mixKnob, 0, 2);
        place(attackKnob, 1, 0);
        place(releaseKnob, 1, 1);
        place(smoothKnob, 1, 2);

        place(lowKnob, 2, 0);
        place(highKnob, 2, 1);

        area.removeFromTop(4);
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

void DecimulCarveEditor::timerCallback()
{
    std::array<float, carve::numBands> level {}, cut {};
    for (size_t b = 0; b < (size_t) carve::numBands; ++b)
    {
        level[b] = processorRef.uiLevelDb[b].load();
        cut[b] = processorRef.uiCutDb[b].load();
    }
    display.setData(level, cut);
    display.setRange(processorRef.apvts.getRawParameterValue(ParamIDs::lowFreq)->load(),
                     processorRef.apvts.getRawParameterValue(ParamIDs::highFreq)->load());
    display.setSidechainConnected(processorRef.sidechainConnected.load());
    display.repaint();

    const float outDb = processorRef.outputPeakDb.load();
    outputMeter.setLevelDb(outDb);
    outputMeter.repaint();
    vuArc.setLevelDb(outDb);
    vuArc.repaint();
    bypassButton.repaint();
}
