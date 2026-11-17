#include "PluginEditor.h"
#include "FactoryPresets.h"
#include "BinaryData.h"
#include <cstring>

// ======================================================================= dibujos
namespace fmui
{
    // Posición de op1..op4 en cada algoritmo (columnas x 0..3, filas y 0..1), conexiones y portadores.
    struct AlgShape { float p[4][2]; int edges[3][2]; int numEdges; bool car[4]; };
    static const AlgShape shapes[8] = {
        { { {0,0},{1,0},{2,0},{3,0} }, { {0,1},{1,2},{2,3} }, 3, { 0,0,0,1 } },
        { { {0,0},{0,1},{1,.5f},{2,.5f} }, { {0,2},{1,2},{2,3} }, 3, { 0,0,0,1 } },
        { { {1,0},{0,1},{1,1},{2,.5f} }, { {0,3},{1,2},{2,3} }, 3, { 0,0,0,1 } },
        { { {0,0},{1,0},{1,1},{2,.5f} }, { {0,1},{1,3},{2,3} }, 3, { 0,0,0,1 } },
        { { {0,0},{1,0},{0,1},{1,1} }, { {0,1},{2,3},{0,0} }, 2, { 0,1,0,1 } },
        { { {1,0},{0,1},{1,1},{2,1} }, { {0,1},{0,2},{0,3} }, 3, { 0,1,1,1 } },
        { { {0,0},{0,1},{1,1},{2,1} }, { {0,1},{0,0},{0,0} }, 1, { 0,1,1,1 } },
        { { {0,0},{1,0},{2,0},{3,0} }, { {0,0},{0,0},{0,0} }, 0, { 1,1,1,1 } },
    };

    bool isCarrier(int alg, int op) { return shapes[juce::jlimit(0, 7, alg)].car[op]; }

    AlgorithmSelector::AlgorithmSelector(juce::RangedAudioParameter& p, const DecimuLookAndFeel& laf)
        : lnf(laf), attachment(p, [this](float v) { selected = juce::jlimit(0, 7, (int) std::lround(v)); repaint(); })
    {
        attachment.sendInitialUpdate();
    }

    juce::Rectangle<float> AlgorithmSelector::cell(int i) const
    {
        auto b = getLocalBounds().toFloat();
        const float w = b.getWidth() / 4.0f, h = b.getHeight() / 2.0f;
        return { b.getX() + (float) (i % 4) * w, b.getY() + (float) (i / 4) * h, w, h };
    }

    void AlgorithmSelector::mouseDown(const juce::MouseEvent& e)
    {
        for (int i = 0; i < 8; ++i)
            if (cell(i).contains(e.position)) { attachment.setValueAsCompleteGesture((float) i); return; }
    }

