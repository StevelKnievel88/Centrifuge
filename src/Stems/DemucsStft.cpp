#include "DemucsStft.h"
#include <cmath>

namespace orbit
{

namespace
{
    // torch-style reflect padding (the edge sample is not repeated).
    void reflectPad (const float* x, int length, int padLeft, int padRight, std::vector<float>& out)
    {
        out.resize ((size_t) (length + padLeft + padRight));
        for (int i = 0; i < padLeft; ++i)
            out[(size_t) i] = x[juce::jlimit (0, length - 1, padLeft - i)];
        std::memcpy (out.data() + padLeft, x, sizeof (float) * (size_t) length);
        for (int j = 0; j < padRight; ++j)
            out[(size_t) (padLeft + length + j)] = x[juce::jlimit (0, length - 1, length - 2 - j)];
    }
}

DemucsStft::DemucsStft()
{
    window.resize (nfft);
    for (int n = 0; n < nfft; ++n)
        window[(size_t) n] = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * n / nfft));
    work.resize (nfft * 2);
}

void DemucsStft::forward (const float* x, int length, float* re, float* im)
{
    const int le = numFrames (length);
    const int pad = hop / 2 * 3;                       // 1536
    const int padRight = pad + le * hop - length;

    // demucs' own padding, then torch.stft's centre padding (n_fft / 2 each side)
    std::vector<float> stage1;
    reflectPad (x, length, pad, padRight, stage1);
    reflectPad (stage1.data(), (int) stage1.size(), nfft / 2, nfft / 2, padded);

    const float norm = 1.0f / std::sqrt ((float) nfft); // normalized=True
    for (int f = 0; f < le; ++f)
    {
        const int start = (f + 2) * hop;               // demucs drops the first two frames
        for (int n = 0; n < nfft; ++n)
            work[(size_t) n] = padded[(size_t) (start + n)] * window[(size_t) n];
        std::fill (work.begin() + nfft, work.end(), 0.0f);

        fft.performRealOnlyForwardTransform (work.data(), true);

        for (int k = 0; k < bins; ++k)
        {
            re[(size_t) k * (size_t) le + (size_t) f] = work[(size_t) k * 2] * norm;
            im[(size_t) k * (size_t) le + (size_t) f] = work[(size_t) k * 2 + 1] * norm;
        }
    }
}

void DemucsStft::inverse (const float* re, const float* im, int length, float* y)
{
    const int le = numFrames (length);
    const int framesTotal = le + 4;                    // two silent frames padded on each side
    const int olaLen = (framesTotal - 1) * hop + nfft;

    ola.assign ((size_t) olaLen, 0.0f);
    envelope.assign ((size_t) olaLen, 0.0f);

    const float scale = std::sqrt ((float) nfft);      // undo normalized=True

    for (int t = 0; t < framesTotal; ++t)
    {
        const int start = t * hop;
        for (int n = 0; n < nfft; ++n)
            envelope[(size_t) (start + n)] += window[(size_t) n] * window[(size_t) n];

        const int f = t - 2;
        if (f < 0 || f >= le) continue;                // zero frames contribute nothing but the envelope

        std::fill (work.begin(), work.end(), 0.0f);
        for (int k = 0; k < bins; ++k)
        {
            work[(size_t) k * 2]     = re[(size_t) k * (size_t) le + (size_t) f] * scale;
            work[(size_t) k * 2 + 1] = im[(size_t) k * (size_t) le + (size_t) f] * scale;
        }
        // bin 2048 (Nyquist) stays zero, as in demucs' _ispec

        fft.performRealOnlyInverseTransform (work.data());

        for (int n = 0; n < nfft; ++n)
            ola[(size_t) (start + n)] += work[(size_t) n] * window[(size_t) n];
    }

    // torch.istft: remove centre padding (n_fft / 2), then demucs removes its own pad (1536)
    const int offset = nfft / 2 + hop / 2 * 3;
    for (int i = 0; i < length; ++i)
    {
        const auto idx = (size_t) (offset + i);
        const float e = envelope[idx];
        y[i] = e > 1.0e-11f ? ola[idx] / e : 0.0f;
    }
}

} // namespace orbit
