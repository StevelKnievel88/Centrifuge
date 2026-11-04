#include "DemucsSeparator.h"
#include "DemucsStft.h"
#include <cmath>
#include <thread>
#include <map>
#include <cstring>

#if ORBIT_HAS_ORT
 #include <onnxruntime_cxx_api.h>
 #if ORBIT_HAS_DML
  #include <dml_provider_factory.h>
 #endif
#endif

namespace orbit
{

#if ORBIT_HAS_ORT
/** One loaded ONNX model. HTDemucs-style models take (mix, spec) and give (spec_out, time_out);
    the RoFormer takes a waveform and gives waveforms. */
struct DemucsSeparator::Model
{
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> inNames, outNames;
    juce::File file;
    bool usingGpu = false, gpuVerified = false;
    int mixInput = 0, specInput = 1, specOutput = 0, timeOutput = 1;
    int length = 343980;     // samples per run
    int sources = 4;
    bool waveform = false;   // RoFormer-style (waveform in, waveform out)
};
#endif

struct DemucsSeparator::Impl
{
#if ORBIT_HAS_ORT
    Ort::Env env { ORT_LOGGING_LEVEL_WARNING, "Centrifuge" };
    std::map<juce::String, std::unique_ptr<Model>> models;   // by file name
    int cpuThreads = 1;
    bool useGpu = true;

