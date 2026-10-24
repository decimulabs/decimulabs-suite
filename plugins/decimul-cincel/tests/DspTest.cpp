// Test rápido del motor de Cincel, sin JUCE: se compila con tests\build_dsp.bat.
// Mide en números lo que el oído no puede verificar acá: que las curvas digitales sigan a las
// analógicas, que cada modo haga lo que dice y que nada explote.

#include "../Source/dsp/CincelEngine.h"
#include "../Source/dsp/CincelFit.h"
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace cincel;

namespace
{
int failures = 0;

void check(bool ok, const std::string& what, const std::string& detail = {})
{
    std::printf("%s %s%s%s\n", ok ? "[ OK ]" : "[FAIL]", what.c_str(), detail.empty() ? "" : "  -> ", detail.c_str());
    if (!ok)
        ++failures;
}

std::string fmt(double v, int decimals = 3)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

double db(double lin) { return 20.0 * std::log10(std::max(lin, 1.0e-12)); }

/** Tolerancia según el nivel de la curva: exigente donde se escucha (cerca de 0 dB o arriba),
    más laxa en lo que ya está muy atenuado. */
double tolerance(double levelDb, double hz, double fs)
{
    // Pegado a Nyquist (arriba de 0.3 fs: ~13 kHz a 44.1 kHz) el modo de latencia cero no
    // puede seguir del todo a un corte empinado: ahí se acepta 1 dB (el modo de fase natural
    // sí lo sigue, porque sobremuestrea).
    if (hz > 0.3 * fs && levelDb > -24.0) return 1.0;
    if (levelDb > -12.0) return 0.25;
    if (levelDb > -24.0) return 0.5;
    if (levelDb > -40.0) return 1.5;
    return 1.0e9;
}

/** Peor error entre el diseño digital y el analógico entre 20 Hz y `top`, dividido por la
    tolerancia (<= 1 está bien). En `worstDb` queda el error en dB de ese punto. */
double worstError(const AnalogDesign& a, double fs, double top, double* worstHz = nullptr, double* worstDb = nullptr)
{
    const auto d = toDigital(a, fs);
    double worst = 0.0;
    for (int i = 0; i <= 400; ++i)
    {
        const double hz = 20.0 * std::pow(top / 20.0, i / 400.0);
        const double ad = db(a.magnitude(hz));
        const double dd = db(std::abs(d.at(2.0 * kPi * hz / fs)));
        const double e = std::abs(ad - dd) / tolerance(ad, hz, fs);
        if (e > worst)
        {
            worst = e;
            if (worstHz != nullptr)
                *worstHz = hz;
            if (worstDb != nullptr)
                *worstDb = ad - dd;
        }
    }
    return worst;
}

void testFft()
{
    std::mt19937 rng(1);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    for (int n : { 8, 64, 1024 })
    {
        std::vector<double> x((size_t) n);
        for (auto& v : x)
            v = u(rng);
        RealFft<double> f;
        f.init(n);
        std::vector<std::complex<double>> X((size_t) (n / 2 + 1));
        f.forward(x.data(), X.data());
        double err = 0.0;
        for (int k = 0; k <= n / 2; ++k)
        {
            std::complex<double> ref;
            for (int t = 0; t < n; ++t)
                ref += x[(size_t) t] * std::polar(1.0, -2.0 * kPi * k * t / n);
            err = std::max(err, std::abs(ref - X[(size_t) k]));
        }
        std::vector<double> back((size_t) n);
        f.inverse(X.data(), back.data());
        double err2 = 0.0;
        for (int t = 0; t < n; ++t)
            err2 = std::max(err2, std::abs(back[(size_t) t] - x[(size_t) t]));
        check(err < 1.0e-9 && err2 < 1.0e-12, "FFT real de " + std::to_string(n) + " puntos contra la DFT directa",
              "error " + fmt(err, 12) + " / vuelta " + fmt(err2, 14));
    }
}

void testMatchedDesigns()
{
    struct Case
    {
        Shape shape;
        int slope;
        double freq, gain, q;
        const char* name;
    };
    const Case cases[] = {
        { Shape::Bell, 1, 1000.0, 12.0, 1.0, "bell 1 kHz +12" },
        { Shape::Bell, 1, 10000.0, 9.0, 2.0, "bell 10 kHz +9 Q2" },
        { Shape::Bell, 1, 16000.0, -12.0, 4.0, "bell 16 kHz -12 Q4" },
        { Shape::Bell, 1, 18000.0, 6.0, 0.7, "bell 18 kHz +6" },
        { Shape::Bell, 1, 40.0, 10.0, 6.0, "bell 40 Hz +10 Q6" },
        { Shape::Bell, 1, 25.0, -20.0, 0.3, "bell 25 Hz -20 Q0.3" },
        { Shape::LowShelf, 1, 120.0, 8.0, 0.71, "low shelf 120 Hz +8" },
        { Shape::LowShelf, 3, 300.0, -10.0, 0.71, "low shelf 24 dB/oct" },
        { Shape::HighShelf, 1, 8000.0, 6.0, 0.71, "high shelf 8 kHz +6" },
        { Shape::HighShelf, 1, 15000.0, 10.0, 1.2, "high shelf 15 kHz +10 Q1.2" },
        { Shape::HighShelf, 5, 4000.0, -8.0, 0.71, "high shelf 36 dB/oct" },
        { Shape::LowCut, 1, 80.0, 0.0, 0.71, "low cut 80 Hz 12 dB" },
        { Shape::LowCut, 8, 30.0, 0.0, 0.71, "low cut 30 Hz 96 dB" },
        { Shape::LowCut, 0, 200.0, 0.0, 0.71, "low cut 6 dB" },
        { Shape::LowCut, 3, 1000.0, 0.0, 3.0, "low cut 24 dB resonante" },
        { Shape::HighCut, 1, 12000.0, 0.0, 0.71, "high cut 12 kHz" },
        { Shape::HighCut, 6, 15000.0, 0.0, 0.71, "high cut 15 kHz 48 dB" },
        { Shape::HighCut, 2, 5000.0, 0.0, 0.71, "high cut 18 dB" },
        { Shape::Notch, 1, 3000.0, 0.0, 8.0, "notch 3 kHz" },
        { Shape::BandPass, 1, 700.0, 0.0, 1.0, "pasabanda 700 Hz" },
        { Shape::BandPass, 5, 2000.0, 0.0, 2.0, "pasabanda 36 dB" },
        { Shape::TiltShelf, 1, 1000.0, 6.0, 0.71, "tilt shelf +6" },
        { Shape::FlatTilt, 1, 1000.0, 12.0, 1.0, "flat tilt +12" },
        { Shape::FlatTilt, 1, 300.0, -20.0, 1.0, "flat tilt -20" },
        { Shape::Bell, 1, 19000.0, 12.0, 8.0, "bell 19 kHz +12 Q8" },
        { Shape::Bell, 1, 25000.0, 6.0, 1.0, "bell 25 kHz (arriba de Nyquist)" },
        { Shape::HighShelf, 3, 18000.0, -9.0, 0.71, "high shelf 18 kHz 24 dB" },
        { Shape::Bell, 1, 10.0, 6.0, 0.5, "bell 10 Hz +6" },
        { Shape::LowCut, 9, 20.0, 0.0, 0.71, "low cut 20 Hz brickwall" },
        { Shape::HighCut, 9, 8000.0, 0.0, 0.71, "high cut 8 kHz brickwall" },
        { Shape::LowShelf, 8, 60.0, 12.0, 0.71, "low shelf 60 Hz 96 dB" },
    };

    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        double worstAll = 0.0;
        std::string worstName;
        for (const auto& c : cases)
        {
            const auto a = designBand(c.shape, c.slope, c.freq, c.gain, c.q);
            double hz = 0.0, edb = 0.0;
            const double top = std::min(20000.0, 0.45 * fs);
            const double e = worstError(a, fs, top, &hz, &edb);
            // Tolerancia: 0.25 dB donde se oye, hasta 20 kHz (un EQ digital común erra varios dB).
            if (e > worstAll)
            {
                worstAll = e;
                worstName = std::string(c.name) + ": " + fmt(edb) + " dB en " + fmt(hz, 0) + " Hz";
            }
            if (e > 1.0)
                check(false, std::string("Curva digital = analógica: ") + c.name + " a " + fmt(fs, 0),
                      fmt(edb) + " dB en " + fmt(hz, 0) + " Hz");
        }
        check(worstAll <= 1.0, "Todas las formas siguen a la analógica a " + fmt(fs, 0) + " Hz",
              "peor caso " + worstName);
    }

