#pragma once

// Diseño de los filtros del EQ (DecimuLabs Prisma, nombre de trabajo).
//
// Cada banda se describe primero como un filtro ANALÓGICO (secciones de 1º y 2º orden en s).
// Esa descripción sirve para tres cosas:
//   - dibujar la curva en pantalla;
//   - el modo de fase lineal (la respuesta en magnitud se muestrea en una grilla de FFT);
//   - pasar a biquads digitales para los modos de latencia cero y fase natural.
//
// El paso a digital es "de magnitud igualada" (como Vicanek, "Matched Second Order Digital
// Filters", 2016): los polos se llevan con invarianza al impulso (exactos, sin la deformación
// de la transformada bilineal) y el numerador se elige para que la magnitud digital coincida
// con la analógica en 0 Hz, en Nyquist y en la frecuencia característica de la sección.
// Así un bell a 15 kHz no se angosta ni se corre como en un EQ digital común.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace prisma
{
constexpr double kPi = 3.14159265358979323846;
constexpr int maxBands = 24;
constexpr int maxSections = 20;

enum class Shape : int { Bell = 0, LowShelf, LowCut, HighShelf, HighCut, Notch, BandPass, TiltShelf, FlatTilt };
constexpr int numShapes = 9;

enum class Placement : int { Stereo = 0, Left, Right, Mid, Side };
constexpr int numPlacements = 5;

// Pendientes: 6, 12, 18, 24, 30, 36, 48, 72, 96 dB/oct y "brickwall".
constexpr int numSlopes = 10;
constexpr int brickwallSlope = 9;

inline int slopeOrder(int slopeIndex)
{
    static constexpr int orders[numSlopes] = { 1, 2, 3, 4, 5, 6, 8, 12, 16, 32 };
    return orders[std::clamp(slopeIndex, 0, numSlopes - 1)];
}

inline bool shapeHasGain(Shape s)
{
    return s == Shape::Bell || s == Shape::LowShelf || s == Shape::HighShelf || s == Shape::TiltShelf
           || s == Shape::FlatTilt;
}
inline bool shapeHasSlope(Shape s) { return s != Shape::Bell && s != Shape::FlatTilt; }
inline bool shapeHasQ(Shape s) { return s != Shape::FlatTilt; }
inline bool shapeIsCut(Shape s) { return s == Shape::LowCut || s == Shape::HighCut; }

/** Lo que el usuario ajusta en una banda. */
struct BandParams
{
    bool used = false;          // la banda existe (se crea con doble clic)
    bool enabled = true;        // prendida / en bypass
    float freq = 1000.0f;       // Hz
    float gainDb = 0.0f;
    float q = 1.0f;
    Shape shape = Shape::Bell;
    int slope = 1;              // índice de pendiente (ver slopeOrder)
    Placement place = Placement::Stereo;

    bool dynamic = false;       // EQ dinámico
    float rangeDb = -6.0f;      // cuánto se puede mover la ganancia (con signo)
    float thresholdDb = -30.0f;
    bool autoThreshold = false;
    bool sidechain = false;     // la dinámica escucha la entrada de sidechain
    bool spectral = false;      // dinámica espectral (por bin de FFT)
};

inline bool isActive(const BandParams& b) { return b.used && b.enabled; }
inline bool usesDynamics(const BandParams& b) { return isActive(b) && b.dynamic && shapeHasGain(b.shape); }

/** Q efectiva: con "Gain-Q" prendido la campana se angosta cuanto más ganancia tiene. */
inline double effectiveQ(Shape shape, double q, double gainDb, bool gainQ)
{
    if (gainQ && shape == Shape::Bell)
        q *= std::pow(10.0, std::abs(gainDb) / 60.0);
    return std::clamp(q, 0.025, 60.0);
}

//==============================================================================
/** Sección analógica normalizada: H(x) con x = s / w.
    2º orden: (n2 x² + n1 x + n0) / (x² + d1 x + d0). 1º orden: (n1 x + n0) / (x + d0). */
struct AnalogSection
{
    double w = 1.0;
    bool firstOrder = false;
    double n0 = 1.0, n1 = 0.0, n2 = 0.0;
    double d0 = 1.0, d1 = 0.0;
    bool invert = false;        // la sección real es la inversa de esta (los recortes se diseñan
                                // como el realce espejado y se invierten: así quedan simétricos)

    std::complex<double> at(double omega) const
    {
        const double u = omega / w;
        std::complex<double> h;
        if (firstOrder)
            h = std::complex<double>(n0, n1 * u) / std::complex<double>(d0, u);
        else
            h = std::complex<double>(n0 - n2 * u * u, n1 * u) / std::complex<double>(d0 - u * u, d1 * u);
        return invert ? 1.0 / h : h;
    }

    double mag2(double omega) const { return std::norm(at(omega)); }
};

struct AnalogDesign
{
    int count = 0;
    double gain = 1.0;
    std::array<AnalogSection, maxSections> s {};

    // Brickwall del modo de fase lineal: un Butterworth de orden muy alto evaluado en forma cerrada.
    int closedOrder = 0;
    bool closedLowpass = true;
    double closedHz = 1000.0;

    void add(const AnalogSection& sec)
    {
        if (count < maxSections)
            s[(size_t) count++] = sec;
    }

    std::complex<double> at(double omega) const
    {
        std::complex<double> h(gain, 0.0);
        for (int i = 0; i < count; ++i)
            h *= s[(size_t) i].at(omega);
        return h;
    }

    double magnitude(double hz) const
    {
        double m = std::abs(at(2.0 * kPi * hz));
        if (closedOrder > 0)
        {
            const double r = closedLowpass ? hz / closedHz : closedHz / std::max(hz, 1.0e-9);
            m /= std::sqrt(1.0 + std::pow(r, 2.0 * closedOrder));
        }
        return m;
    }

    bool isIdentity() const { return count == 0 && closedOrder == 0 && std::abs(gain - 1.0) < 1.0e-12; }
};

namespace detail
{
    /** Q de la sección k (0 = la más resonante) de un Butterworth de orden n. */
    inline double butterQ(int order, int k) { return 1.0 / (2.0 * std::sin((2 * k + 1) * kPi / (2.0 * order))); }

    /** La Q del usuario sobre un Butterworth: 0.707 = Butterworth puro; más arriba agrega
        resonancia a la sección más aguda; más abajo ablanda todas. */
    inline double scaledQ(int order, int k, double q)
    {
        const double qk = butterQ(order, k);
        const double ratio = q / 0.70710678118654752;
        if (ratio >= 1.0)
            return k == 0 ? qk * ratio : qk;
        return qk * ratio;
    }

    inline void addCut(AnalogDesign& d, bool lowpass, double w, int order, double q)
    {
        for (int k = 0; k < order / 2; ++k)
        {
            AnalogSection sec;
            sec.w = w;
            sec.d0 = 1.0;
            sec.d1 = 1.0 / scaledQ(order, k, q);
            sec.n0 = lowpass ? 1.0 : 0.0;
            sec.n1 = 0.0;
            sec.n2 = lowpass ? 0.0 : 1.0;
            d.add(sec);
        }
        if (order % 2 == 1)
        {
            AnalogSection sec;
            sec.w = w;
            sec.firstOrder = true;
            sec.d0 = 1.0;
            sec.n0 = lowpass ? 1.0 : 0.0;
            sec.n1 = lowpass ? 0.0 : 1.0;
            d.add(sec);
        }
    }

    /** Shelf de orden n (Butterworth, Holters y Zölzer): la mitad de la ganancia (en dB) cae
        justo en w, para cualquier Q. */
    inline void addShelf(AnalogDesign& d, bool high, double w, int order, double gainLin, double q)
    {
        const double g1 = std::pow(gainLin, 1.0 / order);
        const double wp = w * std::pow(gainLin, (high ? 1.0 : -1.0) / (2.0 * order));
        const double r = high ? 1.0 / g1 : g1;
        for (int k = 0; k < order / 2; ++k)
        {
            // Un shelf con resonancia descomunal no sirve musicalmente y es numéricamente frágil.
            const double qk = std::min(scaledQ(order, k, q), 16.0);
            const double c = high ? g1 * g1 : 1.0;
            AnalogSection sec;
            sec.w = wp;
            sec.d0 = 1.0;
            sec.d1 = 1.0 / qk;
            sec.n2 = c;
            sec.n1 = c * r / qk;
            sec.n0 = c * r * r;
            d.add(sec);
        }
        if (order % 2 == 1)
        {
            const double c = high ? g1 : 1.0;
            AnalogSection sec;
            sec.w = wp;
            sec.firstOrder = true;
            sec.d0 = 1.0;
            sec.n1 = c;
            sec.n0 = c * r;
            d.add(sec);
        }
    }
}

/** Diseño analógico de una banda con los valores ya efectivos (ganancia con dinámica y
    escala de ganancia, Q efectiva). `linearPhase` = el brickwall va en forma cerrada. */
inline AnalogDesign designBand(Shape shape, int slope, double freq, double gainDb, double q, bool linearPhase = false)
{
    AnalogDesign d;
    const double w = 2.0 * kPi * freq;
    const double gainLin = std::pow(10.0, gainDb / 20.0);
    const int order = slopeOrder(slope);
    const bool flatGain = std::abs(gainDb) < 1.0e-4;

    switch (shape)
    {
        case Shape::Bell:
        {
            if (flatGain)
                break;
            // Un recorte es la inversa exacta del realce del mismo tamaño (con la misma Q).
            const double a = std::sqrt(gainDb < 0.0 ? 1.0 / gainLin : gainLin);
            AnalogSection sec;
            sec.w = w;
            sec.n2 = 1.0;
            sec.n1 = a / q;
            sec.n0 = 1.0;
            sec.d1 = 1.0 / (a * q);
            sec.d0 = 1.0;
            sec.invert = gainDb < 0.0;
            d.add(sec);
            break;
        }
        case Shape::LowShelf:
        case Shape::HighShelf:
        case Shape::TiltShelf:
        {
            if (flatGain)
                break;
            const bool high = shape != Shape::LowShelf;
            const int first = d.count;
            detail::addShelf(d, high, w, std::min(order, 16), gainDb < 0.0 ? 1.0 / gainLin : gainLin, q);
            if (gainDb < 0.0)
                for (int i = first; i < d.count; ++i)
                    d.s[(size_t) i].invert = true;
            if (shape == Shape::TiltShelf)
                d.gain = 1.0 / std::sqrt(gainLin);
            break;
        }
        case Shape::LowCut:
        case Shape::HighCut:
        {
            const bool lowpass = shape == Shape::HighCut;
            if (slope == brickwallSlope && linearPhase)
            {
                d.closedOrder = 64;
                d.closedLowpass = lowpass;
                d.closedHz = freq;
                break;
            }
            detail::addCut(d, lowpass, w, order, slope == brickwallSlope ? 0.70710678118654752 : q);
            break;
        }
        case Shape::Notch:
        case Shape::BandPass:
        {
            const int n = std::max(1, std::min(order, 32) / 2);
            const double spread = std::sqrt(std::pow(2.0, 1.0 / n) - 1.0);
            const double qs = shape == Shape::Notch ? q / spread : q * spread;
            for (int i = 0; i < std::min(n, maxSections); ++i)
            {
                AnalogSection sec;
                sec.w = w;
                sec.d0 = 1.0;
                sec.d1 = 1.0 / qs;
                if (shape == Shape::Notch)
                {
                    sec.n2 = 1.0;
                    sec.n1 = 0.0;
                    sec.n0 = 1.0;
                }
                else
                {
                    sec.n2 = 0.0;
                    sec.n1 = 1.0 / qs;
                    sec.n0 = 0.0;
                }
                d.add(sec);
            }
            break;
        }
        case Shape::FlatTilt:
        {
            if (flatGain)
                break;
            // Pendiente constante en todo el rango: la ganancia es lo que sube (o baja) entre
            // 20 Hz y 20 kHz (10 octavas), pivoteando en `freq`. Se arma con shelves de
            // primer orden, uno por octava, de 5 Hz a 80 kHz.
            const double slopeDbPerOct = gainDb / 10.0;
            const double rho = std::pow(10.0, slopeDbPerOct / 20.0);
            for (int i = -1; i <= 13; ++i)
            {
                const double wc = 2.0 * kPi * 10.0 * std::pow(2.0, i);
                AnalogSection sec;
                sec.firstOrder = true;
                sec.w = wc * std::sqrt(rho);
                sec.d0 = 1.0;
                sec.n1 = 1.0;
                sec.n0 = 1.0 / rho;
                d.add(sec);
            }
            d.gain = 1.0 / std::max(1.0e-9, std::abs(d.at(w)));
            break;
        }
    }
    return d;
}

/** Filtro para escuchar la banda sola ("solo") y para que la dinámica vigile su zona:
    pasabanda en los bell/notch/pasabanda, pasabajos en lo que actúa abajo (low shelf, low
    cut: se oye lo que se saca) y pasaaltos en lo que actúa arriba. */
inline AnalogDesign designAudition(Shape shape, double freq, double q, bool forDetector = false)
{
    AnalogDesign d;
    AnalogSection sec;
    sec.w = 2.0 * kPi * freq;
    sec.d0 = 1.0;
    switch (shape)
    {
        case Shape::Bell:
        case Shape::Notch:
        case Shape::BandPass:
        {
            const double qs = std::clamp(q, forDetector ? 0.5 : 0.3, 30.0);
            sec.d1 = 1.0 / qs;
            sec.n0 = 0.0;
            sec.n1 = 1.0 / qs;
            sec.n2 = 0.0;
            d.add(sec);
            break;
        }
        case Shape::LowShelf:
        case Shape::LowCut:
            sec.d1 = 1.41421356237309505;
            sec.n0 = 1.0;
            sec.n1 = sec.n2 = 0.0;
            d.add(sec);
            break;
        case Shape::HighShelf:
        case Shape::HighCut:
            sec.d1 = 1.41421356237309505;
            sec.n2 = 1.0;
            sec.n0 = sec.n1 = 0.0;
            d.add(sec);
            break;
        case Shape::TiltShelf:
            if (forDetector)
            {
                sec.d1 = 1.41421356237309505;
                sec.n2 = 1.0;
                sec.n0 = sec.n1 = 0.0;
                d.add(sec);
            }
            break;
        case Shape::FlatTilt:
            break;
    }
    return d;
}

//==============================================================================
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

struct BiquadState
{
    double z1 = 0.0, z2 = 0.0;
};

inline double tick(const Biquad& c, BiquadState& s, double x)
{
    const double y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}

inline std::complex<double> biquadAt(const Biquad& b, double omega)
{
    const std::complex<double> z1 = std::polar(1.0, -omega);
    return (b.b0 + z1 * (b.b1 + z1 * b.b2)) / (1.0 + z1 * (b.a1 + z1 * b.a2));
}

struct DigitalDesign
{
    int count = 0;
    std::array<Biquad, maxSections> s {};

    std::complex<double> at(double omega) const
    {
        std::complex<double> h(1.0, 0.0);
        for (int i = 0; i < count; ++i)
            h *= biquadAt(s[(size_t) i], omega);
        return h;
    }
};

namespace detail
{
    inline double sq(double v) { return v * v; }

    /** |B(e^jw)|² de un numerador b0 + b1 z⁻¹ + b2 z⁻² escrito como B0 φ0 + B1 φ1 + B2 φ2, con
        φ1 = sin²(w/2), φ0 = 1 - φ1, φ2 = 4 φ0 φ1 (B0 = (b0+b1+b2)², B1 = (b0-b1+b2)², B2 = -4 b0 b2).
        Lo mismo vale para el denominador con A0, A1, A2. Es lineal en B: eso permite ajustar. */
    struct Phi
    {
        double p0, p1, p2;
        explicit Phi(double om)
        {
            const double sh = std::sin(0.5 * om);
            p1 = sh * sh;
            p0 = 1.0 - p1;
            p2 = 4.0 * p0 * p1;
        }
    };

    inline double denPhi(const Biquad& q, const Phi& f)
    {
        const double A0 = sq(1.0 + q.a1 + q.a2), A1 = sq(1.0 - q.a1 + q.a2), A2 = -4.0 * q.a2;
        return A0 * f.p0 + A1 * f.p1 + A2 * f.p2;
    }

    /** Numerador de fase mínima a partir de B0, B1, B2. Devuelve false si no era realizable con
        los ceros adentro del círculo (se fuerza B2 al límite: ceros sobre el círculo). */
    inline bool factorNumerator(double B0, double B1, double B2, Biquad& q)
    {
        const double S = std::sqrt(std::max(0.0, B0));
        const double D = std::sqrt(std::max(0.0, B1));
        const double m = 0.5 * (S + D);
        const double disc = m * m + B2;
        const double r = std::sqrt(std::max(0.0, disc));
        q.b0 = 0.5 * (m + r);
        q.b2 = 0.5 * (m - r);
        q.b1 = 0.5 * (S - D);
        return disc > 1.0e-9 * m * m;
    }

    /** Frecuencia característica de la sección (rad/s): el medio geométrico entre polos y ceros. */
    inline double characteristic(const AnalogSection& a)
    {
        const double wp = a.firstOrder ? a.w * a.d0 : a.w * std::sqrt(a.d0);
        if (!a.firstOrder && a.n2 > 0.0 && a.n0 > 0.0)
            return std::sqrt(wp * a.w * std::sqrt(a.n0 / a.n2));
        if (a.firstOrder && a.n1 > 0.0 && a.n0 > 0.0)
            return std::sqrt(wp * a.w * a.n0 / a.n1);
        return wp;
    }

    /** La frecuencia más alta entre polos y ceros (rad/s). */
    inline double highestFeature(const AnalogSection& a)
    {
        const double wp = a.firstOrder ? a.w * a.d0 : a.w * std::sqrt(a.d0);
        double wz = 0.0;
        if (!a.firstOrder && a.n2 > 0.0 && a.n0 > 0.0)
            wz = a.w * std::sqrt(a.n0 / a.n2);
        if (a.firstOrder && a.n1 > 0.0 && a.n0 > 0.0)
            wz = a.w * a.n0 / a.n1;
        return std::max(wp, wz);
    }

    enum class Fit { General, Highpass, Notch, Bilinear };

    inline Fit classify(const AnalogSection& a, double fs)
    {
        // Muy por debajo de Nyquist la bilineal es exacta y numéricamente más sana.
        if (highestFeature(a) / fs < 0.03)
            return Fit::Bilinear;
        if (a.n0 == 0.0 && (a.firstOrder ? a.n1 > 0.0 : (a.n1 == 0.0 && a.n2 > 0.0)))
            return Fit::Highpass;
        if (!a.firstOrder && a.n1 == 0.0 && a.n0 > 0.0 && a.n2 > 0.0)
            return Fit::Notch;
        return Fit::General;
    }

    /** Transformada bilineal con la frecuencia característica sin deformar. */
    inline Biquad bilinearSection(const AnalogSection& a, double fs)
    {
        const double wm = std::min(characteristic(a), 0.45 * 2.0 * kPi * fs);
        const double K = wm / std::tan(0.5 * wm / fs);
        const double c = K / a.w;
        Biquad q;
        if (a.firstOrder)
        {
            const double a0 = c + a.d0;
            q.b0 = (a.n1 * c + a.n0) / a0;
            q.b1 = (a.n0 - a.n1 * c) / a0;
            q.a1 = (a.d0 - c) / a0;
            return q;
        }
        const double c2 = c * c;
        const double a0 = c2 + a.d1 * c + a.d0;
        q.b0 = (a.n2 * c2 + a.n1 * c + a.n0) / a0;
        q.b1 = 2.0 * (a.n0 - a.n2 * c2) / a0;
        q.b2 = (a.n2 * c2 - a.n1 * c + a.n0) / a0;
        q.a1 = 2.0 * (a.d0 - c2) / a0;
        q.a2 = (c2 - a.d1 * c + a.d0) / a0;
        return q;
    }

    /** Polos por invarianza al impulso (exactos). Si un polo cae por encima de Nyquist (una
        banda a 25 kHz con 44.1 kHz) se lo deja justo debajo, con su mismo amortiguamiento. */
    inline Biquad matchedPoles(const AnalogSection& a, double fs)
    {
        const double T = 1.0 / fs;
        Biquad q;
        if (a.firstOrder)
        {
            q.a1 = -std::exp(-a.d0 * a.w * T);
            return q;
        }
        const double disc = a.d1 * a.d1 - 4.0 * a.d0;
        if (disc < 0.0)
        {
            const double r = std::exp(-0.5 * a.d1 * a.w * T);
            const double th = std::min(0.5 * std::sqrt(-disc) * a.w * T, 0.95 * kPi);
            q.a1 = -2.0 * r * std::cos(th);
            q.a2 = r * r;
        }
        else
        {
            const double sqd = std::sqrt(disc);
            const double z1 = std::exp(0.5 * (-a.d1 + sqd) * a.w * T);
            const double z2 = std::exp(0.5 * (-a.d1 - sqd) * a.w * T);
            q.a1 = -(z1 + z2);
            q.a2 = z1 * z2;
        }
        return q;
    }

    /** Grilla para ajustar una sección: de wc/16 a Nyquist más un racimo de puntos alrededor
        de wc del ancho de la resonancia (en las secciones muy agudas ahí está todo). */
    struct Grid
    {
        static constexpr int maxPoints = 64;
        int count = 0;
        double om[maxPoints] {}, weight[maxPoints] {};

        void build(const AnalogSection& a, double fs)
        {
            count = 0;
            const double wc = std::clamp(characteristic(a) / fs, 1.0e-6, kPi);
            const double lo = std::clamp(wc / 16.0, 1.0e-5, 0.05);
            const double audibleTop = 2.0 * kPi * 20000.0 / fs;
            constexpr int K = 40, Kc = 9;
            for (int i = 0; i < K; ++i)
                push(lo * std::pow(kPi / lo, i / (double) (K - 1)), 1.0, audibleTop);
            const double qSec = a.firstOrder ? 0.5 : std::sqrt(a.d0) / std::max(a.d1, 1.0e-9);
            const double bwOct = std::clamp(1.4427 / qSec, 0.05, 2.0);
            for (int i = 0; i < Kc; ++i)
            {
                const double o = wc * std::pow(2.0, (i - Kc / 2) * bwOct / (Kc - 1));
                if (o < kPi)
                    push(o, 4.0, audibleTop);
            }
        }

        void push(double o, double w, double audibleTop)
        {
            if (count < maxPoints)
            {
                // Pegado a Nyquist pesa menos: mejor resignar ahí que en el medio de la banda.
                if (o > audibleTop)
                    w *= 0.1;
                else if (o > 0.66 * kPi)
                    w *= 0.3;
                om[count] = o;
                weight[count] = w;
                ++count;
            }
        }
    };

    /** Ajusta el numerador (polos fijos en q) para que |H|² siga a target[k] en la grilla:
        B0 exacto (magnitud en 0 Hz) y B1, B2 por mínimos cuadrados del error relativo (= dB). */
    /** `strict`: la sección se va a invertir, así que sus ceros (que serán polos) tienen que quedar
        estrictamente adentro del círculo; si no se puede, devuelve false y no toca q. */
    inline bool fitNumerator(Biquad& q, bool firstOrder, double dcMag2, const Grid& g, const double* target, bool strict)
    {
        const double A0 = sq(1.0 + q.a1 + q.a2);
        const double B0 = dcMag2 * A0;
        double s11 = 0, s12 = 0, s22 = 0, r1 = 0, r2 = 0;
        for (int i = 0; i < g.count; ++i)
        {
            const Phi f(g.om[i]);
            const double t = target[i] * denPhi(q, f);
            const double y = t - B0 * f.p0;
            const double wgt = g.weight[i] / sq(t + 1.0e-30);
            const double x1 = f.p1, x2 = firstOrder ? 0.0 : f.p2;
            s11 += wgt * x1 * x1;
            s12 += wgt * x1 * x2;
            s22 += wgt * x2 * x2;
            r1 += wgt * x1 * y;
            r2 += wgt * x2 * y;
        }
        if (firstOrder)
        {
            const double B1 = s11 > 0.0 ? r1 / s11 : 0.0;
            const double S = std::sqrt(std::max(0.0, B0));
            const double D = std::sqrt(std::max(0.0, B1));
            q.b0 = 0.5 * (S + D);
            q.b1 = 0.5 * (S - D);
            q.b2 = 0.0;
            return true;
        }
        double B1 = 0.0, B2 = 0.0;
        const double det = s11 * s22 - s12 * s12;
        if (std::abs(det) > 1.0e-300)
        {
            B1 = (r1 * s22 - r2 * s12) / det;
            B2 = (s11 * r2 - s12 * r1) / det;
        }
        if (B1 < 0.0 || std::abs(det) <= 1.0e-300)
        {
            B1 = std::max(0.0, B1);
            B2 = s22 > 0.0 ? (r2 - s12 * B1) / s22 : 0.0;
        }
        Biquad trial = q;
        if (!factorNumerator(B0, B1, B2, trial) && strict)
            return false;
        q = trial;
        return true;
    }

    inline Biquad invertBiquad(const Biquad& q)
    {
        // Se intercambian numerador y denominador. El numerador es de fase mínima, pero en casos
        // extremos (shelves con Q enorme) sus ceros quedan sobre el círculo: se los mete apenas
        // adentro para que, ya como polos, sean estables.
        const double g = q.b0;
        double a1 = q.b1 / g, a2 = q.b2 / g;
        const double disc = a1 * a1 - 4.0 * a2;
        const double radius = disc < 0.0 ? std::sqrt(a2) : 0.5 * (std::abs(a1) + std::sqrt(disc));
        constexpr double maxRadius = 1.0 - 1.0e-9;
        if (radius > maxRadius)
        {
            const double s = maxRadius / radius;
            a1 *= s;
            a2 *= s * s;
        }
        Biquad r;
        r.b0 = 1.0 / g;
        r.b1 = q.a1 / g;
        r.b2 = q.a2 / g;
        r.a1 = a1;
        r.a2 = a2;
        return r;
    }

    /** Pasa una sección analógica (sin invertir) a biquad igualando la magnitud. `willInvert`:
        después se va a invertir (sus ceros tienen que quedar adentro del círculo). */
    inline Biquad matchedSection(const AnalogSection& a, double fs, bool willInvert = false)
    {
        const Fit kind = classify(a, fs);
        if (kind == Fit::Bilinear)
            return bilinearSection(a, fs);

        Biquad q = matchedPoles(a, fs);
        const double nyquist = kPi * fs;
        const double wc = std::clamp(characteristic(a) / fs, 1.0e-6, kPi);

        if (kind == Fit::Highpass)
        {
            // Pasaaltos: los ceros van exactos en 0 Hz, (1 - z⁻¹)ⁿ; solo se ajusta la ganancia.
            constexpr int K = 40;
            const double lo = std::clamp(wc / 16.0, 1.0e-6, 0.05);
            double sr = 0.0, srr = 0.0;
            for (int i = 0; i < K; ++i)
            {
                const double om = lo * std::pow(kPi / lo, i / (double) (K - 1));
                const Phi f(om);
                const double t = a.mag2(om * fs) * denPhi(q, f);
                if (t <= 1.0e-300)
                    continue;
                const double r = (a.firstOrder ? 4.0 * f.p1 : 16.0 * f.p1 * f.p1) / t;
                sr += r;
                srr += r * r;
            }
            const double k = srr > 0.0 ? std::sqrt(std::max(0.0, sr / srr)) : 0.0;
            q.b0 = k;
            q.b1 = a.firstOrder ? -k : -2.0 * k;
            q.b2 = a.firstOrder ? 0.0 : k;
            return q;
        }

        if (kind == Fit::Notch)
        {
            // Notch: tres puntos (0 Hz, Nyquist y el centro) para conservar el pozo infinito.
            const double om = std::min(wc, 0.9 * kPi);
            const Phi f(om);
            const double A0 = sq(1.0 + q.a1 + q.a2), A1 = sq(1.0 - q.a1 + q.a2);
            const double B0 = a.mag2(0.0) * A0;
            const double B1 = a.mag2(nyquist) * A1;
            const double B2 = (a.mag2(om * fs) * denPhi(q, f) - B0 * f.p0 - B1 * f.p1) / f.p2;
            factorNumerator(B0, B1, B2, q);
            return q;
        }

        Grid g;
        g.build(a, fs);
        double target[Grid::maxPoints];
        for (int i = 0; i < g.count; ++i)
            target[i] = a.mag2(g.om[i] * fs);
        // Si el ajuste no es realizable (casos extremos), esa sección va por la bilineal.
        if (!fitNumerator(q, a.firstOrder, a.mag2(0.0), g, target, willInvert))
            return bilinearSection(a, fs);
        return q;
    }

    inline double biquadMag2(const Biquad& b, double om) { return std::norm(biquadAt(b, om)); }
}

/** Biquads digitales (magnitud igualada) para una frecuencia de muestreo.

    Cada sección se ajusta por separado y, si la cascada tiene varias secciones ajustables
    (cortes y shelves empinados), se hacen dos pasadas más en las que cada sección se reajusta
    contra lo que le falta al total: así los pequeños errores de cada una no se suman. */
inline DigitalDesign toDigital(const AnalogDesign& d, double fs)
{
    DigitalDesign out;
    const int n = d.count;

    // Los recortes (secciones invertidas) se diseñan como el realce espejado y se invierten al final.
    bool allInverted = n > 0;
    for (int i = 0; i < n; ++i)
        allInverted = allInverted && d.s[(size_t) i].invert;

    std::array<AnalogSection, maxSections> plain {};
    std::array<bool, maxSections> adjustable {};
    int numAdjustable = 0;
    for (int i = 0; i < n; ++i)
    {
        plain[(size_t) i] = d.s[(size_t) i];
        plain[(size_t) i].invert = false;
        out.s[(size_t) i] = detail::matchedSection(plain[(size_t) i], fs, d.s[(size_t) i].invert);
        adjustable[(size_t) i] = detail::classify(plain[(size_t) i], fs) == detail::Fit::General;
        numAdjustable += adjustable[(size_t) i] ? 1 : 0;
    }
    out.count = n;

    const bool refine = numAdjustable >= 2 && (allInverted || [&] {
        for (int i = 0; i < n; ++i)
            if (d.s[(size_t) i].invert)
                return false;
        return true;
    }());

    if (refine)
    {
        // Grilla común: la de cada sección ajustable, unida.
        constexpr int maxPts = 192;
        double om[maxPts], wgt[maxPts], analog[maxPts];
        int count = 0;
        for (int i = 0; i < n && count < maxPts; ++i)
        {
            if (!adjustable[(size_t) i])
                continue;
            detail::Grid g;
            g.build(plain[(size_t) i], fs);
            for (int k = 0; k < g.count && count < maxPts; ++k)
            {
                om[count] = g.om[k];
                wgt[count] = g.weight[k];
                ++count;
            }
        }
        for (int k = 0; k < count; ++k)
        {
            double m = 1.0;
            for (int i = 0; i < n; ++i)
                m *= plain[(size_t) i].mag2(om[k] * fs);
            analog[k] = m;
        }

        static thread_local double secMag[maxSections][maxPts];
        for (int i = 0; i < n; ++i)
            for (int k = 0; k < count; ++k)
                secMag[i][k] = detail::biquadMag2(out.s[(size_t) i], om[k]);

        detail::Grid g;
        for (int pass = 0; pass < 4; ++pass)
            for (int j = 0; j < n; ++j)
            {
                if (!adjustable[(size_t) j])
                    continue;
                // Grilla de 64 puntos como máximo: se toma uno de cada tanto de la común.
                const int stride = std::max(1, (count + detail::Grid::maxPoints - 1) / detail::Grid::maxPoints);
                double target[detail::Grid::maxPoints];
                g.count = 0;
                for (int k = 0; k < count && g.count < detail::Grid::maxPoints; k += stride)
                {
                    double others = 1.0;
                    for (int i = 0; i < n; ++i)
                        if (i != j)
                            others *= secMag[i][k];
                    if (others < 1.0e-30)
                        continue;
                    g.om[g.count] = om[k];
                    g.weight[g.count] = wgt[k];
                    target[g.count] = analog[k] / others;
                    ++g.count;
                }
                if (g.count < 4)
                    continue;
                detail::fitNumerator(out.s[(size_t) j], plain[(size_t) j].firstOrder, plain[(size_t) j].mag2(0.0), g, target,
                                     d.s[(size_t) j].invert);
                for (int k = 0; k < count; ++k)
                    secMag[j][k] = detail::biquadMag2(out.s[(size_t) j], om[k]);
            }
    }

    for (int i = 0; i < n; ++i)
        if (d.s[(size_t) i].invert)
            out.s[(size_t) i] = detail::invertBiquad(out.s[(size_t) i]);

    if (std::abs(d.gain - 1.0) > 1.0e-12)
    {
        if (out.count == 0)
            out.count = 1;
        auto& b = out.s[0];
        b.b0 *= d.gain;
        b.b1 *= d.gain;
        b.b2 *= d.gain;
    }
    return out;
}
} // namespace prisma