    Model* get (const char* name)
    {
        auto it = models.find (name);
        return it != models.end() ? it->second.get() : nullptr;
    }
#endif
    StemQuality quality = StemQuality::fast;
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
    return ! impl->models.empty();
#else
    return false;
#endif
}

juce::String DemucsSeparator::getBackendDescription() const { return impl->backend; }
StemQuality DemucsSeparator::getQuality() const { return impl->quality; }

#if ORBIT_HAS_ORT
namespace
{
    std::unique_ptr<Ort::Session> openSession (Ort::Env& env, const juce::File& file, bool gpu, int threads)
    {
       #if JUCE_WINDOWS
        const auto path = file.getFullPathName().toWideCharPointer();
       #else
        const auto path = file.getFullPathName().toRawUTF8();
       #endif
        Ort::SessionOptions so;
        so.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
       #if ORBIT_HAS_DML
        if (gpu)
        {
            so.DisableMemPattern();
            so.SetExecutionMode (ExecutionMode::ORT_SEQUENTIAL);
            Ort::ThrowOnError (OrtSessionOptionsAppendExecutionProvider_DML (so, 0));
            return std::make_unique<Ort::Session> (env, path, so);
        }
       #else
        juce::ignoreUnused (gpu);
       #endif
        so.SetIntraOpNumThreads (threads);
        so.SetInterOpNumThreads (1);
        so.DisableCpuMemArena();   // give memory back between runs (several models may be loaded at once)
        return std::make_unique<Ort::Session> (env, path, so);
    }
}
#endif

bool DemucsSeparator::load (const Options& opt, juce::String& error)
{
#if ! ORBIT_HAS_ORT
    juce::ignoreUnused (opt);
    error = "This build has no stem separation engine";
    return false;
#else
    const int cores = (int) juce::jmax (1u, std::thread::hardware_concurrency());
    const int threads = opt.cpuThreads > 0 ? opt.cpuThreads : juce::jmax (1, cores - 2);
    if (threads != impl->cpuThreads || opt.useGpu != impl->useGpu)
        impl->models.clear();   // engine settings changed: reload everything
    impl->cpuThreads = threads;
    impl->useGpu = opt.useGpu;

    auto fileFor = [&] (const juce::String& name)
    {
        if (name == StemModels::fast) return opt.modelFile;
        return opt.findModel != nullptr ? opt.findModel (name) : opt.modelFile.getSiblingFile (name);
    };

    // The best level whose models are all on disk, up to the one asked for.
    auto quality = opt.quality;
    juce::String note;
   #if ORBIT_HAS_DML
    const bool gpuPossible = opt.useGpu;
   #else
    const bool gpuPossible = false;
   #endif
    // The RoFormer needs far more memory than the computer is likely to have when it runs on the CPU.
    if (quality == StemQuality::best && ! gpuPossible && ! opt.allowBestOnCpu)
    {
        quality = StemQuality::high;
        note = " (Best needs the graphics card)";
    }
    auto haveAll = [&] (StemQuality q)
    {
        for (auto& n : StemModels::required (q)) if (! fileFor (n).existsAsFile()) return false;
        return true;
    };
    while (quality != StemQuality::fast && ! haveAll (quality))
        quality = quality == StemQuality::best ? StemQuality::high : (quality == StemQuality::high ? StemQuality::better : StemQuality::fast);

    if (! haveAll (quality))
    {
        error = "Stem model not found: " + fileFor (StemModels::fast).getFullPathName();
        impl->models.clear();
        return false;
    }

    const auto needed = StemModels::required (quality);
    // Free models this level doesn't use (they hold GPU memory).
    for (auto it = impl->models.begin(); it != impl->models.end();)
        it = needed.contains (it->first) && it->second->file == fileFor (it->first) ? std::next (it) : impl->models.erase (it);

    juce::String gpuError;
    for (auto& name : needed)
    {
        if (impl->get (name.toRawUTF8()) != nullptr) continue;
        auto m = std::make_unique<Model>();
        m->file = fileFor (name);
        m->waveform = name == StemModels::roformer;

       #if ORBIT_HAS_DML
        if (opt.useGpu)
        {
            try { m->session = openSession (impl->env, m->file, true, threads); m->usingGpu = true; }
            catch (const std::exception& e) { gpuError = e.what(); m->session.reset(); }
        }
       #endif
        if (m->session == nullptr)
        {
            try { m->session = openSession (impl->env, m->file, false, threads); }
            catch (const std::exception& e)
            {
                error = "Couldn't load " + name + ": " + juce::String (e.what());
                if (quality == StemQuality::fast || quality == StemQuality::better) { impl->models.clear(); return false; }
                // An optional model that won't load: drop to a level that doesn't need it.
                juce::String ignored;
                auto o2 = opt;
                o2.quality = quality == StemQuality::best ? StemQuality::high : StemQuality::better;
                if (o2.quality == opt.quality) o2.quality = StemQuality::fast;
                return load (o2, ignored);
            }
        }

        // Work out which input/output is which from their shapes.
        Ort::AllocatorWithDefaultOptions alloc;
        for (size_t i = 0; i < m->session->GetInputCount(); ++i)
        {
            m->inNames.emplace_back (m->session->GetInputNameAllocated (i, alloc).get());
            auto shape = m->session->GetInputTypeInfo (i).GetTensorTypeAndShapeInfo().GetShape();
            if (shape.size() == 3)
            {
                m->mixInput = (int) i;
                if (shape[2] > 0) m->length = (int) shape[2];
                else if (m->waveform) m->length = 352800;
            }
            else m->specInput = (int) i;
        }
        for (size_t i = 0; i < m->session->GetOutputCount(); ++i)
        {
            m->outNames.emplace_back (m->session->GetOutputNameAllocated (i, alloc).get());
            auto shape = m->session->GetOutputTypeInfo (i).GetTensorTypeAndShapeInfo().GetShape();
            if (shape.size() == 5)
            {
                m->specOutput = (int) i;
                if (shape[1] > 0) m->sources = (int) shape[1];
            }
            else m->timeOutput = (int) i;
        }
        if (m->waveform && ! m->usingGpu && ! opt.allowBestOnCpu)
        {
            // The graphics card couldn't take the RoFormer: Best isn't practical here.
            auto o2 = opt;
            o2.quality = StemQuality::high;
            juce::String ignored;
            const bool ok = load (o2, ignored);
            impl->backend << " (Best: the graphics card couldn't load the RoFormer)";
            return ok;
        }
        const bool layoutOk = m->waveform ? (m->inNames.size() == 1 && m->outNames.size() >= 1)
                                          : (m->inNames.size() == 2 && m->outNames.size() == 2 && m->sources == 4);
        if (! layoutOk)
        {
            error = "Unexpected layout in " + name;
            if (quality == StemQuality::fast || quality == StemQuality::better) { impl->models.clear(); return false; }
            auto o2 = opt;
            o2.quality = StemQuality::better;
            juce::String ignored;
            return load (o2, ignored);
        }
        impl->models[name] = std::move (m);
    }

    impl->quality = quality;
    const bool gpu = impl->models.begin()->second->usingGpu;
    impl->backend = (gpu ? juce::String ("GPU (DirectML)") : "CPU (" + juce::String (threads) + " threads)")
                  + ", " + stemQualityName (quality)
                  + (note.isNotEmpty() ? note : quality != opt.quality ? " (" + stemQualityName (opt.quality) + " needs models that aren't installed)" : juce::String())
                  + (gpuError.isNotEmpty() && ! gpu ? " - GPU failed: " + gpuError : juce::String());
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
    if (impl->models.empty()) { error = "Stem model not loaded"; return false; }
    const auto quality = impl->quality;
    Model* fastModel = impl->get (StemModels::fast);
    Model* drumsModel = quality >= StemQuality::high ? impl->get (StemModels::ftDrums) : nullptr;
    Model* bassModel = quality >= StemQuality::high ? impl->get (StemModels::ftBass) : nullptr;
    Model* vocalsModel = quality == StemQuality::high ? impl->get (StemModels::ftVocals) : nullptr;
    Model* roformer = quality == StemQuality::best ? impl->get (StemModels::roformer) : nullptr;

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
        // Zero-latency cubic resampling, so the stems stay sample-aligned with the original
        // (a delay here would leave the stems slightly late, and muting any stem would sound broken).
        const double ratio = srcRate / modelRate;
        auto resample = [&] (const float* in, float* out)
        {
            auto at = [&] (int64_t i) { return in[juce::jlimit<int64_t> (0, N - 1, i)]; };
            for (int64_t o = 0; o < n44; ++o)
            {
                const double p = (double) o * ratio;
                const auto ip = (int64_t) std::floor (p);
                const float t = (float) (p - (double) ip);
                const float xm1 = at (ip - 1), x0 = at (ip), x1 = at (ip + 1), x2 = at (ip + 2);
                const float c1 = 0.5f * (x1 - xm1);
                const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
                const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                out[o] = ((c3 * t + c2) * t + c1) * t + x0;
            }
        };
        resample (audio.left.data(), rl.data());
        resample (audio.right.data(), rr.data());
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

    // --- 4. Running a model on one window --------------------------------------------
    Model* demucsModel = fastModel != nullptr ? fastModel : drumsModel;
    if (demucsModel == nullptr) { error = "Stem model not loaded"; return false; }
    const int seg = demucsModel->length;
    const int le = DemucsStft::numFrames (seg);
    const size_t specSize = (size_t) DemucsStft::bins * (size_t) le;
    std::vector<float> spec ((size_t) 4 * specSize);
    std::vector<float> ybuf ((size_t) seg);
    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);

