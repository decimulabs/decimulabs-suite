#include "PluginEditor.h"
#include "BinaryData.h"

void DecimulScourEditor::addKnob(KnobCell& k, const juce::String& paramID, const juce::String& caption, const juce::String& suffix,
                                 bool textBox, const char* en, const char* es)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    if (textBox)
        k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    else
    {
        k.slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        k.slider.setPopupDisplayEnabled(true, true, this);          // el valor aparece mientras se arrastra
    }
    k.slider.setTextValueSuffix(suffix);
    addAndMakeVisible(k.slider);

    k.label.setText(caption, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setFont(juce::Font(juce::FontOptions(textBox ? 12.0f : 10.0f)));
    k.label.setColour(juce::Label::textColourId, textBox ? DecimuColours::text : DecimuColours::textMuted);
    addAndMakeVisible(k.label);

    k.attachment = std::make_unique<SliderAttachment>(processorRef.apvts, paramID, k.slider);
    tips.add(k.slider, en, es);
}

void DecimulScourEditor::buildModule(ModuleUi& m, const juce::String& title, const juce::String& onID, const KnobSpec& amount,
                                     const juce::String& typeID, const juce::StringArray& types, const char* typeEn,
                                     const char* typeEs, const std::vector<KnobSpec>& knobs, const char* ledEn, const char* ledEs)
{
    addAndMakeVisible(m.led);
    m.ledAttachment = std::make_unique<ButtonAttachment>(processorRef.apvts, onID, m.led);
    tips.add(m.led, ledEn, ledEs);

    m.titleLabel.setText(title, juce::dontSendNotification);
    m.titleLabel.setJustificationType(juce::Justification::centredLeft);
    m.titleLabel.setFont(lookAndFeel.monoFont(15.0f));
    m.titleLabel.setColour(juce::Label::textColourId, DecimuColours::accentPrimary);
    addAndMakeVisible(m.titleLabel);

    if (typeID.isNotEmpty())
    {
        m.hasSelector = true;
        m.typeCount = types.size();
        m.selector.setOptions(types);
        m.selector.setSelectedIndex((int) processorRef.apvts.getRawParameterValue(typeID)->load());
        addAndMakeVisible(m.selector);
        m.selector.onChange = [this, typeID, count = types.size()](int idx)
        {
            if (auto* param = processorRef.apvts.getParameter(typeID))
                param->setValueNotifyingHost(idx / (float) juce::jmax(1, count - 1));
        };
        tips.add(m.selector, typeEn, typeEs);
    }

    for (const auto& spec : knobs)
    {
        auto cell = std::make_unique<KnobCell>();
        addKnob(*cell, spec.id, spec.caption, spec.suffix, false, spec.en, spec.es);
        m.knobs.push_back(std::move(cell));
    }
    addKnob(m.big, amount.id, amount.caption, amount.suffix, true, amount.en, amount.es);
}

// Rotary cuadrado con un tamaño máximo; el rótulo va pegado al knob.
void DecimulScourEditor::placeKnob(KnobCell& k, juce::Rectangle<int> cell, int maxSide, bool textBox)
{
    const int labelH = 14;
    if (textBox)
    {
        const int box = 16;
        auto labelCell = cell.removeFromTop(labelH + 2);
        const int side = std::clamp(std::min(cell.getWidth() - 4, cell.getHeight() - box - 2), 36, maxSide);
        const int width = std::max(side, 64);
        k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, width, box);
        const auto bounds = cell.withSizeKeepingCentre(width, side + box + 2);
        k.slider.setBounds(bounds);
        k.label.setBounds(labelCell);
    }
    else
    {
        auto labelCell = cell.removeFromBottom(labelH);
        const int side = std::clamp(std::min(cell.getWidth() - 6, cell.getHeight() - 2), 30, maxSide);
        k.slider.setBounds(cell.withSizeKeepingCentre(side, side));
        k.label.setBounds(labelCell);
    }
}

void DecimulScourEditor::setupFader(juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
    addAndMakeVisible(slider);

    label.setText("[ " + text.toUpperCase() + " ]", juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, DecimuColours::text);
    addAndMakeVisible(label);
}

