#include "CentrifugeRotor.h"
#include "BinaryData.h"
#include "Theme.h"

namespace orbit
{

void CentrifugeRotor::paintRotor (juce::Graphics& g, juce::Rectangle<float> area, float angle, float speed,
                                  float fill, float streaks, float streakPhase)
{
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float R = size * 0.5f;
    const float tiny = size < 40.0f ? 1.0f : 0.0f;   // simplify at small sizes

    // Housing
    g.setColour (juce::Colour (0xff1b1d22));
    g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (c));
    g.setColour (juce::Colour (0xff3a3e46));
    g.drawEllipse (juce::Rectangle<float> (size, size).withCentre (c).reduced (size * 0.02f), juce::jmax (1.0f, size * 0.025f));

    const float tubeLen = R * 0.50f, tubeW = R * 0.24f, tubeInner = R * 0.30f;

    auto drawTubes = [&] (float a, float alpha, bool details)
    {
        for (int s = 0; s < 4; ++s)
        {
            const float theta = a + juce::MathConstants<float>::halfPi * (float) s + juce::MathConstants<float>::pi * 0.25f;
            const auto t = juce::AffineTransform::rotation (theta).translated (c);

            // arm (hidden under the hub, only drawn for the sharp copy)
            if (details)
            {
                juce::Path arm;
                arm.addRoundedRectangle (0.0f, -tubeW * 0.18f, tubeInner + tubeLen * 0.2f, tubeW * 0.36f, tubeW * 0.12f);
                g.setColour (juce::Colour (0xff4a4f58));
                g.fillPath (arm, t);
            }

            // tube body
            juce::Path tube;
            tube.addRoundedRectangle (tubeInner, -tubeW * 0.5f, tubeLen, tubeW, tubeW * 0.5f);
            g.setColour (juce::Colour (0xff0d0e11).withMultipliedAlpha (alpha));
            g.fillPath (tube, t);

            // contents: fills from the outer end (heaviest goes outward)
            const auto col = Theme::stemColour (s);
            if (fill > 0.0f)
            {
                const float len = tubeLen * juce::jlimit (0.0f, 1.0f, fill);
                juce::Path liquid;
                liquid.addRoundedRectangle (tubeInner + tubeLen - len, -tubeW * 0.5f, len, tubeW, tubeW * 0.5f);
                g.setColour (col.withMultipliedAlpha (alpha));
                g.fillPath (liquid, t);
            }
            g.setColour (col.withMultipliedAlpha (alpha * (fill > 0.0f ? 1.0f : 0.55f)));
            g.strokePath (tube, juce::PathStrokeType (juce::jmax (1.0f, size * 0.018f)), t);

            if (details && tiny == 0.0f && fill > 0.05f)
            {
                juce::Path shine;
                shine.addRoundedRectangle (tubeInner + tubeLen * 0.15f, -tubeW * 0.32f, tubeLen * 0.7f, tubeW * 0.12f, tubeW * 0.06f);
                g.setColour (juce::Colours::white.withAlpha (0.25f * alpha));
                g.fillPath (shine, t);
            }
        }
    };

    // Motion blur: a few fading copies behind the current angle.
    const int ghosts = speed > 0.05f ? 5 : 0;
    for (int i = ghosts; i >= 1; --i)
        drawTubes (angle - speed * 0.35f * (float) i / (float) ghosts, 0.12f * speed, false);
    drawTubes (angle, 1.0f, true);

