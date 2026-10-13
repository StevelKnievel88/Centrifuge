#include "DemucsSeparator.h"
#include "DemucsStft.h"
#include <cmath>
#include <thread>

#if ORBIT_HAS_ORT
 #include <onnxruntime_cxx_api.h>
 #if ORBIT_HAS_DML
  #include <dml_provider_factory.h>
 #endif
#endif

namespace orbit
{

struct DemucsSeparator::Impl
{
#if ORBIT_HAS_ORT
    Ort::Env env { ORT_LOGGING_LEVEL_WARNING, "Centrifuge" };
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> inNames, outNames;
    int mixInput = 0, specInput = 1, specOutput = 0, timeOutput = 1;
#endif
    int segment = 343980;
    int sources = 4;
    juce::String backend = "not loaded";
    DemucsStft stft;
};

DemucsSeparator::DemucsSeparator() : impl (std::make_unique<Impl>()) {}
DemucsSeparator::~DemucsSeparator() = default;

bool DemucsSeparator::isAvailable()
{
    return ORBIT_HAS_ORT != 0;
}

bool DemucsSeparator::isLoaded() const
{
#if ORBIT_HAS_ORT
    return impl->session != nullptr;
#else
    return false;
#endif
}

juce::String DemucsSeparator::getBackendDescription() const { return impl->backend; }

bool DemucsSeparator::load (const Options& opt, juce::String& error)
{
#if ! ORBIT_HAS_ORT
    juce::ignoreUnused (opt);
    error = "This build has no stem separation engine";
    return false;
#else
    if (! opt.modelFile.existsAsFile())
    {
        error = "Stem model not found: " + opt.modelFile.getFullPathName();
        return false;
    }

    const int cores = (int) juce::jmax (1u, std::thread::hardware_concurrency());
    const int threads = opt.cpuThreads > 0 ? opt.cpuThreads : juce::jmax (1, cores - 2);

   #if JUCE_WINDOWS
    const auto path = opt.modelFile.getFullPathName().toWideCharPointer();
   #else
    const auto path = opt.modelFile.getFullPathName().toRawUTF8();
   #endif

    auto makeCpuOptions = [threads]
    {
        Ort::SessionOptions so;
        so.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
        so.SetIntraOpNumThreads (threads);
        so.SetInterOpNumThreads (1);
        return so;
    };

    impl->session.reset();
    juce::String gpuError;

   #if ORBIT_HAS_DML
    if (opt.useGpu)
    {
        try
        {
            Ort::SessionOptions so;
            so.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
            so.DisableMemPattern();
            so.SetExecutionMode (ExecutionMode::ORT_SEQUENTIAL);
            Ort::ThrowOnError (OrtSessionOptionsAppendExecutionProvider_DML (so, 0));
            impl->session = std::make_unique<Ort::Session> (impl->env, path, so);
            impl->backend = "GPU (DirectML)";
        }
        catch (const std::exception& e)
        {
            gpuError = e.what();
            impl->session.reset();
        }
    }
   #endif

    if (impl->session == nullptr)
    {
        try
        {
            auto so = makeCpuOptions();
            impl->session = std::make_unique<Ort::Session> (impl->env, path, so);
            impl->backend = "CPU (" + juce::String (threads) + " threads)"
                          + (gpuError.isNotEmpty() ? " - GPU failed: " + gpuError : juce::String());
        }
        catch (const std::exception& e)
        {
            error = "Couldn't load the stem model: " + juce::String (e.what());
            impl->session.reset();
            return false;
        }
    }

    // Work out which input/output is which from their shapes.
    Ort::AllocatorWithDefaultOptions alloc;
    impl->inNames.clear();
    impl->outNames.clear();
    for (size_t i = 0; i < impl->session->GetInputCount(); ++i)
    {
        impl->inNames.emplace_back (impl->session->GetInputNameAllocated (i, alloc).get());
        auto shape = impl->session->GetInputTypeInfo (i).GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() == 3)
        {
            impl->mixInput = (int) i;
            if (shape[2] > 0) impl->segment = (int) shape[2];
        }
        else impl->specInput = (int) i;
    }
    for (size_t i = 0; i < impl->session->GetOutputCount(); ++i)
    {
        impl->outNames.emplace_back (impl->session->GetOutputNameAllocated (i, alloc).get());
        auto shape = impl->session->GetOutputTypeInfo (i).GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() == 5)
        {
            impl->specOutput = (int) i;
            if (shape[1] > 0) impl->sources = (int) shape[1];
        }
        else impl->timeOutput = (int) i;
    }

    if (impl->inNames.size() != 2 || impl->outNames.size() != 2 || impl->sources != 4)
    {
        error = "Unexpected stem model layout";
        impl->session.reset();
        return false;
    }
    return true;