    void AlgorithmSelector::paint(juce::Graphics& g)
    {
        const auto accent = DecimuColours::accentPrimary;
        for (int i = 0; i < 8; ++i)
        {
            auto c = cell(i).reduced(3.0f);
            const bool on = i == selected;
            g.setColour(on ? accent.withAlpha(0.14f) : DecimuColours::trackFill);
            g.fillRoundedRectangle(c, 5.0f);
            g.setColour(on ? accent : DecimuColours::textMuted.withAlpha(0.35f));
            g.drawRoundedRectangle(c, 5.0f, on ? 1.6f : 1.0f);

            auto numRow = c.removeFromBottom(15.0f).toNearestInt();
            retro::drawText(g, juce::String(i + 1), numRow, 2, on ? juce::Colours::white : retro::md9(116, 116, 172), juce::Justification::centred);
            if (on && (juce::Time::getMillisecondCounter() / 350) % 2 == 0)
                retro::drawText(g, ">", numRow.withTrimmedLeft(numRow.getWidth() / 2 - 26), 2, retro::md9(255, 206, 52), juce::Justification::centredLeft);

            const auto& s = shapes[i];
            const float box = juce::jmin(c.getHeight() / 2.6f, c.getWidth() / 5.2f);
            const float gx = (c.getWidth() - box) / 3.0f, gy = c.getHeight() - box * 1.4f;
            auto pos = [&](int o) { return juce::Point<float>(c.getX() + box * 0.1f + s.p[o][0] * gx * 0.92f, c.getY() + box * 0.25f + s.p[o][1] * gy); };
            g.setColour(DecimuColours::textMuted.withAlpha(0.8f));
            for (int k = 0; k < s.numEdges; ++k)
            {
                const auto a = pos(s.edges[k][0]) + juce::Point<float>(box / 2, box / 2);
                const auto b = pos(s.edges[k][1]) + juce::Point<float>(box / 2, box / 2);
                g.drawLine({ a, b }, 1.3f);
            }
            // realimentación del op1
            const auto p0 = pos(0);
            juce::Path fb;
            fb.startNewSubPath(p0.x + box, p0.y + box * 0.3f);
            fb.lineTo(p0.x + box * 1.25f, p0.y + box * 0.3f);
            fb.lineTo(p0.x + box * 1.25f, p0.y - box * 0.18f);
            fb.lineTo(p0.x + box * 0.5f, p0.y - box * 0.18f);
            fb.lineTo(p0.x + box * 0.5f, p0.y);
            g.setColour(DecimuColours::accentSecondarySoft.withAlpha(0.8f));
            g.strokePath(fb, juce::PathStrokeType(1.0f));
            for (int o = 0; o < 4; ++o)
            {
                const auto r = juce::Rectangle<float>(box, box).withPosition(pos(o));
                g.setColour(s.car[o] ? DecimuColours::accentRec.withAlpha(on ? 0.85f : 0.45f) : DecimuColours::panel.brighter(0.2f));
                g.fillRoundedRectangle(r, 2.5f);
                g.setColour(s.car[o] ? DecimuColours::accentRec : DecimuColours::textMuted);
                g.drawRoundedRectangle(r, 2.5f, 1.0f);
                g.setColour(DecimuColours::text);
                g.setFont(lnf.monoFont(box * 0.95f));
                g.drawText(juce::String(o + 1), r, juce::Justification::centred);
            }
        }
    }

    void EnvelopeView::paint(juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(retro::md9(0, 52, 0).darker(0.5f));
        g.fillRoundedRectangle(b, 4.0f);
        g.setColour(retro::md9(0, 87, 52));
        g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);

        auto v = [&](const char* k) { return apvts.getRawParameterValue("op" + juce::String(op + 1) + "_" + k)->load(); };
        const float ar = v("ar"), d1r = v("d1r"), d2r = v("d2r"), rr = v("rr"), sl = v("sl"), tl = v("tl");
        const float W = b.getWidth(), H = b.getHeight(), x0 = b.getX(), y0 = b.getY();
        const float top = y0 + 4 + (tl / 127.0f) * (H - 8);
        auto lv = [&](float l) { return top + (y0 + H - 4 - top) * l; };
        auto seg = [&](float r) { return r > 0 ? juce::jmax(3.0f, (W / 6.0f) * (1.0f - r / 33.0f)) : W / 4.5f; };
        const float xa = x0 + seg(ar), x1 = xa + seg(d1r), x2 = x1 + (d2r > 0 ? seg(d2r) * 1.4f : W / 4.0f);
        const float sll = sl / 15.0f, xr = juce::jmin(x0 + W - 2, x2 + seg(rr * 2 + 1));

