#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    juce::String utf8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }
}

void DecimulNavajaEditor::addKnob(Knob& k, const juce::String& id, const juce::String& caption)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    addAndMakeVisible(k.slider);
    k.label.setText(caption, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setFont(juce::Font(juce::FontOptions(11.0f)));
    k.label.setColour(juce::Label::textColourId, DecimuColours::textMuted);
    addAndMakeVisible(k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.apvts, id, k.slider);
}

void DecimulNavajaEditor::placeKnob(Knob& k, juce::Rectangle<int> cell)
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

DecimulNavajaEditor::DecimulNavajaEditor(DecimulNavajaProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);
    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    addKnob(gain, ParamIDs::gain, "GAIN");
    addKnob(pan, ParamIDs::pan, "PAN");
    addKnob(width, ParamIDs::width, "WIDTH");
    addKnob(bassHz, ParamIDs::bassHz, "GRAVES MONO HASTA");
    gain.slider.setTooltip(utf8("Ganancia de salida (±24 dB)."));
    pan.slider.setTooltip(utf8("Balance izquierda/derecha."));
    width.slider.setTooltip(utf8("Ancho estéreo: 0 % mono, 100 % original, 200 % más abierto."));
    bassHz.slider.setTooltip(utf8("Con GRAVES MONO: todo lo que esté debajo de esta frecuencia suena en mono."));

    const juce::String ids[6] = { ParamIDs::invertL, ParamIDs::invertR, ParamIDs::swap, ParamIDs::mono, ParamIDs::bassMono, ParamIDs::dc };
    const char* tips[6] = { "Invierte la fase del canal izquierdo.", "Invierte la fase del canal derecho.", "Intercambia izquierda y derecha.",
                            "Suma todo a mono (para chequear la mezcla).", "Pone en mono los graves (debajo de la frecuencia elegida).",
                            "Saca el corrimiento de continua (DC) con un pasa-altos de 10 Hz." };
    for (size_t i = 0; i < 6; ++i)
    {
        toggles[i].setClickingTogglesState(true);
        toggles[i].setTooltip(utf8(tips[i]));
        addAndMakeVisible(toggles[i]);
        toggleAttach[i] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids[i], toggles[i]);
    }
    addAndMakeVisible(meterL);
    addAndMakeVisible(meterR);

    setSize(820, 420);
    setResizable(true, true);
    setResizeLimits(720, 380, 1400, 800);
    startTimerHz(30);
}

DecimulNavajaEditor::~DecimulNavajaEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DecimulNavajaEditor::resized()
{
    auto content = getLocalBounds().reduced(22, 18);
    content.removeFromBottom(8);
    topbarRect = content.removeFromTop(54);
    content.removeFromTop(8);
    headerLogoArea = topbarRect.reduced(10, 8);

    meterRect = content.removeFromRight(90);
    content.removeFromRight(8);
    switchRect = content.removeFromBottom(120);
    content.removeFromBottom(8);
    knobsRect = content;

    auto k = knobsRect.reduced(10);
    const int w = k.getWidth() / 4;
    placeKnob(gain, k.removeFromLeft(w));
    placeKnob(pan, k.removeFromLeft(w));
    placeKnob(width, k.removeFromLeft(w));
    placeKnob(bassHz, k);

    auto s = switchRect.reduced(10);
    corrBar = s.removeFromBottom(30);
    s.removeFromBottom(10);
    const int bw = (s.getWidth() - 5 * 8) / 6;
    for (auto& t : toggles)
    {
        t.setBounds(s.removeFromLeft(bw).withSizeKeepingCentre(bw, 34));
        s.removeFromLeft(8);
    }

    auto m = meterRect.reduced(14, 12);
    m.removeFromBottom(20);
    meterL.setBounds(m.removeFromLeft(m.getWidth() / 2 - 3));
    m.removeFromLeft(6);
    meterR.setBounds(m);
}

void DecimulNavajaEditor::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    DecimuChrome::paintChassisBackground(g, full, 14.0f);
    DecimuChrome::paintRackHandles(g, full);
    DecimuChrome::paintCornerScrews(g, full, 12.0f);
    DecimuChrome::paintCompartment(g, topbarRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, knobsRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, switchRect.toFloat(), false, 8.0f);
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
    g.drawText("NAVAJA", logo, juce::Justification::centredLeft);
    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(15.0f));
    g.drawText(utf8("UTILIDAD ESTÉREO"), logo, juce::Justification::centredRight);

    // Correlación: de -1 (fase invertida, se cancela en mono) a +1 (mono)
    auto bar = corrBar.toFloat();
    auto label = bar.removeFromLeft(150.0f);
    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(14.0f));
    g.drawText(utf8("CORRELACIÓN ") + juce::String(shownCorr, 2), label, juce::Justification::centredLeft);
    g.setColour(DecimuColours::trackFill);
    g.fillRoundedRectangle(bar, 3.0f);
    const float cx = bar.getCentreX();
    const float x = bar.getX() + bar.getWidth() * (shownCorr + 1.0f) * 0.5f;
    g.setColour(shownCorr < 0.0f ? DecimuColours::accentRec : DecimuColours::accentPrimary);
    g.fillRect(juce::Rectangle<float>(juce::jmin(cx, x), bar.getY() + 4, std::abs(x - cx), bar.getHeight() - 8));
    g.setColour(DecimuColours::textMuted.withAlpha(0.6f));
    g.drawVerticalLine((int) cx, bar.getY(), bar.getBottom());
    g.setFont(lookAndFeel.monoFont(13.0f));
    g.drawText("-1", bar.reduced(6, 0), juce::Justification::centredLeft);
    g.drawText("+1", bar.reduced(6, 0), juce::Justification::centredRight);

    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(13.0f));
    g.drawText("L    R", meterRect.reduced(10).removeFromBottom(16), juce::Justification::centred);
    auto footer = getLocalBounds().removeFromBottom(22).reduced(16, 3);
    g.drawText("DECIMULABS - NAVAJA - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulNavajaEditor::timerCallback()
{
    meterL.setLevelDb(processorRef.peakL.load());
    meterR.setLevelDb(processorRef.peakR.load());
    meterL.repaint();
    meterR.repaint();
    const float c = processorRef.correlation.load();
    if (std::abs(c - shownCorr) > 0.005f)
    {
        shownCorr = c;
        repaint(switchRect);
    }
}
