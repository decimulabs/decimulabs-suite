#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    juce::String utf8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }
}

void DecimulFuelleEditor::addKnob(Knob& k, const juce::String& id, const juce::String& caption, const char* tip)
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

void DecimulFuelleEditor::placeKnob(Knob& k, juce::Rectangle<int> cell)
{
    const auto labelCell = cell;
    cell.removeFromTop(16);
    const int side = std::max(40, std::min(cell.getWidth(), cell.getHeight() - 18));
    const int w = std::max(side, 56);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, w, 16);
    const auto sb = cell.withSizeKeepingCentre(w, side + 18);
    k.slider.setBounds(sb);
    k.label.setBounds(labelCell.getX(), sb.getY() - 16, labelCell.getWidth(), 16);
}

DecimulFuelleEditor::DecimulFuelleEditor(DecimulFuelleProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);
    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    addKnob(size, ParamIDs::size, "SIZE", "Tamaño de la sala: más grande, cola más larga.");
    addKnob(damping, ParamIDs::damping, "DAMPING", "Cuánto se apagan los agudos de la cola.");
    addKnob(predelay, ParamIDs::predelay, "PREDELAY", "Espera antes de que arranque la reverb (despega la voz de la sala).");
    addKnob(lowCut, ParamIDs::lowCut, "LOW CUT", "Saca graves de la cola para que no embarre.");
    addKnob(width, ParamIDs::width, "WIDTH", "Ancho de la cola: 0 % mono, 100 % abierta.");
    addKnob(duck, ParamIDs::duck, "DUCK", "Cuánto se agacha la reverb mientras suena la señal (hasta 30 dB).");
    addKnob(threshold, ParamIDs::threshold, "UMBRAL", "Desde qué nivel de la señal se empieza a agachar.");
    addKnob(release, ParamIDs::release, "RELEASE", "Cuánto tarda la reverb en volver (respirar) cuando la señal se calla.");
    addKnob(mix, ParamIDs::mix, "MIX", "Cuánta reverb se suma a la señal.");
    addAndMakeVisible(meterL);
    addAndMakeVisible(meterR);

    setSize(900, 520);
    setResizable(true, true);
    setResizeLimits(780, 460, 1500, 900);
    startTimerHz(30);
}

DecimulFuelleEditor::~DecimulFuelleEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DecimulFuelleEditor::resized()
{
    auto content = getLocalBounds().reduced(22, 18);
    content.removeFromBottom(8);
    topbarRect = content.removeFromTop(54);
    content.removeFromTop(8);
    headerLogoArea = topbarRect.reduced(10, 8);
    meterRect = content.removeFromRight(90);
    content.removeFromRight(8);
    roomRect = content.removeFromTop(content.getHeight() / 2 - 4);
    content.removeFromTop(8);
    duckRect = content;

    auto r = roomRect.reduced(10);
    r.removeFromTop(10);
    const int w = r.getWidth() / 5;
    for (auto* k : { &size, &damping, &predelay, &lowCut, &width })
        placeKnob(*k, r.removeFromLeft(w));

    auto d = duckRect.reduced(10);
    duckBar = d.removeFromBottom(26);
    d.removeFromBottom(6);
    const int w2 = d.getWidth() / 4;
    for (auto* k : { &duck, &threshold, &release, &mix })
        placeKnob(*k, d.removeFromLeft(w2));

    auto m = meterRect.reduced(14, 12);
    m.removeFromBottom(20);
    meterL.setBounds(m.removeFromLeft(m.getWidth() / 2 - 3));
    m.removeFromLeft(6);
    meterR.setBounds(m);
}

void DecimulFuelleEditor::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    DecimuChrome::paintChassisBackground(g, full, 14.0f);
    DecimuChrome::paintRackHandles(g, full);
    DecimuChrome::paintCornerScrews(g, full, 12.0f);
    DecimuChrome::paintCompartment(g, topbarRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, roomRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, duckRect.toFloat(), false, 8.0f);
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
    g.drawText("FUELLE", logo, juce::Justification::centredLeft);
    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(15.0f));
    g.drawText("DUCKING REVERB", logo, juce::Justification::centredRight);

    g.setFont(lookAndFeel.monoFont(14.0f));
    g.drawText("[ SALA ]", roomRect.reduced(12, 6).removeFromTop(16), juce::Justification::centredLeft);
    g.drawText(utf8("[ AGACHADO ]"), duckRect.reduced(12, 6).removeFromTop(16), juce::Justification::centredLeft);

    auto bar = duckBar.toFloat();
    auto label = bar.removeFromLeft(150.0f);
    const float db = juce::Decibels::gainToDecibels(shownDuck, -30.0f);
    g.setColour(DecimuColours::textMuted);
    g.drawText(utf8("AGACHADO ") + juce::String(db, 1) + " dB", label, juce::Justification::centredLeft);
    g.setColour(DecimuColours::trackFill);
    g.fillRoundedRectangle(bar, 3.0f);
    g.setColour(DecimuColours::accentPrimary);
    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, -db / 30.0f)), 3.0f);

    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(13.0f));
    g.drawText("L    R", meterRect.reduced(10).removeFromBottom(16), juce::Justification::centred);
    auto footer = getLocalBounds().removeFromBottom(22).reduced(16, 3);
    g.drawText("DECIMULABS - FUELLE - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulFuelleEditor::timerCallback()
{
    meterL.setLevelDb(processorRef.peakL.load());
    meterR.setLevelDb(processorRef.peakR.load());
    meterL.repaint();
    meterR.repaint();
    const float d = processorRef.duckGain.load();
    if (std::abs(d - shownDuck) > 0.003f)
    {
        shownDuck = d;
        repaint(duckRect);
    }
}