        juce::Path p;
        p.startNewSubPath(x0, y0 + H - 4);
        p.lineTo(xa, top);
        p.lineTo(x1, lv(sll));
        p.lineTo(x2, d2r > 0 ? lv(juce::jmin(1.0f, sll + 0.5f)) : lv(sll));
        p.lineTo(xr, y0 + H - 4);
        const auto green = retro::md9(116, 255, 172);
        const auto col = tl >= 127 ? green.withAlpha(0.25f) : green;
        g.setColour(col.withAlpha(0.18f));
        juce::Path fill(p); fill.lineTo(x0, y0 + H - 4); fill.closeSubPath();
        g.fillPath(fill);
        g.setColour(col);
        g.strokePath(p, juce::PathStrokeType(1.8f));
        g.setColour(green.withAlpha(0.3f));
        g.drawVerticalLine((int) x2, y0 + 3, y0 + H - 3);
        retro::paintCrt(g, b);
    }

    void RegisterView::paint(juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(retro::md9(0, 52, 0).darker(0.5f));
        g.fillRoundedRectangle(b, 4.0f);
        g.setColour(retro::md9(0, 87, 52));
        g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);

        auto hx = [](int v) { return juce::String::toHexString(v).toUpperCase().paddedLeft('0', 2); };
        auto r = [&](int a) { return (int) proc.regView[(size_t) a].load(); };
        static const char* names[7] = { "DT/MUL", "TL", "KS/AR", "AM/D1R", "D2R", "SL/RR", "SSG-EG" };

        const float lineH = juce::jmin(16.0f, (b.getHeight() - 8) / 10.0f);
        g.setFont(lnf.monoFont(lineH * 1.05f));
        auto area = b.reduced(10.0f, 5.0f);
        auto row = [&](const juce::String& left, const juce::String& right, juce::Colour c)
        {
            auto line = area.removeFromTop(lineH);
            const auto l = line.removeFromLeft(line.getWidth() * 0.42f);
            g.setColour(c.withAlpha(0.25f));
            g.drawText(left, l.translated(0.8f, 0.6f), juce::Justification::centredLeft);
            g.drawText(right, line.translated(0.8f, 0.6f), juce::Justification::centredLeft);
            g.setColour(c);
            g.drawText(left, l, juce::Justification::centredLeft);
            g.drawText(right, line, juce::Justification::centredLeft);
        };
        const auto green = retro::md9(116, 255, 172), dim = green.withAlpha(0.5f);
        row("YM2612  CH1", "OP1 OP2 OP3 OP4", dim);
        static const int order[4] = { 0, 8, 4, 12 };
        for (int k = 0; k < 7; ++k)
        {
            juce::String vals;
            for (int o = 0; o < 4; ++o) vals << hx(r(0x30 + k * 0x10 + order[o])) << "  ";
            row("$" + juce::String::toHexString(3 + k).toUpperCase() + "x " + names[k], vals, green);
        }
        row("$B0 ALG/FB  " + hx(r(0xB0)), "$B4 LR/LFO " + hx(r(0xB4)), green);
        row("$22 LFO     " + hx(r(0x22)), "", green);
        retro::paintCrt(g, b);
    }
}

// ======================================================================= editor
void DecimulFMEditor::addKnob(Knob& k, const juce::String& paramID, const juce::String& caption)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 15);
    addAndMakeVisible(k.slider);
    k.label.set(caption, retro::md9(206, 206, 255));
    addAndMakeVisible(k.label);
    k.attachment = std::make_unique<SliderAttachment>(proc.apvts, paramID, k.slider);
}

void DecimulFMEditor::placeKnob(Knob& k, juce::Rectangle<int> cell)
{
    k.label.setBounds(cell.removeFromTop(16));
    const int side = juce::jmin(cell.getWidth(), cell.getHeight() - 19);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, juce::jmax(40, cell.getWidth() - 2), 19);
    k.slider.setBounds(cell.withSizeKeepingCentre(juce::jmax(side, cell.getWidth()), side + 19));
}

static void styleCaption(juce::Label& l, const juce::String& text, juce::Colour c = DecimuColours::textMuted)
{
    l.setText(text, juce::dontSendNotification);
    l.setColour(juce::Label::textColourId, c);
    l.setFont(juce::Font(juce::FontOptions(12.0f)));
}

