#pragma once

#include "DecimuLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

/**
    Textos de ayuda (tooltips) en dos idiomas para toda la suite: aparecen al dejar el
    puntero quieto sobre un control, en **inglés por defecto**, y un botoncito EN/ES
    abajo de la ventana los pasa a español. La elección queda guardada para el usuario
    (un archivito en su carpeta de datos), así todos los plugins de la suite abren en el
    idioma que eligió.

    Uso en un editor:

        DecimuTooltips tips { *this };                       // miembro del editor

        // constructor
        tips.add(cutoffSlider, "Cutoff frequency", "Frecuencia de corte");
        addAndMakeVisible(tips.getSwitch());

        // resized()
        tips.layout(getLocalBounds());

    `add()` acepta cualquier componente de JUCE que muestre tooltips (Slider, Button,
    Label, ComboBox, TextEditor...) y los componentes propios que hereden de
    `juce::SettableTooltipClient`. Sin la ventana de tooltips (TooltipWindow) los
    `setTooltip()` no se ven nunca: de eso se encarga esta clase.
*/
namespace DecimuLang
{
    enum class Lang { en = 0, es = 1 };

    inline juce::File settingsFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("decimulabs")
            .getChildFile("suite-settings.txt");
    }

    /** Idioma elegido por el usuario; inglés si nunca eligió. */
    inline Lang load()
    {
        const auto file = settingsFile();
        if (file.existsAsFile() && file.loadFileAsString().containsIgnoreCase("tooltips=es"))
            return Lang::es;
        return Lang::en;
    }

    inline void save(Lang lang)
    {
        const auto file = settingsFile();
        file.getParentDirectory().createDirectory();
        file.replaceWithText(lang == Lang::es ? "tooltips=es\n" : "tooltips=en\n");
    }
}

/** Interruptor chico EN | ES. Un click alterna el idioma. */
class DecimuLangSwitch : public juce::Button
{
public:
    DecimuLangSwitch() : juce::Button("language") { setClickingTogglesState(false); }

    void setLanguage(DecimuLang::Lang newLang)
    {
        if (newLang != lang)
        {
            lang = newLang;
            repaint();
        }
    }

    DecimuLang::Lang getLanguage() const { return lang; }

    void paintButton(juce::Graphics& g, bool isMouseOver, bool) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        const auto accent = DecimuColours::accentPrimary;

        g.setColour(isMouseOver ? DecimuColours::panel.brighter(0.15f) : DecimuColours::panel);
        g.fillRoundedRectangle(bounds, 4.0f);

        if (auto* laf = dynamic_cast<DecimuLookAndFeel*>(&getLookAndFeel()))
            g.setFont(laf->monoFont(13.0f));
        else
            g.setFont(12.0f);

        auto drawHalf = [&](juce::Rectangle<float> half, const juce::String& caption, bool active)
        {
            if (active)
            {
                g.setColour(accent.withAlpha(0.22f));
                g.fillRoundedRectangle(half.reduced(1.5f), 3.0f);
            }
            g.setColour(active ? accent : DecimuColours::textMuted);
            g.drawText(caption, half, juce::Justification::centred);
        };

        auto left = bounds.removeFromLeft(bounds.getWidth() * 0.5f);
        drawHalf(left, "EN", lang == DecimuLang::Lang::en);
        drawHalf(bounds, "ES", lang == DecimuLang::Lang::es);

        g.setColour(DecimuColours::textMuted.withAlpha(0.55f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 1.0f);
    }

private:
    DecimuLang::Lang lang = DecimuLang::Lang::en;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimuLangSwitch)
};

/** Ventana de tooltips + registro de textos en los dos idiomas + el interruptor EN/ES. */
class DecimuTooltips
{
public:
    explicit DecimuTooltips(juce::Component& owner)
        : window(&owner, 450), language(DecimuLang::load())
    {
        langSwitch.setLanguage(language);
        langSwitch.onClick = [this]
        {
            setLanguage(language == DecimuLang::Lang::en ? DecimuLang::Lang::es : DecimuLang::Lang::en);
        };
        add(langSwitch, "Language of these hover tips: English / Spanish",
            "Idioma de estos textos de ayuda: inglés / español");
    }

    /** Registra el texto de un control en los dos idiomas (si falta el español, queda el inglés). */
    template <typename T>
    void add(T& component, const juce::String& english, const juce::String& spanish = {})
    {
        items.push_back({ [&component](const juce::String& text) { component.setTooltip(text); },
                          english, spanish.isEmpty() ? english : spanish });
        apply(items.back());
    }

    DecimuLang::Lang getLanguage() const { return language; }

    void setLanguage(DecimuLang::Lang newLanguage)
    {
        language = newLanguage;
        DecimuLang::save(language);
        langSwitch.setLanguage(language);
        for (auto& item : items)
            apply(item);
    }

    /** El botón EN/ES: hay que agregarlo al editor con `addAndMakeVisible(tips.getSwitch())`. */
    juce::Component& getSwitch() { return langSwitch; }

    /** Lo ubica en la franja de abajo del editor (la del pie), centrado. */
    void layout(juce::Rectangle<int> editorBounds)
    {
        auto footer = editorBounds.removeFromBottom(22);
        langSwitch.setBounds(footer.withSizeKeepingCentre(60, 17));
    }

private:
    struct Item
    {
        std::function<void(const juce::String&)> set;
        juce::String english, spanish;
    };

    void apply(const Item& item) const
    {
        item.set(language == DecimuLang::Lang::es ? item.spanish : item.english);
    }

    juce::TooltipWindow window;
    DecimuLang::Lang language;
    DecimuLangSwitch langSwitch;
    std::vector<Item> items;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecimuTooltips)
};
