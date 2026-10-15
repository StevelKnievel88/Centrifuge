#pragma once
#include "CentrifugeRotor.h"
#include "Theme.h"

#ifndef CENTRIFUGE_BUILD_INFO
 #define CENTRIFUGE_BUILD_INFO ""
#endif

namespace orbit
{

/** Version (semantic versioning: MAJOR.MINOR.PATCH) and credits. Opened from the logo in the top-left corner. */
class AboutView : public juce::Component
{
public:
    AboutView()
    {
        addAndMakeVisible (rotor);
        rotor.setProgress (1.0f);
        rotor.setInterceptsMouseClicks (false, false);
    }

    static juce::String version() { return JUCE_APPLICATION_VERSION_STRING; }
    static juce::String buildInfo() { return CENTRIFUGE_BUILD_INFO; }

    void show()
    {
        setVisible (true);
        toFront (false);
        rotor.setSpinning (true);
        juce::Timer::callAfterDelay (1400, [safe = juce::Component::SafePointer<CentrifugeRotor> (&rotor)]
                                     { if (safe != nullptr) safe->setSpinning (false); });
    }

    std::function<void()> onClose;

    void mouseDown (const juce::MouseEvent&) override { setVisible (false); if (onClose) onClose(); }

    void resized() override
    {
        card = getLocalBounds().withSizeKeepingCentre (juce::jmin (520, getWidth() - 40), juce::jmin (400, getHeight() - 40));
        rotor.setBounds (card.withTrimmedTop (28).removeFromTop (140).withSizeKeepingCentre (140, 140));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.72f));
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (card.toFloat(), 10.0f);
        g.setColour (Theme::line);
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 10.0f, 1.0f);

        auto r = card.reduced (28).withTrimmedTop (150);
        g.setColour (Theme::text);
        g.setFont (Theme::font (30.0f, true));
        g.drawText ("Centrifuge", r.removeFromTop (38), juce::Justification::centred);

        g.setColour (Theme::accent);
        g.setFont (Theme::font (16.0f, true));
        auto v = "Version " + version();
        if (buildInfo().isNotEmpty()) v << "   (" << buildInfo() << ")";
        g.drawText (v, r.removeFromTop (24), juce::Justification::centred);

        g.setColour (Theme::textDim);
        g.setFont (Theme::font (13.0f));
        g.drawText ("Plays your rekordbox library on the Pioneer XDJ-XZ, with stems", r.removeFromTop (22), juce::Justification::centred);
        r.removeFromTop (14);

        g.setColour (Theme::line);
        g.fillRect (r.removeFromTop (1).reduced (40, 0));
        r.removeFromTop (12);

        g.setColour (Theme::textFaint);
        g.setFont (Theme::font (12.0f));
        g.drawFittedText ("Built with JUCE, ONNX Runtime (DirectML), SQLite3 Multiple Ciphers and Signalsmith Stretch.\n"
                          "Stem separation: HTDemucs (Meta, MIT licence), exported with demucs.onnx (Sevag Hanssian).\n"
                          "rekordbox file formats: Deep Symmetry (crate-digger), rekordcrate and pyrekordbox.\n"
                          "Inter typeface. rekordbox is a trademark of AlphaTheta Corporation.",
                          r.removeFromTop (78), juce::Justification::centredTop, 5);

        g.setColour (Theme::textFaint.withAlpha (0.7f));
        g.drawText ("Click anywhere to close", card.reduced (16).removeFromBottom (18), juce::Justification::centred);
    }

private:
    CentrifugeRotor rotor;
    juce::Rectangle<int> card;
};

} // namespace orbit
