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

    // Texto del tooltip en un bloque de hasta ~340 px, con las líneas parejas.
    juce::TextLayout layoutTooltipText(const juce::String& text, juce::Colour colour)
    {
        juce::AttributedString s;
        s.setJustification(juce::Justification::topLeft);
        s.append(text, juce::Font(juce::FontOptions(13.0f)), colour);

        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths(s, 340.0f);
        return layout;
    }
}

DecimuLookAndFeel::DecimuLookAndFeel(const DecimuAccentTheme& theme)
{
    DecimuColours::accentPrimary = theme.primary;
    DecimuColours::accentSecondary = theme.secondary;
    DecimuColours::accentSecondarySoft = theme.secondarySoft;

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
    setColour(juce::PopupMenu::highlightedBackgroundColourId, DecimuColours::accentSecondary);
    setColour(juce::Slider::textBoxTextColourId, DecimuColours::text);
    setColour(juce::Slider::textBoxBackgroundColourId, DecimuColours::panel);
    setColour(juce::Slider::textBoxOutlineColourId, DecimuColours::textMuted);
    setColour(juce::TextButton::textColourOffId, DecimuColours::text);
    setColour(juce::TextButton::textColourOnId, DecimuColours::background);
}

juce::Font DecimuLookAndFeel::displayFont(float height, bool bold) const
{
    return makeFont(bold ? chakraBold : chakraRegular, height);
}

juce::Font DecimuLookAndFeel::monoFont(float height) const
{
    return makeFont(vt323, height);
}

juce::Font DecimuLookAndFeel::getLabelFont(juce::Label&)
{
    return displayFont(15.0f);
}

juce::Font DecimuLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return monoFont(20.0f);
}

juce::Font DecimuLookAndFeel::getTextButtonFont(juce::TextButton&, int)
{
    return monoFont(18.0f);
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

    juce::ColourGradient grad(DecimuColours::accentSecondary, track.getX(), bottomY,
                               DecimuColours::accentPrimary, track.getX(), topY, false);
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

    // Thumb: cápsula cromada de verdad (degradé de 5 tonos) con sombra proyectada,
    // en vez del degradé de 2 tonos de la primera versión.
    constexpr float thumbHeight = 11.0f;
    auto thumb = juce::Rectangle<float>(bounds.getX(), sliderPos - thumbHeight * 0.5f,
                                          bounds.getWidth(), thumbHeight);

    juce::Path thumbPath;
    thumbPath.addRoundedRectangle(thumb, 2.0f);
    juce::DropShadow(juce::Colours::black.withAlpha(0.55f), 5, { 0, 2 }).drawForPath(g, thumbPath);

    const bool active = slider.isMouseOverOrDragging();
    juce::ColourGradient thumbGrad(juce::Colours::white, thumb.getX(), thumb.getY(),
                                    active ? DecimuColours::accentSecondarySoft : juce::Colour(0xff5a6280),
                                    thumb.getX(), thumb.getBottom(), false);
    thumbGrad.addColour(0.18, juce::Colour(0xffd7dce6));
    thumbGrad.addColour(0.46, juce::Colour(0xff98a3ba));
    thumbGrad.addColour(0.58, juce::Colour(0xffb7c0d4));
    g.setGradientFill(thumbGrad);
    g.fillPath(thumbPath);
    g.setColour(juce::Colours::black.withAlpha(0.7f));
    g.strokePath(thumbPath, juce::PathStrokeType(1.0f));

    g.setColour(DecimuColours::background.withAlpha(0.6f));
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

    // Aro de marcas radiales tipo potenciómetro real (antes era solo un arco liso).
    constexpr int numTicks = 20;
    for (int i = 0; i <= numTicks; ++i)
    {
        const float t = rotaryStartAngle + (float) i / (float) numTicks * (rotaryEndAngle - rotaryStartAngle);
        const float inner = radius * 0.86f, outer = radius * 0.98f;
        juce::Point<float> p1(centre.x + inner * std::sin(t), centre.y - inner * std::cos(t));
        juce::Point<float> p2(centre.x + outer * std::sin(t), centre.y - outer * std::cos(t));
        g.setColour(DecimuColours::textMuted.withAlpha(0.5f));
        g.drawLine({ p1, p2 }, 1.0f);
    }

    // Arco de valor (desde el mínimo hasta la posición actual), degradé azul->cian.
    juce::Path valueArc;
    valueArc.addCentredArc(centre.x, centre.y, radius * 0.78f, radius * 0.78f, 0.0f, rotaryStartAngle, angle, true);
    juce::ColourGradient arcGrad(DecimuColours::accentSecondary, centre.x - radius, centre.y,
                                  DecimuColours::accentPrimary, centre.x + radius, centre.y, false);
    g.setGradientFill(arcGrad);
    g.strokePath(valueArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Cuerpo del knob: sombra proyectada + degradé base + brillo especular arriba a
    // la izquierda, como una perilla real fotografiada con una luz de arriba.
    auto knobBounds = bounds.reduced(radius * 0.34f);

    juce::Path knobPath;
    knobPath.addEllipse(knobBounds);
    juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 8, { 0, 3 }).drawForPath(g, knobPath);

    juce::ColourGradient bodyGrad(juce::Colour(0xff454b60), knobBounds.getX(), knobBounds.getY(),
                                   juce::Colour(0xff1a1c26), knobBounds.getRight(), knobBounds.getBottom(), false);
    g.setGradientFill(bodyGrad);
    g.fillEllipse(knobBounds);

    juce::ColourGradient highlight(juce::Colours::white.withAlpha(0.35f),
                                    knobBounds.getX() + knobBounds.getWidth() * 0.32f,
                                    knobBounds.getY() + knobBounds.getHeight() * 0.26f,
                                    juce::Colours::transparentWhite,
                                    knobBounds.getX() + knobBounds.getWidth() * 0.32f,
                                    knobBounds.getY() + knobBounds.getHeight() * 0.75f, true);
    g.setGradientFill(highlight);
    g.fillEllipse(knobBounds);

    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.drawEllipse(knobBounds, 1.0f);

    // Puntero con leve resplandor.
    juce::Point<float> pointerEnd(centre.x + radius * 0.6f * std::sin(angle),
                                   centre.y - radius * 0.6f * std::cos(angle));
    g.setColour(DecimuColours::accentPrimary.withAlpha(0.4f));
    g.drawLine({ centre, pointerEnd }, 4.0f);
    juce::ColourGradient pointerGrad(DecimuColours::accentPrimary, centre.x, centre.y,
                                      DecimuColours::accentSecondarySoft, pointerEnd.x, pointerEnd.y, false);
    g.setGradientFill(pointerGrad);
    g.drawLine({ centre, pointerEnd }, 2.0f);
}

void DecimuLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                      int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0, 0, (float) width, (float) height);

    g.setColour(DecimuColours::panel);
    g.fillRect(bounds);
    g.setColour(box.isMouseOver() ? DecimuColours::accentSecondarySoft : DecimuColours::textMuted);
    g.drawRect(bounds, 1.0f);

    auto arrowZone = bounds.removeFromRight(18.0f);
    auto c = arrowZone.getCentre();
    juce::Path arrow;
    arrow.addTriangle(c.x - 4.0f, c.y - 3.0f, c.x + 4.0f, c.y - 3.0f, c.x, c.y + 4.0f);
    g.setColour(DecimuColours::accentPrimary);
    g.fillPath(arrow);
}

void DecimuLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                              const juce::Colour&, bool isMouseOverButton, bool isButtonDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    auto colour = isButtonDown ? DecimuColours::accentSecondary
                : isMouseOverButton ? DecimuColours::panel.brighter(0.2f)
                : DecimuColours::panel;
    g.setColour(colour);
    g.fillRect(bounds);
    g.setColour(DecimuColours::textMuted);
    g.drawRect(bounds, 1.0f);
}

void DecimuLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height);

    g.setColour(DecimuColours::background.withAlpha(0.98f));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(DecimuColours::accentPrimary.withAlpha(0.75f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.0f);

    layoutTooltipText(text, DecimuColours::text).draw(g, bounds.reduced(9.0f, 6.0f));
}

juce::Rectangle<int> DecimuLookAndFeel::getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                                         juce::Rectangle<int> parentArea)
{
    const auto layout = layoutTooltipText(tipText, DecimuColours::text);
    const int w = (int) std::ceil(layout.getWidth()) + 20;
    const int h = (int) std::ceil(layout.getHeight()) + 14;

    return juce::Rectangle<int>(screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 8,
                                w, h)
        .constrainedWithin(parentArea);
}

void DecimuLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool, bool)
{
    // Reinterpreta el toggle de Bypass como el indicador "REC" del sitio de
    // decimulabs: punto rojo encendido + "REC" cuando el plugin está procesando
    // (no bypassed), punto gris apagado + "BYPASS" cuando está en bypass.
    auto bounds = button.getLocalBounds().toFloat();
    const bool bypassed = button.getToggleState();

    auto dotArea = bounds.removeFromLeft(bounds.getHeight()).reduced(5.0f);

    // El punto "REC" pulsa suavemente cuando el plugin está activo (no bypassed) — el
    // editor llama a bypassButton.repaint() a 30Hz para animarlo (ver timerCallback).
    float alpha = 1.0f;
    if (!bypassed)
    {
        const double phase = juce::Time::getMillisecondCounterHiRes() * 0.003;
        alpha = 0.55f + 0.45f * (float) std::sin(phase);
    }
    g.setColour((bypassed ? DecimuColours::textMuted : DecimuColours::accentRec).withAlpha(alpha));
    g.fillEllipse(dotArea);

    g.setColour(bypassed ? DecimuColours::textMuted : DecimuColours::text);
    g.setFont(monoFont(18.0f));
    g.drawText(bypassed ? "BYPASS" : "REC", bounds.reduced(4.0f, 0.0f), juce::Justification::centredLeft);
}