#endif
}

bool DemucsSeparator::separate (const TrackAudio& audio, StemAudio& out,
                                const std::function<bool (double)>& progress, juce::String& error)
{
#if ! ORBIT_HAS_ORT
    juce::ignoreUnused (audio, out, progress);
    error = "This build has no stem separation engine";
    return false;
#else
    if (impl->session == nullptr) { error = "Stem model not loaded"; return false; }

    constexpr double modelRate = 44100.0;
    const double srcRate = audio.sampleRate;
    const int64_t N = audio.numFrames;
    if (N <= 0) { error = "Empty track"; return false; }

    // --- 1. Input at 44.1 kHz ---------------------------------------------------
    std::vector<float> rl, rr;
    const float* inL = audio.left.data();
    const float* inR = audio.right.data();
    int64_t n44 = N;
    const bool sameRate = std::abs (srcRate - modelRate) < 0.5;

    if (! sameRate)
    {
        n44 = (int64_t) std::floor ((double) N * modelRate / srcRate);
        rl.resize ((size_t) n44);
        rr.resize ((size_t) n44);
        juce::WindowedSincInterpolator il, ir;
        const double ratio = srcRate / modelRate;
        il.process (ratio, audio.left.data(), rl.data(), (int) n44, (int) N, 0);
        ir.process (ratio, audio.right.data(), rr.data(), (int) n44, (int) N, 0);
        inL = rl.data();
        inR = rr.data();
    }

    // --- 2. Whole-track normalisation (demucs.separate) -------------------------
    double sum = 0.0;
    for (int64_t i = 0; i < n44; ++i) sum += 0.5 * (inL[i] + inR[i]);
    const double mean = sum / (double) n44;
    double var = 0.0;
    for (int64_t i = 0; i < n44; ++i) { const double d = 0.5 * (inL[i] + inR[i]) - mean; var += d * d; }
    const double stdev = std::sqrt (var / (double) juce::jmax<int64_t> (1, n44 - 1));
    const float fMean = (float) mean;
    const float fStd = (float) juce::jmax (1.0e-8, stdev);

    // --- 3. Output buffers --------------------------------------------------------
    out.sampleRate = srcRate;
    out.numFrames = N;
    for (auto& ch : out.channels) ch.assign ((size_t) N, 0);
    out.validFrames = 0;

    // --- 4. Segmenting / overlap-add (demucs.apply.apply_model, overlap 0.25) ----
    const int seg = impl->segment;
    const int stride = (int) ((1.0 - 0.25) * seg);
    const int half = seg / 2;
    std::vector<float> weight ((size_t) seg);
    for (int i = 0; i < seg; ++i)
        weight[(size_t) i] = (float) (i < half ? i + 1 : seg - i) / (float) juce::jmax (half, seg - half);

    const int le = DemucsStft::numFrames (seg);
    const size_t specSize = (size_t) DemucsStft::bins * (size_t) le;

    std::vector<float> mix ((size_t) 2 * seg);
    std::vector<float> spec ((size_t) 4 * specSize);
    std::vector<float> tmpRe (specSize), tmpIm (specSize), ybuf ((size_t) seg);

    // Sliding accumulators (44.1 kHz): 4 sources x 2 channels, plus summed weights.
    const size_t accLen = (size_t) seg * 2;
    std::array<std::vector<float>, 8> acc;
    for (auto& a : acc) a.assign (accLen, 0.0f);
    std::vector<float> wsum (accLen, 0.0f);
    int64_t base = 0;

    // Finalised 44.1 kHz samples, with a little history for resampling to the track's rate.
    constexpr int hist = 3;
    std::array<std::vector<float>, 8> fin;
    for (auto& f : fin) f.assign (hist, 0.0f);
    int64_t finStart = -hist;        // 44.1k index of fin[k][0]
    int64_t nextOut = 0;             // next output frame (track rate) to write
    const double outTo44 = modelRate / srcRate;

    auto emitOutput = [&] (int64_t finalisedUpTo, bool last)
    {
        // Produce output frames whose 44.1k neighbourhood is finalised.
        while (nextOut < N)
        {
            float v[8];
            if (sameRate)
            {
                if (nextOut >= finalisedUpTo) break;
                const auto idx = (size_t) (nextOut - finStart);
                for (size_t k = 0; k < 8; ++k) v[k] = fin[k][idx];
            }
            else
            {
                const double p = (double) nextOut * outTo44;
                const auto ip = (int64_t) std::floor (p);
                if (! last && ip + 2 >= finalisedUpTo) break;
                const float t = (float) (p - (double) ip);
                for (size_t k = 0; k < 8; ++k)
                {
                    auto at = [&] (int64_t i) -> float
                    {
                        const auto rel = i - finStart;
                        if (rel < 0 || rel >= (int64_t) fin[k].size()) return 0.0f;
                        return fin[k][(size_t) rel];
                    };
                    const float xm1 = at (ip - 1), x0 = at (ip), x1 = at (ip + 1), x2 = at (ip + 2);
                    const float c1 = 0.5f * (x1 - xm1);
                    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
                    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                    v[k] = ((c3 * t + c2) * t + c1) * t + x0;
                }
            }

            // drums 0, bass 1, other 2, vocals 3. "Other" absorbs the residual so stems sum to the original.
            const auto i = (size_t) nextOut;
            const float oL = audio.left[i], oR = audio.right[i];
            out.channels[0][i] = StemAudio::fromFloat (v[0]);
            out.channels[1][i] = StemAudio::fromFloat (v[1]);
            out.channels[2][i] = StemAudio::fromFloat (v[2]);
            out.channels[3][i] = StemAudio::fromFloat (v[3]);
            out.channels[6][i] = StemAudio::fromFloat (v[6]);
            out.channels[7][i] = StemAudio::fromFloat (v[7]);
            out.channels[4][i] = StemAudio::fromFloat (oL - (v[0] + v[2] + v[6]));
            out.channels[5][i] = StemAudio::fromFloat (oR - (v[1] + v[3] + v[7]));
            ++nextOut;
        }
        out.validFrames.store (nextOut, std::memory_order_release);
    };

    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
    const std::array<int64_t, 3> mixShape { 1, 2, seg };
    const std::array<int64_t, 4> specShape { 1, 4, DemucsStft::bins, le };
    std::vector<const char*> inNames, outNames;
    for (auto& s : impl->inNames) inNames.push_back (s.c_str());
    for (auto& s : impl->outNames) outNames.push_back (s.c_str());

    const int64_t numSegments = (n44 + stride - 1) / stride;
    int64_t segIndex = 0;

    for (int64_t offset = 0; offset < n44; offset += stride, ++segIndex)
    {
        if (progress && ! progress ((double) segIndex / (double) numSegments))
        {
            error = "Cancelled";
            return false;
        }

        const int chunkLen = (int) std::min<int64_t> (seg, n44 - offset);
        const int delta = seg - chunkLen;
        const int64_t start = offset - delta / 2;

        for (int j = 0; j < seg; ++j)
        {
            const int64_t s = start + j;
            const bool inside = s >= 0 && s < n44;
            mix[(size_t) j] = inside ? (inL[s] - fMean) / fStd : 0.0f;
            mix[(size_t) (seg + j)] = inside ? (inR[s] - fMean) / fStd : 0.0f;
        }

        for (int c = 0; c < 2; ++c)
            impl->stft.forward (mix.data() + (size_t) c * (size_t) seg, seg,
                                spec.data() + (size_t) (2 * c) * specSize,
                                spec.data() + (size_t) (2 * c + 1) * specSize);

        std::vector<Ort::Value> inputs;
        inputs.reserve (2);
        Ort::Value mixTensor = Ort::Value::CreateTensor<float> (memInfo, mix.data(), mix.size(), mixShape.data(), mixShape.size());
        Ort::Value specTensor = Ort::Value::CreateTensor<float> (memInfo, spec.data(), spec.size(), specShape.data(), specShape.size());
        std::array<Ort::Value*, 2> byIndex {};
        byIndex[(size_t) impl->mixInput] = &mixTensor;
        byIndex[(size_t) impl->specInput] = &specTensor;
        inputs.push_back (std::move (*byIndex[0]));
        inputs.push_back (std::move (*byIndex[1]));

        std::vector<Ort::Value> outputs;
        try
        {
            outputs = impl->session->Run (Ort::RunOptions { nullptr }, inNames.data(), inputs.data(), inputs.size(),
                                          outNames.data(), outNames.size());
        }
        catch (const std::exception& e)
        {
            error = "Stem model failed: " + juce::String (e.what());
            return false;
        }

        const float* xSpec = outputs[(size_t) impl->specOutput].GetTensorData<float>();
        const float* xTime = outputs[(size_t) impl->timeOutput].GetTensorData<float>();

        // Rebuild each source/channel: iSTFT(freq branch) + time branch, denormalise later.
        for (int s = 0; s < 4; ++s)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float* re = xSpec + ((size_t) (s * 4 + 2 * c)) * specSize;
                const float* im = xSpec + ((size_t) (s * 4 + 2 * c + 1)) * specSize;
                impl->stft.inverse (re, im, seg, ybuf.data());
                const float* xt = xTime + ((size_t) (s * 2 + c)) * (size_t) seg;

                auto& a = acc[(size_t) (s * 2 + c)];
                const int trim = delta / 2;
                const auto accOffset = (size_t) (offset - base);
                for (int j = 0; j < chunkLen; ++j)
                    a[accOffset + (size_t) j] += weight[(size_t) j] * (ybuf[(size_t) (trim + j)] + xt[trim + j]);
            }
        }
        {
            const auto accOffset = (size_t) (offset - base);
            for (int j = 0; j < chunkLen; ++j)
                wsum[accOffset + (size_t) j] += weight[(size_t) j];
        }

        // Everything before the next segment's start won't change any more: finalise it.
        const bool last = offset + stride >= n44;
        const int64_t upTo = last ? n44 : offset + stride;
        const auto count = (size_t) (upTo - base);

        for (size_t k = 0; k < 8; ++k)
        {
            auto& f = fin[k];
            // keep `hist` samples of history for interpolation
            std::vector<float> next;
            next.reserve (hist + count);
            next.insert (next.end(), f.end() - hist, f.end());
            for (size_t j = 0; j < count; ++j)
                next.push_back (acc[k][j] / juce::jmax (1.0e-8f, wsum[j]) * fStd + fMean);
            f.swap (next);
        }
        finStart = base - hist;

        // slide accumulators
        for (auto& a : acc)
        {
            std::memmove (a.data(), a.data() + count, sizeof (float) * (accLen - count));
            std::fill (a.end() - (long) count, a.end(), 0.0f);
        }
        std::memmove (wsum.data(), wsum.data() + count, sizeof (float) * (accLen - count));
        std::fill (wsum.end() - (long) count, wsum.end(), 0.0f);
        base = upTo;

        emitOutput (upTo, last);
    }

    emitOutput (n44, true);
    out.validFrames.store (N, std::memory_order_release);
    if (progress) progress (1.0);
    return true;
#endif
}

} // namespace orbit