DecimulFMEditor::DecimulFMEditor(DecimulFMProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&lookAndFeel);
    wordmarkImage = juce::ImageFileFormat::loadFrom(BinaryData::wordmark_png, (size_t) BinaryData::wordmark_pngSize);
    dogImage = juce::ImageFileFormat::loadFrom(BinaryData::dog_png, (size_t) BinaryData::dog_pngSize);

    // ---- barra de arriba ----
    refreshPresetBox();
    presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) proc.setCurrentProgram(presetBox.getSelectedId() - 1); };
    addAndMakeVisible(presetBox);
    prevButton.onClick = [this] { proc.setCurrentProgram((proc.getCurrentProgram() + factory::count - 1) % factory::count); };
    nextButton.onClick = [this] { proc.setCurrentProgram((proc.getCurrentProgram() + 1) % factory::count); };
    addAndMakeVisible(prevButton);
    addAndMakeVisible(nextButton);
    ladderButton.setClickingTogglesState(true);
    ladderButton.setColour(juce::TextButton::buttonColourId, DecimuColours::trackFill);
    ladderButton.setColour(juce::TextButton::buttonOnColourId, DecimuColours::accentSecondary.withAlpha(0.75f));
    ladderButton.setColour(juce::TextButton::textColourOffId, DecimuColours::textMuted);
    ladderButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addAndMakeVisible(ladderButton);
    ladderAttach = std::make_unique<ButtonAttachment>(proc.apvts, "ladder", ladderButton);
    addKnob(outputKnob, "gain", "OUTPUT");
    addKnob(bendKnob, "bendRange", "BEND");

    // ---- algoritmo ----
    algSelector = std::make_unique<fmui::AlgorithmSelector>(*proc.apvts.getParameter("alg"), lookAndFeel);
    addAndMakeVisible(*algSelector);
    algLabel.set("ALGORITHM", juce::Colours::white); algLabel.just = juce::Justification::centredLeft;
    addAndMakeVisible(algLabel);
    addKnob(fbKnob, "fb", "FEEDBACK");

    // ---- LFO ----
    lfoLabel.set("LFO", juce::Colours::white); lfoLabel.just = juce::Justification::centredLeft;
    addAndMakeVisible(lfoLabel);
    addAndMakeVisible(lfoOn);
    lfoOnAttach = std::make_unique<ButtonAttachment>(proc.apvts, "lfoOn", lfoOn);
    styleCaption(lfoOnLabel, "ON");
    addAndMakeVisible(lfoOnLabel);
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("lfoFreq"))) lfoRate.addItemList(c->choices, 1);
    addAndMakeVisible(lfoRate);
    lfoRateAttach = std::make_unique<ComboAttachment>(proc.apvts, "lfoFreq", lfoRate);
    addKnob(amsKnob, "ams", "AMS");
    addKnob(pmsKnob, "pms", "PMS");

    // ---- registros ----
    regView = std::make_unique<fmui::RegisterView>(proc, lookAndFeel);
    addAndMakeVisible(*regView);
    regLabel.set("CHIP REGISTERS", juce::Colours::white); regLabel.just = juce::Justification::centredLeft;
    addAndMakeVisible(regLabel);

    // ---- operadores ----
    static const char* keys[9] = { "ar", "d1r", "sl", "d2r", "rr", "tl", "ks", "mul", "dt" };
    static const char* caps[9] = { "AR", "D1R", "SL", "D2R", "RR", "TL", "KS", "MUL", "DT" };
    static const char* tipEn[9] = {
        "AR - attack rate, 0 to 31: how fast the sound appears. 31 = instant, low values = slow fade-in (strings, pads).",
        "D1R - first decay, 0 to 31: how fast it falls after the attack, down to SL.",
        "SL - sustain level, 0 to 15: where the first decay stops. 0 = stays at full level, 15 = falls almost to silence.",
        "D2R - second decay, 0 to 31: keeps falling while the key is held. 0 = holds forever (organ).",
        "RR - release, 0 to 15: how long it rings after you let go of the key. Low = long tail.",
        "TL - total level, 0 to 127: 0 is loudest, 127 is silent. On an operator that modulates, this is the brightness.",
        "KS - key scale, 0 to 3: higher notes get faster envelopes, like a real piano.",
        "MUL - frequency multiplier, 0 to 15 (0 = half). On modulators: 1 warm, 2 hollow, odd values like 7 or 11 give bells and metal.",
        "DT - detune, -3 to +3: a tiny pitch shift. Detuning two operators against each other makes the sound wider." };
    static const char* tipEs[9] = {
        "AR - ataque, 0 a 31: qué tan rápido aparece el sonido. 31 = instantáneo, valores bajos = entra despacio (cuerdas, colchones).",
        "D1R - primera caída, 0 a 31: qué tan rápido baja después del ataque, hasta el nivel SL.",
        "SL - nivel de sostén, 0 a 15: dónde se frena la primera caída. 0 = queda al máximo, 15 = cae casi al silencio.",
        "D2R - segunda caída, 0 a 31: sigue bajando mientras tenés la tecla apretada. 0 = se mantiene para siempre (órgano).",
        "RR - suelta, 0 a 15: cuánto sigue sonando al soltar la tecla. Bajo = cola larga.",
        "TL - volumen, 0 a 127: 0 es lo más fuerte, 127 es mudo. En un operador que modula, esto es el brillo.",
        "KS - escala de teclado, 0 a 3: las notas agudas tienen envolventes más rápidas, como un piano de verdad.",
        "MUL - multiplicador de frecuencia, 0 a 15 (0 = la mitad). En los que modulan: 1 cálido, 2 hueco, impares como 7 u 11 dan campanas y metal.",
        "DT - desafinado, -3 a +3: corre apenas la afinación. Desafinar dos operadores entre sí ensancha el sonido." };
    for (int o = 0; o < 4; ++o)
    {
        auto& op = ops[(size_t) o];
        const juce::String pre = "op" + juce::String(o + 1) + "_";
        op.env = std::make_unique<fmui::EnvelopeView>(proc.apvts, o);
        addAndMakeVisible(*op.env);
        for (int k = 0; k < 9; ++k)
        {
            addKnob(op.knobs[(size_t) k], pre + keys[k], caps[k]);
            tips.add(op.knobs[(size_t) k].slider, tipEn[k], tipEs[k]);
        }
        addAndMakeVisible(op.am);
        op.amAttach = std::make_unique<ButtonAttachment>(proc.apvts, pre + "am", op.am);
        styleCaption(op.amLabel, "AM");
        addAndMakeVisible(op.amLabel);
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter(pre + "ssg"))) op.ssg.addItemList(c->choices, 1);
        addAndMakeVisible(op.ssg);
        op.ssgAttach = std::make_unique<ComboAttachment>(proc.apvts, pre + "ssg", op.ssg);
        op.roleLabel.just = juce::Justification::centredRight;
        addAndMakeVisible(op.roleLabel);

        tips.add(*op.env, "Envelope of this operator: how its level moves in time (attack, decays, release). The higher it starts, the louder (TL).",
                 "Envolvente de este operador: cómo se mueve su nivel en el tiempo (ataque, caídas, suelta). Más arriba empieza, más fuerte (TL).");
        tips.add(op.am, "AM: this operator follows the LFO tremolo (set its depth with AMS).",
                 "AM: este operador sigue el trémolo del LFO (la profundidad se elige con AMS).");
        tips.add(op.ssg, "SSG-EG: repeating envelopes of the chip (saw, triangle, one-shot). Great for pulsing pads and weird textures.",
                 "SSG-EG: envolventes que se repiten, propias del chip (sierra, triángulo, una vez). Sirven para colchones que laten y texturas raras.");
    }

    // ---- textos de ayuda ----
    addAndMakeVisible(tips.getSwitch());
    tips.add(presetBox, "Factory sounds, grouped by family. Also selectable from your DAW's program list or with MIDI program change.",
             "Sonidos de fábrica, agrupados por familia. También se eligen desde la lista de programas del DAW o con program change MIDI.");
    tips.add(prevButton, "Previous sound", "Sonido anterior");
    tips.add(nextButton, "Next sound", "Sonido siguiente");
    tips.add(ladderButton, "DAC distortion of the first Mega Drive model (the 'ladder effect'): a bit of grit on quiet sounds. Off = the cleaner later models.",
             "Distorsión del DAC de la primera Mega Drive (el 'ladder effect'): un poco de suciedad en los sonidos bajos. Apagado = los modelos posteriores, más limpios.");
    tips.add(outputKnob.slider, "Output level, -24 to +18 dB.", "Volumen de salida, -24 a +18 dB.");
    tips.add(bendKnob.slider, "Pitch bend range in semitones (0 to 12).", "Rango del pitch bend en semitonos (0 a 12).");
    tips.add(*algSelector, "Algorithm: how the 4 operators connect. Red boxes are heard; the others change their timbre (FM). Algorithm 1 chains everything (brightest), 8 adds the four (organ).",
             "Algoritmo: cómo se conectan los 4 operadores. Los cuadraditos rojos se escuchan; los otros les cambian el timbre (FM). El 1 encadena todo (lo más brillante), el 8 suma los cuatro (órgano).");
    tips.add(fbKnob.slider, "Feedback of operator 1 on itself, 0 to 7. Turns a pure tone into a saw and, at the top, into noise. THE Mega Drive sound.",
             "Realimentación del operador 1 sobre sí mismo, 0 a 7. Convierte un tono puro en una sierra y, al máximo, en ruido. EL sonido de la Mega Drive.");
    tips.add(lfoOn, "Turns the chip's single LFO on (shared by the whole chip).", "Prende el único LFO del chip (lo comparte todo el chip).");
    tips.add(lfoRate, "LFO speed: the 8 rates the chip offers.", "Velocidad del LFO: las 8 que ofrece el chip.");
    tips.add(amsKnob.slider, "AMS - tremolo depth: 0, 1.4, 5.9 or 11.8 dB. Only on operators with AM lit.",
             "AMS - profundidad del trémolo: 0; 1,4; 5,9 u 11,8 dB. Solo en los operadores con AM prendido.");
    tips.add(pmsKnob.slider, "PMS - vibrato depth, 0 to 7 (up to about 80 cents).", "PMS - profundidad del vibrato, 0 a 7 (hasta unos 80 centésimos).");
    tips.add(*regView, "The bytes this sound writes into the chip, live (channel 1). The same values a Mega Drive game would send.",
             "Los bytes que este sonido le escribe al chip, en vivo (canal 1). Los mismos valores que mandaría un juego de Mega Drive.");

    // ---- góndola de cartuchos ----
    {
        juce::Array<retro::CartridgeShelf::Family> fams;
        for (int i = 0; i < factory::count; ++i)
        {
            const auto cat = juce::String::fromUTF8(factory::presets[i].cat);
            if (fams.isEmpty() || fams.getReference(fams.size() - 1).name != cat) fams.add({ cat, {}, {} });
            fams.getReference(fams.size() - 1).programs.add(i);
            fams.getReference(fams.size() - 1).names.add(proc.getProgramName(i));
        }
        shelf = std::make_unique<retro::CartridgeShelf>(fams, [this](int prog) { proc.setCurrentProgram(prog); });
        shelf->onClose = [this] { shelf->setVisible(false); };
        addChildComponent(*shelf);
    }
    shelfButton.onClick = [this] { shelf->setCurrent(proc.getCurrentProgram()); shelf->setVisible(true); shelf->toFront(true); shelf->grabKeyboardFocus(); };
    shelfButton.setColour(juce::TextButton::buttonColourId, retro::md9(52, 52, 87));
    addAndMakeVisible(shelfButton);
    tips.add(shelfButton, "Browse the factory sounds as a shelf of cartridges, one per family.",
             "Recorrer los sonidos de fábrica como una góndola de cartuchos, uno por familia.");

    // ---- arranque tipo consola (una vez por sesión) ----
    if (! retro::BootScreen::alreadyShown)
    {
        boot = std::make_unique<retro::BootScreen>(dogImage);
        boot->onDone = [this] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<DecimulFMEditor>(this)] { if (sp != nullptr) sp->boot.reset(); }); };
        addAndMakeVisible(*boot);
    }

    setSize(1200, 780);
    setResizable(true, true);
    setResizeLimits(1100, 740, 1800, 1150);
    startTimerHz(20);
}

