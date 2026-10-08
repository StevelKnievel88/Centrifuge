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
void Deck::post (const Command& c)
{
    const juce::ScopedLock sl (producerLock);
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0)       queue[(size_t) s1] = c;
    else if (n2 > 0)  queue[(size_t) s2] = c;
    else              return; // queue full: drop (only happens if audio has stopped)
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
    stretchActive = false;
}

void Deck::handleCommands()
{
    const int ready = fifo.getNumReady();
    if (ready <= 0) return;
    int s1, n1, s2, n2;
    fifo.prepareToRead (ready, s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) apply (queue[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) apply (queue[(size_t) (s2 + i)]);
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
    if (playing || scratching)
    {
        const double savedPos = pos;
        readResampled (fadeTailL.data(), fadeTailR.data(), fadeLen, (srcRate / deviceRate) * juce::jmax (0.25, tempoRatio));
        pos = savedPos;
        fadeTailLen = fadeLen;
    }

    pos = juce::jlimit (0.0, (double) track->audio->numFrames - 1.0, sec * srcRate);
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
    stems = nullptr;
    playing = cuePreview = scratching = false;
    hotPreview = -1;
    loopActive = false;
    loopIn = loopOut = -1.0;
    stretchActive = false;
    fadeTailLen = 0;
    bendRate = 0.0;
    stemMuted.fill (false);

    if (track == nullptr || track->audio == nullptr)
    {
        pos = 0.0;
        return;
    }

    srcRate = track->audio->sampleRate;
    hot = track->hotCues;
    hotSet = track->hotCueSet;

    cuePoint = 0.0;
    if (! track->memoryCues.empty())
        cuePoint = track->memoryCues.front().startSec;
    pos = cuePoint * srcRate;
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

        case Cmd::attachStems:
            if (track != nullptr && track->serial == c.serial)
                stems = static_cast<StemAudio*> (c.ptr);
            break;

        case Cmd::shift:
            shiftHeld = c.down;
            break;

        case Cmd::play:      if (c.down && track != nullptr) { playing = true; cuePreview = false; hotPreview = -1; syncSnapRequested = syncOn; } break;
        case Cmd::pause:     if (c.down) playing = false; break;

        case Cmd::playToggle:
            if (! c.down || track == nullptr) break;
            if (playing && ! cuePreview && hotPreview < 0)
                playing = false;
            else
            {
                if (! playing) syncSnapRequested = syncOn;
                playing = true;
                cuePreview = false;
                hotPreview = -1;
            }
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
                    break;
                }
                if (! hotSet[(size_t) i])
                {
                    setHotCueAt (i, quantizeTime (now));
                    break;
                }
                const auto& hc = hot[(size_t) i];
                if (hc.isLoop())
                {
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
                    jumpTo (target, false);
                }
                else
                {
                    hotPreview = i;
                    hotPreviewReturn = hc.startSec;
                    cuePoint = hc.startSec;
                    jumpTo (hc.startSec, false);
                    playing = true;
                }
            }
            else if (hotPreview == i)
            {
                hotPreview = -1;
                playing = false;
                jumpTo (hotPreviewReturn, false);
            }
            break;
        }

        case Cmd::hotCueDelete:
            if (c.down && c.index >= 0 && c.index < 8) hotSet[(size_t) c.index] = false;
            break;

        case Cmd::memoryCuePrev:
        case Cmd::memoryCueNext:
        {
            if (! c.down || track == nullptr || track->memoryCues.empty()) break;
            const bool next = c.cmd == Cmd::memoryCueNext;
            const CuePoint* best = nullptr;
            for (auto& m : track->memoryCues)
            {
                if (next && m.startSec > now + 0.05 && (best == nullptr || m.startSec < best->startSec)) best = &m;
                if (! next && m.startSec < now - 0.05 && (best == nullptr || m.startSec > best->startSec)) best = &m;
            }
            if (best != nullptr)
            {
                cuePoint = best->startSec;
                jumpTo (cuePoint, false);
                if (best->isLoop()) { loopIn = best->startSec; loopOut = best->endSec; loopActive = false; }
            }
            break;
        }

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
                scratching = true;
                scratchVelocity = 0.0;
                jogTicksPending = 0.0;
            }
            else if (! c.down)
            {
                scratching = false;
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
                const double len = loopOut - loopIn;
                const double newLen = c.cmd == Cmd::loopHalve ? len * 0.5 : len * 2.0;
                if (newLen > 0.005 && newLen < 600.0)
                    loopOut = loopIn + newLen;
            }
            break;

        case Cmd::loopOff:
            if (c.down) loopActive = false;
            break;

        case Cmd::beatJump:
        {
            if (! c.down || track == nullptr) break;
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

        case Cmd::keyLockSet:
            keyLock = c.value > 0.5;
            stretchActive = false;
            break;

        case Cmd::quantizeToggle:
            if (c.down) quantize = ! quantize;
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

        case Cmd::stemReset:
            if (c.down) { stemMuted.fill (false); stemTarget.fill (1.0f); }
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

void Deck::readResampled (float* outL, float* outR, int n, double step)
{
    const auto& a = *track->audio;
    const double loopInF = loopIn * srcRate, loopOutF = loopOut * srcRate;
    const bool looping = loopActive && loopOutF > loopInF + 8.0;

    // Stem gain ramp across this block
    std::array<float, 4> target;
    bool stemsActive = false;
    for (size_t s = 0; s < 4; ++s)
    {
        target[s] = stemMuted[s] ? 0.0f : stemTarget[s];
        stemsActive = stemsActive || target[s] < 0.999f || stemCurrent[s] < 0.999f;
    }
    stemsActive = stemsActive && stems != nullptr && stems->numFrames == a.numFrames;
    stemsValid = stemsActive ? stems->validFrames.load (std::memory_order_acquire) : 0;

    // Fast path: straight copy (no resampling, no stems, no loop crossing)
    if (! stemsActive && step == 1.0 && pos == std::floor (pos) && pos >= 0
        && (int64_t) pos + n < a.numFrames && (! looping || pos + n < loopOutF || pos >= loopOutF))
    {
        std::memcpy (outL, a.left.data() + (size_t) pos, sizeof (float) * (size_t) n);
        std::memcpy (outR, a.right.data() + (size_t) pos, sizeof (float) * (size_t) n);
        pos += n;
        stemCurrent = target;
        return;
    }

    float g[4];
    const float invN = n > 0 ? 1.0f / (float) n : 0.0f;

    for (int i = 0; i < n; ++i)
    {
        if (looping && step > 0.0 && pos >= loopOutF)
            pos = loopInF + std::fmod (pos - loopInF, loopOutF - loopInF);

        const float* gp = nullptr;
        if (stemsActive)
        {
            const float t = (float) (i + 1) * invN;
            for (size_t s = 0; s < 4; ++s)
                g[s] = stemCurrent[s] + (target[s] - stemCurrent[s]) * t;
            gp = g;
        }

        const double fl = std::floor (pos);
        const auto ip = (int64_t) fl;
        const float t = (float) (pos - fl);

        float l0, r0, l1, r1, l2, r2, l3, r3;
        frameAt (ip - 1, l0, r0, gp);
        frameAt (ip,     l1, r1, gp);
        frameAt (ip + 1, l2, r2, gp);
        frameAt (ip + 2, l3, r3, gp);
        outL[i] = hermite (l0, l1, l2, l3, t);
        outR[i] = hermite (r0, r1, r2, r3, t);

        pos += step;
    }

    if (pos < 0.0) pos = 0.0;
    stemCurrent = target;
}

//==============================================================================
void Deck::process (float* outL, float* outR, int n)
{
    if (track == nullptr || track->audio == nullptr || track->audio->numFrames == 0)
    {
        juce::FloatVectorOperations::clear (outL, n);
        juce::FloatVectorOperations::clear (outR, n);
        publish();
        return;
    }

    const double blockSec = n / deviceRate;
    const double baseStep = srcRate / deviceRate;
    const double secPerTick = secondsPerRevolution / ticksPerRevolution;

    // Jog wheel
    if (scratching)
    {
        const double inst = (jogTicksPending * secPerTick) / blockSec;
        jogTicksPending = 0.0;
        scratchVelocity += (inst - scratchVelocity) * 0.4;
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

    double speed = 0.0;
    if (scratching)    speed = scratchVelocity;
    else if (playing)  speed = tempoRatio + bendRate;

    const bool useStretch = keyLock && playing && ! scratching && speed > 0.05;

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

    // Cross-fade out of the previous position after a jump
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
    pub.playing = playing;
    pub.keyLock = keyLock;
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
        pub.stemGain[s] = stemMuted[s] ? 0.0f : stemTarget[s];
}

} // namespace orbit