    if (speed > 0.3f && tiny == 0.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f * speed));
        g.drawEllipse (juce::Rectangle<float> (R * 1.3f, R * 1.3f).withCentre (c), R * 0.5f);
    }

    // Streaks: stem colour flying out from the hub to each tube
    if (streaks > 0.0f && tiny == 0.0f)
    {
        for (int s = 0; s < 4; ++s)
        {
            const float theta = angle + juce::MathConstants<float>::halfPi * (float) s + juce::MathConstants<float>::pi * 0.25f;
            for (int k = 0; k < 3; ++k)
            {
                const float ph = std::fmod (streakPhase + (float) k / 3.0f + (float) s * 0.13f, 1.0f);
                const float r0 = R * (0.18f + 0.55f * ph);
                const float trail = 0.35f;
                juce::Path p;
                p.addCentredArc (c.x, c.y, r0, r0, 0.0f, theta + juce::MathConstants<float>::halfPi - trail, theta + juce::MathConstants<float>::halfPi, true);
                g.setColour (Theme::stemColour (s).withAlpha (streaks * (1.0f - ph) * 0.9f));
                g.strokePath (p, juce::PathStrokeType (juce::jmax (1.5f, size * 0.025f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
    }

    // Hub with the track's waveform
    const float hubR = R * 0.28f;
    auto hub = juce::Rectangle<float> (hubR * 2.0f, hubR * 2.0f).withCentre (c);
    g.setColour (juce::Colour (0xff2a2d33));
    g.fillEllipse (hub);
    g.setColour (juce::Colour (0xff60656f));
    g.drawEllipse (hub, juce::jmax (1.0f, size * 0.015f));

    const float wf = 1.0f - juce::jlimit (0.0f, 1.0f, streaks > 0.0f ? fill : 0.0f) * 0.7f; // waveform fades as it separates
    juce::Path wave;
    const int bars = tiny > 0.0f ? 5 : 9;
    const float bw = hubR * 1.3f / (float) bars;
    static const float shape[] = { 0.25f, 0.55f, 0.85f, 0.45f, 1.0f, 0.5f, 0.8f, 0.4f, 0.2f };
    for (int i = 0; i < bars; ++i)
    {
        const float h = hubR * 0.9f * shape[(i * 9) / bars];
        wave.addRoundedRectangle (c.x - hubR * 0.65f + i * bw + bw * 0.2f, c.y - h * 0.5f, bw * 0.6f, h, bw * 0.3f);
    }
    g.setColour (juce::Colours::white.withAlpha (wf));
    g.fillPath (wave, juce::AffineTransform::rotation (angle * 0.0f, c.x, c.y));
}

//==============================================================================
CentrifugeRotor::CentrifugeRotor() : vblank (this, [this] (double t) { tick (t); })
{
    setInterceptsMouseClicks (false, false);
}

void CentrifugeRotor::setProgress (float p)
{
    p = juce::jlimit (0.0f, 1.0f, p);
    if (std::abs (p - progress) > 0.002f) { progress = p; repaint(); }
}

void CentrifugeRotor::setSpinning (bool s)
{
    if (s != spinning) { spinning = s; lastTime = 0.0; repaint(); }
}

void CentrifugeRotor::tick (double t)
{
    if (! spinning && speed <= 0.001f) { lastTime = 0.0; return; }
    const float dt = lastTime > 0.0 ? (float) juce::jlimit (0.0, 0.1, t - lastTime) : 0.0f;
    lastTime = t;
    speed += ((spinning ? 1.0f : 0.0f) - speed) * juce::jmin (1.0f, dt * 2.5f);
    if (! spinning && speed < 0.002f) speed = 0.0f;
    angle = std::fmod (angle + speed * dt * 14.0f, juce::MathConstants<float>::twoPi);
    streakPhase = std::fmod (streakPhase + dt * 1.6f, 1.0f);
    repaint();
}

void CentrifugeRotor::paint (juce::Graphics& g)
{
    paintRotor (g, getLocalBounds().toFloat().reduced (1.0f), angle, speed, progress, spinning ? speed : 0.0f, streakPhase);
}

//==============================================================================
CentrifugeSplash::CentrifugeSplash() : vblank (this, [this] (double t) { tick (t); })
{
    setOpaque (false);
}

void CentrifugeSplash::tick (double t)
{
    if (start < 0.0) start = t;
    const double prev = elapsed;
    elapsed = t - start;
    const float dt = (float) juce::jlimit (0.0, 0.1, elapsed - prev);

    // 0-0.35 s still, spin up to 1.4 s, hold, spin down from 2.1 s, fade out 2.6-3.0 s
    const float spd = (float) juce::jlimit (0.0, 1.0, elapsed < 0.35 ? 0.0 : elapsed < 1.4 ? (elapsed - 0.35) / 1.05
                                                     : elapsed < 2.1 ? 1.0 : 1.0 - (elapsed - 2.1) / 0.6);
    angle = std::fmod (angle + spd * dt * 16.0f, juce::MathConstants<float>::twoPi);
    repaint();

    if (elapsed > 3.0 && onFinished)
    {
        auto f = onFinished;
        onFinished = nullptr;
        f();
    }
}

void CentrifugeSplash::paint (juce::Graphics& g)
{
    const float e = (float) elapsed;
    const float alpha = e < 2.6f ? 1.0f : juce::jmax (0.0f, 1.0f - (e - 2.6f) / 0.4f);
    g.setColour (Theme::bg.withAlpha (alpha));
    g.fillAll();
    if (alpha <= 0.0f) return;

    const float spd = juce::jlimit (0.0f, 1.0f, e < 0.35f ? 0.0f : e < 1.4f ? (e - 0.35f) / 1.05f : e < 2.1f ? 1.0f : 1.0f - (e - 2.1f) / 0.6f);
    const float fill = juce::jlimit (0.0f, 1.0f, (e - 0.9f) / 1.1f);
    const float streaks = e > 0.8f && e < 2.3f ? spd : 0.0f;

    const float size = juce::jmin ((float) getHeight() * 0.42f, (float) getWidth() * 0.4f, 320.0f);
    auto area = juce::Rectangle<float> (size, size).withCentre (getLocalBounds().toFloat().getCentre().translated (0.0f, -size * 0.12f));

    juce::Graphics::ScopedSaveState ss (g);
    g.setOpacity (alpha);
    CentrifugeRotor::paintRotor (g, area, angle, spd, fill, streaks, std::fmod (e * 1.6f, 1.0f));

    // the Centrifuge logo under it
    static const juce::Image mark = juce::ImageCache::getFromMemory (BinaryData::centrifugetopbarlogo_png, BinaryData::centrifugetopbarlogo_pngSize);
    if (mark.isValid())
    {
        auto lr = area.withY (area.getBottom() + size * 0.08f).withHeight (size * 0.22f).expanded (size * 0.4f, 0.0f).toNearestInt();
        g.drawImageWithin (mark, lr.getX(), lr.getY(), lr.getWidth(), lr.getHeight(), juce::RectanglePlacement::centred);
    }
}

} // namespace orbit