    auto runSession = [&] (Model& m, std::vector<Ort::Value>& inputs, std::vector<Ort::Value>& outputs) -> bool
    {
        std::vector<const char*> in, outN;
        for (auto& n : m.inNames) in.push_back (n.c_str());
        for (auto& n : m.outNames) outN.push_back (n.c_str());
        try
        {
            outputs = m.session->Run (Ort::RunOptions { nullptr }, in.data(), inputs.data(), inputs.size(), outN.data(), outN.size());
            return true;
        }
        catch (const std::exception& e)
        {
            error = "Stem model failed: " + juce::String (e.what());
            return false;
        }
    };

    /** HTDemucs on a normalised window (2 x seg) -> 4 sources x 2 channels x seg (normalised). */
    auto runDemucs = [&] (Model& m, std::vector<float>& mix, std::array<std::vector<float>, 8>& result) -> bool
    {
        for (int c = 0; c < 2; ++c)
            impl->stft.forward (mix.data() + (size_t) c * (size_t) seg, seg,
                                spec.data() + (size_t) (2 * c) * specSize,
                                spec.data() + (size_t) (2 * c + 1) * specSize);

        const std::array<int64_t, 3> mixShape { 1, 2, seg };
        const std::array<int64_t, 4> specShape { 1, 4, DemucsStft::bins, le };
        auto makeInputs = [&]
        {
            std::vector<Ort::Value> inputs;
            Ort::Value mixTensor = Ort::Value::CreateTensor<float> (memInfo, mix.data(), mix.size(), mixShape.data(), mixShape.size());
            Ort::Value specTensor = Ort::Value::CreateTensor<float> (memInfo, spec.data(), spec.size(), specShape.data(), specShape.size());
            std::array<Ort::Value*, 2> byIndex {};
            byIndex[(size_t) m.mixInput] = &mixTensor;
            byIndex[(size_t) m.specInput] = &specTensor;
            inputs.push_back (std::move (*byIndex[0]));
            inputs.push_back (std::move (*byIndex[1]));
            return inputs;
        };

        auto inputs = makeInputs();
        std::vector<Ort::Value> outputs;
        if (! runSession (m, inputs, outputs)) return false;

        // The first time the GPU runs this model, check it against the CPU. If the graphics driver
        // gives different results, use the CPU for this model from now on.
        if (m.usingGpu && ! m.gpuVerified)
        {
            m.gpuVerified = true;
            try
            {
                auto cpu = openSession (impl->env, m.file, false, impl->cpuThreads);
                auto cin = makeInputs();
                std::vector<const char*> in, outN;
                for (auto& n : m.inNames) in.push_back (n.c_str());
                for (auto& n : m.outNames) outN.push_back (n.c_str());
                auto cout = cpu->Run (Ort::RunOptions { nullptr }, in.data(), cin.data(), cin.size(), outN.data(), outN.size());

                double maxRef = 0.0, maxErr = 0.0;
                for (size_t o = 0; o < 2; ++o)
                {
                    const auto count = cout[o].GetTensorTypeAndShapeInfo().GetElementCount();
                    const float* x = cout[o].GetTensorData<float>();
                    const float* y = outputs[o].GetTensorData<float>();
                    for (size_t i = 0; i < count; ++i)
                    {
                        maxRef = juce::jmax (maxRef, std::abs ((double) x[i]));
                        maxErr = juce::jmax (maxErr, StemAudio::isFiniteBits (y[i]) ? std::abs ((double) x[i] - (double) y[i]) : 1.0e9);
                    }
                }
                if (maxErr > 0.02 * juce::jmax (1.0, maxRef))
                {
                    m.session = std::move (cpu);
                    m.usingGpu = false;
                    impl->backend = "CPU (" + juce::String (impl->cpuThreads) + " threads), " + stemQualityName (quality)
                                  + " - the GPU gave wrong results";
                    outputs = std::move (cout);
                }
            }
            catch (const std::exception&) {}
        }

        const float* xSpec = outputs[(size_t) m.specOutput].GetTensorData<float>();
        const float* xTime = outputs[(size_t) m.timeOutput].GetTensorData<float>();
        for (int s = 0; s < 4; ++s)
            for (int c = 0; c < 2; ++c)
            {
                const float* re = xSpec + ((size_t) (s * 4 + 2 * c)) * specSize;
                const float* im = xSpec + ((size_t) (s * 4 + 2 * c + 1)) * specSize;
                impl->stft.inverse (re, im, seg, ybuf.data());
                const float* xt = xTime + ((size_t) (s * 2 + c)) * (size_t) seg;
                auto& r = result[(size_t) (s * 2 + c)];
                r.resize ((size_t) seg);
                for (int j = 0; j < seg; ++j) r[(size_t) j] = ybuf[(size_t) j] + xt[j];
            }
        return true;
    };

