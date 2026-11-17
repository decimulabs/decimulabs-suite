#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    juce::String utf8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }
}

void DecimulEstelaEditor::addKnob(Knob& k, const juce::String& id, const juce::String& caption, const char* tip)
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

void DecimulEstelaEditor::placeKnob(Knob& k, juce::Rectangle<int> cell)
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

DecimulEstelaEditor::DecimulEstelaEditor(DecimulEstelaProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);
    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    addKnob(time, ParamIDs::timeMs, "TIEMPO (ms)", "Tiempo del eco en milisegundos (cuando SYNC está apagado).");
    addKnob(feedback, ParamIDs::feedback, "FEEDBACK", "Cuántas repeticiones: más alto, más ecos.");
    addKnob(lowCut, ParamIDs::lowCut, "LOW CUT", "Saca graves de los ecos (cada vuelta un poco más).");
    addKnob(highCut, ParamIDs::highCut, "HIGH CUT", "Oscurece los ecos (cada vuelta un poco más).");
    addKnob(wobble, ParamIDs::wobble, "WOBBLE", "Temblor de cinta: los ecos se desafinan apenas, como un delay de cinta.");
    addKnob(duck, ParamIDs::duck, "DUCK", "Cuánto se agachan los ecos mientras suena la señal (hasta 30 dB).");
    addKnob(threshold, ParamIDs::threshold, "UMBRAL", "Desde qué nivel de la señal se empiezan a agachar los ecos.");
    addKnob(release, ParamIDs::release, "RELEASE", "Cuánto tardan los ecos en volver cuando la señal se calla.");
    addKnob(mix, ParamIDs::mix, "MIX", "Cuánto eco se suma a la señal.");

    division.addItemList(DecimulEstelaProcessor::divisionNames(), 1);
    division.setTooltip(utf8("Figura del eco cuando SYNC está prendido (sigue el tempo del DAW)."));
    addAndMakeVisible(division);
    divisionAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts, ParamIDs::division, division);
    syncPill.setClickingTogglesState(true);
    pingPill.setClickingTogglesState(true);
    syncPill.setTooltip(utf8("Tiempo sincronizado al tempo del DAW (figuras) o libre en ms."));
    pingPill.setTooltip(utf8("Los ecos van saltando de izquierda a derecha."));
    addAndMakeVisible(syncPill);
    addAndMakeVisible(pingPill);
    syncAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ParamIDs::sync, syncPill);
    pingAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ParamIDs::pingPong, pingPill);
    addAndMakeVisible(meterL);
    addAndMakeVisible(meterR);

    setSize(900, 520);
    setResizable(true, true);
    setResizeLimits(780, 460, 1500, 900);
    startTimerHz(30);
}

DecimulEstelaEditor::~DecimulEstelaEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DecimulEstelaEditor::resized()
{
    auto content = getLocalBounds().reduced(22, 18);
    content.removeFromBottom(8);
    topbarRect = content.removeFromTop(54);
    content.removeFromTop(8);
    headerLogoArea = topbarRect.reduced(10, 8);
    meterRect = content.removeFromRight(90);
    content.removeFromRight(8);
    echoRect = content.removeFromTop(content.getHeight() / 2 - 4);
    content.removeFromTop(8);
    duckRect = content;

    auto e = echoRect.reduced(10);
    auto left = e.removeFromLeft(130);
    left.removeFromTop(18);
    syncPill.setBounds(left.removeFromTop(30));
    left.removeFromTop(8);
    division.setBounds(left.removeFromTop(28));
    left.removeFromTop(8);
    pingPill.setBounds(left.removeFromTop(30));
    e.removeFromLeft(10);
    const int w = e.getWidth() / 5;
    for (auto* k : { &time, &feedback, &lowCut, &highCut, &wobble })
        placeKnob(*k, e.removeFromLeft(w));

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

void DecimulEstelaEditor::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    DecimuChrome::paintChassisBackground(g, full, 14.0f);
    DecimuChrome::paintRackHandles(g, full);
    DecimuChrome::paintCornerScrews(g, full, 12.0f);
    DecimuChrome::paintCompartment(g, topbarRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, echoRect.toFloat(), true, 8.0f);
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
    g.drawText("ESTELA", logo, juce::Justification::centredLeft);
    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(15.0f));
    g.drawText("DUCKING DELAY  -  " + juce::String(shownTime, 0) + " ms", logo, juce::Justification::centredRight);

    g.setFont(lookAndFeel.monoFont(14.0f));
    g.drawText("[ ECO ]", echoRect.reduced(12, 6).removeFromTop(16), juce::Justification::centredLeft);
    g.drawText(utf8("[ AGACHADO ]"), duckRect.reduced(12, 6).removeFromTop(16), juce::Justification::centredLeft);

    // Cuánto se están agachando los ecos ahora
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
    g.drawText("DECIMULABS - ESTELA - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulEstelaEditor::timerCallback()
{
    meterL.setLevelDb(processorRef.peakL.load());
    meterR.setLevelDb(processorRef.peakR.load());
    meterL.repaint();
    meterR.repaint();
    const bool sync = processorRef.apvts.getRawParameterValue(ParamIDs::sync)->load() > 0.5f;
    time.slider.setAlpha(sync ? 0.4f : 1.0f);
    division.setAlpha(sync ? 1.0f : 0.4f);
    const float d = processorRef.duckGain.load(), t = processorRef.uiTimeMs.load();
    if (std::abs(d - shownDuck) > 0.003f || std::abs(t - shownTime) > 0.5f)
    {
        shownDuck = d;
        shownTime = t;
        repaint();
    }
}
