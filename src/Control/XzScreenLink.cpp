#include "XzScreenLink.h"
#include <juce_events/juce_events.h>
#include <cstring>

namespace orbit
{

namespace
{
#pragma pack(push, 1)
struct FrameHeader            // "VJFS" (the receiver's own layout, little-endian on both ends)
{
    char magic[4];
    juce::uint8 version;      // 1 = RGB565
    juce::uint8 deck;
    juce::uint16 flags;       // 1 visible, 4 exclusive (covers the whole stock screen)
    juce::uint32 sequence;
    juce::uint16 w, h, x, y;
    juce::uint32 payloadLength;
    float playhead;
};
struct TouchEvent             // "VJTE"
{
    char magic[4];
    juce::uint8 version, type;    // type 1 down, 2 up
    juce::uint16 flags;
    juce::uint32 sequence;
    juce::uint16 x, y;
    juce::int32 value;
};
#pragma pack(pop)
static_assert (sizeof (FrameHeader) == 28, "VJFS header");
static_assert (sizeof (TouchEvent) == 20, "VJTE event");

constexpr int framePort = 50005, discoveryPort = 50006;
constexpr juce::uint32 fullRefreshMs = 2000;   // also keeps the overlay up while nothing moves

bool writeAll (juce::StreamingSocket& s, const void* data, int bytes)
{
    auto* p = static_cast<const char*> (data);
    while (bytes > 0)
    {
        const int n = s.write (p, bytes);
        if (n <= 0) return false;
        p += n;
        bytes -= n;
    }
    return true;
}
} // namespace

XzScreenLink::XzScreenLink() : juce::Thread ("XZ screen")
{
    startThread();
}

XzScreenLink::~XzScreenLink()
{
    signalThreadShouldExit();
    frameArrived.signal();
    stopThread (4000);
}

void XzScreenLink::setEnabled (bool on)
{
    enabled = on;
    frameArrived.signal();
}

juce::String XzScreenLink::getStatus() const
{
    if (! enabled.load()) return "OFF";
    std::lock_guard<std::mutex> l (statusLock);
    return status;
}

void XzScreenLink::setStatus (const juce::String& s)
{
    std::lock_guard<std::mutex> l (statusLock);
    status = s;
}

void XzScreenLink::submitFrame (const juce::Image& frameIn)
{
    if (! enabled.load()) return;
    auto frame = frameIn;
    if (frame.getWidth() != width || frame.getHeight() != height)
        frame = frame.rescaled (width, height, juce::Graphics::mediumResamplingQuality);

    std::vector<juce::uint16> px ((size_t) (width * height));
    {
        const juce::Image::BitmapData bd (frame, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const auto c = bd.getPixelColour (x, y);
                px[(size_t) (y * width + x)] = (juce::uint16) (((c.getRed() >> 3) << 11) | ((c.getGreen() >> 2) << 5) | (c.getBlue() >> 3));
            }
    }
    {
        std::lock_guard<std::mutex> l (frameLock);
        pending = std::move (px);
        havePending = true;
    }
    frameArrived.signal();
}

void XzScreenLink::run()
{
    while (! threadShouldExit())
    {
        if (! enabled.load())
        {
            connected = false;
            frameArrived.wait (500);
            continue;
        }
        juce::String ip;
        setStatus ("LOOKING FOR THE XDJ-XZ");
        if (! discover (ip)) { wait (1000); continue; }
        serve (ip);
        connected = false;
    }
}

bool XzScreenLink::discover (juce::String& ip)
{
    juce::DatagramSocket sock (true);
    if (! sock.bindToPort (0)) return false;
    const char request[] = "VJDISCOVER";
    for (auto* target : { "255.255.255.255", "169.254.255.255" })
        sock.write (target, discoveryPort, request, (int) std::strlen (request));

    const auto until = juce::Time::getMillisecondCounter() + 800;
    while (juce::Time::getMillisecondCounter() < until && ! threadShouldExit())
    {
        if (sock.waitUntilReady (true, 100) != 1) continue;
        char reply[128] = {};
        juce::String sender;
        int senderPort = 0;
        const int n = sock.read (reply, (int) sizeof (reply) - 1, false, sender, senderPort);
        if (n <= 0) continue;
        const auto words = juce::StringArray::fromTokens (juce::String (reply, (size_t) n), " ", "");
        if (words.size() >= 3 && words[0] == "VJXZ")
        {
            ip = words[2] != "0.0.0.0" ? words[2] : sender;
            return ip.isNotEmpty();
        }
    }
    return false;
}

void XzScreenLink::serve (const juce::String& ip)
{
    juce::StreamingSocket s;
    if (! s.connect (ip, framePort, 2000)) return;
    connected = true;
    setStatus ("CONNECTED " + ip);

    std::vector<juce::uint16> shown, next;
    juce::uint32 sequence = 1, lastFull = 0;
    bool touchDown = false;

    while (! threadShouldExit() && enabled.load())
    {
        // Touches coming back
        while (s.waitUntilReady (true, 0) == 1)
        {
            TouchEvent ev {};
            int got = 0;
            while (got < (int) sizeof (ev))
            {
                const int n = s.read (reinterpret_cast<char*> (&ev) + got, (int) sizeof (ev) - got, true);
                if (n <= 0) return;
                got += n;
            }
            if (std::memcmp (ev.magic, "VJTE", 4) == 0 && (ev.type == 1 || ev.type == 2))
            {
                const bool down = ev.type == 1;
                if (down == touchDown) continue;
                touchDown = down;
                const int x = ev.x, y = ev.y;
                juce::MessageManager::callAsync ([this, down, x, y] { if (onTouch) onTouch (down, x, y); });
            }
        }

        frameArrived.wait (100);
        {
            std::lock_guard<std::mutex> l (frameLock);
            if (! havePending) continue;
            next.swap (pending);
            havePending = false;
        }

        // Only the rows that changed, unless it's time for a whole picture
        int y0 = 0, y1 = height - 1;
        const auto now = juce::Time::getMillisecondCounter();
        if (shown.size() == next.size() && now - lastFull < fullRefreshMs)
        {
            auto rowSame = [&] (int y) { return std::memcmp (&shown[(size_t) (y * width)], &next[(size_t) (y * width)], (size_t) width * 2) == 0; };
            while (y0 < height && rowSame (y0)) ++y0;
            if (y0 == height) { shown.swap (next); continue; }
            while (y1 > y0 && rowSame (y1)) --y1;
        }
        else
            lastFull = now;

        FrameHeader h {};
        std::memcpy (h.magic, "VJFS", 4);
        h.version = 1;
        h.flags = 1 | 4;
        h.sequence = sequence++;
        h.w = (juce::uint16) width;
        h.h = (juce::uint16) (y1 - y0 + 1);
        h.x = 0;
        h.y = (juce::uint16) y0;
        h.payloadLength = (juce::uint32) (width * h.h * 2);
        if (! writeAll (s, &h, (int) sizeof (h)) || ! writeAll (s, &next[(size_t) (y0 * width)], (int) h.payloadLength))
            return;
        shown.swap (next);
    }
}

} // namespace orbit
