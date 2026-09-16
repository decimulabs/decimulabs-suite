#include "DecimuLookAndFeel.h"
#include "BinaryData.h"
#include <cmath>

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

    // Línea de referencia en 0 para sliders bipolares (dB): mismo gesto que las marcas
    // "-10 / 0 / +10" de un fader de hardware, simplificado a la línea de unity gain.
    if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
    {
        const float zeroProp = (float) slider.valueToProportionOfLength(0.0);
        const float zeroY = juce::jmap(zeroProp, 0.0f, 1.0f, bottomY, topY);
        g.setColour(DecimuColours::textMuted.withAlpha(0.8f));
        g.drawLine(bounds.getX(), zeroY, bounds.getRight(), zeroY, 1.0f);
    }

    // Thumb: cápsula tipo fader real, con degradé metálico y una línea-ranura al medio.
    constexpr float thumbHeight = 10.0f;
    auto thumb = juce::Rectangle<float>(bounds.getX(), sliderPos - thumbHeight * 0.5f,
                                          bounds.getWidth(), thumbHeight);
    const bool active = slider.isMouseOverOrDragging();
    juce::ColourGradient thumbGrad(active ? juce::Colours::white : DecimuColours::text.brighter(0.2f),
                                    thumb.getX(), thumb.getY(),
                                    active ? DecimuColours::accentBlueSoft : DecimuColours::textMuted,
                                    thumb.getX(), thumb.getBottom(), false);
    g.setGradientFill(thumbGrad);
    g.fillRoundedRectangle(thumb, 2.0f);
    g.setColour(DecimuColours::background);
    g.drawLine(thumb.getX() + 2.0f, sliderPos, thumb.getRight() - 2.0f, sliderPos, 1.5f);
}

void DecimuLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPosProportional, float rotaryStartAngle,
                                          float rotaryEndAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height).reduced(3.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    // Aro de fondo (todo el recorrido posible).
    juce::Path bgArc;
    bgArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(DecimuColours::trackFill);
    g.strokePath(bgArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Arco de valor (desde el mínimo hasta la posición actual), degradé azul->cian.
    juce::Path valueArc;
    valueArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, rotaryStartAngle, angle, true);
    juce::ColourGradient arcGrad(DecimuColours::accentBlue, centre.x - radius, centre.y,
                                  DecimuColours::accentCyan, centre.x + radius, centre.y, false);
    g.setGradientFill(arcGrad);
    g.strokePath(valueArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Cuerpo del knob: degradé sutil tipo metal cepillado + borde.
    auto knobBounds = bounds.reduced(radius * 0.32f);
    juce::ColourGradient bodyGrad(DecimuColours::panel.brighter(0.2f), knobBounds.getX(), knobBounds.getY(),
                                   DecimuColours::panel.darker(0.5f), knobBounds.getRight(), knobBounds.getBottom(), false);
    g.setGradientFill(bodyGrad);
    g.fillEllipse(knobBounds);
    g.setColour(DecimuColours::textMuted.withAlpha(0.6f));
    g.drawEllipse(knobBounds, 1.0f);

    // Puntero: línea blanca desde el centro hacia el borde, en la dirección del valor.
    // (Convención de addCentredArc: 0 rad = arriba, positivo = sentido horario.)
    juce::Point<float> pointerEnd(centre.x + radius * 0.62f * std::sin(angle),
                                   centre.y - radius * 0.62f * std::cos(angle));
    g.setColour(juce::Colours::white);
    g.drawLine({ centre, pointerEnd }, 2.0f);
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