    /** RoFormer vocals on a raw (not normalised) window of its own length -> vocals, 2 x length. */
    std::vector<float> rawWindow, vocalsRaw;
    auto runRoformer = [&] (Model& m) -> bool
    {
        const std::array<int64_t, 3> shape { 1, 2, m.length };
        std::vector<Ort::Value> inputs;
        inputs.push_back (Ort::Value::CreateTensor<float> (memInfo, rawWindow.data(), rawWindow.size(), shape.data(), shape.size()));
        std::vector<Ort::Value> outputs;
        if (! runSession (m, inputs, outputs)) return false;
        // sources [1, S, 2, length]: source 0 = vocals
        const float* v = outputs[0].GetTensorData<float>();
        vocalsRaw.assign (v, v + (size_t) 2 * (size_t) m.length);
        for (auto& x : vocalsRaw) if (! StemAudio::isFiniteBits (x)) x = 0.0f;
        return true;
    };

    // --- 5. Segmenting / overlap-add (demucs.apply.apply_model, overlap 0.25) ----------
    const int stride = (int) ((1.0 - 0.25) * seg);
    const int half = seg / 2;
    std::vector<float> weight ((size_t) seg);
    for (int i = 0; i < seg; ++i)
        weight[(size_t) i] = (float) (i < half ? i + 1 : seg - i) / (float) juce::jmax (half, seg - half);

