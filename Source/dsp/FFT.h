#pragma once
// Radix-2 complex FFT in double precision. Twiddles are computed once, exactly per index.
#include <complex>
#include <vector>
#include <cmath>
#include <utility>

namespace dab {

constexpr double kPi = 3.14159265358979323846264338327950288;
using cd = std::complex<double>;

class FFT {
public:
    FFT() = default;
    explicit FFT (int order) { init (order); }

    void init (int order)
    {
        n = 1 << order;
        rev.assign ((size_t) n, 0);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < order; ++b)
                if (i & (1 << b)) r |= 1 << (order - 1 - b);
            rev[(size_t) i] = r;
        }
        tw.resize ((size_t) (n / 2));
        for (int k = 0; k < n / 2; ++k)
            tw[(size_t) k] = std::polar (1.0, -2.0 * kPi * k / n);
        // per-stage contiguous twiddles (stage len: entries [len/2 - 1, len - 1)), cache friendly
        stw.resize ((size_t) std::max (1, n - 1));
        for (int len = 2; len <= n; len <<= 1)
            for (int j = 0; j < len / 2; ++j)
                stw[(size_t) (len / 2 - 1 + j)] = tw[(size_t) (j * (n / len))];
    }

    int size() const { return n; }

    void forward (cd* d) const { transform (d, false); }

    void inverse (cd* d) const
    {
        transform (d, true);
        const double s = 1.0 / n;
        for (int i = 0; i < n; ++i) d[i] *= s;
    }

private:
    void transform (cd* d, bool inv) const
    {
        for (int i = 0; i < n; ++i)
            if (i < rev[(size_t) i]) std::swap (d[i], d[rev[(size_t) i]]);

        // plain double arithmetic (std::complex operator* adds NaN/inf recovery that costs a lot)
        double* a = reinterpret_cast<double*> (d);
        const double sg = inv ? -1.0 : 1.0;
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2;
            const cd* w0 = stw.data() + (half - 1);
            for (int i = 0; i < n; i += len)
            {
                double* p = a + 2 * i;
                double* q = a + 2 * (i + half);
                for (int j = 0; j < half; ++j)
                {
                    const double wr = w0[j].real(), wi = sg * w0[j].imag();
                    const double qr = q[2 * j], qi = q[2 * j + 1];
                    const double vr = qr * wr - qi * wi, vi = qr * wi + qi * wr;
                    const double ur = p[2 * j], ui = p[2 * j + 1];
                    p[2 * j] = ur + vr; p[2 * j + 1] = ui + vi;
                    q[2 * j] = ur - vr; q[2 * j + 1] = ui - vi;
                }
            }
        }
    }

    int n = 0;
    std::vector<int> rev;
    std::vector<cd> tw, stw;
};

inline int ceilLog2 (int x) { int o = 0; while ((1 << o) < x) ++o; return o; }

// FFT of a real signal of length N = 2^order through a complex FFT of length N/2 (half the work).
// Spectrum: bins 0 .. N/2 only (the rest is the mirror image).
class RealFFT {
public:
    void init (int order)
    {
        N = 1 << order; M = N / 2;
        half.init (order - 1);
        w.resize ((size_t) M + 1);
        for (int k = 0; k <= M; ++k) w[(size_t) k] = std::polar (1.0, -2.0 * kPi * k / N);
        z.assign ((size_t) M, cd (0.0));
    }
    int size() const { return N; }

    void forward (const double* x, cd* X)
    {
        for (int m = 0; m < M; ++m) z[(size_t) m] = cd (x[2 * m], x[2 * m + 1]);
        half.forward (z.data());
        for (int k = 0; k <= M; ++k)
        {
            const cd a = z[(size_t) (k % M)], b = std::conj (z[(size_t) ((M - k) % M)]);
            const double er = 0.5 * (a.real() + b.real()), ei = 0.5 * (a.imag() + b.imag());
            const double orr = 0.5 * (a.imag() - b.imag()), oi = -0.5 * (a.real() - b.real());   // (a - b) / 2i
            const double wr = w[(size_t) k].real(), wi = w[(size_t) k].imag();
            X[k] = cd (er + orr * wr - oi * wi, ei + orr * wi + oi * wr);
        }
    }

    // X: bins 0 .. N/2. Scaled so that inverse (forward (x)) == x.
    void inverse (const cd* X, double* x)
    {
        for (int k = 0; k < M; ++k)
        {
            const cd a = X[k], b = std::conj (X[M - k]);
            const double er = 0.5 * (a.real() + b.real()), ei = 0.5 * (a.imag() + b.imag());
            const double dr = 0.5 * (a.real() - b.real()), di = 0.5 * (a.imag() - b.imag());
            const double wr = w[(size_t) k].real(), wi = -w[(size_t) k].imag();          // W^-k
            const double orr = dr * wr - di * wi, oi = dr * wi + di * wr;
            z[(size_t) k] = cd (er - oi, ei + orr);                                       // E + i O
        }
        half.inverse (z.data());
        for (int m = 0; m < M; ++m) { x[2 * m] = z[(size_t) m].real(); x[2 * m + 1] = z[(size_t) m].imag(); }
    }

private:
    int N = 0, M = 0;
    FFT half;
    std::vector<cd> w, z;
};

} // namespace dab
