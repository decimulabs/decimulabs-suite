#include "DecimuLookAndFeel.h"
#include "BinaryData.h"

namespace
{
    juce::Font makeFont(const juce::Typeface::Ptr& tf, float height)
    {
        auto opts = juce::FontOptions().withHeight(height);
        if (tf != nullptr)
            opts = opts.withTypeface(tf);
        return juce::Font(opts);
    }
}

DecimuLookAndFeel::DecimuLookAndFeel()
{
    chakraRegular = juce::Typeface::createSystemTypefaceFor(
        BinaryData::ChakraPetchRegular_ttf, (size_t) BinaryData::ChakraPetchRegular_ttfSize);
    chakraBold = juce::Typeface::createSystemTypefaceFor(
        BinaryData::ChakraPetchBold_ttf, (size_t) BinaryData::ChakraPetchBold_ttfSize);
    vt323 = juce::Typeface::createSystemTypefaceFor(
        BinaryData::VT323Regular_ttf, (size_t) BinaryData::VT323Regular_ttfSize);

    setColour(juce::ResizableWindow::backgroundColourId, DecimuColours::background);
    setColour(juce::Label::textColourId, DecimuColours::text);
    setColour(juce::ComboBox::backgroundColourId, DecimuColours::panel);
    setColour(juce::ComboBox::textColourId, DecimuColours::text);
    setColour(juce::ComboBox::outlineColourId, DecimuColours::textMuted);
    setColour(juce::PopupMenu::backgroundColourId, DecimuColours::panel);
    setColour(juce::PopupMenu::textColourId, DecimuColours::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, DecimuColours::accentBlue);
    setColour(juce::Slider::textBoxTextColourId, DecimuColours::text);
    setColour(juce::Slider::textBoxBackgroundColourId, DecimuColours::panel);
    setColour(juce::Slider::textBoxOutlineColourId, DecimuColours::textMuted);
    setColour(juce::TextButton::textColourOffId, DecimuColours::text);
    setColour(juce::TextButton::textColourOnId, DecimuColours::background);
}

juce::Font DecimuLookAndFeel::getLabelFont(juce::Label&)
{
    return makeFont(chakraRegular, 15.0f);
}

juce::Font DecimuLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return makeFont(vt323, 20.0f);
}

juce::Font DecimuLookAndFeel::getTextButtonFont(juce::TextButton&, int)
{
    return makeFont(vt323, 18.0f);
}

void DecimuLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float minSliderPos, float maxSliderPos,
                                          juce::Slider::SliderStyle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height);
    auto track = bounds.withSizeKeepingCentre(6.0f, bounds.getHeight());

    g.setColour(DecimuColours::trackFill);
    g.fillRect(track);
    g.setColour(DecimuColours::textMuted.withAlpha(0.5f));
    g.drawRect(track, 1.0f);

    // Relleno del thumb hasta el extremo "inferior" del track (funciona tanto para
    // sliders unipolares 0-100% como para los bipolares en dB — es una simplificación
    // deliberada, no un split boost/cut en el centro).
    const float bottomY = juce::jmax(minSliderPos, maxSliderPos);
    const float topY = juce::jmin(minSliderPos, maxSliderPos);
    const float fillTop = juce::jlimit(topY, bottomY, sliderPos);
    auto fill = track.withTop(fillTop).withBottom(bottomY);

    juce::ColourGradient grad(DecimuColours::accentBlue, track.getX(), bottomY,
                               DecimuColours::accentCyan, track.getX(), topY, false);
    g.setGradientFill(grad);
    g.fillRect(fill);

    // Thumb: barra horizontal tipo fader real, con una línea-ranura al medio.
    constexpr float thumbHeight = 8.0f;
    auto thumb = juce::Rectangle<float>(bounds.getX(), sliderPos - thumbHeight * 0.5f,
                                          bounds.getWidth(), thumbHeight);
    g.setColour(slider.isMouseOverOrDragging() ? juce::Colours::white : DecimuColours::text);
    g.fillRect(thumb);
    g.setColour(DecimuColours::background);
    g.drawLine(thumb.getX() + 2.0f, sliderPos, thumb.getRight() - 2.0f, sliderPos, 1.5f);
}

void DecimuLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                      int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0, 0, (float) width, (float) height);

    g.setColour(DecimuColours::panel);
    g.fillRect(bounds);
    g.setColour(box.isMouseOver() ? DecimuColours::accentBlueSoft : DecimuColours::textMuted);
    g.drawRect(bounds, 1.0f);

    auto arrowZone = bounds.removeFromRight(18.0f);
    auto c = arrowZone.getCentre();
    juce::Path arrow;
    arrow.addTriangle(c.x - 4.0f, c.y - 3.0f, c.x + 4.0f, c.y - 3.0f, c.x, c.y + 4.0f);
    g.setColour(DecimuColours::accentCyan);
    g.fillPath(arrow);
}

void DecimuLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                              const juce::Colour&, bool isMouseOverButton, bool isButtonDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    auto colour = isButtonDown ? DecimuColours::accentBlue
                : isMouseOverButton ? DecimuColours::panel.brighter(0.2f)
                : DecimuColours::panel;
    g.setColour(colour);
    g.fillRect(bounds);
    g.setColour(DecimuColours::textMuted);
    g.drawRect(bounds, 1.0f);
}

void DecimuLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool, bool)
{
    // Reinterpreta el toggle de Bypass como el indicador "REC" del sitio de
    // decimulabs: punto rojo encendido + "REC" cuando el plugin está procesando
    // (no bypassed), punto gris apagado + "BYPASS" cuando está en bypass.
    auto bounds = button.getLocalBounds().toFloat();
    const bool bypassed = button.getToggleState();

    auto dotArea = bounds.removeFromLeft(bounds.getHeight()).reduced(5.0f);
    g.setColour(bypassed ? DecimuColours::textMuted : DecimuColours::accentRec);
    g.fillEllipse(dotArea);

    g.setColour(bypassed ? DecimuColours::textMuted : DecimuColours::text);
    g.setFont(makeFont(vt323, 18.0f));
    g.drawText(bypassed ? "BYPASS" : "REC", bounds.reduced(4.0f, 0.0f), juce::Justification::centredLeft);
}
