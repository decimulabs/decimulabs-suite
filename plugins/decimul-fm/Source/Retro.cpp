#include "Retro.h"
#include "PixelFont.h"

namespace retro
{
    // ---------------------------------------------------------------- colores
    static uint8_t level(uint8_t v)
    {
        static const uint8_t L[8] = { 0, 52, 87, 116, 144, 172, 206, 255 };
        int best = 0;
        for (int i = 1; i < 8; ++i) if (std::abs((int) L[i] - v) < std::abs((int) L[best] - v)) best = i;
        return L[best];
    }
    juce::Colour md9(juce::Colour c) { return juce::Colour(level(c.getRed()), level(c.getGreen()), level(c.getBlue()), c.getAlpha()); }
    juce::Colour md9(uint8_t r, uint8_t g, uint8_t b) { return md9(juce::Colour(r, g, b)); }

    // ---------------------------------------------------------------- letra 5x7
    static const pixelfont::Glyph* find(juce::juce_wchar cp)
    {
        for (int i = 0; i < pixelfont::count; ++i)
            if (pixelfont::glyphs[i].cp == (uint32_t) cp) return &pixelfont::glyphs[i];
        if (cp >= 'a' && cp <= 'z') return nullptr;
        return nullptr;
    }

    juce::String upper(const juce::String& t)
    {
        juce::String u = t.toUpperCase();
        const char* from[] = { "á", "é", "í", "ó", "ú", "ñ", "ü" };
        const char* to[]   = { "Á", "É", "Í", "Ó", "Ú", "Ñ", "Ü" };
        for (int i = 0; i < 7; ++i) u = u.replace(juce::String::fromUTF8(from[i]), juce::String::fromUTF8(to[i]));
        return u;
    }

    void drawTinted(juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> r, juce::Colour c)
    {
        if (! img.isValid()) return;
        juce::Graphics::ScopedSaveState s(g);
        g.setColour(c);
        g.drawImage(img, r, juce::RectanglePlacement::centred, true);
    }

    int textWidth(const juce::String& text, int scale) { return juce::jmax(0, text.length() * 6 - 1) * scale; }

    void drawText(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, int scale,
                  juce::Colour colour, juce::Justification just, bool shadow)
    {
        const int w = textWidth(text, scale), h = 7 * scale;
        int x = area.getX(), y = area.getY() + (area.getHeight() - h) / 2;
        if (just.testFlags(juce::Justification::horizontallyCentred)) x = area.getX() + (area.getWidth() - w) / 2;
        else if (just.testFlags(juce::Justification::right)) x = area.getRight() - w;
        if (just.testFlags(juce::Justification::top)) y = area.getY() + 2 * scale;

        auto pass = [&](int dx, int dy, juce::Colour c)
        {
            g.setColour(c);
            int cx = x;
            for (auto p = text.getCharPointer(); ! p.isEmpty();)
            {
                const auto ch = p.getAndAdvance();
                if (auto* gl = find(ch))
                    for (int r = 0; r < gl->n; ++r)
                        for (int k = 0; k < 5; ++k)
                            if (gl->rows[r] & (16 >> k))
                                g.fillRect(cx + k * scale + dx, y + (gl->top + r) * scale + dy, scale, scale);
                cx += 6 * scale;
            }
        };
        if (shadow) pass(juce::jmax(1, scale / 2), juce::jmax(1, scale / 2), juce::Colours::black.withAlpha(colour.getFloatAlpha() * 0.8f));
        pass(0, 0, colour);
    }

    void drawTextGradient(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, int scale,
                          juce::Colour top, juce::Colour bottom, juce::Justification just)
    {
        const int w = textWidth(text, scale), h = 7 * scale;
        int x = area.getX(), y = area.getY() + (area.getHeight() - h) / 2;
        if (just.testFlags(juce::Justification::horizontallyCentred)) x = area.getX() + (area.getWidth() - w) / 2;
        else if (just.testFlags(juce::Justification::right)) x = area.getRight() - w;
        auto pass = [&](int dx, int dy, bool outline)
        {
            int cx = x;
            for (auto p = text.getCharPointer(); ! p.isEmpty();)
            {
                if (auto* gl = find(p.getAndAdvance()))
                    for (int r = 0; r < gl->n; ++r)
                    {
                        const int row = gl->top + r;
                        g.setColour(outline ? juce::Colours::black : md9(top.interpolatedWith(bottom, juce::jlimit(0.0f, 1.0f, row / 6.0f))));
                        for (int k = 0; k < 5; ++k)
                            if (gl->rows[r] & (16 >> k)) g.fillRect(cx + k * scale + dx, y + row * scale + dy, scale, scale);
                    }
                cx += 6 * scale;
            }
        };
        const int o = juce::jmax(1, scale / 2);
        for (int dy = -o; dy <= o; dy += o) for (int dx = -o; dx <= o; dx += o) if (dx || dy) pass(dx, dy, true);
        pass(o, o + o, true);   // sombra abajo
        pass(0, 0, false);
    }

