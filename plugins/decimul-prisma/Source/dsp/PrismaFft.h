#pragma once

// FFT propia (radix 2, iterativa) para el EQ: compleja y real, en float o double.
// Solo biblioteca estándar, así el motor se prueba sin JUCE. init() reserva memoria:
// llamarlo fuera del hilo de audio; forward()/inverse() no reservan nada.

#include <cmath>
#include <complex>
#include <vector>

namespace prisma
{
template <typename T>
class ComplexFft
{
public:
    using C = std::complex<T>;

    void init(int size)
    {
        n = size;
        rev.assign((size_t) n, 0);
        int bits = 0;
        while ((1 << bits) < n)
            ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        tw.resize((size_t) std::max(1, n / 2));
        for (int k = 0; k < n / 2; ++k)
        {
            const double a = -2.0 * 3.14159265358979323846 * k / n;
            tw[(size_t) k] = C((T) std::cos(a), (T) std::sin(a));
        }
    }

    int size() const { return n; }

    /** Transformada directa, en el lugar, sin normalizar. */
    void forward(C* d) const { run(d, false); }
    /** Transformada inversa, en el lugar, SIN el 1/n. */
    void inverse(C* d) const { run(d, true); }

private:
    void run(C* d, bool inv) const
    {
        for (int i = 0; i < n; ++i)
            if (i < rev[(size_t) i])
                std::swap(d[i], d[rev[(size_t) i]]);

        T* f = reinterpret_cast<T*>(d);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1;
            const int step = n / len;
            for (int i = 0; i < n; i += len)
            {
                for (int j = 0; j < half; ++j)
                {
                    const C w = tw[(size_t) (j * step)];
                    const T wr = w.real(), wi = inv ? -w.imag() : w.imag();
                    T* u = f + 2 * (i + j);
                    T* v = f + 2 * (i + j + half);
                    const T vr = v[0] * wr - v[1] * wi;
                    const T vi = v[0] * wi + v[1] * wr;
                    v[0] = u[0] - vr;
                    v[1] = u[1] - vi;
                    u[0] += vr;
                    u[1] += vi;
                }
            }
        }
    }

    int n = 0;
    std::vector<int> rev;
    std::vector<C> tw;
};

/** FFT de una señal real de n muestras (n potencia de 2, n >= 4): n/2 + 1 bins. */
template <typename T>
class RealFft
{
public:
    using C = std::complex<T>;

    void init(int size)
    {
        n = size;
        half.init(n / 2);
        scratch.assign((size_t) (n / 2), C());
        w.resize((size_t) (n / 2 + 1));
        for (int k = 0; k <= n / 2; ++k)
        {
            const double a = -2.0 * 3.14159265358979323846 * k / n;
            w[(size_t) k] = C((T) std::cos(a), (T) std::sin(a));
        }
    }

    int size() const { return n; }

    /** in: n muestras reales. out: n/2 + 1 bins (sin normalizar). */
    void forward(const T* in, C* out)
    {
        const int h = n / 2;
        for (int k = 0; k < h; ++k)
            scratch[(size_t) k] = C(in[2 * k], in[2 * k + 1]);
        half.forward(scratch.data());
        for (int k = 0; k <= h; ++k)
        {
            const C zk = scratch[(size_t) (k % h)];
            const C zc = std::conj(scratch[(size_t) ((h - k) % h)]);
            const C fe = (zk + zc) * (T) 0.5;
            const C fo = (zk - zc) * C(0, (T) -0.5);
            out[k] = fe + w[(size_t) k] * fo;
        }
    }

    /** in: n/2 + 1 bins (espectro de una señal real). out: n muestras, ya escaladas (1/n). */
    void inverse(const C* in, T* out)
    {
        const int h = n / 2;
        for (int k = 0; k < h; ++k)
        {
            const C xk = in[k];
            const C xc = std::conj(in[h - k]);
            const C fe = (xk + xc) * (T) 0.5;
            const C fo = (xk - xc) * (T) 0.5 * std::conj(w[(size_t) k]);
            scratch[(size_t) k] = fe + C(0, 1) * fo;
        }
        half.inverse(scratch.data());
        const T s = (T) 1 / (T) h;
        for (int k = 0; k < h; ++k)
        {
            out[2 * k] = scratch[(size_t) k].real() * s;
            out[2 * k + 1] = scratch[(size_t) k].imag() * s;
        }
    }

private:
    int n = 0;
    ComplexFft<T> half;
    std::vector<C> scratch, w;
};
} // namespace prisma