DecimulFMEditor::~DecimulFMEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DecimulFMEditor::refreshPresetBox()
{
    presetBox.clear(juce::dontSendNotification);
    const char* lastCat = nullptr;
    for (int i = 0; i < factory::count; ++i)
    {
        if (lastCat == nullptr || std::strcmp(lastCat, factory::presets[i].cat) != 0)
            presetBox.addSectionHeading(juce::String::fromUTF8(lastCat = factory::presets[i].cat));
        presetBox.addItem(proc.getProgramName(i), i + 1);
    }
    presetBox.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification);
    lastProgram = proc.getCurrentProgram();
}

void DecimulFMEditor::timerCallback()
{
    if (proc.getCurrentProgram() != lastProgram)
    {
        lastProgram = proc.getCurrentProgram();
        presetBox.setSelectedId(lastProgram + 1, juce::dontSendNotification);
    }
    const int alg = (int) proc.apvts.getRawParameterValue("alg")->load();
    for (int o = 0; o < 4; ++o)
    {
        auto& op = ops[(size_t) o];
        const bool car = fmui::isCarrier(alg, o);
        op.roleLabel.set(car ? "CARRIER" : "MODULATOR", car ? retro::md9(255, 87, 52) : retro::md9(116, 116, 172));
        if (op.env->carrier != car) { op.env->carrier = car; repaint(op.area); }
        op.env->repaint();
    }
    regView->repaint();
    algSelector->repaint();
}