    // Comparación: un bell a 16 kHz con transformada bilineal común se angosta y se corre.
    {
        const auto a = designBand(Shape::Bell, 1, 16000.0, 12.0, 2.0);
        const auto d = toDigital(a, 44100.0);
        const double at14 = db(std::abs(d.at(2.0 * kPi * 14000.0 / 44100.0)));
        const double an14 = db(a.magnitude(14000.0));
        check(std::abs(at14 - an14) < 0.25, "Bell a 16 kHz (44.1 kHz): el flanco en 14 kHz coincide",
              "digital " + fmt(at14, 2) + " / analógico " + fmt(an14, 2) + " dB");
    }

    // Shelves: la mitad de la ganancia justo en la frecuencia, para cualquier pendiente y Q.
    for (int slope : { 0, 1, 3, 6 })
        for (double q : { 0.5, 0.71, 2.0 })
        {
            const auto a = designBand(Shape::LowShelf, slope, 500.0, 12.0, q);
            const double mid = db(a.magnitude(500.0));
            if (std::abs(mid - 6.0) > 0.01)
                check(false, "Low shelf: +6 dB en la frecuencia", "pendiente " + std::to_string(slope) + " Q " + fmt(q) + ": " + fmt(mid));
        }
    check(true, "Shelves: la mitad de la ganancia cae en la frecuencia (todas las pendientes y Q)");

    // Cortes: -3 dB en la frecuencia (Q = 0.707) y la pendiente prometida una década abajo.
    for (int slope = 0; slope < numSlopes - 1; ++slope)
    {
        const auto a = designBand(Shape::LowCut, slope, 1000.0, 0.0, 0.70710678);
        const double at = db(a.magnitude(1000.0));
        const double oct = db(a.magnitude(500.0)) - db(a.magnitude(250.0));
        const int n = slopeOrder(slope);
        const double expected = 10.0 * std::log10((1.0 + std::pow(4.0, 2.0 * n)) / (1.0 + std::pow(2.0, 2.0 * n)));
        if (std::abs(at + 3.0103) > 0.01 || std::abs(oct - expected) > 0.15)
            check(false, "Low cut pendiente " + std::to_string(slope), "en f " + fmt(at) + " dB, octava " + fmt(oct));
    }
    check(true, "Cortes: -3 dB en la frecuencia y 6/12/18/.../96 dB por octava");

