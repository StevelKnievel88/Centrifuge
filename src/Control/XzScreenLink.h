#pragma once
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

namespace orbit
{

/** Laptop mode: sends Centrifuge's deck view to the XDJ-XZ's screen over a network cable.

    The Centrifuge loader on the deck runs a receiver: it answers "VJDISCOVER" on UDP 50006 with
    "VJXZ 1 <ip> 50005", takes 800 x 480 RGB565 pictures (or any band of one) on TCP 50005 and shows them over
    the stock screen, and sends touches on the screen back on the same connection.

    Frames are handed over from the message thread; a background thread finds the deck, connects, and sends
    only the rows that changed (with a full picture every couple of seconds). If the deck goes away it starts
    looking again. */
class XzScreenLink : private juce::Thread
{
public:
    static constexpr int width = 800, height = 480;

    XzScreenLink();
    ~XzScreenLink() override;

    void setEnabled (bool on);
    bool isEnabled() const { return enabled.load(); }
    bool isConnected() const { return connected.load(); }
    /** "OFF", "LOOKING FOR THE XDJ-XZ", "CONNECTED 169.254.x.x", ... */
    juce::String getStatus() const;

    /** The latest picture (any format; scaled to 800 x 480 if it isn't). Only the newest one is ever sent. */
    void submitFrame (const juce::Image& frame);

    /** A touch on the XZ screen: down (true) / up (false), in screen pixels. Called on the message thread. */
    std::function<void (bool down, int x, int y)> onTouch;

private:
    void run() override;
    bool discover (juce::String& ip);
    void serve (const juce::String& ip);
    void setStatus (const juce::String& s);

    std::atomic<bool> enabled { true }, connected { false };
    mutable std::mutex statusLock;
    juce::String status { "LOOKING FOR THE XDJ-XZ" };

    std::mutex frameLock;
    std::vector<juce::uint16> pending;       // RGB565, newest frame not sent yet
    bool havePending = false;
    juce::WaitableEvent frameArrived;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XzScreenLink)
};

} // namespace orbit
