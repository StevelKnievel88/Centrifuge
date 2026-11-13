#include "Deck.h"
#include "signalsmith-stretch/signalsmith-stretch.h"
#include <cmath>

namespace orbit
{

struct Deck::Stretcher
{
    signalsmith::stretch::SignalsmithStretch<float> s;
};

namespace
{
    constexpr int fadeLen = 96;

    inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
    {
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    inline double wrapHalf (double x) noexcept
    {
        x -= std::floor (x + 0.5);
        return x;
    }
}

Deck::Deck (int i) : index (i), stretch (std::make_unique<Stretcher>())
{
    fadeTailL.resize (fadeLen);
    fadeTailR.resize (fadeLen);
}

Deck::~Deck() = default;

//==============================================================================
namespace
{
    /** Objects handed to the audio thread by pointer. The command holds a reference of its own until the deck has
        taken it, so nothing can free the object while the command waits in the queue (several quick edits used
        to let the release pool free a track the deck was about to switch to). */
    juce::ReferenceCountedObject* objectIn (const Deck::Command& c)
    {
        if (c.ptr == nullptr) return nullptr;
        switch (c.cmd)
        {
            case Deck::Cmd::load:
            case Deck::Cmd::swapTrackData: return static_cast<LoadedTrack*> (c.ptr);
            case Deck::Cmd::attachStems:   return static_cast<StemAudio*> (c.ptr);
            default:                       return nullptr;
        }
    }
}

void Deck::post (const Command& c)
{
    auto* obj = objectIn (c);
    if (obj != nullptr) obj->incReferenceCount();
    const juce::ScopedLock sl (producerLock);
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0)       queue[(size_t) s1] = c;
    else if (n2 > 0)  queue[(size_t) s2] = c;
    else
    {
        // queue full: drop (only happens if audio has stopped)
        if (obj != nullptr) obj->decReferenceCount();
        return;
    }
    fifo.finishedWrite (n1 + n2);
}

LoadedTrack::Ptr Deck::getLoadedTrack() const
{
    const juce::ScopedLock sl (uiTrackLock);
    return uiTrack;
}

void Deck::setLoadedTrackForUi (LoadedTrack::Ptr t)
{
    const juce::ScopedLock sl (uiTrackLock);
    uiTrack = std::move (t);
    uiStems = nullptr;
}

//==============================================================================
void Deck::prepare (double sr, int block)
{
    deviceRate = sr;
    maxBlock = juce::jmax (64, block);
    const size_t tmp = (size_t) juce::jmax (maxBlock * 4 + 64, 1 << 16);
    tmpL.assign (tmp, 0.0f);
    tmpR.assign (tmp, 0.0f);
    stretch->s.presetCheaper (2, (float) sr);
    stretch->s.setTransposeSemitones (0.0f);
    lastTranspose = 0.0;
    stretchActive = false;
}

void Deck::handleCommands()
{
    const int ready = fifo.getNumReady();
    if (ready <= 0) return;
    int s1, n1, s2, n2;
    fifo.prepareToRead (ready, s1, n1, s2, n2);
    auto run = [this] (const Command& c)
    {
        apply (c);
        // the deck holds its own reference now (or didn't take it, and the release pool still has one),
        // so this never frees anything on the audio thread
        if (auto* obj = objectIn (c)) obj->decReferenceCountWithoutDeleting();
    };
    for (int i = 0; i < n1; ++i) run (queue[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) run (queue[(size_t) (s2 + i)]);
    fifo.finishedRead (n1 + n2);
}

//==============================================================================
double Deck::quantizeTime (double sec) const noexcept
{
    if (! quantize || track == nullptr || ! track->grid.isValid()) return sec;
    return track->grid.snap (sec);
}

double Deck::heardPositionSec() const noexcept
{
    double p = pos;
    if (stretchActive)
    {
        const double speed = tempoRatio;
        const double latencyIn = stretch->s.inputLatency() + speed * stretch->s.outputLatency();
        p -= latencyIn * (srcRate / deviceRate);
        if (loopActive && loopOut > loopIn && p < loopIn * srcRate)
            p += (loopOut - loopIn) * srcRate;
    }
    return juce::jmax (0.0, p / srcRate);
}

double Deck::baseBpmAtPosition() const noexcept
{
    if (track == nullptr) return 0.0;
    if (track->grid.isValid()) return track->grid.bpmAt (heardPositionSec());
    return track->info.bpm;
}

double Deck::currentBpm() const noexcept
{
    return baseBpmAtPosition() * tempoRatio;
}

double Deck::beatPositionNow() const noexcept
{
    if (track == nullptr || ! track->grid.isValid()) return 0.0;
    return track->grid.beatPosition (heardPositionSec());
}

void Deck::setSyncTarget (double ratio, double phaseErrorBeats, bool snapPhase)
{
    syncRatio = ratio;
    if (snapPhase && (playing || cuePreview) && track != nullptr && track->grid.isValid())
    {
        const double now = heardPositionSec();
        const double target = track->grid.timeAtBeat (track->grid.beatPosition (now) + phaseErrorBeats);
        jumpTo (target, false);
        phaseNudge = 0.0;
    }
    else
    {
        phaseNudge = juce::jlimit (-0.004, 0.004, phaseErrorBeats * 0.08);
    }
}

//==============================================================================
void Deck::jumpTo (double sec, bool quantized)
{
    if (track == nullptr || track->audio == nullptr) return;
    if (quantized) sec = quantizeTime (sec);

    // Render a short tail from the old position so the cut can be cross-faded.
    if ((playing || scratching || audibleLast) && fadeTailLen == 0)
    {
        const double savedPos = pos;
        readResampled (fadeTailL.data(), fadeTailR.data(), fadeLen, audibleLast ? lastStep : (srcRate / deviceRate) * juce::jmax (0.25, tempoRatio));
        pos = savedPos;
        fadeTailLen = fadeLen;
    }

    pos = juce::jlimit (0.0, (double) track->audio->numFrames - 1.0, sec * srcRate);
    xfRemain = 0;
    stretchActive = false;
}

void Deck::setHotCueAt (int idx, double sec)
{
    if (idx < 0 || idx >= 8) return;
    CuePoint c;
    c.type = CuePoint::Type::hot;
    c.hotIndex = idx;
    c.startSec = sec;
    c.colour = juce::Colour (0xff28e214);
    hot[(size_t) idx] = c;
    hotSet[(size_t) idx] = true;
}

void Deck::loadTrack (LoadedTrack* t)
{
    track = t;
    playSeg = -1;
    stems = nullptr;
    playing = cuePreview = scratching = false;
    coasting = braking = slipping = startRamp = reversing = scrubbing = false;
    searchDir = 0;
    prevLoopActive = false;
    motor = 1.0;
    releaseT = 1.0;
    hotPreview = -1;
    loopActive = false;
    loopIn = loopOut = -1.0;
    endAutoLoop();
    stretchActive = false;
    fadeTailLen = 0;
    audibleLast = false;
    bendRate = 0.0;
    keyShift = 0;
    stemMuted.fill (false);

    if (track == nullptr || track->audio == nullptr)
    {
        pos = 0.0;
        return;
    }

    srcRate = track->audio->sampleRate;
    hot = track->hotCues;
    hotSet = track->hotCueSet;

    numLocs = 0;
    for (auto& m : track->memoryCues)
        if (numLocs < maxLocators)
            locs[(size_t) numLocs++] = { m.startSec, m.isLoop() ? m.endSec : -1.0, (m.colour.isTransparent() ? juce::Colour (m.isLoop() ? loopColour : 0xffe62828) : m.colour).getARGB(),
                                                m.isLoop() ? m.loopMode : 0, m.cycles };
    std::sort (locs.begin(), locs.begin() + numLocs, [] (const Loc& a, const Loc& b) { return a.start < b.start; });

    numSkipsLocal = 0;
    for (auto& k : track->skips)
        if (numSkipsLocal < maxSkips && k.second > k.first)
            skips[(size_t) numSkipsLocal++] = { k.first, k.second };
    sortSkips();
    skipPending = -1.0;
    xfRemain = 0;

    cuePoint = 0.0;
    if (numLocs > 0)
        cuePoint = locs[0].start;
    pos = cuePoint * srcRate;
}

bool Deck::loopAtPlayhead (double now) const noexcept
{
    if (loopIn < 0.0 || loopOut <= loopIn) return false;
    return loopActive || (! playing && std::abs (now - loopIn) < 0.03);
}

void Deck::addLocator (double sec, juce::Colour colour, double endSec, int mode, int cycles)
{
    if (nearestLocator (sec, 0.01) >= 0 || numLocs >= maxLocators) return;
    locs[(size_t) numLocs++] = { sec, endSec, colour.getARGB(), endSec > sec ? mode : 0, cycles };
    std::sort (locs.begin(), locs.begin() + numLocs, [] (const Loc& a, const Loc& b) { return a.start < b.start; });
    cueEdited();
}

int Deck::nearestLocator (double sec, double within) const noexcept
{
    int best = -1;
    double bestD = within;
    for (int i = 0; i < numLocs; ++i)
    {
        const double d = std::abs (locs[(size_t) i].start - sec);
        if (d <= bestD) { bestD = d; best = i; }
    }
    return best;
}

void Deck::sortSkips()
{
    std::sort (skips.begin(), skips.begin() + numSkipsLocal, [] (const Skip& a, const Skip& b) { return a.from < b.from; });
    // Merge overlaps
    int w = 0;
    for (int i = 0; i < numSkipsLocal; ++i)
    {
        if (w > 0 && skips[(size_t) i].from <= skips[(size_t) w - 1].to)
            skips[(size_t) w - 1].to = juce::jmax (skips[(size_t) w - 1].to, skips[(size_t) i].to);
        else
            skips[(size_t) w++] = skips[(size_t) i];
    }
    numSkipsLocal = w;
}

void Deck::addSkip (double from, double to)
{
    if (to < from) std::swap (from, to);
    if (track == nullptr || track->audio == nullptr || to - from < 0.05) return;
    to = juce::jmin (to, track->audio->lengthSeconds());
    if (numSkipsLocal >= maxSkips) return;
    skips[(size_t) numSkipsLocal++] = { from, to };
    sortSkips();
    cueEdited();
}

void Deck::startStretch()
{
    auto& s = stretch->s;
    s.reset();
    const double speed = juce::jmax (0.05, tempoRatio);
    const int len = juce::jmin ((int) tmpL.size(), s.outputSeekLength ((float) speed));
    readResampled (tmpL.data(), tmpR.data(), len, srcRate / deviceRate);
    float* ins[2] = { tmpL.data(), tmpR.data() };
    s.outputSeek (ins, len);
    stretchCarry = 0.0;
    stretchActive = true;
}

void Deck::beginPlay() noexcept
{
    if (braking) { braking = false; startRamp = startOn && startSec > 0.0; return; }   // back up to speed from where it is
    if (! playing) { motor = startOn && startSec > 0.0 ? 0.0 : 1.0; startRamp = motor < 1.0; }
    playing = true;
    releaseT = 1.0;
}

void Deck::beginStop() noexcept
{
    if (playing && brakeOn && brakeSec > 0.0 && ! cuePreview && hotPreview < 0 && ! scratching) { braking = true; startRamp = false; return; }
    playing = false;
    braking = false;
}

void Deck::slipBegin (int source) noexcept
{
    if (! slip || slipping || track == nullptr) return;
    slipping = true;
    slipSource = source;
    slipSec = heardPositionSec();
    slipWasPlaying = playing;
}

void Deck::slipEnd() noexcept
{
    if (! slipping) return;
    slipping = false;
    if (slipWasPlaying && track != nullptr && track->audio != nullptr)
    {
        jumpTo (juce::jmin (slipSec, track->audio->lengthSeconds()), false);
        playing = true;
        braking = false;
        motor = 1.0;
    }
}

void Deck::endScratch() noexcept
{
    const double v = scratchVelocity;
    scratching = false;
    coasting = false;
    if (slipping && slipSource == 1) { slipEnd(); return; }
    if (playing && releaseSec > 0.0) { releaseFrom = v; releaseT = 0.0; }
}

//==============================================================================
void Deck::apply (const Command& c)
{
    const double now = (track != nullptr) ? heardPositionSec() : 0.0;
    const bool haveGrid = track != nullptr && track->grid.isValid();

    switch (c.cmd)
    {
        case Cmd::load:
            loadTrack (static_cast<LoadedTrack*> (c.ptr));
            break;

        case Cmd::swapTrackData:
        {
            auto* t = static_cast<LoadedTrack*> (c.ptr);
            if (t == nullptr || track == nullptr || t->audio != track->audio) break;
            track = t;
            if (playSeg >= (int) t->playMap.size()) playSeg = -1;   // the play map may have changed
            hot = t->hotCues;
            hotSet = t->hotCueSet;
            numLocs = 0;
            for (auto& m : t->memoryCues)
                if (numLocs < maxLocators)
                    locs[(size_t) numLocs++] = { m.startSec, m.isLoop() ? m.endSec : -1.0, (m.colour.isTransparent() ? juce::Colour (m.isLoop() ? loopColour : 0xffe62828) : m.colour).getARGB(),
                                                m.isLoop() ? m.loopMode : 0, m.cycles };
            std::sort (locs.begin(), locs.begin() + numLocs, [] (const Loc& a, const Loc& b) { return a.start < b.start; });
            numSkipsLocal = 0;
            for (auto& k : t->skips)
                if (numSkipsLocal < maxSkips && k.second > k.first)
                    skips[(size_t) numSkipsLocal++] = { k.first, k.second };
            sortSkips();
            ++dataVersion;
            break;
        }

        case Cmd::attachStems:
            if (track != nullptr && track->serial == c.serial)
                stems = static_cast<StemAudio*> (c.ptr);
            break;

        case Cmd::shift:
            shiftHeld = c.down;
            break;

        case Cmd::play:
            if (c.down && track != nullptr)
            {
                if (slipping && slipSource == 5) { slipEnd(); break; }   // slip: carries on from where it would have got to
                if (! playing) syncSnapRequested = syncOn;
                beginPlay(); cuePreview = false; hotPreview = -1;
            }
            break;
        case Cmd::pause:     if (c.down) { if (playing && ! braking) slipBegin (5); beginStop(); } break;

        case Cmd::playToggle:
            if (! c.down || track == nullptr) break;
            if (playing && ! braking && ! cuePreview && hotPreview < 0)
            {
                slipBegin (5);   // with slip on, the track carries on underneath the pause
                beginStop();
            }
            else if (slipping && slipSource == 5)
                slipEnd();
            else
            {
                if (! playing) syncSnapRequested = syncOn;
                beginPlay();
                cuePreview = false;
                hotPreview = -1;
            }
            break;

        case Cmd::scrubHold:
            if (track == nullptr) break;
            if (c.down && ! scratching)
            {
                if (playing && ! braking) slipBegin (1);
                scratching = true;
                scrubbing = false;
                scratchVelocity = 0.0;
                jogTicksPending = 0.0;
                sinceTickSec = 0.0;
            }
            else if (! c.down && scratching && ! jogTouched) { scrubbing = false; endScratch(); }
            break;

        case Cmd::brakeBeats:
            brakeBeats = juce::jlimit (0.0, 64.0, c.value);
            break;

        case Cmd::search:
            searchDir = c.value > 0.5 ? 1 : c.value < -0.5 ? -1 : 0;
            break;

        case Cmd::scrub:
            if (track == nullptr || c.value == 0.0) break;
            if (! scratching)
            {
                if (playing && ! braking) slipBegin (1);
                scratching = scrubbing = true;
                scratchVelocity = 0.0;
                jogTicksPending = 0.0;
            }
            sinceTickSec = 0.0;
            jogTicksPending += c.value * ticksPerRevolution / 40.0;   // one click = 1/40 of a turn
            break;

        case Cmd::reverse:
            if (track == nullptr) break;
            if (c.down && ! reversing) { reversing = true; if (playing) slipBegin (6); }
            else if (! c.down && reversing) { reversing = false; if (slipping && slipSource == 6) slipEnd(); }
            break;

        case Cmd::vinylTime:
            if (c.index == 0) brakeSec = juce::jlimit (0.0, 10.0, c.value);
            else if (c.index == 1) startSec = juce::jlimit (0.0, 10.0, c.value);
            else releaseSec = juce::jlimit (0.0, 10.0, c.value);
            break;
        case Cmd::vinylBrakeSet:
            if (c.index == 0) brakeOn = c.value > 0.5; else startOn = c.value > 0.5;
            break;
        case Cmd::throwSet:
            throwThreshold = juce::jmax (0.0, c.value);
            break;
        case Cmd::slipToggle:
        case Cmd::slipSet:
            if (! c.down) break;
            slip = c.cmd == Cmd::slipToggle ? ! slip : c.value > 0.5;
            if (! slip) slipping = false;
            break;
        case Cmd::gainSet:
            gainTarget = (float) juce::jlimit (0.0, 8.0, c.value);
            break;

        case Cmd::cue:
            if (track == nullptr) break;
            if (c.down)
            {
                if (playing && ! cuePreview && hotPreview < 0)
                {
                    playing = false;
                    jumpTo (cuePoint, false);
                }
                else if (! playing)
                {
                    if (std::abs (now - cuePoint) > 0.01)
                    {
                        cuePoint = quantizeTime (now);
                        jumpTo (cuePoint, false);
                    }
                    cuePreview = true;
                    playing = true;
                }
            }
            else if (cuePreview)
            {
                cuePreview = false;
                playing = false;
                jumpTo (cuePoint, false);
            }
            break;

        case Cmd::hotCue:
        {
            const int i = c.index;
            if (track == nullptr || i < 0 || i >= 8) break;
            if (c.down)
            {
                if (shiftHeld)
                {
                    hotSet[(size_t) i] = false;
                    cueEdited();
                    break;
                }
                if (! hotSet[(size_t) i])
                {
                    if (loopAtPlayhead (now))
                    {
                        // A loop is set here: the pad saves it as a hot loop (orange).
                        setHotCueAt (i, loopIn);
                        hot[(size_t) i].endSec = loopOut;
                        hot[(size_t) i].colour = juce::Colour (loopColour);
                    }
                    else
                        setHotCueAt (i, quantizeTime (now));
                    cueEdited();
                    break;
                }
                const auto& hc = hot[(size_t) i];
                if (hc.isLoop())
                {
                    endAutoLoop();
                    if (playing) slipBegin (2);
                    loopIn = hc.startSec;
                    loopOut = hc.endSec;
                    loopActive = true;
                    jumpTo (hc.startSec, false);
                    playing = true;
                    break;
                }
                if (playing && hotPreview < 0 && ! cuePreview)
                {
                    double target = hc.startSec;
                    if (quantize && haveGrid)
                    {
                        // Keep the beat phase so the jump lands in time.
                        const double frac = wrapHalf (track->grid.beatPosition (now));
                        target = track->grid.timeAtBeat (track->grid.beatPosition (hc.startSec) + frac);
                    }
                    slipBegin (3);
                    jumpTo (target, false);
                }
                else
                {
                    // stopped: play on from the hot cue (like pressing play there, not a held cue)
                    hotPreview = -1;
                    cuePreview = false;
                    jumpTo (hc.startSec, false);
                    if (! playing) syncSnapRequested = syncOn;
                    beginPlay();
                    motor = 1.0;
                    startRamp = false;
                }
            }
            else if (hotPreview == i)
            {
                hotPreview = -1;
                playing = false;
                jumpTo (hotPreviewReturn, false);
            }
            else if (slipping && slipSource == 3)
                slipEnd();
            break;
        }

        case Cmd::hotCueDelete:
            if (c.down && c.index >= 0 && c.index < 8 && hotSet[(size_t) c.index]) { hotSet[(size_t) c.index] = false; cueEdited(); }
            break;

        case Cmd::hotCueColour:
            if (c.index >= 0 && c.index < 8 && hotSet[(size_t) c.index])
            {
                hot[(size_t) c.index].colour = juce::Colour ((juce::uint32) c.value);
                cueEdited();
            }
            break;

        case Cmd::memoryCuePrev:
        case Cmd::memoryCueNext:
        {
            // Moves between markers (locators and hot cues) while the track is stopped, like CALL on the CDJs
            if (! c.down || track == nullptr || playing) break;
            const bool next = c.cmd == Cmd::memoryCueNext;
            double bestT = -1.0, bestEnd = -1.0;
            auto consider = [&] (double t, double e)
            {
                if (t < 0.0) return;
                if (next ? (t > now + 0.05 && (bestT < 0.0 || t < bestT)) : (t < now - 0.05 && (bestT < 0.0 || t > bestT)))
                { bestT = t; bestEnd = e; }
            };
            for (int k = 0; k < numLocs; ++k) consider (locs[(size_t) k].start, locs[(size_t) k].end);
            for (int k = 0; k < 8; ++k) if (hotSet[(size_t) k]) consider (hot[(size_t) k].startSec, hot[(size_t) k].endSec);
            if (bestT >= 0.0)
            {
                cuePoint = bestT;
                jumpTo (cuePoint, false);
                if (bestEnd > bestT) { loopIn = bestT; loopOut = bestEnd; loopActive = false; }
            }
            break;
        }

        case Cmd::locatorAdd:
            if (c.down && track != nullptr)
            {
                // From the UI a time is given; from a button, store the cue point when stopped (like rekordbox's
                // MEMORY button) or the playing position.
                if (c.value < 0.0 && loopAtPlayhead (now))
                {
                    addLocator (loopIn, juce::Colour (loopColour), loopOut);   // a loop locator
                    break;
                }
                const double at = c.value >= 0.0 ? quantizeTime (c.value) : (playing ? quantizeTime (now) : cuePoint);
                addLocator (at, juce::Colour (0xffe62828));
            }
            break;

        case Cmd::locatorDelete:
            if (c.index >= 0 && c.index < numLocs)
            {
                if (autoLoc >= 0) { endAutoLoop(); loopActive = false; }
                for (int k = c.index; k < numLocs - 1; ++k) locs[(size_t) k] = locs[(size_t) k + 1];
                --numLocs;
                cueEdited();
            }
            break;

        case Cmd::locatorDeleteNearest:
            if (c.down && track != nullptr)
            {
                int k = nearestLocator (now, 0.1);
                if (k < 0) k = nearestLocator (cuePoint, 0.1);
                if (k >= 0)
                {
                    for (int j = k; j < numLocs - 1; ++j) locs[(size_t) j] = locs[(size_t) j + 1];
                    --numLocs;
                    cueEdited();
                }
            }
            break;

        case Cmd::locatorLoopAdd:
            if (track != nullptr && c.value2 > c.value)
                addLocator (c.value, juce::Colour (loopColour), c.value2);
            break;

        case Cmd::locatorLoopMode:
            if (c.index >= 0 && c.index < numLocs && locs[(size_t) c.index].end > locs[(size_t) c.index].start)
            {
                auto& l = locs[(size_t) c.index];
                l.mode = juce::jlimit (0, 2, (int) c.value);
                l.cycles = juce::jlimit (1, 64, (int) c.value2);
                if (autoLoc == c.index && l.mode == 0) { endAutoLoop(); loopActive = false; }
                cueEdited();
            }
            break;

        case Cmd::locatorColour:
            if (c.index >= 0 && c.index < numLocs) { locs[(size_t) c.index].colour = (juce::uint32) c.value; cueEdited(); }
            break;

        case Cmd::skipIn:
            if (c.down && track != nullptr) skipPending = quantizeTime (now);
            break;

        case Cmd::skipOut:
            if (c.down && track != nullptr && skipPending >= 0.0)
            {
                addSkip (skipPending, quantizeTime (now));
                skipPending = -1.0;
            }
            break;

        case Cmd::skipSet:
            if (c.down && track != nullptr)
            {
                if (skipPending < 0.0) skipPending = quantizeTime (now);
                else { addSkip (skipPending, quantizeTime (now)); skipPending = -1.0; }
            }
            break;

        case Cmd::skipAdd:
            addSkip (quantizeTime (c.value), quantizeTime (c.value2));
            break;

        case Cmd::skipDelete:
            if (c.index >= 0 && c.index < numSkipsLocal)
            {
                for (int k = c.index; k < numSkipsLocal - 1; ++k) skips[(size_t) k] = skips[(size_t) k + 1];
                --numSkipsLocal;
                cueEdited();
            }
            break;

        case Cmd::skipDeleteNearest:
            if (c.down && track != nullptr)
            {
                // The skip we're in or just passed, else the next one.
                int best = -1;
                double bestD = 1.0e9;
                for (int k = 0; k < numSkipsLocal; ++k)
                {
                    const double d = juce::jmin (std::abs (now - skips[(size_t) k].from), std::abs (now - skips[(size_t) k].to));
                    if (d < bestD) { bestD = d; best = k; }
                }
                if (skipPending >= 0.0) { skipPending = -1.0; break; }
                if (best >= 0)
                {
                    for (int k = best; k < numSkipsLocal - 1; ++k) skips[(size_t) k] = skips[(size_t) k + 1];
                    --numSkipsLocal;
                    cueEdited();
                }
            }
            break;

        case Cmd::skipsToggle:
            if (c.down) skipsOn = ! skipsOn;
            break;

        case Cmd::skipsSet:
            skipsOn = c.value > 0.5;
            break;

        case Cmd::setPitchFader:
        {
            const double v = juce::jlimit (-1.0, 1.0, c.value);
            if (pitchPickup)
            {
                const double faderRatio = 1.0 + v * pitchRange;
                const double prevRatio = 1.0 + pitchFader * pitchRange;
                const bool crossed = (faderRatio - heldRatio) * (prevRatio - heldRatio) <= 0.0;
                if (crossed || std::abs (faderRatio - heldRatio) < 0.001)
                    pitchPickup = false;
            }
            pitchFader = v;
            break;
        }

        case Cmd::setPitchRange:
            pitchRange = juce::jlimit (0.02, 1.0, c.value);
            break;

        case Cmd::pitchBend:
            bendRate = juce::jlimit (-0.5, 0.5, c.value);
            break;

        case Cmd::tempoReset:
            if (c.down) { pitchFader = 0.0; pitchPickup = false; }
            break;

        case Cmd::jogTouch:
            jogTouched = c.down;
            if (c.down && vinylMode && track != nullptr)
            {
                if (coasting) { coasting = false; break; }   // caught the platter again: carry on scratching
                if (playing && ! braking) slipBegin (1);
                scratching = true;
                scrubbing = false;
                scratchVelocity = 0.0;
                jogTicksPending = 0.0;
                sinceTickSec = 0.0;
            }
            else if (! c.down && scratching)
            {
                // A fast spin when letting go (a backspin, or a throw forwards) plays out until the platter slows down
                const double t = playing ? tempoRatio : 0.0;
                if (throwThreshold > 0.0 && (scratchVelocity < -throwThreshold || scratchVelocity > t + throwThreshold))
                {
                    coasting = true;
                    coastSec = 0.0;
                }
                else endScratch();
            }
            break;

        case Cmd::jogTurn:
            jogTicksPending += c.value;
            break;

        case Cmd::setVinylMode:
            vinylMode = c.value > 0.5;
            break;

        case Cmd::loopIn:
            if (c.down && track != nullptr)
            {
                endAutoLoop();
                loopIn = quantizeTime (now);
                loopOut = -1.0;
                loopActive = false;
            }
            break;

        case Cmd::loopOut:
            if (c.down && track != nullptr && loopIn >= 0.0)
            {
                double out = quantizeTime (now);
                if (out <= loopIn + 0.01)
                    out = loopIn + (haveGrid ? track->grid.beatLengthAt (loopIn) : 0.5);
                loopOut = out;
                loopActive = true;
            }
            break;

        case Cmd::reloopExit:
            if (! c.down || track == nullptr) break;
            endAutoLoop();
            if (loopActive)
                loopActive = false;
            else if (loopIn >= 0.0 && loopOut > loopIn)
            {
                loopActive = true;
                jumpTo (loopIn, false);
            }
            break;

        case Cmd::beatLoop:
        {
            if (! c.down || track == nullptr) break;
            endAutoLoop();
            const double beats = juce::jlimit (1.0 / 32.0, 512.0, c.value);
            if (haveGrid)
            {
                const auto& g = track->grid;
                double startBeat = quantize ? std::floor (g.beatPosition (now) + 1.0e-4) : g.beatPosition (now);
                const double start = g.timeAtBeat (startBeat);
                const double end = g.timeAtBeat (startBeat + beats);
                if (loopActive && std::abs (loopIn - start) < 0.005 && std::abs (loopOut - end) < 0.005)
                    loopActive = false;
                else
                {
                    loopIn = start;
                    loopOut = end;
                    loopActive = true;
                }
            }
            else
            {
                const double bpm = track->info.bpm > 0 ? track->info.bpm : 120.0;
                loopIn = now;
                loopOut = now + beats * 60.0 / bpm;
                loopActive = true;
            }
            break;
        }

        case Cmd::loopHalve:
        case Cmd::loopDouble:
            if (c.down && loopIn >= 0.0 && loopOut > loopIn)
            {
                endAutoLoop();
                const double len = loopOut - loopIn;
                const double newLen = c.cmd == Cmd::loopHalve ? len * 0.5 : len * 2.0;
                if (newLen > 0.005 && newLen < 600.0)
                    loopOut = loopIn + newLen;
            }
            break;

        case Cmd::loopOff:
            if (c.down) { loopActive = false; endAutoLoop(); }
            break;

        case Cmd::beatJump:
        {
            if (track == nullptr) break;
            if (! c.down) { if (slipping && slipSource == 4) slipEnd(); break; }
            if (playing) slipBegin (4);
            double target;
            if (haveGrid)
                target = track->grid.timeAtBeat (track->grid.beatPosition (now) + c.value);
            else
                target = now + c.value * 60.0 / (track->info.bpm > 0 ? track->info.bpm : 120.0);

            if (loopActive)
            {
                const double shift = target - now;
                loopIn += shift;
                loopOut += shift;
            }
            jumpTo (target, false);
            break;
        }

        case Cmd::syncToggle:
        case Cmd::syncSet:
        {
            if (! c.down) break;
            const bool newSync = c.cmd == Cmd::syncToggle ? ! syncOn : c.value > 0.5;
            if (newSync && ! syncOn)
                syncSnapRequested = true;
            if (! newSync && syncOn)
            {
                heldRatio = tempoRatio;
                pitchPickup = true;
                syncRatio = 0.0;
                phaseNudge = 0.0;
            }
            syncOn = newSync;
            break;
        }

        case Cmd::masterSet:
            if (c.down) masterRequested = true;
            break;

        case Cmd::keyLockToggle:
            if (c.down) { keyLock = ! keyLock; stretchActive = false; }
            break;

        case Cmd::keyShiftBy:
            if (c.down) keyShift = juce::jlimit (-12, 12, keyShift + (int) std::lround (c.value));
            break;

        case Cmd::keyShiftSet:
            keyShift = juce::jlimit (-12, 12, (int) std::lround (c.value));
            break;

        case Cmd::keyLockSet:
            keyLock = c.value > 0.5;
            stretchActive = false;
            break;

        case Cmd::quantizeToggle:
            if (c.down) quantize = ! quantize;
            break;

        case Cmd::quantizeSet:
            quantize = c.value > 0.5;
            break;

        case Cmd::seekFraction:
            if (track != nullptr && track->audio != nullptr)
                jumpTo (juce::jlimit (0.0, 1.0, c.value) * track->audio->lengthSeconds(), false);
            break;

        case Cmd::seekSeconds:
            jumpTo (c.value, false);
            break;

        case Cmd::stemGain:
            if (c.index >= 0 && c.index < 4) stemTarget[(size_t) c.index] = (float) juce::jlimit (0.0, 1.0, c.value);
            break;

        case Cmd::stemMuteToggle:
            if (c.down && c.index >= 0 && c.index < 4) stemMuted[(size_t) c.index] = ! stemMuted[(size_t) c.index];
            break;

        case Cmd::stemSolo:
        {
            if (! c.down || c.index < 0 || c.index >= 4) break;
            bool alreadySolo = ! stemMuted[(size_t) c.index];
            for (int s = 0; s < 4; ++s)
                if (s != c.index) alreadySolo = alreadySolo && stemMuted[(size_t) s];
            for (int s = 0; s < 4; ++s)
                stemMuted[(size_t) s] = alreadySolo ? false : (s != c.index);
            break;
        }

        case Cmd::stemMode:
        {
            const int m = juce::jlimit (0, 1, (int) c.value);   // 0 VOL, 1 FILTER
            for (int s = 0; s < 4; ++s)
                if (c.index < 0 || c.index == s) stemMode[(size_t) s] = m;
            break;
        }

        case Cmd::stemReset:
            if (c.down) { stemMuted.fill (false); stemTarget.fill (0.5f); stemFilterTarget.fill (0.5f); }
            break;

        case Cmd::stemFilter:
            if (c.index >= 0 && c.index < 4) stemFilterTarget[(size_t) c.index] = (float) juce::jlimit (0.0, 1.0, c.value);
            break;

        case Cmd::stemResonance:
            stemReso = (float) juce::jlimit (0.0, 1.0, c.value);
            break;

        case Cmd::eject:
            if (c.down && ! playing) loadTrack (nullptr);
            break;
    }
}

//==============================================================================
inline void Deck::frameAt (int64_t i, float& l, float& r, const float* g) const noexcept
{
    const auto& a = *track->audio;
    if (i < 0 || i >= a.numFrames) { l = r = 0.0f; return; }
    l = a.left[(size_t) i];
    r = a.right[(size_t) i];

    if (g != nullptr && i < stemsValid)
    {
        // Stem 3 ("Inst") also carries whatever the model didn't assign anywhere, so the four stems
        // always add up to the original track.
        const auto& s = *stems;
        float mixL = 0, mixR = 0;
        for (size_t k = 0; k < 4; ++k)
        {
            mixL += g[k] * (float) s.channels[k * 2][(size_t) i];
            mixR += g[k] * (float) s.channels[k * 2 + 1][(size_t) i];
        }
        l = mixL * StemAudio::toFloat;
        r = mixR * StemAudio::toFloat;
    }
}

inline void Deck::sampleAt (double p, float& l, float& r, const float* g) const noexcept
{
    const double fl = std::floor (p);
    const auto ip = (int64_t) fl;
    const float t = (float) (p - fl);
    float l0, r0, l1, r1, l2, r2, l3, r3;
    frameAt (ip - 1, l0, r0, g);
    frameAt (ip,     l1, r1, g);
    frameAt (ip + 1, l2, r2, g);
    frameAt (ip + 2, l3, r3, g);
    l = hermite (l0, l1, l2, l3, t);
    r = hermite (r0, r1, r2, r3, t);
}

void Deck::stemAutomationAt (double sec, std::array<float, 4>& g, std::array<int, 4>* modes) const noexcept
{
    // Each stem's gain at this point: every stem locator up to here applied in turn (fades part-way if we're inside one).
    // A locator's mode says whether the change is a level fade or a filter sweep.
    g = { 1, 1, 1, 1 };
    if (modes != nullptr) *modes = { 0, 0, 0, 0 };
    for (auto& l : track->stemLocs)
    {
        if (l.timeSec > sec) break;
        for (size_t s = 0; s < 4; ++s)
        {
            if (l.target[s] < 0) continue;
            if (modes != nullptr) (*modes)[s] = l.modeFor ((int) s);
            const float to = l.target[s] > 0 ? 1.0f : 0.0f;
            const double fs = l.fadeSecFor ((int) s);
            if (fs > 0.001 && sec < l.timeSec + fs)
                g[s] = g[s] + (to - g[s]) * (float) ((sec - l.timeSec) / fs);
            else
                g[s] = to;
        }
    }
}

void Deck::readResampled (float* outL, float* outR, int n, double step)
{
    const auto& a = *track->audio;
    double loopInF = loopIn * srcRate, loopOutF = loopOut * srcRate;
    bool looping = loopActive && loopOutF > loopInF + 8.0;

    // Skips only apply while playing forwards (not scratching or previewing backwards).
    const bool skipping = skipsOn && numSkipsLocal > 0 && step > 0.0 && ! scratching;
    int nextSkip = -1;
    double skipFromF = 0.0, skipToF = 0.0;
    auto findNextSkip = [&]
    {
        nextSkip = -1;
        for (int k = 0; k < numSkipsLocal; ++k)
            if (skips[(size_t) k].to * srcRate > pos + 1.0) { nextSkip = k; skipFromF = skips[(size_t) k].from * srcRate; skipToF = skips[(size_t) k].to * srcRate; break; }
    };
    if (skipping) findNextSkip();

    // Active / cycle loop locators: the next one ahead engages when playback runs into it.
    double autoStartF = -1.0;
    int autoNext = -1;
    auto findNextAuto = [&]
    {
        autoNext = -1;
        if (scratching || step <= 0.0) return;
        for (int k = 0; k < numLocs; ++k)
        {
            const auto& L = locs[(size_t) k];
            if (L.mode != 0 && L.end > L.start && L.start * srcRate > pos) { autoNext = k; autoStartF = L.start * srcRate; return; }
        }
    };
    findNextAuto();

    // Play map (phrase repeats / section rearrange): the stretch of the track playing now, and where it ends.
    const auto& map = track->playMap;
    const int numSeg = (int) map.size();
    const bool mapping = skipsOn && numSeg > 0 && step > 0.0 && ! scratching;
    double segEndF = -1.0;
    if (mapping)
    {
        const double p = pos / srcRate;
        auto inside = [&] (int k) { return k >= 0 && k < numSeg && p >= map[(size_t) k].start - 0.002 && p < map[(size_t) k].end; };
        if (! inside (playSeg))
        {
            // After a seek (or the first time): the next stretch that holds this position, else any that does.
            int found = -1;
            for (int j = 1; j <= numSeg && found < 0; ++j)
            {
                const int k = ((playSeg < 0 ? -1 : playSeg) + j + numSeg) % numSeg;
                if (inside (k)) found = k;
            }
            playSeg = found;
        }
        if (playSeg >= 0) segEndF = map[(size_t) playSeg].end * srcRate;
    }
    else if (numSeg == 0) playSeg = -1;

    // Stem controls are bipolar - the centre leaves a stem untouched. VOL: left of centre fades it down (right does
    // nothing); FILTER: left closes a low-pass, right closes a high-pass. Stem locators then fade or sweep on top.
    // Each stem ends up with a level and how open its low-pass and high-pass are (1 = open), ramped across the block.
    std::array<float, 4> target, tLp, tHp;
    bool stemsActive = false;
    std::array<float, 4> autoG { 1, 1, 1, 1 };
    std::array<int, 4> autoMode { 0, 0, 0, 0 };
    if (skipsOn && ! track->stemLocs.empty())
        stemAutomationAt ((pos + n * std::abs (step)) / srcRate, autoG, &autoMode);
    stemAutoNow = autoG;
    for (size_t s = 0; s < 4; ++s)
    {
        // Every stem has both: the volume control (left of centre fades it down) and the filter control
        const float v = stemTarget[s], fv = stemFilterTarget[s];
        float level = 1.0f, lp = 1.0f, hp = 1.0f;
        if (stemMuted[s]) level = 0.0f;
        else level = juce::jmin (1.0f, v * 2.0f);
        if (fv < 0.48f) lp = fv / 0.48f;
        else if (fv > 0.52f) hp = (1.0f - fv) / 0.48f;
        const float ag = autoG[s];
        if (autoMode[s] == 1) lp = juce::jmin (lp, ag);
        else if (autoMode[s] == 2) hp = juce::jmin (hp, ag);
        else level *= ag;
        target[s] = level;
        tLp[s] = lp;
        tHp[s] = hp;
        stemsActive = stemsActive || level < 0.999f || stemCurrent[s] < 0.999f || lp < 0.999f || hp < 0.999f
                   || stemLpNow[s] < 0.999f || stemHpNow[s] < 0.999f;
    }
    stemsActive = stemsActive && stems != nullptr && stems->numFrames == a.numFrames;
    stemsValid = stemsActive ? stems->validFrames.load (std::memory_order_acquire) : 0;

    // Stems being filtered are taken out of the level mix and run through their own filters.
    int numFiltered = 0;
    std::array<int, 4> filtered {};
    for (size_t s = 0; s < 4; ++s)
    {
        auto& f = stemFilters[s];
        const bool on = stemsActive && (tLp[s] < 0.999f || tHp[s] < 0.999f || stemLpNow[s] < 0.999f || stemHpNow[s] < 0.999f);
        if (on && ! f.on) f = {};   // starts clean
        f.on = on;
        if (on) filtered[(size_t) numFiltered++] = (int) s;
    }

    // Fast path: straight copy (no resampling, no stems, no loop or skip crossing, no cross-fade running)
    const bool skipInBlock = (nextSkip >= 0 && skipFromF < pos + n + 1.0) || (autoNext >= 0 && autoStartF < pos + n * step + 1.0)
                          || (autoLoc >= 0 && looping) || (segEndF >= 0.0 && segEndF < pos + n * step + 1.0);
    if (! stemsActive && step == 1.0 && pos == std::floor (pos) && pos >= 0 && xfRemain == 0 && ! skipInBlock
        && (int64_t) pos + n < a.numFrames && (! looping || pos + n < loopOutF || pos >= loopOutF))
    {
        std::memcpy (outL, a.left.data() + (size_t) pos, sizeof (float) * (size_t) n);
        std::memcpy (outR, a.right.data() + (size_t) pos, sizeof (float) * (size_t) n);
        pos += n;
        stemCurrent = target;
        stemLpNow = tLp;
        stemHpNow = tHp;
        return;
    }

    float g[4];
    const float invN = n > 0 ? 1.0f / (float) n : 0.0f;
    const float piOverFs = juce::MathConstants<float>::pi / (float) deviceRate;
    struct Coeffs { float a1 = 1, a2 = 0, a3 = 0; bool on = false; };
    std::array<Coeffs, 4> cLp {}, cHp {};
    std::array<float, 4> famp {};
    const float fk = 1.4f - 1.25f * stemReso;   // damping: 1.4 gentle .. 0.15 rings (the resonance control)
    auto makeCoeffs = [&] (float fc)
    {
        Coeffs c;
        const float gg = std::tan (piOverFs * juce::jlimit (10.0f, 0.45f * (float) deviceRate, fc));
        c.a1 = 1.0f / (1.0f + gg * (gg + fk));
        c.a2 = gg * c.a1;
        c.a3 = gg * c.a2;
        c.on = true;
        return c;
    };
    auto filterCoeffs = [&] (int s, float t)
    {
        const auto si = (size_t) s;
        const float lp = stemLpNow[si] + (tLp[si] - stemLpNow[si]) * t, hp = stemHpNow[si] + (tHp[si] - stemHpNow[si]) * t;
        cLp[si] = lp < 0.999f ? makeCoeffs (20.0f * std::pow (1000.0f, lp)) : Coeffs {};
        cHp[si] = hp < 0.999f ? makeCoeffs (20.0f * std::pow (1000.0f, 1.0f - hp)) : Coeffs {};
        // the last bit of either filter fades out, so fully closed is silent
        famp[si] = juce::jmin (1.0f, lp / 0.06f) * juce::jmin (1.0f, hp / 0.06f);
    };
    auto stemAt = [&] (int s, double p, float& l, float& r)
    {
        float oh[4] = { 0, 0, 0, 0 };
        oh[s] = 1.0f;
        sampleAt (p, l, r, oh);
    };

    for (int i = 0; i < n; ++i)
    {
        // Jumps inside the block (loop wrap, skip) cross-fade from the old position over a few ms.
        double jumpedFrom = -1.0;
        if (autoNext >= 0 && pos >= autoStartF)
        {
            // Ran into an active / cycle loop: engage it.
            const auto& L = locs[(size_t) autoNext];
            loopIn = L.start;
            loopOut = L.end;
            loopActive = true;
            autoLoc = autoNext;
            cyclesLeft = L.mode == 2 ? L.cycles : 0;
            loopInF = loopIn * srcRate;
            loopOutF = loopOut * srcRate;
            looping = loopOutF > loopInF + 8.0;
            autoNext = -1;
        }
        if (looping && step > 0.0 && pos >= loopOutF)
        {
            if (autoLoc >= 0 && cyclesLeft > 0 && --cyclesLeft <= 0)
            {
                // A cycle loop has played its passes: carry on past it.
                looping = false;
                loopActive = false;
                endAutoLoop();
                findNextAuto();
            }
            else
            {
                jumpedFrom = pos;
                pos = loopInF + std::fmod (pos - loopInF, loopOutF - loopInF);
            }
        }
        if (segEndF >= 0.0 && pos >= segEndF)
        {
            // End of this stretch of the play map: on to the next (a repeat jumps back, a rearrange jumps anywhere).
            const double over = pos - segEndF;
            if (playSeg + 1 < numSeg)
            {
                ++playSeg;
                const double ns = map[(size_t) playSeg].start * srcRate;
                if (std::abs (ns - segEndF) > 1.0)
                {
                    if (jumpedFrom < 0.0) jumpedFrom = pos;
                    pos = ns + over;
                }
                segEndF = map[(size_t) playSeg].end * srcRate;
            }
            else
            {
                // The plan has played out: that's the end of the track.
                if (map.back().end * srcRate < (double) a.numFrames - srcRate * 0.05)
                {
                    if (jumpedFrom < 0.0) jumpedFrom = pos;
                    pos = (double) a.numFrames - 1.0;
                }
                playSeg = -1;
                segEndF = -1.0;
            }
            if (skipping) findNextSkip();
            findNextAuto();
        }
        if (nextSkip >= 0 && pos >= skipFromF)
        {
            if (pos < skipToF)
            {
                if (jumpedFrom < 0.0) jumpedFrom = pos;
                pos = juce::jmin ((double) a.numFrames - 1.0, skipToF + (pos - skipFromF));
            }
            findNextSkip();
        }
        if (jumpedFrom >= 0.0)
        {
            xfPos = jumpedFrom;
            xfRemain = xfadeLen;
        }

        const float* gp = nullptr;
        float fg[4] = { 0, 0, 0, 0 };
        if (stemsActive)
        {
            const float t = (float) (i + 1) * invN;
            for (size_t s = 0; s < 4; ++s)
                g[s] = stemCurrent[s] + (target[s] - stemCurrent[s]) * t;
            for (int k = 0; k < numFiltered; ++k)
            {
                const int s = filtered[(size_t) k];
                if ((i & 31) == 0) filterCoeffs (s, t);       // the sweep follows the control every 32 samples
                fg[s] = g[s];                                 // its level, applied after the filters
                g[s] = 0.0f;                                  // filtered stems are added below
            }
            gp = g;
        }

        const float w = xfRemain > 0 ? (float) xfRemain / (float) xfadeLen : 0.0f;
        float l, r;
        sampleAt (pos, l, r, gp);
        if (xfRemain > 0)
        {
            float ol, orr;
            sampleAt (xfPos, ol, orr, gp);
            l = l * (1.0f - w) + ol * w;
            r = r * (1.0f - w) + orr * w;
        }
        if (numFiltered > 0 && (int64_t) pos + 2 < stemsValid)
            for (int k = 0; k < numFiltered; ++k)
            {
                const int s = filtered[(size_t) k];
                float sl, sr;
                stemAt (s, pos, sl, sr);
                if (xfRemain > 0)
                {
                    float ol, orr;
                    stemAt (s, xfPos, ol, orr);
                    sl = sl * (1.0f - w) + ol * w;
                    sr = sr * (1.0f - w) + orr * w;
                }
                auto& f = stemFilters[(size_t) s];
                auto svf = [fk] (const Coeffs& c, float x, float& s1, float& s2, bool low)
                {
                    const float v3 = x - s2, v1 = c.a1 * s1 + c.a2 * v3, v2 = s2 + c.a2 * s1 + c.a3 * v3;
                    s1 = 2.0f * v1 - s1;
                    s2 = 2.0f * v2 - s2;
                    return low ? v2 : x - fk * v1 - v2;
                };
                const auto& lpc = cLp[(size_t) s];
                const auto& hpc = cHp[(size_t) s];
                if (lpc.on) { sl = svf (lpc, sl, f.lp1L, f.lp2L, true); sr = svf (lpc, sr, f.lp1R, f.lp2R, true); }
                if (hpc.on) { sl = svf (hpc, sl, f.hp1L, f.hp2L, false); sr = svf (hpc, sr, f.hp1R, f.hp2R, false); }
                const float amp = famp[(size_t) s] * fg[s];
                l += amp * sl;
                r += amp * sr;
            }
        if (xfRemain > 0)
        {
            xfPos += step;
            --xfRemain;
        }
        outL[i] = l;
        outR[i] = r;

        pos += step;
    }

    if (pos < 0.0) pos = 0.0;
    stemCurrent = target;
    stemLpNow = tLp;
    stemHpNow = tHp;
}

//==============================================================================
void Deck::process (float* outL, float* outR, int n)
{
    if (track == nullptr || track->audio == nullptr || track->audio->numFrames == 0)
    {
        juce::FloatVectorOperations::clear (outL, n);
        juce::FloatVectorOperations::clear (outR, n);
        audibleLast = false;
        fadeTailLen = 0;
        publish();
        return;
    }

    const double blockSec = n / deviceRate;
    const double baseStep = srcRate / deviceRate;
    const double secPerTick = secondsPerRevolution / ticksPerRevolution;

    // Search buttons: through the track at 8x while held (playing carries on from where it lands)
    if (searchDir != 0 && track->audio != nullptr)
    {
        pos = juce::jlimit (0.0, (double) track->audio->numFrames - 1.0, pos + searchDir * 7.0 * blockSec * srcRate);
        stretchActive = false;
    }

    // Jog wheel
    if (scratching)
    {
        sinceTickSec = jogTicksPending != 0.0 ? 0.0 : sinceTickSec + blockSec;
        const double inst = (jogTicksPending * secPerTick) / blockSec;
        jogTicksPending = 0.0;
        scratchVelocity += (inst - scratchVelocity) * 0.4;
        if (scrubbing && ! jogTouched && sinceTickSec > 0.15) { scrubbing = false; endScratch(); }   // the scrub dial stopped
        if (coasting)
        {
            // Let go of a fast spin: keep following the platter until it has nearly stopped (or settled to play speed)
            coastSec += blockSec;
            const double t = playing ? tempoRatio : 0.0;
            if (coastSec > 0.04 && ((scratchVelocity > -0.2 && scratchVelocity < t + 0.15) || sinceTickSec > 0.12))
                endScratch();
        }
    }
    else if (jogTicksPending != 0.0)
    {
        if (playing)
            bendRate = juce::jlimit (-0.3, 0.3, bendRate + jogTicksPending * 0.0008);
        else
        {
            pos = juce::jlimit (0.0, (double) track->audio->numFrames - 1.0, pos + jogTicksPending * secPerTick * srcRate);
            stretchActive = false;
        }
        jogTicksPending = 0.0;
    }
    bendRate *= std::pow (0.5, blockSec / 0.12);
    if (std::abs (bendRate) < 1.0e-5) bendRate = 0.0;

    // Tempo
    if (syncOn && syncRatio > 0.0)
        tempoRatio = syncRatio * (1.0 + phaseNudge);
    else if (pitchPickup)
        tempoRatio = heldRatio;
    else
        tempoRatio = 1.0 + pitchFader * pitchRange;

    // Vinyl speed adjust: the brake slows to a stop, the start (and letting go of a scratch) comes up to speed
    if (! playing) braking = false;
    if (braking)
    {
        // synced: the stop takes that many beats at the tempo it's playing
        const double bpmNow = baseBpmAtPosition() * tempoRatio;
        const double dur = brakeBeats > 0.0 && bpmNow > 1.0 ? brakeBeats * 60.0 / bpmNow : brakeSec;
        motor -= blockSec / juce::jmax (0.01, dur);
        if (motor <= 0.0) { motor = 0.0; braking = false; playing = false; }
    }
    else if (playing && startRamp)
    {
        motor += blockSec / juce::jmax (0.01, startSec);
        if (motor >= 1.0) { motor = 1.0; startRamp = false; }
    }
    else motor = 1.0;

    // Slip: where the track would be without the scratch / loop / jump
    if (loopActive && ! prevLoopActive && playing) slipBegin (2);
    if (! loopActive && prevLoopActive && slipping && slipSource == 2) slipEnd();
    prevLoopActive = loopActive;
    if (slipping && slipWasPlaying) slipSec += blockSec * tempoRatio;

    double speed = 0.0;
    if (scratching)    speed = scratchVelocity;
    else if (playing)
    {
        speed = (tempoRatio + bendRate) * motor;
        if (releaseT < 1.0)
        {
            speed = releaseFrom + (speed - releaseFrom) * releaseT;
            releaseT = juce::jmin (1.0, releaseT + blockSec / juce::jmax (0.01, releaseSec));
        }
    }
    else releaseT = 1.0;
    if (reversing && playing && ! scratching) speed = -speed;   // the reverse button: backwards at the same speed

    // Key lock and/or key shift go through the pitch-shifter; otherwise plain resampling (cheapest).
    // Brakes, starts and scratch releases sound like vinyl, so they skip it.
    const bool useStretch = (keyLock || keyShift != 0) && playing && ! scratching && speed > 0.05 && motor >= 0.999 && releaseT >= 1.0;
    if (useStretch)
    {
        // Key lock off + key shift on: the tempo still bends the pitch like vinyl, plus the shift.
        const double transpose = keyShift + (keyLock ? 0.0 : 12.0 * std::log2 (juce::jmax (0.05, speed)));
        if (std::abs (transpose - lastTranspose) > 1.0e-4)
        {
            stretch->s.setTransposeSemitones ((float) transpose);
            lastTranspose = transpose;
        }
    }

    if (useStretch)
    {
        if (! stretchActive)
            startStretch();

        const double want = n * speed + stretchCarry;
        int m = (int) std::floor (want);
        stretchCarry = want - m;
        m = juce::jlimit (0, (int) tmpL.size(), m);

        readResampled (tmpL.data(), tmpR.data(), m, baseStep);
        float* ins[2] = { tmpL.data(), tmpR.data() };
        float* outs[2] = { outL, outR };
        stretch->s.process (ins, m, outs, n);
    }
    else
    {
        stretchActive = false;
        if (std::abs (speed) < 1.0e-6)
        {
            juce::FloatVectorOperations::clear (outL, n);
            juce::FloatVectorOperations::clear (outR, n);
        }
        else
        {
            readResampled (outL, outR, n, baseStep * speed);
        }
    }

    const bool audibleNow = std::abs (speed) >= 1.0e-6;

    // Stopping (pause, cue release, end of a hot cue preview): fade the old sound out over ~2 ms instead of cutting it
    if (audibleLast && ! audibleNow && fadeTailLen == 0)
    {
        const double savedPos = pos;
        readResampled (fadeTailL.data(), fadeTailR.data(), fadeLen, lastStep);
        pos = savedPos;
        fadeTailLen = fadeLen;
    }
    // Starting from silence: fade in over ~2 ms
    if (! audibleLast && audibleNow && fadeTailLen == 0)
    {
        const int len = juce::jmin (fadeLen, n);
        for (int i = 0; i < len; ++i)
        {
            const float w = (float) i / (float) len;
            outL[i] *= w;
            outR[i] *= w;
        }
    }
    audibleLast = audibleNow;
    if (audibleNow) lastStep = baseStep * speed;

    // Cross-fade out of the previous position after a jump (or to silence after a stop)
    if (fadeTailLen > 0)
    {
        const int len = juce::jmin (fadeTailLen, n);
        for (int i = 0; i < len; ++i)
        {
            const float w = (float) i / (float) len;
            outL[i] = outL[i] * w + fadeTailL[(size_t) i] * (1.0f - w);
            outR[i] = outR[i] * w + fadeTailR[(size_t) i] * (1.0f - w);
        }
        fadeTailLen = 0;
    }

    // End of track
    if (pos >= (double) track->audio->numFrames - 1.0 && ! scratching)
    {
        playing = false;
        cuePreview = false;
        pos = (double) track->audio->numFrames - 1.0;
    }

    // Auto gain (smoothed)
    if (gainNow != gainTarget || gainNow != 1.0f)
    {
        const float g0 = gainNow, g1 = gainTarget;
        for (int i = 0; i < n; ++i)
        {
            const float g = g0 + (g1 - g0) * (float) (i + 1) / (float) n;
            outL[i] *= g;
            outR[i] *= g;
        }
        gainNow = g1;
    }

    auto rangeL = juce::FloatVectorOperations::findMinAndMax (outL, n);
    auto rangeR = juce::FloatVectorOperations::findMinAndMax (outR, n);
    pub.peakL = std::max (std::abs (rangeL.getStart()), std::abs (rangeL.getEnd()));
    pub.peakR = std::max (std::abs (rangeR.getStart()), std::abs (rangeR.getEnd()));

    publish();
}

void Deck::publish()
{
    if (track == nullptr || track->audio == nullptr)
    {
        pub.trackSerial = 0;
        pub.numLocators = 0;
        pub.numSkips = 0;
        pub.playing = false;
        pub.stemsReady = false;
        pub.lengthSec = 0.0;
        pub.positionSec = 0.0;
        return;
    }

    const double heard = heardPositionSec();
    const double len = track->audio->lengthSeconds();
    pub.trackSerial = track->serial;
    pub.positionSec = heard;
    pub.lengthSec = len;
    pub.tempoRatio = tempoRatio;
    pub.bpm = baseBpmAtPosition() * tempoRatio;
    pub.pitchRange = pitchRange;
    pub.cuePointSec = cuePoint;
    pub.loopInSec = loopIn;
    pub.loopOutSec = loopOut;
    pub.loopActive = loopActive;
    pub.playing = playing && ! braking;
    pub.braking = braking;
    pub.slip = slip;
    pub.slipping = slipping;
    pub.reversing = reversing;
    pub.searchDir = searchDir;
    pub.brakeSeconds = brakeSec;
    pub.brakeBeats = brakeBeats;
    pub.slipSec = slipping ? slipSec : -1.0;
    pub.brakeOn = brakeOn;
    pub.startOn = startOn;
    pub.gain = gainTarget;
    pub.keyLock = keyLock;
    pub.keyShift = keyShift;
    pub.sync = syncOn;
    pub.quantize = quantize;
    pub.vinylMode = vinylMode;
    pub.jogTouched = jogTouched;
    pub.stemsReady = stems != nullptr;
    pub.stemsProgress = stems != nullptr && stems->numFrames > 0 ? (float) ((double) stems->validFrames.load() / (double) stems->numFrames) : 0.0f;
    pub.endWarning = playing && len - heard < 30.0;

    for (size_t i = 0; i < 8; ++i)
    {
        pub.hotStart[i] = hotSet[i] ? hot[i].startSec : -1.0;
        pub.hotEnd[i] = hotSet[i] ? hot[i].endSec : -1.0;
        pub.hotColour[i] = hot[i].colour.getARGB();
    }
    for (size_t s = 0; s < 4; ++s)
        pub.stemGain[s] = stemMuted[s] ? 0.0f : stemTarget[s];   // the control (centre 0.5 = untouched)
    for (size_t s = 0; s < 4; ++s) pub.stemAuto[s] = stemAutoNow[s];
    for (size_t s = 0; s < 4; ++s) pub.stemMode[s] = stemMode[s];
    for (size_t s = 0; s < 4; ++s) pub.stemFilter[s] = stemFilterTarget[s];
    pub.stemResonance = stemReso;
    pub.playSegment = playSeg;

    for (int i = 0; i < maxLocators; ++i)
    {
        const bool on = i < numLocs;
        pub.locStart[(size_t) i] = on ? locs[(size_t) i].start : -1.0;
        pub.locEnd[(size_t) i] = on ? locs[(size_t) i].end : -1.0;
        pub.locColour[(size_t) i] = on ? locs[(size_t) i].colour : 0u;
        pub.locMode[(size_t) i] = on ? locs[(size_t) i].mode : 0;
        pub.locCycles[(size_t) i] = on ? locs[(size_t) i].cycles : 4;
    }
    pub.numLocators = numLocs;
    pub.autoLoopLocator = autoLoc;
    pub.cyclesLeft = cyclesLeft;
    for (int i = 0; i < maxSkips; ++i)
    {
        const bool on = i < numSkipsLocal;
        pub.skipFrom[(size_t) i] = on ? skips[(size_t) i].from : -1.0;
        pub.skipTo[(size_t) i] = on ? skips[(size_t) i].to : -1.0;
    }
    pub.numSkips = numSkipsLocal;
    pub.skipsOn = skipsOn;
    pub.skipPendingFrom = skipPending;
    pub.cueEdits = editCount;
    pub.trackDataVersion = dataVersion;
}

} // namespace orbit