    // "Better" runs a second window half a stride later (an odd offset, so the model sees the audio at a
    // different alignment) and averages the two, like demucs' random shifts.
    const int shiftB = quality == StemQuality::better ? stride / 2 + 1237 : 0;

    std::vector<float> mix ((size_t) 2 * seg), inst ((size_t) 2 * seg);
    std::array<std::vector<float>, 8> res, tmp;

    // Sliding accumulators (44.1 kHz): 4 sources x 2 channels, plus summed weights.
    const size_t accLen = (size_t) seg * 3;
    std::array<std::vector<float>, 8> acc;
    for (auto& a : acc) a.assign (accLen, 0.0f);
    std::vector<float> wsum (accLen, 0.0f);
    int64_t base = 0;

    auto processWindow = [&] (int64_t offset) -> bool
    {
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

        if (quality == StemQuality::best && roformer != nullptr && drumsModel != nullptr)
        {
            // Vocals from the RoFormer (on real audio around the window), then drums and bass from what's left.
            const int R = roformer->length;
            const int64_t rStart = start - (R - seg) / 2;
            rawWindow.assign ((size_t) 2 * (size_t) R, 0.0f);
            for (int j = 0; j < R; ++j)
            {
                const int64_t s = rStart + j;
                if (s < 0 || s >= n44) continue;
                rawWindow[(size_t) j] = inL[s];
                rawWindow[(size_t) (R + j)] = inR[s];
            }
            if (! runRoformer (*roformer)) return false;
            const int off = (int) (start - rStart);
            for (auto& r : res) r.assign ((size_t) seg, 0.0f);
            for (int c = 0; c < 2; ++c)
                for (int j = 0; j < seg; ++j)
                {
                    const float v = (off + j >= 0 && off + j < R) ? vocalsRaw[(size_t) c * (size_t) R + (size_t) (off + j)] : 0.0f;
                    res[(size_t) (6 + c)][(size_t) j] = (v - fMean) / fStd;
                    inst[(size_t) c * (size_t) seg + (size_t) j] = mix[(size_t) c * (size_t) seg + (size_t) j] - v / fStd;
                }
            if (! runDemucs (*drumsModel, inst, tmp)) return false;
            res[0] = tmp[0]; res[1] = tmp[1];
            if (! runDemucs (bassModel != nullptr ? *bassModel : *drumsModel, inst, tmp)) return false;
            res[2] = tmp[2]; res[3] = tmp[3];
        }
        else if (quality == StemQuality::high && drumsModel != nullptr && bassModel != nullptr && vocalsModel != nullptr)
        {
            // Each fine-tuned model is a specialist for one stem ("other" is what's left over, as always).
            for (auto& r : res) r.assign ((size_t) seg, 0.0f);
            if (! runDemucs (*drumsModel, mix, tmp)) return false;
            res[0] = tmp[0]; res[1] = tmp[1];
            if (! runDemucs (*bassModel, mix, tmp)) return false;
            res[2] = tmp[2]; res[3] = tmp[3];
            if (! runDemucs (*vocalsModel, mix, tmp)) return false;
            res[6] = tmp[6]; res[7] = tmp[7];
        }
        else if (! runDemucs (*demucsModel, mix, res)) return false;

        const int trim = delta / 2;
        const auto accOffset = (size_t) (offset - base);
        for (size_t k = 0; k < 8; ++k)
        {
            auto& a = acc[k];
            const auto& r = res[k];
            for (int j = 0; j < chunkLen; ++j)
                a[accOffset + (size_t) j] += weight[(size_t) j] * r[(size_t) (trim + j)];
        }
        for (int j = 0; j < chunkLen; ++j)
            wsum[accOffset + (size_t) j] += weight[(size_t) j];
        return true;
    };

    const int64_t numSegments = (n44 + stride - 1) / stride;
    int64_t segIndex = 0;

    for (int64_t offset = 0; offset < n44; offset += stride, ++segIndex)
    {
        if (progress && ! progress ((double) segIndex / (double) numSegments))
        {
            error = "Cancelled";
            return false;
        }

        if (! processWindow (offset)) return false;
        if (shiftB > 0 && offset + shiftB < n44 && ! processWindow (offset + shiftB)) return false;

        // Everything before the next window's start won't change any more: finalise it.
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
