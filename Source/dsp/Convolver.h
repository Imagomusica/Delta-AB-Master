#pragma once
// Uniformly partitioned overlap-save FFT convolution in double precision,
// wrapped in a FIFO so the host block size does not matter.
// Latency is exactly blockSize samples, on top of the filter's own delay.
#include "FFT.h"
#include <algorithm>

namespace dab {

// Convolution block for a filter of N taps: 512 at 44.1/48 kHz (more at higher rates), and at most 16 partitions
// for long filters (export), which keeps the cost low; the extra latency only matters when listening.
inline int convBlock (double fs, int N)
{
    const int base = 512 * std::max (1, 1 << ceilLog2 ((int) std::floor (fs / 48000.0 + 1e-9)));
    return std::max (base, 1 << std::max (0, ceilLog2 (N + 1) - 4));
}

class PartitionedConvolver {
public:
    void prepare (const std::vector<double>& ir, int blockSizePow2)
    {
        B = blockSizePow2;
        fft.init (ceilLog2 (2 * B));
        P = std::max (1, (int) ((ir.size() + (size_t) B - 1) / (size_t) B));
        H.assign ((size_t) P, std::vector<cd> ((size_t) (2 * B)));
        X.assign ((size_t) P, std::vector<cd> ((size_t) (2 * B)));
        for (int p = 0; p < P; ++p)
        {
            auto& h = H[(size_t) p];
            for (int i = 0; i < B; ++i)
            {
                const size_t idx = (size_t) (p * B + i);
                h[(size_t) i] = idx < ir.size() ? ir[idx] : 0.0;
            }
            fft.forward (h.data());
        }
        inBuf.assign ((size_t) (2 * B), 0.0);
        tmp.assign ((size_t) (2 * B), 0.0);
        acc.assign ((size_t) (2 * B), 0.0);
        fdlPos = 0;
    }

    int blockSize() const { return B; }

    void reset()
    {
        for (auto& x : X) std::fill (x.begin(), x.end(), cd (0.0));
        std::fill (inBuf.begin(), inBuf.end(), 0.0);
        fdlPos = 0;
    }

    // Processes exactly B samples.
    void processBlock (const double* in, double* out)
    {
        std::copy (inBuf.begin() + B, inBuf.end(), inBuf.begin());
        std::copy (in, in + B, inBuf.begin() + B);
        for (int i = 0; i < 2 * B; ++i) tmp[(size_t) i] = inBuf[(size_t) i];
        fft.forward (tmp.data());
        X[(size_t) fdlPos] = tmp;

        std::fill (acc.begin(), acc.end(), cd (0.0));
        for (int p = 0; p < P; ++p)
        {
            const auto& x = X[(size_t) ((fdlPos - p + P) % P)];
            const auto& h = H[(size_t) p];
            for (int k = 0; k < 2 * B; ++k) acc[(size_t) k] += x[(size_t) k] * h[(size_t) k];
        }
        fft.inverse (acc.data());
        for (int i = 0; i < B; ++i) out[i] = acc[(size_t) (B + i)].real();
        fdlPos = (fdlPos + 1) % P;
    }

private:
    int B = 0, P = 0, fdlPos = 0;
    FFT fft;
    std::vector<std::vector<cd>> H, X;
    std::vector<double> inBuf;
    std::vector<cd> tmp, acc;
};

class FifoConvolver {
public:
    void prepare (const std::vector<double>& ir, int blockSizePow2)
    {
        conv.prepare (ir, blockSizePow2);
        B = blockSizePow2;
        inF.assign ((size_t) B, 0.0);
        outF.assign ((size_t) B, 0.0);
        pos = 0;
    }

    int latency() const { return B; }

    void reset()
    {
        conv.reset();
        std::fill (inF.begin(), inF.end(), 0.0);
        std::fill (outF.begin(), outF.end(), 0.0);
        pos = 0;
    }

    // Keeps the block clock running without convolving (input taken as silence, output silent).
    void skip (double* out, int n)
    {
        std::fill (out, out + n, 0.0);
        pos = (pos + n) % B;
    }

    void process (const double* in, double* out, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            inF[(size_t) pos] = in[i];
            out[i] = outF[(size_t) pos];
            if (++pos == B)
            {
                conv.processBlock (inF.data(), outF.data());
                pos = 0;
            }
        }
    }

private:
    PartitionedConvolver conv;
    int B = 0, pos = 0;
    std::vector<double> inF, outF;
};

} // namespace dab
