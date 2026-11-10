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

        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    cd w = tw[(size_t) (j * step)];
                    if (inv) w = std::conj (w);
                    const cd u = d[i + j], v = d[i + j + half] * w;
                    d[i + j] = u + v;
                    d[i + j + half] = u - v;
                }
        }
    }

    int n = 0;
    std::vector<int> rev;
    std::vector<cd> tw;
};

inline int ceilLog2 (int x) { int o = 0; while ((1 << o) < x) ++o; return o; }

} // namespace dab