void DecimulFMEditor::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    retro::paintGrid(g, full, 22.0f, 14.0f);
    DecimuChrome::paintRackHandles(g, full);
    DecimuChrome::paintCornerScrews(g, full, 12.0f);

    // perilleros: negro liso sobre la cuadrícula, borde gris fino (portadoras con borde de acento)
    auto panel = [&g](juce::Rectangle<int> r, juce::Colour edge)
    {
        auto f = r.toFloat();
        g.setColour(juce::Colours::black.withAlpha(0.8f)); g.fillRoundedRectangle(f, 8.0f);
        g.setColour(edge); g.drawRoundedRectangle(f.reduced(0.5f), 8.0f, 1.0f);
    };
    const auto grey = juce::Colours::white.withAlpha(0.22f);
    panel(topbarRect, grey); panel(algRect, grey); panel(lfoRect, grey); panel(regRect, grey);

    const int alg = (int) proc.apvts.getRawParameterValue("alg")->load();
    for (int o = 0; o < 4; ++o)
    {
        const auto& op = ops[(size_t) o];
        const bool car = fmui::isCarrier(alg, o);
        panel(op.area, car ? DecimuColours::accentPrimary.withAlpha(0.7f) : grey);
        retro::drawText(g, "OP" + juce::String(o + 1), op.area.reduced(12, 6).removeFromTop(22), 3, DecimuColours::accentPrimary);
    }

    auto logoArea = headerLogoArea;
    logoArea.removeFromLeft(10);
    if (wordmarkImage.isValid())
    {
        auto wordmarkArea = logoArea.removeFromLeft(juce::jmin(logoArea.getWidth() - 70, 232)).reduced(0, 4);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(wordmarkImage, wordmarkArea.toFloat(), juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
        logoArea.removeFromLeft(18);
    }
    retro::drawTextGradient(g, "FM", logoArea.removeFromLeft(66), 5, retro::md9(255, 255, 172), retro::md9(206, 87, 0));
    if (logoArea.getWidth() > retro::textWidth("YM2612", 2))
        retro::drawText(g, "YM2612", logoArea, 2, retro::md9(172, 172, 206));

    g.setColour(DecimuColours::textMuted);
    g.setFont(lookAndFeel.monoFont(13.0f));
    auto footer = getLocalBounds().removeFromBottom(22).reduced(16, 3);
    g.drawText("DECIMULABS - FM v" JucePlugin_VersionString " - GPLv3", footer, juce::Justification::centredLeft);
    g.drawText("decimulabs.com", footer, juce::Justification::centredRight);
}