    void paintWindow(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour top, juce::Colour bottom, float corner)
    {
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(r.expanded(1.0f), corner + 1);
        {
            juce::Graphics::ScopedSaveState s(g);
            juce::Path clip; clip.addRoundedRectangle(r, corner);
            g.reduceClipRegion(clip);
            const int y0 = (int) r.getY(), y1 = (int) r.getBottom();
            for (int y = y0; y < y1; ++y)   // cada renglón redondeado a los 512 colores: quedan las bandas
            {
                g.setColour(md9(top.interpolatedWith(bottom, (float) (y - y0) / juce::jmax(1, y1 - y0 - 1))));
                g.fillRect(r.getX(), (float) y, r.getWidth(), 1.0f);
            }
        }
        g.setColour(md9(206, 206, 255));
        g.drawRoundedRectangle(r.reduced(1.0f), corner, 2.0f);
        g.setColour(md9(52, 52, 87));
        g.drawRoundedRectangle(r.reduced(3.5f), juce::jmax(1.0f, corner - 2), 1.0f);
    }

    // ---------------------------------------------------------------- fondos
    void paintGrid(juce::Graphics& g, juce::Rectangle<float> b, float s, float corner)
    {
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(b, corner);
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip; clip.addRoundedRectangle(b, corner);
        g.reduceClipRegion(clip);
        g.setColour(juce::Colours::white.withAlpha(0.20f));
        for (float x = b.getX() + std::fmod(b.getWidth(), s) / 2; x < b.getRight(); x += s) g.fillRect(x, b.getY(), 1.0f, b.getHeight());
        for (float y = b.getY() + std::fmod(b.getHeight(), s) / 2; y < b.getBottom(); y += s) g.fillRect(b.getX(), y, b.getWidth(), 1.0f);
    }