DecimulScourEditor::DecimulScourEditor(DecimulScourProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    addAndMakeVisible(tips.getSwitch());
    addAndMakeVisible(scope);
    tips.add(scope, "The last second and a half of the output waveform: you can see the wobble, the tape dropouts, the crackle and the reverb tail.",
             "El último segundo y medio de la forma de onda de la salida: se ven el wobble, las caídas de la cinta, los chasquidos y la cola del reverb.");

    // ---- los seis módulos ----
    buildModule(modules[0], "NOISE", ParamIDs::noiseOn,
                { ParamIDs::noiseAmt.toRawUTF8(), "LEVEL", " %",
                  "Noise level. 0 is silent, 100 is loud. The type is picked above.",
                  "Nivel del ruido. 0 es silencio, 100 es fuerte. El tipo se elige arriba." },
                ParamIDs::noiseType, { "Vinyl", "Tape", "Hum", "Static", "VHS", "Dust" },
                "Type of noise: Vinyl (hiss and crackle), Tape (hiss), Hum (50 Hz mains and harmonics), Static (bursts of crackle), VHS (hiss in bands plus the line whine), Dust (sparse specks).",
                "Tipo de ruido: Vinyl (siseo y chasquidos), Tape (siseo de cinta), Hum (zumbido de 50 Hz y armónicos), Static (ráfagas de crujido), VHS (siseo en bandas y el silbido de línea), Dust (motas sueltas).",
                { { ParamIDs::noiseTone.toRawUTF8(), "TONE", " %", "Tone of the noise: low is dark and muffled, high is bright.", "Tono del ruido: bajo es oscuro y apagado, alto es brillante." },
                  { ParamIDs::noiseFollow.toRawUTF8(), "FOLLOW", " %", "Follow: the noise level follows your signal. At 100 % the noise only appears while there is sound.", "Follow: el nivel del ruido sigue a tu señal. Al 100 % el ruido solo aparece mientras hay sonido." },
                  { ParamIDs::noiseDuck.toRawUTF8(), "DUCK", " %", "Duck: the noise gets quieter while your signal plays, and comes up in the silences.", "Duck: el ruido baja mientras suena tu señal y sube en los silencios." },
                  { ParamIDs::noiseFlux.toRawUTF8(), "FLUX", " %", "Flux: a slow random drift that moves this module's amount, so it never sounds exactly the same.", "Flux: una deriva aleatoria lenta que mueve la cantidad de este módulo, para que no suene siempre igual." } },
                "Turns the noise module on or off.", "Prende o apaga el módulo de ruido.");
    modules[0].hasPill = true;
    modules[0].pill.setClickingTogglesState(true);
    modules[0].pill.setButtonText("PRE");
    addAndMakeVisible(modules[0].pill);
    modules[0].pillAttachment = std::make_unique<ButtonAttachment>(processorRef.apvts, ParamIDs::noisePost, modules[0].pill);
    tips.add(modules[0].pill, "Routing: PRE adds the noise before the other modules, so they process it too; POST adds it after them, clean.",
             "Routing: PRE suma el ruido antes de los otros módulos, así que lo procesan; POST lo suma después, limpio.");

    buildModule(modules[1], "WOBBLE", ParamIDs::wobOn,
                { ParamIDs::wobAmt.toRawUTF8(), "DEPTH", " %",
                  "Depth of the pitch wobble, like a worn tape machine: slow wow and fast flutter. At 100 % the wow bends the pitch by about 45 cents.",
                  "Profundidad del temblor de tono, como una cinta gastada: wow lento y flutter rápido. Al 100 % el wow mueve el tono unos 45 centésimos." },
                {}, {}, nullptr, nullptr,
                { { ParamIDs::wobBalance.toRawUTF8(), "WOW-FLUT", " %", "Balance between the slow wow (0) and the fast flutter (100).", "Balance entre el wow lento (0) y el flutter rápido (100)." },
                  { ParamIDs::wowRate.toRawUTF8(), "WOW RATE", " Hz", "Speed of the slow wow.", "Velocidad del wow lento." },
                  { ParamIDs::flutRate.toRawUTF8(), "FLUT RATE", " Hz", "Speed of the fast flutter.", "Velocidad del flutter rápido." },
                  { ParamIDs::wobStereo.toRawUTF8(), "STEREO", " %", "Stereo: 0 moves both channels together, 100 moves them out of step, which widens it like a chorus.", "Stereo: 0 mueve los dos canales juntos, 100 los desfasa, y se ensancha como un chorus." },
                  { ParamIDs::wobMix.toRawUTF8(), "MIX", " %", "Mix of this module: 100 is pure wobble, lower blends the dry signal back and makes a chorus.", "Mezcla de este módulo: 100 es wobble puro, más bajo vuelve a sumar la señal seca y se hace un chorus." },
                  { ParamIDs::wobFlux.toRawUTF8(), "FLUX", " %", "Flux: a slow random drift that moves this module's depth and speeds.", "Flux: una deriva aleatoria lenta que mueve la profundidad y las velocidades de este módulo." } },
                "Turns the wobble module on or off.", "Prende o apaga el módulo de wobble.");

    buildModule(modules[2], "DISTORT", ParamIDs::disOn,
                { ParamIDs::disAmt.toRawUTF8(), "DRIVE", " %", "Drive: how hard the signal is pushed into the distortion. The level is compensated.", "Drive: cuánto se empuja la señal hacia la distorsión. El nivel está compensado." },
                ParamIDs::disType, { "Tube", "Tape", "Diode", "Fold" },
                "Type of distortion: Tube (warm, asymmetric), Tape (soft and dull), Diode (edgy, asymmetric), Fold (harsh, wavefolder).",
                "Tipo de distorsión: Tube (cálida, asimétrica), Tape (suave y apagada), Diode (con filo, asimétrica), Fold (áspera, plegador de onda).",
                { { ParamIDs::disFocus.toRawUTF8(), "FOCUS", " Hz", "Focus: only what is above this frequency gets distorted, so you can keep the low end clean. At the minimum everything is distorted.", "Focus: solo se distorsiona lo que está por encima de esta frecuencia, así los graves quedan limpios. En el mínimo se distorsiona todo." },
                  { ParamIDs::disMix.toRawUTF8(), "MIX", " %", "Mix between the clean and the distorted signal.", "Mezcla entre la señal limpia y la distorsionada." },
                  { ParamIDs::disFlux.toRawUTF8(), "FLUX", " %", "Flux: a slow random drift that moves the drive.", "Flux: una deriva aleatoria lenta que mueve el drive." } },
                "Turns the distortion module on or off.", "Prende o apaga el módulo de distorsión.");

    buildModule(modules[3], "DIGITAL", ParamIDs::digOn,
                { ParamIDs::digAmt.toRawUTF8(), "AMOUNT", " %", "How much the digital quality is degraded: lower sample rate (down to 2.2 kHz) and fewer bits (down to 4).", "Cuánto se degrada la calidad digital: menos frecuencia de muestreo (hasta 2.2 kHz) y menos bits (hasta 4)." },
                {}, {}, nullptr, nullptr,
                { { ParamIDs::digBalance.toRawUTF8(), "RATE-BITS", " %", "Balance between reducing the sample rate (0, aliasing) and reducing the bits (100, grainy quantization). 50 does both.", "Balance entre bajar la frecuencia de muestreo (0, aliasing) y bajar los bits (100, cuantización granulada). 50 hace las dos." },
                  { ParamIDs::digFocus.toRawUTF8(), "FOCUS", " Hz", "Focus: only what is above this frequency gets crushed. At the minimum everything is.", "Focus: solo se degrada lo que está por encima de esta frecuencia. En el mínimo se degrada todo." },
                  { ParamIDs::digSmooth.toRawUTF8(), "SMOOTH", " %", "Smooth: a low-pass after the crusher that tames the harsh digital edges.", "Smooth: un pasa-bajos después del degradador que suaviza los bordes digitales ásperos." },
                  { ParamIDs::digMix.toRawUTF8(), "MIX", " %", "Mix between the clean and the degraded signal.", "Mezcla entre la señal limpia y la degradada." },
                  { ParamIDs::digFlux.toRawUTF8(), "FLUX", " %", "Flux: a slow random drift that moves the amount.", "Flux: una deriva aleatoria lenta que mueve la cantidad." } },
                "Turns the digital module on or off.", "Prende o apaga el módulo digital.");

    buildModule(modules[4], "SPACE", ParamIDs::spaceOn,
                { ParamIDs::spaceAmt.toRawUTF8(), "LEVEL", " %", "Level of the lo-fi reverb added to the signal.", "Nivel del reverb lo-fi que se suma a la señal." },
                {}, {}, nullptr, nullptr,
                { { ParamIDs::spaceDecay.toRawUTF8(), "DECAY", " %", "How long the reverb lasts, from a small room to a long tail.", "Cuánto dura el reverb, de un cuarto chico a una cola larga." },
                  { ParamIDs::spacePre.toRawUTF8(), "PREDELAY", " ms", "Time before the reverb starts.", "Tiempo antes de que arranque el reverb." },
                  { ParamIDs::spaceFocus.toRawUTF8(), "FOCUS", "", "Focus: to the left the reverb gets darker, to the right thinner (less low end).", "Focus: hacia la izquierda el reverb se oscurece, hacia la derecha se adelgaza (menos graves)." },
                  { ParamIDs::spaceStereo.toRawUTF8(), "STEREO", " %", "Width of the reverb: 0 is mono, 100 is wide.", "Ancho del reverb: 0 es mono, 100 es ancho." },
                  { ParamIDs::spaceFlux.toRawUTF8(), "FLUX", " %", "Flux: a slow random drift that moves the level and the decay.", "Flux: una deriva aleatoria lenta que mueve el nivel y el decay." } },
                "Turns the reverb module on or off.", "Prende o apaga el módulo de reverb.");

    buildModule(modules[5], "MAGNETIC", ParamIDs::magOn,
                { ParamIDs::magAmt.toRawUTF8(), "AMOUNT", " %", "How worn the tape is: dulls the highs, shakes the level and adds dropouts.", "Qué tan gastada está la cinta: apaga los agudos, hace temblar el nivel y suma caídas." },
                {}, {}, nullptr, nullptr,
                { { ParamIDs::magBalance.toRawUTF8(), "WEAR-FLUT", " %", "Balance between wear (0: lost highs and slow level wander) and flutter (100: fast level shaking).", "Balance entre desgaste (0: agudos perdidos y nivel que se mueve lento) y flutter (100: nivel que tiembla rápido)." },
                  { ParamIDs::magRate.toRawUTF8(), "RATE", " Hz", "Speed of the level wander and the flutter.", "Velocidad de la deriva del nivel y del flutter." },
                  { ParamIDs::magDropouts.toRawUTF8(), "DROPOUTS", " %", "How often the tape drops out: sudden dips in volume.", "Qué tan seguido se cae la cinta: bajones bruscos de volumen." },
                  { ParamIDs::magStereo.toRawUTF8(), "STEREO", " %", "Stereo: 0 shakes both channels together, 100 each on its own.", "Stereo: 0 mueve los dos canales juntos, 100 cada uno por su cuenta." },
                  { ParamIDs::magFlux.toRawUTF8(), "FLUX", " %", "Flux: a slow random drift that moves the amount.", "Flux: una deriva aleatoria lenta que mueve la cantidad." } },
                "Turns the magnetic tape module on or off.", "Prende o apaga el módulo de cinta magnética.");

    // ---- visualizadores de cada módulo (el dibujo de arriba de la columna) ----
    {
        using K = ModuleViz::Kind;
        const K kinds[] = { K::noise, K::wobble, K::distort, K::digital, K::space, K::magnetic };
        const char* en[] = {
            "The noise being generated: its waveform, with the crackle of the vinyl or the bursts of static. It scales itself so you can see it at any level.",
            "How far the wobble is moving the delay: a slow wave for the wow, with the fast flutter riding on it. A flat line means no pitch movement.",
            "The curve of the distortion: input across, output up. The diagonal is no distortion; the more drive, the more it bends and flattens. Each type has its own shape.",
            "A sine wave as the digital module leaves it: the steps are the lower sample rate, and the coarse levels are the fewer bits. The grey line is the original.",
            "The tail of the reverb: the first bar is the direct sound, then the predelay gap, then the decay. A longer decay makes a longer tail.",
            "The level of the tape over the last few seconds. Red marks are the dropouts, the sudden dips in volume." };
        const char* es[] = {
            "El ruido que se está generando: su forma de onda, con los chasquidos del vinilo o las ráfagas de la estática. Se escala sola para verse a cualquier nivel.",
            "Cuánto mueve el wobble al retardo: una onda lenta para el wow, con el flutter rápido encima. Una línea plana es que no hay movimiento de tono.",
            "La curva de la distorsión: entrada a lo ancho, salida hacia arriba. La diagonal es sin distorsión; con más drive se dobla y se aplana. Cada tipo tiene su forma.",
            "Una onda sinusoidal como la deja el módulo digital: los escalones son la frecuencia de muestreo más baja y los niveles gruesos son los menos bits. La línea gris es la original.",
            "La cola del reverb: la primera barra es el sonido directo, después el hueco del predelay y después la caída. Más decay es una cola más larga.",
            "El nivel de la cinta en los últimos segundos. Las marcas rojas son las caídas, los bajones bruscos de volumen." };
        for (size_t i = 0; i < modules.size(); ++i)
        {
            modules[i].viz = std::make_unique<ModuleViz>(kinds[i], processorRef.apvts);
            addAndMakeVisible(*modules[i].viz);
            tips.add(*modules[i].viz, en[i], es[i]);
        }
    }

    // ---- globales ----
    addKnob(magnitudeKnob, ParamIDs::magnitude, "MAGNITUDE", " %", true,
            "Magnitude: scales the amount of every module at once. 100 % is what the module knobs say, 0 % turns all the effects off.",
            "Magnitude: escala la cantidad de todos los módulos a la vez. 100 % es lo que dicen las perillas de cada módulo, 0 % apaga todos los efectos.");
    addKnob(widthKnob, ParamIDs::width, "WIDTH", " %", true, "Stereo width of the result: 0 is mono, 100 is untouched, 200 doubles the sides.",
            "Ancho estéreo del resultado: 0 es mono, 100 queda igual, 200 duplica los lados.");
    addKnob(mixKnob, ParamIDs::mix, "MIX", " %", true, "Overall mix between your original signal and the whole effect.",
            "Mezcla general entre tu señal original y todo el efecto.");

    eqTitle.setText("EQ", juce::dontSendNotification);
    eqTitle.setJustificationType(juce::Justification::centredLeft);
    eqTitle.setFont(lookAndFeel.monoFont(15.0f));
    eqTitle.setColour(juce::Label::textColourId, DecimuColours::accentPrimary);
    addAndMakeVisible(eqTitle);
    addAndMakeVisible(eqLed);
    eqLedAttachment = std::make_unique<ButtonAttachment>(processorRef.apvts, ParamIDs::eqOn, eqLed);
    tips.add(eqLed, "Turns the final EQ on or off.", "Prende o apaga el EQ final.");
    addKnob(eqLowKnob, ParamIDs::eqLow, "LOW CUT", " Hz", false, "Low cut: removes the lows below this frequency, after all the effects. At the minimum it does nothing.",
            "Low cut: saca los graves por debajo de esta frecuencia, después de todos los efectos. En el mínimo no hace nada.");
    addKnob(eqHighKnob, ParamIDs::eqHigh, "HIGH CUT", " Hz", false, "High cut: removes the highs above this frequency, after all the effects. At the maximum it does nothing.",
            "High cut: saca los agudos por encima de esta frecuencia, después de todos los efectos. En el máximo no hace nada.");
    addKnob(eqToneKnob, ParamIDs::eqTone, "TONE", "", false, "Tone: tilts the whole sound. To the right it gets brighter and thinner, to the left darker and warmer.",
            "Tone: inclina todo el sonido. Hacia la derecha es más brillante y delgado, hacia la izquierda más oscuro y cálido.");

    setupFader(inputSlider, inputLabel, "Input");
    setupFader(outputSlider, outputLabel, "Output");
    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outputMeter);
    addAndMakeVisible(vuArc);
    addAndMakeVisible(bypassButton);
    inputAttach = std::make_unique<SliderAttachment>(processorRef.apvts, ParamIDs::inGain, inputSlider);
    outputAttach = std::make_unique<SliderAttachment>(processorRef.apvts, ParamIDs::outGain, outputSlider);
    bypassAttach = std::make_unique<ButtonAttachment>(processorRef.apvts, ParamIDs::bypass, bypassButton);
    tips.add(inputSlider, "Input gain, -24 to +12 dB. Push it to drive the distortion harder.", "Ganancia de entrada, -24 a +12 dB. Subila para empujar más la distorsión.");
    tips.add(outputSlider, "Output gain, -24 to +12 dB.", "Ganancia de salida, -24 a +12 dB.");
    tips.add(bypassButton, "Bypass: the audio passes through untouched (the dot goes grey).", "Bypass: el audio pasa sin tocarse (el punto se pone gris).");

    setSize(1260, 750);
    setResizable(true, true);
    setResizeLimits(1100, 690, 1800, 1050);
    startTimerHz(30);
}