    // Flat tilt: pendiente constante (gain/10 dB por octava) y 0 dB en el pivote.
    {
        const auto a = designBand(Shape::FlatTilt, 1, 1000.0, 12.0, 1.0);
        double worst = 0.0;
        for (double hz = 40.0; hz < 16000.0; hz *= 1.25)
            worst = std::max(worst, std::abs(db(a.magnitude(hz)) - 1.2 * std::log2(hz / 1000.0)));
        check(worst < 0.15, "Flat tilt +12: recta de 1.2 dB por octava que pasa por 0 dB en el pivote", "desvío " + fmt(worst));
    }
}
//==============================================================================
// Motor completo

constexpr int block = 512;

struct Rig
{
    double fs = 48000.0;
    Engine e;
    std::array<BandParams, maxBands> bands {};
    GlobalParams g;

    explicit Rig(double sampleRate = 48000.0, bool stereo = true) : fs(sampleRate)
    {
        e.prepare(fs, block, stereo);
        e.setOffline(true);
    }

    void run(std::vector<float>& L, std::vector<float>* R, const std::vector<float>* scL = nullptr)
    {
        for (size_t s = 0; s < L.size(); s += block)
        {
            const int n = (int) std::min<size_t>(block, L.size() - s);
            e.process(L.data() + s, R != nullptr ? R->data() + s : nullptr, scL != nullptr ? scL->data() + s : nullptr,
                      scL != nullptr ? scL->data() + s : nullptr, n, bands.data(), g);
        }
    }
};

