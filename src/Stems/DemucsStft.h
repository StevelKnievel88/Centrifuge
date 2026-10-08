#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace orbit
{

/** The exact STFT / inverse STFT that HTDemucs wraps around its network (moved out of the ONNX
    model because ONNX can't export them). Mirrors demucs' _spec/_ispec with torch.stft semantics:
    n_fft 4096, hop 1024, periodic Hann window, normalized, centred with reflect padding. */
class DemucsStft
{
public:
    static constexpr int nfft = 4096;
    static constexpr int hop = 1024;
    static constexpr int bins = 2048;   // the Nyquist bin is dropped

    DemucsStft();

    static int numFrames (int length) noexcept { return (length + hop - 1) / hop; }

    /** One channel of `length` samples -> re/im arrays laid out [bin * frames + frame]. */
    void forward (const float* x, int length, float* re, float* im);

    /** Inverse of forward(): re/im [bin * frames + frame] -> `length` samples. */
    void inverse (const float* re, const float* im, int length, float* y);

private:
    juce::dsp::FFT fft { 12 };
    std::vector<float> window, work, padded, ola, envelope;
};

} // namespace orbit