DecimulScourEditor::~DecimulScourEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DecimulScourEditor::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    DecimuChrome::paintChassisBackground(g, full, 14.0f);
    DecimuChrome::paintRackHandles(g, full);
    DecimuChrome::paintCornerScrews(g, full, 12.0f);

    DecimuChrome::paintCompartment(g, topbarRect.toFloat(), false, 8.0f);
    DecimuChrome::paintCompartment(g, inputRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, outputRect.toFloat(), true, 8.0f);
    DecimuChrome::paintCompartment(g, eqRect.toFloat(), false, 8.0f);
    for (auto& m : modules)
        DecimuChrome::paintCompartment(g, m.bounds.toFloat(), true, 8.0f);

    auto logoArea = headerLogoArea;
    if (dogImage.isValid())
    {
        auto dogArea = logoArea.removeFromLeft(logoArea.getHeight());
        g.drawImage(dogImage, dogArea.toFloat(), juce::RectanglePlacement::centred);
        logoArea.removeFromLeft(8);
    }
    if (wordmarkImage.isValid())
    {
        auto wordmarkArea = logoArea.removeFromLeft(juce::jmin(logoArea.getWidth(), 190)).reduced(0, 9);
        g.drawImage(wordmarkImage, wordmarkArea.toFloat(),
                    juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
        logoArea.removeFromLeft(14);
    }

    g.setColour(DecimuColours::accentPrimary);
    g.setFont(lookAndFeel.monoFont(24.0f));
    g.drawText("SCOUR", logoArea, juce::Justification::centredLeft);

    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(13.0f));
    auto footer = getLocalBounds().removeFromBottom(22).reduced(16, 3);
    g.drawText("DECIMULABS - SCOUR - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulScourEditor::layoutModule(ModuleUi& m)
{
    auto col = m.bounds.reduced(8, 6);
    auto head = col.removeFromTop(24);
    m.led.setBounds(head.removeFromLeft(24).reduced(2));
    head.removeFromLeft(2);
    m.titleLabel.setBounds(head);
    m.viz->setBounds(col.removeFromTop(60));
    col.removeFromTop(4);

    if (m.hasSelector)
    {
        m.selector.setBounds(col.removeFromTop(m.typeCount * 17 + 6).reduced(2, 0));
        col.removeFromTop(2);
    }

    const int bigH = std::clamp(col.getHeight() * 29 / 100, 118, 160);
    auto bigArea = col.removeFromBottom(bigH);
    placeKnob(m.big, bigArea, 96, true);

    if (m.hasPill)
    {
        auto pillArea = col.removeFromBottom(30).reduced(14, 2);
        m.pill.setBounds(pillArea);
    }

    const int n = (int) m.knobs.size();
    const int rows = std::max(1, (n + 1) / 2);
    const int cellH = std::min(col.getHeight() / rows, 84);
    const int cellW = col.getWidth() / 2;
    auto grid = col.withHeight(cellH * rows);
    for (int i = 0; i < n; ++i)
    {
        auto cell = juce::Rectangle<int>(grid.getX() + (i % 2) * cellW, grid.getY() + (i / 2) * cellH, cellW, cellH);
        placeKnob(*m.knobs[(size_t) i], cell.reduced(1), 58, false);
    }
}

void DecimulScourEditor::resized()
{
    tips.layout(getLocalBounds());

    auto content = getLocalBounds().reduced(22, 18);
    content.removeFromBottom(8);

    topbarRect = content.removeFromTop(66);
    content.removeFromTop(8);

    auto header = topbarRect.reduced(10, 8);
    bypassButton.setBounds(header.removeFromRight(110));
    vuArc.setBounds(header.removeFromRight(96).reduced(2));
    header.removeFromRight(8);
    placeKnob(magnitudeKnob, header.removeFromRight(112), 40, true);
    header.removeFromRight(8);
    const int scopeW = std::clamp(header.getWidth() * 55 / 100, 220, 520);
    scope.setBounds(header.removeFromRight(scopeW).reduced(0, 2));
    header.removeFromRight(10);
    headerLogoArea = header;

    constexpr int gap = 8;
    inputRect = content.removeFromLeft(104);
    content.removeFromLeft(gap);
    outputRect = content.removeFromRight(150);
    content.removeFromRight(gap);
    eqRect = content.removeFromBottom(92);
    content.removeFromBottom(gap);

    const int moduleW = (content.getWidth() - 5 * gap) / 6;
    for (int i = 0; i < 6; ++i)
    {
        modules[(size_t) i].bounds = juce::Rectangle<int>(content.getX() + i * (moduleW + gap), content.getY(), moduleW, content.getHeight());
        layoutModule(modules[(size_t) i]);
    }

    // EQ final: LED + título a la izquierda, tres perillas repartidas
    {
        auto area = eqRect.reduced(10, 6);
        auto left = area.removeFromLeft(110);
        eqLed.setBounds(left.removeFromLeft(24).withSizeKeepingCentre(22, 22));
        left.removeFromLeft(4);
        eqTitle.setBounds(left);
        const int w = area.getWidth() / 3;
        placeKnob(eqLowKnob, area.removeFromLeft(w), 52, false);
        placeKnob(eqHighKnob, area.removeFromLeft(w), 52, false);
        placeKnob(eqToneKnob, area, 52, false);
    }

    // entrada
    {
        auto col = inputRect.reduced(8, 10);
        inputLabel.setBounds(col.removeFromBottom(20));
        inputMeter.setBounds(col.removeFromLeft(16));
        col.removeFromLeft(2);
        inputSlider.setBounds(col);
    }
    // salida: WIDTH y MIX arriba, fader y medidor abajo
    {
        auto col = outputRect.reduced(8, 10);
        auto knobs = col.removeFromTop(std::min(col.getHeight() * 44 / 100, 250));
        placeKnob(widthKnob, knobs.removeFromTop(knobs.getHeight() / 2), 60, true);
        placeKnob(mixKnob, knobs, 60, true);
        col.removeFromTop(6);
        outputLabel.setBounds(col.removeFromBottom(20));
        outputMeter.setBounds(col.removeFromLeft(16));
        col.removeFromLeft(2);
        outputSlider.setBounds(col);
    }
}

void DecimulScourEditor::timerCallback()
{
    DecimulScourProcessor::Frame frames[256];
    float mins[256], maxs[256];
    int count;
    while ((count = processorRef.readFrames(frames, 256)) > 0)
    {
        for (int i = 0; i < count; ++i)
        {
            mins[i] = frames[i].minV;
            maxs[i] = frames[i].maxV;
            for (auto& m : modules)
                m.viz->pushFrame(frames[i]);
        }
        scope.pushFrames(mins, maxs, count);
        if (count < 256)
            break;
    }
    scope.repaint();

    // selectores y botones que no tienen attachment propio
    const juce::String typeIds[] = { ParamIDs::noiseType, {}, ParamIDs::disType, {}, {}, {} };
    for (size_t i = 0; i < modules.size(); ++i)
        if (modules[i].hasSelector)
            modules[i].selector.setSelectedIndex((int) processorRef.apvts.getRawParameterValue(typeIds[i])->load());
    modules[0].pill.setButtonText(modules[0].pill.getToggleState() ? "POST" : "PRE");

    inputMeter.setLevelDb(processorRef.inputPeakDb.load());
    inputMeter.repaint();
    const float outDb = processorRef.outputPeakDb.load();
    outputMeter.setLevelDb(outDb);
    outputMeter.repaint();
    vuArc.setLevelDb(outDb);
    vuArc.repaint();
    bypassButton.repaint();
    for (auto& m : modules)
    {
        m.led.repaint();
        m.viz->repaint();
    }
}