std::vector<float> sine(double fs, double hz, double amp, int n)
{
    std::vector<float> v((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = (float) (amp * std::sin(2.0 * kPi * hz * i / fs));
    return v;
}

/** Amplitud de la componente `hz` entre [from, from + len). */
double toneAmp(const std::vector<float>& v, double fs, double hz, int from, int len)
{
    double re = 0.0, im = 0.0;
    for (int i = from; i < from + len; ++i)
    {
        re += v[(size_t) i] * std::cos(2.0 * kPi * hz * i / fs);
        im += v[(size_t) i] * std::sin(2.0 * kPi * hz * i / fs);
    }
    return 2.0 * std::sqrt(re * re + im * im) / len;
}

BandParams band(Shape shape, double freq, double gain, double q, int slope = 1)
{
    BandParams b;
    b.used = true;
    b.shape = shape;
    b.freq = (float) freq;
    b.gainDb = (float) gain;
    b.q = (float) q;
    b.slope = slope;
    return b;
}

/** Ganancia medida (dB) de un tono, ya asentado. */
double measuredGain(Rig& r, double hz)
{
    const int n = (int) (r.fs * 0.9) + r.e.latency() + 16384;
    auto L = sine(r.fs, hz, 0.25, n);
    auto R = L;
    r.e.prepare(r.fs, block, true);
    r.e.setOffline(true);
    r.run(L, &R);
    const int len = (int) (r.fs * 0.25);
    return db(toneAmp(L, r.fs, hz, n - len - 1, len) / 0.25);
}

void testModesFollowAnalog()
{
    const double freqs[] = { 50.0, 200.0, 1000.0, 3000.0, 8000.0, 12000.0, 15000.0, 18000.0 };
    for (double fs : { 44100.0, 48000.0 })
        for (int m = 0; m < 3; ++m)
        {
            Rig r(fs);
            r.g.mode = (Mode) m;
            r.g.lpResolution = 3;
            r.bands[0] = band(Shape::Bell, 1000.0, 9.0, 1.0);
            r.bands[1] = band(Shape::HighShelf, 12000.0, -6.0, 0.71);
            r.bands[2] = band(Shape::LowCut, 40.0, 0.0, 0.71, 3);
            r.bands[3] = band(Shape::Bell, 16000.0, 6.0, 3.0);
            double worst = 0.0, worstHz = 0.0, worstGot = 0.0, worstWant = 0.0;
            for (double hz : freqs)
            {
                double want = 0.0;
                for (int i = 0; i < 4; ++i)
                {
                    const auto& b = r.bands[(size_t) i];
                    want += db(designBand(b.shape, b.slope, b.freq, b.gainDb, b.q).magnitude(hz));
                }
                const double got = measuredGain(r, hz);
                // Latencia cero: 1 dB arriba de 0.3 fs (ver tolerance). Los otros dos: 0.25 dB.
                const double tol = (m == 0 && hz > 0.3 * fs) ? 1.0 : 0.25;
                const double e = std::abs(got - want) / tol;
                if (e > worst)
                {
                    worst = e;
                    worstHz = hz;
                    worstGot = got;
                    worstWant = want;
                }
            }
            const char* names[] = { "latencia cero", "fase natural", "fase lineal" };
            check(worst <= 1.0, std::string("Medido con tonos (") + names[m] + ", " + fmt(fs, 0) + " Hz) = curva analógica",
                  "peor en " + fmt(worstHz, 0) + " Hz: " + fmt(worstGot, 2) + " dB (debía " + fmt(worstWant, 2) + ")");
        }
}

void testLatencyAndBypass()
{
    for (int m = 0; m < 3; ++m)
        for (bool spectralOn : { false, true })
        {
            Rig r(48000.0);
            r.g.mode = (Mode) m;
            r.g.lpResolution = 1;
            if (spectralOn)
            {
                r.bands[5] = band(Shape::Bell, 3000.0, 0.0, 1.0);
                r.bands[5].dynamic = true;
                r.bands[5].spectral = true;
                r.bands[5].thresholdDb = 0.0;   // no actúa: solo suma la latencia
            }
            const int n = 48000;
            std::vector<float> L((size_t) n, 0.0f);
            L[1000] = 1.0f;
            auto R = L;
            r.run(L, &R);
            int peak = 0;
            for (int i = 0; i < n; ++i)
                if (std::abs(L[(size_t) i]) > std::abs(L[(size_t) peak]))
                    peak = i;
            const int lat = r.e.latency();
            double rest = 0.0;
            for (int i = 0; i < n; ++i)
                if (i != peak)
                    rest = std::max(rest, (double) std::abs(L[(size_t) i]));
            const char* names[] = { "latencia cero", "fase natural", "fase lineal" };
            // En fase natural el impulso sale apenas suavizado (los filtros de media banda cortan
            // por encima de ~21 kHz): pico un poco menor y una mínima oscilación alrededor.
            const double tol = m == 1 ? 0.2 : 0.02;
            check(peak - 1000 == lat && std::abs(L[(size_t) peak] - 1.0) < tol && rest < tol,
                  std::string("Latencia informada exacta: ") + names[m] + (spectralOn ? " + espectral" : ""),
                  std::to_string(lat) + " muestras, pico en +" + std::to_string(peak - 1000) + ", valor " + fmt(L[(size_t) peak], 4)
                      + ", resto " + fmt(rest, 4));
        }

    // Bypass: la salida es la entrada, retrasada lo mismo que el proceso.
    {
        Rig r(48000.0);
        r.g.mode = Mode::LinearPhase;
        r.g.lpResolution = 0;
        r.bands[0] = band(Shape::Bell, 1000.0, 12.0, 1.0);
        r.g.bypass = true;
        const int n = 48000;
        auto L = sine(48000.0, 440.0, 0.5, n);
        const auto in = L;
        auto R = L;
        r.run(L, &R);
        const int lat = r.e.latency();
        double err = 0.0;
        for (int i = 24000; i < n; ++i)
            err = std::max(err, (double) std::abs(L[(size_t) i] - in[(size_t) (i - lat)]));
        check(err < 1.0e-5, "Bypass: sale la entrada exacta, compensando la latencia", "error " + fmt(err, 7));
    }
}

void testLinearPhaseIsSymmetric()
{
    Rig r(48000.0);
    r.g.mode = Mode::LinearPhase;
    r.g.lpResolution = 2;
    r.bands[0] = band(Shape::Bell, 2000.0, 9.0, 2.0);
    r.bands[1] = band(Shape::LowCut, 100.0, 0.0, 0.71, 3);
    const int n = 48000;
    std::vector<float> L((size_t) n, 0.0f);
    L[2000] = 1.0f;
    auto R = L;
    r.run(L, &R);
    const int c = 2000 + r.e.latency();
    double asym = 0.0, energy = 0.0;
    for (int k = 1; k < 4000; ++k)
    {
        asym = std::max(asym, (double) std::abs(L[(size_t) (c + k)] - L[(size_t) (c - k)]));
        energy = std::max(energy, (double) std::abs(L[(size_t) (c + k)]));
    }
    check(asym < 1.0e-4 * std::max(1.0, energy * 1000.0), "Fase lineal: la respuesta al impulso es simétrica (no corre la fase)",
          "asimetría " + fmt(asym, 7));
}

void testDynamics()
{
    for (int m = 0; m < 3; ++m)
    {
        auto runCase = [&](double amp)
        {
            Rig r(48000.0);
            r.g.mode = (Mode) m;
            r.g.lpResolution = 1;
            r.bands[0] = band(Shape::Bell, 1000.0, 0.0, 1.0);
            r.bands[0].dynamic = true;
            r.bands[0].rangeDb = -9.0;
            r.bands[0].thresholdDb = -30.0;
            const int n = 48000;
            auto L = sine(48000.0, 1000.0, amp, n);
            auto R = L;
            r.run(L, &R);
            return db(toneAmp(L, 48000.0, 1000.0, n - 9600, 9600) / amp);
        };
        const double loud = runCase(std::pow(10.0, -6.0 / 20.0));   // -6 dBFS: 24 dB arriba del umbral
        const double quiet = runCase(std::pow(10.0, -48.0 / 20.0));
        const char* names[] = { "latencia cero", "fase natural", "fase lineal" };
        check(loud < -8.0 && loud > -9.6 && std::abs(quiet) < 0.2,
              std::string("EQ dinámico (") + names[m] + "): baja el rango entero si se pasa, nada si no llega",
              "fuerte " + fmt(loud, 2) + " dB, bajo " + fmt(quiet, 2) + " dB");
    }

    // Sidechain: la banda escucha la otra señal.
    {
        Rig r(48000.0);
        r.bands[0] = band(Shape::Bell, 1000.0, 0.0, 1.0);
        r.bands[0].dynamic = true;
        r.bands[0].rangeDb = -12.0;
        r.bands[0].thresholdDb = -40.0;
        r.bands[0].sidechain = true;
        const int n = 48000;
        auto L = sine(48000.0, 1000.0, 0.005, n);   // la señal está bajo el umbral...
        auto R = L;
        auto sc = sine(48000.0, 1000.0, 0.5, n);    // ...pero el sidechain no
        r.run(L, &R, &sc);
        const double g = db(toneAmp(L, 48000.0, 1000.0, n - 9600, 9600) / 0.005);
        check(g < -11.0, "EQ dinámico con sidechain: baja cuando el sidechain se pasa", fmt(g, 2) + " dB");
    }
}

void testSoloAndStereo()
{
    {
        Rig r(48000.0);
        r.bands[0] = band(Shape::Bell, 1000.0, 3.0, 2.0);
        r.g.soloBand = 0;
        const int n = 24000;
        auto a = sine(48000.0, 1000.0, 0.3, n);
        auto b = sine(48000.0, 100.0, 0.3, n);
        std::vector<float> L((size_t) n);
        for (int i = 0; i < n; ++i)
            L[(size_t) i] = a[(size_t) i] + b[(size_t) i];
        auto R = L;
        r.run(L, &R);
        const double keep = db(toneAmp(L, 48000.0, 1000.0, n - 9600, 9600) / 0.3);
        const double gone = db(toneAmp(L, 48000.0, 100.0, n - 9600, 9600) / 0.3);
        check(keep > 2.0 && gone < -25.0, "Solo de banda: se oye solo la zona de la banda", "1 kHz " + fmt(keep, 1) + " dB, 100 Hz " + fmt(gone, 1) + " dB");
    }
    {
        Rig r(48000.0);
        r.bands[0] = band(Shape::LowCut, 300.0, 0.0, 0.71, 3);
        r.g.soloBand = 0;
        const int n = 24000;
        auto L = sine(48000.0, 80.0, 0.3, n);
        auto R = L;
        r.run(L, &R);
        const double heard = db(toneAmp(L, 48000.0, 80.0, n - 9600, 9600) / 0.3);
        check(heard > -1.0, "Solo de un low cut: se oye lo que se está sacando", "80 Hz " + fmt(heard, 2) + " dB");
    }
    {
        // Banda en Side: un sonido mono no se toca; lo que está a los costados, sí.
        Rig r(48000.0);
        r.bands[0] = band(Shape::Bell, 1000.0, -12.0, 1.0);
        r.bands[0].place = Placement::Side;
        const int n = 24000;
        auto L = sine(48000.0, 1000.0, 0.3, n);
        auto R = L;
        r.run(L, &R);
        const double mono = db(toneAmp(L, 48000.0, 1000.0, n - 9600, 9600) / 0.3);
        Rig r2(48000.0);
        r2.bands = r.bands;
        auto L2 = sine(48000.0, 1000.0, 0.3, n);
        std::vector<float> R2((size_t) n);
        for (int i = 0; i < n; ++i)
            R2[(size_t) i] = -L2[(size_t) i];
        r2.run(L2, &R2);
        const double side = db(toneAmp(L2, 48000.0, 1000.0, n - 9600, 9600) / 0.3);
        check(std::abs(mono) < 0.05 && side < -11.5, "Banda en Side: el centro queda intacto y los costados bajan",
              "centro " + fmt(mono, 2) + " dB, costados " + fmt(side, 2) + " dB");

        // Lo mismo en fase lineal (matriz 2x2).
        Rig r3(48000.0);
        r3.bands = r.bands;
        r3.g.mode = Mode::LinearPhase;
        r3.g.lpResolution = 1;
        auto L3 = sine(48000.0, 1000.0, 0.3, n);
        std::vector<float> R3((size_t) n);
        for (int i = 0; i < n; ++i)
            R3[(size_t) i] = -L3[(size_t) i];
        r3.run(L3, &R3);
        const double side3 = db(toneAmp(L3, 48000.0, 1000.0, n - 9600, 9600) / 0.3);
        check(side3 < -11.5, "Banda en Side en fase lineal", fmt(side3, 2) + " dB");
    }
    {
        // Banda solo en el canal izquierdo.
        Rig r(48000.0);
        r.bands[0] = band(Shape::Bell, 1000.0, 6.0, 1.0);
        r.bands[0].place = Placement::Left;
        const int n = 24000;
        auto L = sine(48000.0, 1000.0, 0.3, n);
        auto R = L;
        r.run(L, &R);
        const double gl = db(toneAmp(L, 48000.0, 1000.0, n - 9600, 9600) / 0.3);
        const double gr = db(toneAmp(R, 48000.0, 1000.0, n - 9600, 9600) / 0.3);
        check(std::abs(gl - 6.0) < 0.1 && std::abs(gr) < 0.05, "Banda en Left: solo cambia el izquierdo", fmt(gl, 2) + " / " + fmt(gr, 2) + " dB");
    }
}

void testSpectral()
{
    // Un tono fuerte y otro débil dentro de la misma banda: la dinámica espectral baja el fuerte
    // sin tocar al débil (la común bajaría los dos).
    for (bool spectralOn : { false, true })
    {
        Rig r(48000.0);
        r.bands[0] = band(Shape::Bell, 2000.0, 0.0, 0.7);
        r.bands[0].dynamic = true;
        r.bands[0].spectral = spectralOn;
        r.bands[0].rangeDb = -10.0;
        r.bands[0].thresholdDb = -30.0;
        const int n = 72000;
        auto a = sine(48000.0, 2000.0, 0.5, n);
        auto b = sine(48000.0, 2600.0, 0.004, n);
        std::vector<float> L((size_t) n);
        for (int i = 0; i < n; ++i)
            L[(size_t) i] = a[(size_t) i] + b[(size_t) i];
        auto R = L;
        r.run(L, &R);
        const double loud = db(toneAmp(L, 48000.0, 2000.0, n - 19200, 19200) / 0.5);
        const double weak = db(toneAmp(L, 48000.0, 2600.0, n - 19200, 19200) / 0.004);
        if (spectralOn)
            check(loud < -8.0 && weak > -1.5, "Dinámica espectral: baja la resonancia fuerte y deja la débil",
                  "fuerte " + fmt(loud, 2) + " dB, débil " + fmt(weak, 2) + " dB");
        else
            check(loud < -8.0 && weak < -6.0, "Dinámica común (para comparar): baja las dos",
                  "fuerte " + fmt(loud, 2) + " dB, débil " + fmt(weak, 2) + " dB");
    }
}

void testOutputAndCharacter()
{
    {
        Rig r(48000.0);
        r.g.outputDb = -6.0;
        r.g.pan = 0.5;
        r.g.phaseInvert = true;
        const int n = 24000;
        auto L = sine(48000.0, 500.0, 0.4, n);
        auto R = L;
        const auto in = L;
        r.run(L, &R);
        const double gl = db(toneAmp(L, 48000.0, 500.0, n - 9600, 9600) / 0.4);
        const double gr = db(toneAmp(R, 48000.0, 500.0, n - 9600, 9600) / 0.4);
        double corr = 0.0;
        for (int i = n - 9600; i < n; ++i)
            corr += L[(size_t) i] * in[(size_t) i];
        check(std::abs(gl - (-6.0 + db(0.5))) < 0.05 && std::abs(gr + 6.0) < 0.05 && corr < 0.0,
              "Salida: ganancia, balance y fase invertida", fmt(gl, 2) + " / " + fmt(gr, 2) + " dB");
    }
    {
        // Auto gain: un realce ancho de +6 dB se compensa (el promedio queda cerca de 0).
        Rig r(48000.0);
        r.bands[0] = band(Shape::Bell, 1000.0, 6.0, 0.3);
        r.g.autoGain = true;
        const int n = 24000;
        auto L = sine(48000.0, 1000.0, 0.2, n);
        auto R = L;
        r.run(L, &R);
        const double g = db(toneAmp(L, 48000.0, 1000.0, n - 9600, 9600) / 0.2);
        check(r.e.autoGainDb() < -2.0 && g < 6.0 + r.e.autoGainDb() + 0.1, "Auto gain: compensa el volumen que suma la curva",
              "corrección " + fmt(r.e.autoGainDb(), 2) + " dB");
    }
    {
        // Carácter: con la curva plana no hace nada; con un realce agrega armónicos (pocos).
        for (int c = 1; c <= 2; ++c)
        {
            Rig flat(48000.0);
            flat.g.character = (Character) c;
            const int n = 24000;
            auto L = sine(48000.0, 200.0, 0.25, n);
            const auto in = L;
            auto R = L;
            flat.run(L, &R);
            double err = 0.0;
            for (int i = 1000; i < n; ++i)
                err = std::max(err, (double) std::abs(L[(size_t) i] - in[(size_t) i]));

            Rig boost(48000.0);
            boost.g.character = (Character) c;
            boost.bands[0] = band(Shape::LowShelf, 400.0, 9.0, 0.71);
            auto L2 = sine(48000.0, 200.0, 0.25, n);
            auto R2 = L2;
            boost.run(L2, &R2);
            const double h2 = db(toneAmp(L2, 48000.0, 400.0, n - 9600, 9600) / toneAmp(L2, 48000.0, 200.0, n - 9600, 9600));
            const double h3 = db(toneAmp(L2, 48000.0, 600.0, n - 9600, 9600) / toneAmp(L2, 48000.0, 200.0, n - 9600, 9600));
            // A -12 dBFS con +9 dB de realce: armónicos chicos (sutil), no una distorsión.
            check(err < 1.0e-6 && std::max(h2, h3) > -80.0 && std::max(h2, h3) < -35.0,
                  std::string("Carácter ") + (c == 1 ? "Subtle" : "Warm") + ": nada con la curva plana, armónicos con el realce",
                  "2º " + fmt(h2, 1) + " dB, 3º " + fmt(h3, 1) + " dB");
        }
    }
}

void testFuzzAndChanges()
{
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    double worst = 0.0;
    bool finite = true;
    for (int trial = 0; trial < 6; ++trial)
    {
        Rig r(trial % 2 == 0 ? 44100.0 : 96000.0);
        r.g.mode = (Mode) (trial % 3);
        r.g.lpResolution = 0;
        const int n = 48000;
        auto L = sine(r.fs, 330.0, 0.3, n);
        std::uniform_real_distribution<float> noise(-0.2f, 0.2f);
        for (auto& s : L)
            s += noise(rng);
        auto R = L;
        for (size_t s = 0; s < L.size(); s += block)
        {
            // Cada bloque, parámetros nuevos al azar (lo peor que puede hacer un usuario o una automatización).
            for (int i = 0; i < 8; ++i)
            {
                auto& b = r.bands[(size_t) i];
                b.used = u(rng) > 0.2;
                b.enabled = u(rng) > 0.1;
                b.shape = (Shape) (int) (u(rng) * numShapes);
                b.slope = (int) (u(rng) * numSlopes);
                b.freq = (float) (10.0 * std::pow(3000.0, u(rng)));
                b.gainDb = (float) (-30.0 + 60.0 * u(rng));
                b.q = (float) (0.025 * std::pow(1600.0, u(rng)));
                b.place = (Placement) (int) (u(rng) * numPlacements);
                b.dynamic = u(rng) > 0.6;
                b.rangeDb = (float) (-24.0 + 48.0 * u(rng));
            }
            const int len = (int) std::min<size_t>(block, L.size() - s);
            r.e.process(L.data() + s, R.data() + s, nullptr, nullptr, len, r.bands.data(), r.g);
            for (int i = 0; i < len; ++i)
            {
                finite = finite && std::isfinite(L[s + (size_t) i]) && std::isfinite(R[s + (size_t) i]);
                worst = std::max(worst, (double) std::abs(L[s + (size_t) i]));
            }
        }
    }
    // Ocho bandas de hasta +30 dB pueden apilarse mucho; lo que importa es que no haya NaN ni
    // un filtro inestable (crecería sin límite).
    check(finite && worst < 1.0e5, "Parámetros al azar en cada bloque (todos los modos): sin NaN ni explosiones", "pico " + fmt(worst, 1));

    // Estabilidad de cada biquad diseñado (polos adentro del círculo) y fase mínima de los
    // recortes invertidos, con miles de combinaciones al azar.
    {
        int unstable = 0, total = 0;
        for (int t = 0; t < 4000; ++t)
        {
            const double fs = (t % 4 == 0) ? 44100.0 : (t % 4 == 1 ? 48000.0 : (t % 4 == 2 ? 96000.0 : 192000.0));
            const auto shape = (Shape) (int) (u(rng) * numShapes);
            const int slope = (int) (u(rng) * numSlopes);
            const double f = 10.0 * std::pow(3000.0, u(rng));
            const double gdb = -30.0 + 60.0 * u(rng);
            const double q = 0.025 * std::pow(1600.0, u(rng));
            const auto d = toDigital(designBand(shape, slope, f, gdb, q), fs);
            for (int s = 0; s < d.count; ++s)
            {
                const auto& c = d.s[(size_t) s];
                ++total;
                const bool stable = std::abs(c.a2) < 1.0 && std::abs(c.a1) < 1.0 + c.a2 + 1.0e-12;
                const bool finiteC = std::isfinite(c.b0) && std::isfinite(c.b1) && std::isfinite(c.b2);
                if (!stable || !finiteC)
                {
                    ++unstable;
                    std::printf("       inestable: forma %d pendiente %d f %.1f g %.2f q %.4f fs %.0f sec %d/%d  a1 %.9f a2 %.9f b %.6g %.6g %.6g\n",
                                (int) shape, slope, f, gdb, q, fs, s, d.count, c.a1, c.a2, c.b0, c.b1, c.b2);
                }
            }
        }
        check(unstable == 0, "Todos los biquads diseñados son estables (4000 bandas al azar)",
              std::to_string(total) + " secciones, " + std::to_string(unstable) + " inestables");
    }

    // Barrido rápido de frecuencia: sin saltos bruscos (clics) en la salida.
    {
        Rig r(48000.0);
        r.bands[0] = band(Shape::Bell, 200.0, 12.0, 4.0);
        const int n = 96000;
        auto L = sine(48000.0, 1000.0, 0.2, n);
        auto R = L;
        double maxStep = 0.0;
        float prev = 0.0f;
        for (size_t s = 0; s < L.size(); s += block)
        {
            r.bands[0].freq = (float) (200.0 * std::pow(40.0, (double) s / n));
            const int len = (int) std::min<size_t>(block, L.size() - s);
            r.e.process(L.data() + s, R.data() + s, nullptr, nullptr, len, r.bands.data(), r.g);
            for (int i = 0; i < len; ++i)
            {
                if (s + (size_t) i > 0)
                    maxStep = std::max(maxStep, (double) std::abs(L[s + (size_t) i] - prev));
                prev = L[s + (size_t) i];
            }
        }
        // Un seno de 1 kHz a 0.2 con +12 dB (0.8) cambia como mucho ~0.105 por muestra.
        check(maxStep < 0.13, "Barrido rápido de un bell de +12 dB: sin saltos (clics)", "salto máximo " + fmt(maxStep, 4));
    }
    // Cambio de forma: se funde, no salta.
    {
        Rig r(48000.0);
        r.bands[0] = band(Shape::Bell, 1000.0, 12.0, 1.0);
        const int n = 48000;
        auto L = sine(48000.0, 1000.0, 0.2, n);
        auto R = L;
        double maxStep = 0.0;
        float prev = 0.0f;
        for (size_t s = 0; s < L.size(); s += block)
        {
            if (s == 24576)
                r.bands[0].shape = Shape::HighCut;
            const int len = (int) std::min<size_t>(block, L.size() - s);
            r.e.process(L.data() + s, R.data() + s, nullptr, nullptr, len, r.bands.data(), r.g);
            for (int i = 0; i < len; ++i)
            {
                if (s + (size_t) i > 0)
                    maxStep = std::max(maxStep, (double) std::abs(L[s + (size_t) i] - prev));
                prev = L[s + (size_t) i];
            }
        }
        check(maxStep < 0.17, "Cambio de forma de una banda: se funde sin clic", "salto máximo " + fmt(maxStep, 4));
    }
}

void testFit()
{
    // Una curva hecha con tres bandas conocidas: el ajuste tiene que reproducirla.
    FitGrid grid;
    grid.build(160);
    const auto b1 = designBand(Shape::Bell, 1, 200.0, 4.0, 1.0);
    const auto b2 = designBand(Shape::Bell, 1, 3000.0, -6.0, 2.0);
    const auto b3 = designBand(Shape::HighShelf, 1, 10000.0, 3.0, 0.71);
    for (int i = 0; i < grid.size(); ++i)
    {
        const double hz = grid.hz[(size_t) i];
        grid.target[(size_t) i] = db(b1.magnitude(hz)) + db(b2.magnitude(hz)) + db(b3.magnitude(hz));
    }
    const auto t0 = std::chrono::steady_clock::now();
    const auto fitted = fitBandsToCurve(grid, 8);
    const double ms = 1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    double rms = 0.0, worst = 0.0;
    for (int i = 0; i < grid.size(); ++i)
    {
        double m = 0.0;
        for (const auto& b : fitted)
            m += db(designBand(b.shape, b.slope, b.freq, b.gainDb, b.q).magnitude(grid.hz[(size_t) i]));
        const double e = m - grid.target[(size_t) i];
        rms += e * e;
        worst = std::max(worst, std::abs(e));
    }
    rms = std::sqrt(rms / grid.size());
    check(rms < 0.25 && worst < 0.8, "EQ Match / Sketch: las bandas ajustadas siguen a la curva",
          std::to_string(fitted.size()) + " bandas, error medio " + fmt(rms) + " dB, peor " + fmt(worst) + " dB, " + fmt(ms, 0) + " ms");

    // Un dibujo a mano (escalón suave): se arma con pocas bandas y sin barbaridades.
    for (int i = 0; i < grid.size(); ++i)
    {
        const double x = std::log2(grid.hz[(size_t) i] / 1000.0);
        grid.target[(size_t) i] = 5.0 * std::tanh(1.5 * x) - 3.0 * std::exp(-x * x * 8.0);
    }
    const auto sketch = fitBandsToCurve(grid, 6);
    double rms2 = 0.0;
    for (int i = 0; i < grid.size(); ++i)
    {
        double m = 0.0;
        for (const auto& b : sketch)
            m += db(designBand(b.shape, b.slope, b.freq, b.gainDb, b.q).magnitude(grid.hz[(size_t) i]));
        rms2 += (m - grid.target[(size_t) i]) * (m - grid.target[(size_t) i]);
    }
    rms2 = std::sqrt(rms2 / grid.size());
    check(rms2 < 0.6 && sketch.size() <= 6, "EQ Sketch: un trazo a mano se arma con pocas bandas",
          std::to_string(sketch.size()) + " bandas, error medio " + fmt(rms2) + " dB");
}

void testCpu()
{
    for (int m = 0; m < 3; ++m)
    {
        Rig r(48000.0);
        r.g.mode = (Mode) m;
        r.g.lpResolution = 2;
        for (int i = 0; i < 24; ++i)
        {
            r.bands[(size_t) i] = band(i % 3 == 0 ? Shape::HighShelf : Shape::Bell, 40.0 * std::pow(1.25, i), (i % 2 ? 4.0 : -4.0), 1.5);
            r.bands[(size_t) i].dynamic = i % 6 == 0;
        }
        r.bands[0] = band(Shape::LowCut, 30.0, 0.0, 0.71, 8);
        const int n = 48000 * 10;
        auto L = sine(48000.0, 440.0, 0.3, n);
        auto R = L;
        const auto t0 = std::chrono::steady_clock::now();
        r.run(L, &R);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const char* names[] = { "latencia cero", "fase natural", "fase lineal (High, armado en el hilo)" };
        std::printf("       CPU 24 bandas (4 dinámicas) %s: %.1f%% de un núcleo\n", names[m], 100.0 * sec / 10.0);
    }
}
} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    testFft();
    testMatchedDesigns();
    testModesFollowAnalog();
    testLatencyAndBypass();
    testLinearPhaseIsSymmetric();
    testDynamics();
    testSoloAndStereo();
    testSpectral();
    testOutputAndCharacter();
    testFuzzAndChanges();
    testFit();
    testCpu();
    std::printf(failures == 0 ? "\nTODO OK\n" : "\nHAY FALLAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