    void paintCrt(juce::Graphics& g, juce::Rectangle<float> b, float corner)
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip; clip.addRoundedRectangle(b, corner);
        g.reduceClipRegion(clip);
        g.setColour(juce::Colours::black.withAlpha(0.28f));
        for (float y = b.getY() + 1; y < b.getBottom(); y += 3) g.fillRect(b.getX(), y, b.getWidth(), 1.0f);
        juce::ColourGradient vig(juce::Colours::transparentBlack, b.getCentreX(), b.getCentreY(),
                                 juce::Colours::black.withAlpha(0.45f), b.getX(), b.getY(), true);
        g.setGradientFill(vig);
        g.fillRect(b);
        // reflejo del vidrio
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.06f), b.getX(), b.getY(),
                                               juce::Colours::transparentWhite, b.getX(), b.getY() + b.getHeight() * 0.45f, false));
        g.fillRect(b.withHeight(b.getHeight() * 0.45f));
    }

    // ---------------------------------------------------------------- sprites 48x48 (DecimuLabs)
    // Se dibujan con formas y degradés y después pasan por el "conversor a 16 bits": colores del chip
    // (512) con tramado ordenado 2x2 y contorno oscuro de 1 punto, como los sprites de la época.
    namespace
    {
        using P = juce::Point<float>;
        juce::Colour C(int r, int g, int b, float a = 1.0f) { return juce::Colour((juce::uint8) r, (juce::uint8) g, (juce::uint8) b, a); }
        void grad(juce::Graphics& g, juce::Colour a, juce::Colour b, P p0, P p1) { g.setGradientFill(juce::ColourGradient(a, p0, b, p1, false)); }

        void drawBass(juce::Graphics& g)
        {
            g.addTransform(juce::AffineTransform::rotation(-0.62f, 24, 24));
            // mástil y clavijero
            grad(g, C(206, 144, 87), C(116, 52, 0), { 22, 0 }, { 27, 0 });
            g.fillRect(22.0f, 0.0f, 4.0f, 28.0f);
            g.setColour(C(255, 206, 144, 0.7f));
            for (float y = 4; y < 27; y += 3.2f) g.fillRect(22.0f, y, 4.0f, 0.8f);
            grad(g, C(87, 52, 52), C(52, 0, 0), { 19, -7 }, { 29, 0 });
            g.fillRoundedRectangle(19.5f, -7.0f, 9.0f, 8.0f, 2.0f);
            g.setColour(C(230, 230, 255));
            for (int k = 0; k < 2; ++k) { g.fillEllipse(17.0f, -6.0f + k * 4, 3, 3); g.fillEllipse(28.0f, -6.0f + k * 4, 3, 3); }
            // cuerpo
            juce::Path body;
            body.addEllipse(11, 25, 26, 20);
            body.addEllipse(10, 19, 9, 12);
            body.addEllipse(28, 17, 8, 13);
            grad(g, C(255, 116, 87), C(116, 0, 0), { 12, 18 }, { 36, 46 });
            g.fillPath(body);
            g.setColour(C(255, 206, 172, 0.55f));
            g.fillEllipse(14, 27, 6, 10);
            // golpeador, micrófonos y puente
            grad(g, C(255, 255, 255), C(172, 172, 206), { 16, 28 }, { 30, 40 });
            g.fillEllipse(16.5f, 29, 13, 10);
            g.setColour(C(20, 20, 30));
            g.fillRoundedRectangle(19.5f, 30.5f, 9, 2.4f, 1);
            g.fillRoundedRectangle(19.5f, 35, 9, 2.4f, 1);
            g.setColour(C(206, 206, 230));
            g.fillRect(19.5f, 40.0f, 9.0f, 2.0f);
            g.setColour(C(255, 255, 230, 0.8f));
            for (float x = 22.5f; x < 26.5f; x += 1.2f) g.fillRect(x, 0.0f, 0.5f, 41.0f);
        }

        void drawKeys(juce::Graphics& g)
        {
            grad(g, C(144, 144, 172), C(40, 40, 70), { 0, 13 }, { 0, 40 });
            g.fillRoundedRectangle(2, 13, 44, 26, 3);
            g.setColour(C(20, 20, 40));
            g.fillRect(5.0f, 16.0f, 38.0f, 6.0f);
            g.setColour(C(255, 52, 52)); g.fillEllipse(7, 17.5f, 3, 3);
            g.setColour(C(52, 255, 116)); g.fillEllipse(11, 17.5f, 3, 3);
            for (int k = 0; k < 3; ++k)
            {
                grad(g, C(230, 230, 255), C(87, 87, 116), { 28.0f + k * 5, 16 }, { 31.0f + k * 5, 21 });
                g.fillEllipse(27.0f + k * 5, 16.5f, 4, 4);
            }
            const float x0 = 4, w = 40.0f / 10;
            for (int k = 0; k < 10; ++k)
            {
                grad(g, C(255, 255, 255), C(172, 172, 206), { 0, 24 }, { 0, 37 });
                g.fillRect(x0 + k * w + 0.4f, 24.0f, w - 0.8f, 13.0f);
            }
            static const int bk[] = { 1, 2, 4, 5, 6, 8, 9 };
            for (int k : bk)
            {
                grad(g, C(70, 70, 90), C(0, 0, 0), { 0, 24 }, { 0, 31 });
                g.fillRect(x0 + k * w - 1.3f, 24.0f, 2.6f, 7.5f);
                g.setColour(C(172, 172, 206, 0.8f));
                g.fillRect(x0 + k * w - 0.8f, 24.5f, 0.6f, 6.0f);
            }
        }

        void drawTrumpet(juce::Graphics& g)
        {
            const auto gold0 = C(255, 240, 144), gold1 = C(172, 116, 0);
            // tubo de abajo
            g.setColour(C(206, 144, 0));
            g.drawRoundedRectangle(10, 23, 20, 10, 4, 2.6f);
            // tubo principal + boquilla
            grad(g, gold0, gold1, { 0, 20 }, { 0, 25 });
            g.fillRect(6.0f, 20.0f, 25.0f, 4.0f);
            g.fillRect(1.0f, 20.5f, 6.0f, 3.0f);
            g.fillEllipse(0.0f, 19.5f, 3.0f, 5.0f);
            // campana
            juce::Path bell;
            bell.startNewSubPath(29, 19.5f);
            bell.cubicTo(36, 19, 41, 14, 46, 8);
            bell.lineTo(46, 36);
            bell.cubicTo(41, 30, 36, 25, 29, 24.5f);
            bell.closeSubPath();
            grad(g, gold0, gold1, { 30, 10 }, { 40, 34 });
            g.fillPath(bell);
            grad(g, C(255, 255, 206), C(206, 144, 0), { 44, 8 }, { 47, 36 });
            g.fillEllipse(43.5f, 7.5f, 4, 29);
            // pistones
            for (int k = 0; k < 3; ++k)
            {
                const float x = 12.0f + k * 5.5f;
                grad(g, gold0, gold1, { x, 0 }, { x + 3, 0 });
                g.fillRect(x, 12.0f, 3.2f, 9.0f);
                grad(g, C(255, 255, 255), C(116, 116, 144), { x - 1, 9 }, { x + 4, 12 });
                g.fillRoundedRectangle(x - 0.8f, 9.0f, 4.8f, 3.2f, 1.2f);
            }
            g.setColour(C(255, 255, 230, 0.7f));
            g.fillRect(6.0f, 20.6f, 24.0f, 0.8f);
        }

        void drawViolin(juce::Graphics& g)
        {
            juce::Path body;
            body.addEllipse(14, 12, 20, 15);
            body.addEllipse(11, 24, 26, 21);
            body.addRectangle(17, 20, 14, 10);
            grad(g, C(230, 144, 52), C(87, 30, 0), { 12, 12 }, { 36, 46 });
            g.fillPath(body);
            g.setColour(C(255, 230, 172, 0.45f));
            g.fillEllipse(15, 14, 5, 9);
            g.fillEllipse(13.5f, 27, 5, 12);
            // efes
            g.setColour(C(40, 10, 0));
            g.drawLine(17.5f, 27, 18.5f, 35, 1.2f);
            g.drawLine(30.5f, 27, 29.5f, 35, 1.2f);
            // diapasón, cordal, puente, voluta
            grad(g, C(70, 70, 90), C(0, 0, 0), { 22, 0 }, { 26, 0 });
            g.fillRect(22.0f, 3.0f, 4.0f, 28.0f);
            g.fillRoundedRectangle(21.0f, 37.0f, 6.0f, 7.0f, 2.0f);
            g.setColour(C(255, 230, 172));
            g.fillRect(18.5f, 33.5f, 11.0f, 1.8f);
            grad(g, C(206, 116, 52), C(87, 30, 0), { 21, 0 }, { 27, 6 });
            g.fillEllipse(21.0f, -1.0f, 6.0f, 6.0f);
            g.setColour(C(230, 230, 255, 0.85f));
            for (float x = 22.6f; x < 26; x += 0.9f) g.fillRect(x, 3.0f, 0.35f, 35.0f);
        }

        void drawNote(juce::Graphics& g)
        {
            const auto a = C(172, 255, 255), b = C(0, 87, 172);
            juce::Path p;
            p.addRectangle(16, 8, 3, 26);
            p.addRectangle(37, 4, 3, 26);
            p.startNewSubPath(16, 8); p.lineTo(40, 3); p.lineTo(40, 10); p.lineTo(16, 15); p.closeSubPath();
            juce::Path h1, h2;
            h1.addEllipse(5, 30, 14, 10); h1.applyTransform(juce::AffineTransform::rotation(-0.35f, 12, 35));
            h2.addEllipse(26, 26, 14, 10); h2.applyTransform(juce::AffineTransform::rotation(-0.35f, 33, 31));
            p.addPath(h1); p.addPath(h2);
            grad(g, a, b, { 6, 4 }, { 40, 44 });
            g.fillPath(p);
            g.setColour(C(255, 255, 255, 0.7f));
            g.fillEllipse(8, 31.5f, 4, 2.5f);
            g.fillEllipse(29, 27.5f, 4, 2.5f);
            g.fillRect(16.6f, 15.0f, 0.8f, 16.0f);
        }

        void drawBell(juce::Graphics& g)
        {
            juce::Path bell;
            bell.startNewSubPath(24, 7);
            bell.cubicTo(15, 7, 13, 14, 13, 24);
            bell.cubicTo(13, 32, 9, 35, 5, 38);
            bell.lineTo(43, 38);
            bell.cubicTo(39, 35, 35, 32, 35, 24);
            bell.cubicTo(35, 14, 33, 7, 24, 7);
            bell.closeSubPath();
            grad(g, C(255, 240, 172), C(144, 87, 0), { 10, 8 }, { 40, 40 });
            g.fillPath(bell);
            g.setColour(C(255, 255, 255, 0.65f));
            juce::Path shine;
            shine.startNewSubPath(18, 12); shine.cubicTo(16, 18, 16, 28, 13, 34);
            g.strokePath(shine, juce::PathStrokeType(2.2f));
            grad(g, C(206, 144, 0), C(116, 52, 0), { 0, 36 }, { 0, 41 });
            g.fillEllipse(5, 35.5f, 38, 5);
            g.setColour(C(52, 30, 0));
            g.fillEllipse(21, 38.5f, 6, 6);
            // moño rojo arriba
            grad(g, C(255, 87, 87), C(144, 0, 0), { 14, 2 }, { 34, 9 });
            juce::Path bow;
            bow.addTriangle(24, 6, 14, 1, 14, 10);
            bow.addTriangle(24, 6, 34, 1, 34, 10);
            g.fillPath(bow);
            g.fillEllipse(22, 4, 4, 4);
        }

        void drawDrum(juce::Graphics& g)
        {
            // palillos
            g.setColour(C(206, 144, 87));
            g.drawLine(12, 1, 30, 17, 2.6f);
            g.drawLine(36, 1, 18, 17, 2.6f);
            g.setColour(C(255, 230, 172));
            g.fillEllipse(10.5f, 0, 3, 3); g.fillEllipse(34.5f, 0, 3, 3);
            // casco cilíndrico (luz al medio)
            juce::ColourGradient shell(C(116, 0, 0), 5, 0, C(116, 0, 0), 43, 0, false);
            shell.addColour(0.35, C(255, 87, 87));
            shell.addColour(0.5, C(255, 172, 172));
            g.setGradientFill(shell);
            g.fillRect(5.0f, 22.0f, 38.0f, 15.0f);
            g.fillEllipse(5, 31, 38, 12);
            // tensores
            for (int k = 0; k < 5; ++k)
            {
                const float x = 8.0f + k * 8;
                grad(g, C(255, 255, 255), C(116, 116, 144), { x, 0 }, { x + 2.5f, 0 });
                g.fillRect(x, 24.0f, 2.4f, 13.0f);
            }
            // aro de abajo
            g.setColour(C(206, 206, 230));
            g.drawEllipse(5, 31, 38, 12, 1.8f);
            // parche y aro de arriba
            grad(g, C(255, 255, 255), C(172, 172, 206), { 0, 15 }, { 0, 28 });
            g.fillEllipse(5, 16, 38, 12);
            g.setColour(C(230, 230, 255));
            g.drawEllipse(5, 16, 38, 12, 2.0f);
        }

        void drawBolt(juce::Graphics& g)
        {
            juce::Path bolt;
            bolt.startNewSubPath(30, 1); bolt.lineTo(11, 26); bolt.lineTo(22, 26); bolt.lineTo(15, 47);
            bolt.lineTo(39, 17); bolt.lineTo(27, 17); bolt.lineTo(35, 1); bolt.closeSubPath();
            grad(g, C(255, 255, 206), C(255, 116, 0), { 14, 2 }, { 34, 46 });
            g.fillPath(bolt);
            g.setColour(C(255, 255, 255, 0.8f));
            g.drawLine(29, 3, 15, 24, 1.2f);
            auto star = [&](float x, float y, float r)
            {
                juce::Path s;
                s.startNewSubPath(x, y - r); s.lineTo(x + r * 0.25f, y - r * 0.25f); s.lineTo(x + r, y);
                s.lineTo(x + r * 0.25f, y + r * 0.25f); s.lineTo(x, y + r); s.lineTo(x - r * 0.25f, y + r * 0.25f);
                s.lineTo(x - r, y); s.lineTo(x - r * 0.25f, y - r * 0.25f); s.closeSubPath();
                g.setColour(C(255, 255, 255)); g.fillPath(s);
            };
            star(7, 9, 5); star(41, 33, 4); star(41, 7, 3); star(8, 38, 3);
        }

        juce::Image render16(int index)
        {
            constexpr int N = 48;
            juce::Image src(juce::Image::ARGB, N, N, true);
            {
                juce::Graphics g(src);
                switch (index)
                {
                    case 0: drawBass(g); break;   case 1: drawKeys(g); break;
                    case 2: drawTrumpet(g); break; case 3: drawViolin(g); break;
                    case 4: drawNote(g); break;   case 5: drawBell(g); break;
                    case 6: drawDrum(g); break;   default: drawBolt(g); break;
                }
            }
            // a 512 colores con tramado ordenado 2x2
            static const float bayer[2][2] = { { 0.125f, 0.625f }, { 0.875f, 0.375f } };
            static const int L[8] = { 0, 52, 87, 116, 144, 172, 206, 255 };
            auto q = [&](int v, float t)
            {
                for (int i = 0; i < 7; ++i)
                    if (v <= L[i + 1]) { const float f = (float) (v - L[i]) / (float) (L[i + 1] - L[i]); return f > t ? L[i + 1] : L[i]; }
                return 255;
            };
            juce::Image out(juce::Image::ARGB, N, N, true);
            std::vector<uint8_t> solid((size_t) (N * N), 0);
            for (int y = 0; y < N; ++y)
                for (int x = 0; x < N; ++x)
                {
                    const auto c = src.getPixelAt(x, y);
                    if (c.getAlpha() < 110) continue;
                    const float a = c.getFloatAlpha(), t = bayer[y & 1][x & 1];
                    auto un = [&](juce::uint8 v) { return juce::jlimit(0, 255, (int) std::lround(v / a)); };
                    out.setPixelAt(x, y, juce::Colour((juce::uint8) q(un(c.getRed()), t), (juce::uint8) q(un(c.getGreen()), t), (juce::uint8) q(un(c.getBlue()), t)));
                    solid[(size_t) (y * N + x)] = 1;
                }
            // contorno oscuro de 1 punto, del color del vecino oscurecido
            juce::Image res = out.createCopy();
            for (int y = 0; y < N; ++y)
                for (int x = 0; x < N; ++x)
                {
                    if (solid[(size_t) (y * N + x)]) continue;
                    static const int d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                    for (auto& o : d)
                    {
                        const int nx = x + o[0], ny = y + o[1];
                        if (nx < 0 || ny < 0 || nx >= N || ny >= N || ! solid[(size_t) (ny * N + nx)]) continue;
                        const auto n = out.getPixelAt(nx, ny);
                        res.setPixelAt(x, y, md9(n.darker(2.2f).withMultipliedBrightness(0.6f)));
                        break;
                    }
                }
            return res;
        }
    }

    juce::Colour familyColour(int i)
    {
        static const juce::uint8 C3[8][3] = { {52,87,206}, {206,52,52}, {206,116,0}, {144,52,172}, {0,144,116}, {172,144,0}, {172,52,0}, {52,144,206} };
        i = juce::jlimit(0, 7, i);
        return md9(C3[i][0], C3[i][1], C3[i][2]);
    }

    void drawSprite(juce::Graphics& g, int index, juce::Rectangle<int> area, juce::Colour)
    {
        static juce::Image cache[8];
        index = juce::jlimit(0, 7, index);
        if (! cache[index].isValid()) cache[index] = render16(index);
        const int s = juce::jmax(1, juce::jmin(area.getWidth(), area.getHeight()) / 48);
        const int x0 = area.getX() + (area.getWidth() - 48 * s) / 2, y0 = area.getY() + (area.getHeight() - 48 * s) / 2;
        juce::Graphics::ScopedSaveState save(g);
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        // sombra proyectada
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.drawImage(cache[index], x0 + 2 * s, y0 + 2 * s, 48 * s, 48 * s, 0, 0, 48, 48, true);
        g.setOpacity(1.0f);
        g.drawImage(cache[index], x0, y0, 48 * s, 48 * s, 0, 0, 48, 48);
    }

    // ---------------------------------------------------------------- arranque
    bool BootScreen::alreadyShown = false;

    BootScreen::BootScreen(juce::Image d) : dog(std::move(d))
    {
        t0 = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(60);
    }

    void BootScreen::finish()
    {
        if (! isVisible()) return;
        alreadyShown = true;
        stopTimer();
        setVisible(false);
        if (onDone) onDone();
    }

    void BootScreen::timerCallback()
    {
        const double p = (juce::Time::getMillisecondCounterHiRes() - t0) / 2000.0;
        if (p >= 1.0) { finish(); return; }
        setAlpha(p > 0.82 ? (float) ((1.0 - p) / 0.18) : 1.0f);
        repaint();
    }

    void BootScreen::paint(juce::Graphics& g)
    {
        const double p = (juce::Time::getMillisecondCounterHiRes() - t0) / 2000.0;
        auto b = getLocalBounds();
        g.fillAll(juce::Colours::black);
        g.setOpacity(1.0f);
        {
            juce::Graphics::ScopedSaveState s(g);
            g.setOpacity((float) juce::jmin(1.0, p * 3.0));
            paintGrid(g, b.toFloat(), 22.0f, 0.0f);
        }
        const auto accent = md9(255, 172, 87);
        auto centre = b.withSizeKeepingCentre(juce::jmin(520, b.getWidth() - 40), 300);
        // perro: de mosaico a nítido
        if (dog.isValid())
        {
            auto d = centre.removeFromTop(140).withSizeKeepingCentre(140, 140);
            const int block = juce::jmax(1, (int) std::lround(24.0 * (1.0 - juce::jmin(1.0, p / 0.45))));
            auto small = dog.rescaled(juce::jmax(1, d.getWidth() / block), juce::jmax(1, d.getHeight() / block), juce::Graphics::lowResamplingQuality);
            g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
            drawTinted(g, small, d.toFloat(), md9(206, 206, 255));
        }
        centre.removeFromTop(14);
        if (p > 0.3)
        {
            // "DECIMULABS" aparece de arriba hacia abajo, como un barrido
            auto row = centre.removeFromTop(48);
            juce::Graphics::ScopedSaveState s(g);
            g.reduceClipRegion(row.withHeight((int) (row.getHeight() * juce::jmin(1.0, (p - 0.3) / 0.15))));
            drawText(g, "DECIMULABS", row, 6, juce::Colours::white, juce::Justification::centred);
        }
        else centre.removeFromTop(48);
        centre.removeFromTop(10);
        if (p > 0.5) drawText(g, "FM  -  YM2612", centre.removeFromTop(30), 3, accent, juce::Justification::centred);
        if (p > 0.62 && std::fmod(p * 6.0, 1.0) < 0.6)
            drawText(g, "6 VOICES - 4 OPERATORS - 8 ALGORITHMS", centre.removeFromTop(30), 2, md9(172, 172, 206), juce::Justification::centred);
        paintCrt(g, b.toFloat(), 0.0f);
    }

    // ---------------------------------------------------------------- góndola de cartuchos
    CartridgeShelf::CartridgeShelf(juce::Array<Family> f, std::function<void(int)> onPick)
        : fams(std::move(f)), pick(std::move(onPick))
    {
        setWantsKeyboardFocus(true);
    }

    void CartridgeShelf::resized() {}

    juce::Rectangle<int> CartridgeShelf::closeRect() const { return { getWidth() - 64, 14, 44, 32 }; }

    juce::Rectangle<int> CartridgeShelf::boxRect(int i) const
    {
        auto area = getLocalBounds().reduced(30, 10).withTrimmedTop(48).withTrimmedBottom(16);
        if (open >= 0)
        {
            auto left = area.removeFromLeft(area.getWidth() * 32 / 100);
            if (i != open) return {};
            const int h = juce::jmin(left.getHeight(), (int) (left.getWidth() / 0.72f));
            return left.withSizeKeepingCentre((int) (h * 0.72f), h);
        }
        const int cols = 4, rows = 2, gap = 18;
        const int cw = (area.getWidth() - gap * (cols - 1)) / cols, ch = (area.getHeight() - gap * (rows - 1)) / rows;
        const int h = juce::jmin(ch, (int) (cw / 0.72f)), w = (int) (h * 0.72f);
        const int gx = area.getX() + (area.getWidth() - (w * cols + gap * (cols - 1))) / 2;
        const int gy = area.getY() + (area.getHeight() - (h * rows + gap * (rows - 1))) / 2;
        return { gx + (i % cols) * (w + gap), gy + (i / cols) * (h + gap), w, h };
    }

    juce::Rectangle<int> CartridgeShelf::itemRect(int i) const
    {
        auto area = getLocalBounds().reduced(30, 10).withTrimmedTop(48).withTrimmedBottom(16);
        area.removeFromLeft(area.getWidth() * 32 / 100 + 20);
        return { area.getX(), area.getY() + 8 + i * 34, juce::jmin(area.getWidth(), 520), 30 };
    }

    void CartridgeShelf::paintBox(juce::Graphics& g, int i, juce::Rectangle<int> r, bool hot) const
    {
        if (hot) r.translate(0, -4);
        const auto fam = familyColour(i);
        // sombra y caja
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRect(r.translated(5, 6));
        g.setColour(juce::Colours::black);
        g.fillRect(r);
        // franja de arriba: grilla propia + DECIMULABS
        auto top = r.removeFromTop(juce::jmax(18, r.getHeight() / 9));
        paintGrid(g, top.toFloat(), 5.0f, 0.0f);
        const int ts = top.getHeight() >= 26 ? 2 : 1;
        drawText(g, "DECIMULABS", top.reduced(6, 0), ts, juce::Colours::white, juce::Justification::centredLeft);
        drawText(g, "FM", top.reduced(6, 0), ts, md9(255, 172, 87), juce::Justification::centredRight);
        // lomo de color
        auto spine = r.removeFromLeft(juce::jmax(5, r.getWidth() / 22));
        g.setColour(fam.brighter(0.3f));
        g.fillRect(spine);
        // abajo: nombre de la familia
        auto bottom = r.removeFromBottom(juce::jmax(40, r.getHeight() / 4));
        g.setColour(md9(0, 0, 52));
        g.fillRect(bottom);
        auto nameArea = bottom.reduced(6, 2);
        const auto name = upper(fams[i].name);
        auto nameRows = nameArea.removeFromTop(nameArea.getHeight() * 7 / 10);
        if (textWidth(name, 2) <= nameRows.getWidth() || ! name.containsChar(' '))
            drawText(g, name, nameRows, textWidth(name, 2) <= nameRows.getWidth() ? 2 : 1, juce::Colours::white, juce::Justification::centred);
        else
        {
            // dos renglones, cortando en el espacio más cerca de la mitad
            int cut = name.length() / 2, best = -1;
            for (int k = 0; k < name.length(); ++k) if (name[k] == ' ' && (best < 0 || std::abs(k - cut) < std::abs(best - cut))) best = k;
            const auto a = name.substring(0, best), b = name.substring(best + 1);
            const int sc = juce::jmax(textWidth(a, 2), textWidth(b, 2)) <= nameRows.getWidth() ? 2 : 1;
            auto r1 = nameRows.removeFromTop(nameRows.getHeight() / 2);
            drawText(g, a, r1, sc, juce::Colours::white, juce::Justification::centred);
            drawText(g, b, nameRows, sc, juce::Colours::white, juce::Justification::centred);
        }
        drawText(g, juce::String(fams[i].programs.size()) + " SONIDOS", nameArea, 1, md9(172, 172, 206), juce::Justification::centred);
        // arte: degradé, rayos y el sprite
        g.setGradientFill(juce::ColourGradient(fam.brighter(0.4f), (float) r.getX(), (float) r.getY(),
                                               fam.darker(0.7f), (float) r.getX(), (float) r.getBottom(), false));
        g.fillRect(r);
        {
            juce::Graphics::ScopedSaveState s(g);
            g.reduceClipRegion(r);
            g.setColour(juce::Colours::white.withAlpha(0.08f));
            const auto c = r.getCentre().toFloat();
            for (int k = 0; k < 12; ++k)
            {
                juce::Path ray;
                const float a0 = k * juce::MathConstants<float>::twoPi / 12, a1 = a0 + 0.22f, R = (float) r.getWidth() * 1.5f;
                ray.addTriangle(c, c + juce::Point<float>(std::cos(a0), std::sin(a0)) * R, c + juce::Point<float>(std::cos(a1), std::sin(a1)) * R);
                g.fillPath(ray);
            }
        }
        auto art = r.reduced(r.getWidth() / 8);
        if (hot)
        {
            // respira: sube y baja de a un pixel del sprite
            const int px = juce::jmax(1, juce::jmin(art.getWidth(), art.getHeight()) / 48);
            const double t = juce::Time::getMillisecondCounter() / 1000.0;
            art.translate(0, -px * (int) std::lround(1.5 + 1.5 * std::sin(t * 5.0)));
        }
        drawSprite(g, i, art, fam);
        // borde
        g.setColour(hot ? md9(255, 206, 52) : md9(87, 87, 116));
        g.drawRect(r.getUnion(top).getUnion(bottom).getUnion(spine), hot ? 3 : 2);
    }

    void CartridgeShelf::paint(juce::Graphics& g)
    {
        paintGrid(g, getLocalBounds().toFloat(), 22.0f, 0.0f);
        auto title = getLocalBounds().reduced(30, 14).removeFromTop(32);
        drawText(g, open >= 0 ? upper(fams[open].name) : "CARTUCHOS", title, 4, juce::Colours::white, juce::Justification::centredLeft);
        drawText(g, "X", closeRect(), 3, hover == -100 ? md9(255, 206, 52) : md9(172, 172, 206), juce::Justification::centred);

        for (int i = 0; i < fams.size(); ++i)
        {
            auto r = boxRect(i);
            if (! r.isEmpty()) paintBox(g, i, r, open < 0 && hover == i);
        }
        if (open >= 0)
        {
            const auto& f = fams[open];
            auto back = itemRect(0);
            drawText(g, "< VOLVER A LOS CARTUCHOS", back, 2, hover == -2 ? md9(255, 206, 52) : md9(172, 172, 206));
            for (int k = 0; k < f.names.size(); ++k)
            {
                auto r = itemRect(k + 1);
                const bool cur = f.programs[k] == current, hot = hover == 1000 + k;
                if (hot || cur)
                {
                    g.setColour(cur ? familyColour(open).withAlpha(0.55f) : juce::Colours::white.withAlpha(0.1f));
                    g.fillRect(r);
                }
                drawText(g, (cur ? "> " : "  ") + f.names[k], r.reduced(8, 0), 2, hot ? md9(255, 206, 52) : juce::Colours::white);
            }
        }
        paintCrt(g, getLocalBounds().toFloat(), 0.0f);
    }

    void CartridgeShelf::mouseMove(const juce::MouseEvent& e)
    {
        int h = -1;
        const auto pt = e.getPosition();
        if (closeRect().contains(pt)) h = -100;
        else if (open < 0) { for (int i = 0; i < fams.size(); ++i) if (boxRect(i).contains(pt)) h = i; }
        else
        {
            if (itemRect(0).contains(pt)) h = -2;
            for (int k = 0; k < fams[open].names.size(); ++k) if (itemRect(k + 1).contains(pt)) h = 1000 + k;
        }
        if (h != hover) { hover = h; repaint(); }
    }

    void CartridgeShelf::mouseDown(const juce::MouseEvent& e)
    {
        mouseMove(e);
        if (hover == -100) { if (onClose) onClose(); return; }
        if (open < 0) { if (hover >= 0) { open = hover; hover = -1; repaint(); } return; }
        if (hover == -2 || boxRect(open).contains(e.getPosition())) { open = -1; hover = -1; repaint(); return; }
        if (hover >= 1000) { current = fams[open].programs[hover - 1000]; pick(current); repaint(); }
    }

    bool CartridgeShelf::keyPressed(const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::escapeKey)
        {
            if (open >= 0) { open = -1; repaint(); }
            else if (onClose) onClose();
            return true;
        }
        return false;
    }

    // ---------------------------------------------------------------- apariencia 16 bits
    static bool insideSliderOrCombo(juce::Label& l)
    {
        auto* p = l.getParentComponent();
        return dynamic_cast<juce::Slider*>(p) != nullptr || dynamic_cast<juce::ComboBox*>(p) != nullptr;
    }

    void FMLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& l)
    {
        if (! insideSliderOrCombo(l) || l.isBeingEdited()) { DecimuLookAndFeel::drawLabel(g, l); return; }
        auto b = l.getLocalBounds();
        const bool combo = dynamic_cast<juce::ComboBox*>(l.getParentComponent()) != nullptr;
        if (! combo)
        {
            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.fillRect(b.reduced(2, 1));
            g.setColour(juce::Colours::white.withAlpha(0.2f));
            g.drawRect(b.reduced(2, 1), 1);
        }
        const auto t = l.getText();
        int sc = 2;
        while (sc > 1 && textWidth(t, sc) > b.getWidth() - 8) --sc;
        if (b.getHeight() < 16) sc = 1;
        drawText(g, t, b.reduced(combo ? 8 : 2, 0), sc, combo ? juce::Colours::white : md9(116, 255, 172),
                 combo ? juce::Justification::centredLeft : juce::Justification::centred, false);
    }

    void FMLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced(1.0f);
        const bool on = b.getToggleState();
        auto top = on ? md9(172, 52, 0) : juce::Colours::black, bot = top;
        if (over && ! on) top = juce::Colour(0xff1a1a1a);
        if (down) std::swap(top, bot);
        g.setColour(top); g.fillRoundedRectangle(r, 4.0f); g.setColour(juce::Colours::white.withAlpha(over ? 0.5f : 0.25f)); g.drawRoundedRectangle(r, 4.0f, 1.0f);
    }

    void FMLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool over, bool)
    {
        auto r = b.getLocalBounds().reduced(6, 0);
        const auto t = upper(b.getButtonText());
        int sc = 2;
        while (sc > 1 && textWidth(t, sc) > r.getWidth()) --sc;
        drawText(g, t, r, sc, over ? md9(255, 255, 172) : juce::Colours::white, juce::Justification::centred);
    }

    void FMLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
    {
        auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(1.0f);
        g.setColour(box.isMouseOver(true) ? juce::Colour(0xff1a1a1a) : juce::Colours::black); g.fillRoundedRectangle(r, 4.0f); g.setColour(juce::Colours::white.withAlpha(0.25f)); g.drawRoundedRectangle(r, 4.0f, 1.0f);
        // flechita pixelada
        const int s = juce::jmax(1, h / 14);
        const int ax = w - 8 - 5 * s, ay = h / 2 - s;
        g.setColour(md9(255, 206, 52));
        for (int k = 0; k < 3; ++k) g.fillRect(ax + k * s, ay + k * s, (5 - 2 * k) * s, s);
    }

    void FMLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
    {
        g.fillAll(juce::Colours::black); g.setColour(juce::Colours::white.withAlpha(0.25f)); g.drawRect(0, 0, w, h, 1);
    }

    void FMLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                          bool isHighlighted, bool isTicked, bool, const juce::String& text, const juce::String&,
                                          const juce::Drawable*, const juce::Colour*)
    {
        auto r = area.reduced(5, 0);
        if (isSeparator) { g.setColour(md9(116, 116, 206)); g.fillRect(r.withSizeKeepingCentre(r.getWidth(), 1)); return; }
        if (isHighlighted && isActive)
        {
            g.setColour(md9(255, 172, 87));
            g.fillRect(r.reduced(0, 1));
        }
        const auto col = ! isActive ? md9(116, 116, 172) : isHighlighted ? juce::Colours::black : juce::Colours::white;
        if (isTicked) drawText(g, ">", r.withTrimmedLeft(4), 2, isHighlighted ? juce::Colours::black : md9(255, 172, 87), juce::Justification::centredLeft, false);
        drawText(g, text, r.withTrimmedLeft(20), 2, col, juce::Justification::centredLeft, ! isHighlighted);
    }

    void FMLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
    {
        auto r = area.reduced(8, 0);
        drawText(g, upper(name), r.withTrimmedTop(4), 2, md9(255, 172, 87), juce::Justification::centredLeft);
        g.setColour(md9(255, 172, 87).withAlpha(0.5f));
        g.fillRect(r.getX(), r.getBottom() - 2, r.getWidth(), 1);
    }

    void FMLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int, int& w, int& h)
    {
        h = isSeparator ? 8 : 24;
        w = textWidth(text, 2) + 50;
    }
}