void DecimulFMEditor::resized()
{
    tips.layout(getLocalBounds());
    auto content = getLocalBounds().reduced(22, 18);
    content.removeFromBottom(10);
    constexpr int gap = 8;

    // barra de arriba
    topbarRect = content.removeFromTop(86);
    content.removeFromTop(gap);
    {
        auto h = topbarRect.reduced(10, 4);
        // logo con lugar fijo: perro + DECIMULABS + FM
        headerLogoArea = h.removeFromLeft(juce::jmin(h.getWidth() / 3, 64 + 8 + 170 + 18 + 70));
        h.removeFromLeft(10);
        placeKnob(outputKnob, h.removeFromRight(70));
        h.removeFromRight(4);
        placeKnob(bendKnob, h.removeFromRight(62));
        h.removeFromRight(10);
        ladderButton.setBounds(h.removeFromRight(104).withSizeKeepingCentre(104, 30));
        h.removeFromRight(12);
        // lo que queda: CARTUCHOS + selector (el selector se achica primero)
        shelfButton.setBounds(h.removeFromLeft(132).withSizeKeepingCentre(132, 30));
        h.removeFromLeft(10);
        auto presetArea = h.withSizeKeepingCentre(juce::jmin(380, h.getWidth()), 30).withX(h.getX());
        nextButton.setBounds(presetArea.removeFromRight(30));
        presetArea.removeFromRight(4);
        prevButton.setBounds(presetArea.removeFromLeft(30));
        presetArea.removeFromLeft(4);
        presetBox.setBounds(presetArea);
    }
    if (shelf) shelf->setBounds(getLocalBounds());
    if (boot) boot->setBounds(getLocalBounds());

    // fila del medio: algoritmo | LFO | registros
    auto mid = content.removeFromTop(juce::jmax(200, content.getHeight() * 36 / 100));
    content.removeFromTop(gap);
    regRect = mid.removeFromRight(juce::jmin(330, mid.getWidth() * 30 / 100));
    mid.removeFromRight(gap);
    lfoRect = mid.removeFromRight(juce::jmin(230, mid.getWidth() * 28 / 100));
    mid.removeFromRight(gap);
    algRect = mid;
    {
        auto a = algRect.reduced(12, 8);
        algLabel.setBounds(a.removeFromTop(18));
        placeKnob(fbKnob, a.removeFromRight(86).withSizeKeepingCentre(86, juce::jmin(110, a.getHeight())));
        a.removeFromRight(6);
        algSelector->setBounds(a);
    }
    {
        auto l = lfoRect.reduced(12, 8);
        lfoLabel.setBounds(l.removeFromTop(18));
        auto row = l.removeFromTop(30);
        lfoOn.setBounds(row.removeFromLeft(18).withSizeKeepingCentre(16, 16));
        lfoOnLabel.setBounds(row.removeFromLeft(30));
        lfoRate.setBounds(row.reduced(0, 2));
        l.removeFromTop(6);
        auto knobs = l.withSizeKeepingCentre(l.getWidth(), juce::jmin(l.getHeight(), 110));
        placeKnob(amsKnob, knobs.removeFromLeft(knobs.getWidth() / 2).reduced(4, 0));
        placeKnob(pmsKnob, knobs.reduced(4, 0));
    }
    {
        auto r = regRect.reduced(12, 8);
        regLabel.setBounds(r.removeFromTop(18));
        regView->setBounds(r);
    }

    // los 4 operadores
    const int w = (content.getWidth() - 3 * gap) / 4;
    for (int o = 0; o < 4; ++o)
    {
        auto& op = ops[(size_t) o];
        op.area = content.removeFromLeft(w);
        content.removeFromLeft(gap);
        auto a = op.area.reduced(12, 6);
        op.roleLabel.setBounds(a.removeFromTop(22));
        a.removeFromTop(2);
        op.env->setBounds(a.removeFromTop(juce::jmax(44, a.getHeight() / 5)));
        a.removeFromTop(4);
        auto bottom = a.removeFromBottom(26);
        op.am.setBounds(bottom.removeFromLeft(18).withSizeKeepingCentre(16, 16));
        op.amLabel.setBounds(bottom.removeFromLeft(30));
        op.ssg.setBounds(bottom.reduced(0, 1));
        a.removeFromBottom(4);
        // 3 filas de 3 perillas
        const int rh = a.getHeight() / 3;
        for (int rowI = 0; rowI < 3; ++rowI)
        {
            auto row = a.removeFromTop(rh);
            const int cw = row.getWidth() / 3;
            for (int c = 0; c < 3; ++c)
                placeKnob(op.knobs[(size_t) (rowI * 3 + c)], row.removeFromLeft(cw).reduced(3, 1));
        }
    }
}

juce::AudioProcessorEditor* DecimulFMProcessor::createEditor() { return new DecimulFMEditor(*this); }
