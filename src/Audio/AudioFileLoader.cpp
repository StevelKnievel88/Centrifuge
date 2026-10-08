#include "AudioFileLoader.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <mfapi.h>
 #include <mfidl.h>
 #include <mfreadwrite.h>
 #include <mferror.h>
#endif

namespace orbit
{

#if JUCE_WINDOWS
namespace
{
    template <typename T>
    struct ComRelease
    {
        T* p = nullptr;
        ~ComRelease() { if (p != nullptr) p->Release(); }
        T** operator&() { return &p; }
        T* operator->() const { return p; }
        explicit operator bool() const { return p != nullptr; }
    };

    /** Windows Media Foundation decode: covers AAC / M4A / ALAC and anything else Windows can play. */
    bool decodeWithMediaFoundation (const juce::File& file, TrackAudio& out, juce::String& error, const std::atomic<bool>* cancel)
    {
        const HRESULT coInit = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
        const bool needUninit = SUCCEEDED (coInit);

        bool ok = false;
        if (SUCCEEDED (MFStartup (MF_VERSION, MFSTARTUP_LITE)))
        {
            ComRelease<IMFSourceReader> reader;
            if (SUCCEEDED (MFCreateSourceReaderFromURL (file.getFullPathName().toWideCharPointer(), nullptr, &reader)))
            {
                reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE);
                reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

                ComRelease<IMFMediaType> wanted;
                MFCreateMediaType (&wanted);
                wanted->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                wanted->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_Float);
                reader->SetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, wanted.p);

                ComRelease<IMFMediaType> actual;
                if (SUCCEEDED (reader->GetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual)))
                {
                    const UINT32 channels = MFGetAttributeUINT32 (actual.p, MF_MT_AUDIO_NUM_CHANNELS, 2);
                    const UINT32 rate = MFGetAttributeUINT32 (actual.p, MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
                    out.sampleRate = rate;

                    PROPVARIANT var;
                    PropVariantInit (&var);
                    if (SUCCEEDED (reader->GetPresentationAttribute ((DWORD) MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var)) && var.vt == VT_UI8)
                    {
                        const auto frames = (size_t) ((double) var.uhVal.QuadPart * 1.0e-7 * rate) + 4096;
                        out.left.reserve (frames);
                        out.right.reserve (frames);
                    }
                    PropVariantClear (&var);

                    for (;;)
                    {
                        if (cancel != nullptr && cancel->load()) break;

                        DWORD flags = 0;
                        ComRelease<IMFSample> sample;
                        if (FAILED (reader->ReadSample ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample)))
                            break;
                        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
                        {
                            ok = true;
                            break;
                        }
                        if (! sample) continue;

                        ComRelease<IMFMediaBuffer> buffer;
                        if (FAILED (sample->ConvertToContiguousBuffer (&buffer))) continue;

                        BYTE* bytes = nullptr;
                        DWORD len = 0;
                        if (SUCCEEDED (buffer->Lock (&bytes, nullptr, &len)))
                        {
                            const auto* f = reinterpret_cast<const float*> (bytes);
                            const size_t frames = len / (sizeof (float) * juce::jmax (1u, channels));
                            for (size_t i = 0; i < frames; ++i)
                            {
                                const float l = f[i * channels];
                                out.left.push_back (l);
                                out.right.push_back (channels > 1 ? f[i * channels + 1] : l);
                            }
                            buffer->Unlock();
                        }
                    }
                }
            }
            MFShutdown();
        }

        if (needUninit) CoUninitialize();

        out.numFrames = (int64_t) out.left.size();
        if (! ok || out.numFrames == 0)
        {
            error = "Windows couldn't decode this file";
            return false;
        }
        return true;
    }
}
#endif

AudioFileLoader::AudioFileLoader()
{
    formats.registerBasicFormats();
}

TrackAudio::Ptr AudioFileLoader::load (const juce::File& file, juce::String& error, const std::atomic<bool>* cancel)
{
    if (! file.existsAsFile())
    {
        error = "File not found: " + file.getFullPathName();
        return {};
    }

    TrackAudio::Ptr audio = new TrackAudio();
    const auto ext = file.getFileExtension().toLowerCase();

   #if JUCE_WINDOWS
    const bool preferMf = ext == ".m4a" || ext == ".aac" || ext == ".mp4" || ext == ".alac" || ext == ".m4p";
    if (preferMf)
        return decodeWithMediaFoundation (file, *audio, error, cancel) ? audio : TrackAudio::Ptr();
   #endif

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
       #if JUCE_WINDOWS
        return decodeWithMediaFoundation (file, *audio, error, cancel) ? audio : TrackAudio::Ptr();
       #else
        error = "Unsupported file format: " + file.getFileName();
        return {};
       #endif
    }

    audio->sampleRate = reader->sampleRate;
    const auto total = reader->lengthInSamples;
    if (total <= 0 || total > (int64_t) 48000 * 60 * 120)
    {
        error = "Unexpected track length";
        return {};
    }

    audio->left.resize ((size_t) total);
    audio->right.resize ((size_t) total);

    constexpr int block = 1 << 16;
    juce::AudioBuffer<float> buf ((int) juce::jmax (2u, reader->numChannels), block);
    for (int64_t pos = 0; pos < total; pos += block)
    {
        if (cancel != nullptr && cancel->load())
        {
            error = "Cancelled";
            return {};
        }
        const int n = (int) std::min<int64_t> (block, total - pos);
        reader->read (&buf, 0, n, pos, true, true);
        std::memcpy (audio->left.data() + pos, buf.getReadPointer (0), sizeof (float) * (size_t) n);
        std::memcpy (audio->right.data() + pos, buf.getReadPointer (reader->numChannels > 1 ? 1 : 0), sizeof (float) * (size_t) n);
    }

    audio->numFrames = total;
    return audio;
}

} // namespace orbit
