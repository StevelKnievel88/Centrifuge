#pragma once
#include "Theme.h"
#include "../Audio/AudioEngine.h"
#include "../Audio/MusicalKey.h"

namespace orbit
{

/** The PREP keyboard: two octaves on screen (and the computer keys A W S E D F T G Y H U J K ... while it has the
    focus), a sound to play - built-in basses or your own samples - and the notes of PREP's key marked on the keys,
    so you can play along and hear whether it fits. */
class KeysPanel : public juce::Component, private juce::Timer
{
public:
    explicit KeysPanel (AudioEngine& e) : engine (e), keys (e.getKeySynth().getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        addAndMakeVisible (keys);
        keys.setAvailableRange (lowest, lowest + 24);   // two octaves and the C above (the - / + buttons move it)
        keys.setOctaveForMiddleC (4);                    // C4 = middle C (so C2 = 36)
        keys.setScrollButtonsVisible (false);
        keys.setKeyPressBaseOctave (lowest / 12);
        keys.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::warn.withAlpha (0.8f));
        keys.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Theme::warn.withAlpha (0.25f));
        keys.scaleOf = [this] { return currentKey(); };

        addAndMakeVisible (sound);
        for (int p = 0; p < KeySynth::numPresets; ++p) sound.addItem (KeySynth::presetName (p), p + 1);
        sound.addSeparator();
        sound.addItem ("Your sample - one, played at every pitch...", 100);
        sound.addItem ("Your samples - one per note (a folder)...", 101);
        sound.setSelectedId (1, juce::dontSendNotification);
        sound.onChange = [this] { soundChosen(); };

        addAndMakeVisible (octDown); addAndMakeVisible (octUp);
        octDown.onClick = [this] { shift (-12); };
        octUp.onClick = [this] { shift (12); };
        addAndMakeVisible (volume);
        volume.setSliderStyle (juce::Slider::LinearHorizontal);
        volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volume.setRange (0.0, 1.5, 0.01);
        volume.setValue (0.8, juce::dontSendNotification);
        volume.onValueChange = [this] { engine.getKeySynth().setVolume ((float) volume.getValue()); };
        setSize (720, 210);
        startTimerHz (4);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bg);
        auto top = getLocalBounds().removeFromTop (40).reduced (10, 6).toFloat();
        const auto k = currentKey();
        juce::String t = "PREP: " + (k.isValid() ? MusicalKey::display (k.toString()) : juce::String ("no key"));
        if (k.isValid()) t << "  - its notes are marked";
        psyched::paintLabel (g, t, top.withTrimmedLeft (430.0f), juce::Justification::centredRight, Theme::textDim, 10.0f);
        psyched::paintLabel (g, "OCT", octLabel.toFloat(), juce::Justification::centred, Theme::textFaint, 9.0f);
        psyched::paintLabel (g, "VOL", volLabel.toFloat(), juce::Justification::centredRight, Theme::textFaint, 9.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (40).reduced (10, 6);
        sound.setBounds (top.removeFromLeft (250));
        top.removeFromLeft (10);
        octDown.setBounds (top.removeFromLeft (28));
        octLabel = top.removeFromLeft (30);
        octUp.setBounds (top.removeFromLeft (28));
        top.removeFromLeft (8);
        volLabel = top.removeFromLeft (30);
        volume.setBounds (top.removeFromLeft (90));
        keys.setBounds (r.reduced (8, 4));
        keys.setKeyWidth ((float) keys.getWidth() / 15.0f);   // two octaves and a top C
    }

private:
    struct Keys : juce::MidiKeyboardComponent
    {
        using juce::MidiKeyboardComponent::MidiKeyboardComponent;
        std::function<MusicalKey()> scaleOf;

        // the notes of the key: tinted, the root brighter
        int inScale (int note) const
        {
            if (scaleOf == nullptr) return 0;
            const auto k = scaleOf();
            if (! k.isValid()) return 0;
            static const int major[] = { 0, 2, 4, 5, 7, 9, 11 }, minor[] = { 0, 2, 3, 5, 7, 8, 10 };
            const int rel = ((note - k.pitch) % 12 + 12) % 12;
            if (rel == 0) return 2;
            for (int i : (k.minor ? minor : major)) if (i == rel) return 1;
            return 0;
        }
        void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour line, juce::Colour text) override
        {
            juce::MidiKeyboardComponent::drawWhiteNote (note, g, area, down, over, line, text);
            if (const int s = inScale (note); s > 0)
            {
                g.setColour (Theme::accent.withAlpha (s == 2 ? 0.55f : 0.22f));
                g.fillRect (area.reduced (2.0f, 0.0f).removeFromBottom (area.getHeight() * 0.3f));
            }
        }
        void drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour fill) override
        {
            juce::MidiKeyboardComponent::drawBlackNote (note, g, area, down, over, fill);
            if (const int s = inScale (note); s > 0)
            {
                g.setColour (Theme::accent.withAlpha (s == 2 ? 0.8f : 0.45f));
                g.fillRect (area.reduced (3.0f, 0.0f).removeFromBottom (area.getHeight() * 0.25f));
            }
        }
    };

    MusicalKey currentKey() const
    {
        auto t = engine.deck (AudioEngine::prepDeck).getLoadedTrack();
        if (t == nullptr) return {};
        auto k = MusicalKey::parse (t->info.key);
        if (k.isValid()) k.pitch = ((k.pitch + engine.deck (AudioEngine::prepDeck).state().keyShift.load()) % 12 + 12) % 12;
        return k;
    }

    void timerCallback() override
    {
        // PREP's track (or its key shift) changed: re-mark the keys
        const auto k = currentKey();
        const int sig = k.isValid() ? k.pitch * 2 + (k.minor ? 1 : 0) : -1;
        if (sig != lastKey) { lastKey = sig; keys.repaint(); repaint(); }
    }

    void shift (int by)
    {
        lowest = juce::jlimit (12, 72, lowest + by);
        keys.setAvailableRange (lowest, lowest + 24);
        keys.setKeyPressBaseOctave (lowest / 12);
    }

    void soundChosen()
    {
        const int id = sound.getSelectedId();
        if (id >= 1 && id <= KeySynth::numPresets) { engine.getKeySynth().setPreset (id - 1); return; }
        if (id == 100)
        {
            chooser = std::make_unique<juce::FileChooser> ("A sample to play at every pitch", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (! f.existsAsFile()) return;
                // its note: from the name if it has one (Bass C2.wav), else C2
                int root = KeySynth::noteFromName (f.getFileNameWithoutExtension());
                if (root < 0) root = 36;
                if (engine.getKeySynth().setSingleSample (f, root))
                {
                    sound.changeItemText (100, "Your sample: " + f.getFileName() + " (" + juce::MidiMessage::getMidiNoteName (root, true, true, 4) + ")...");
                }
            });
        }
        else if (id == 101)
        {
            chooser = std::make_unique<juce::FileChooser> ("A folder of samples (one per note: names with notes like C2, F#3 go there)", juce::File());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
            {
                const auto dir = fc.getResult();
                if (! dir.isDirectory()) return;
                auto files = dir.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
                const int n = engine.getKeySynth().setMultiSample (files, lowest);
                sound.changeItemText (101, "Your samples: " + dir.getFileName() + " (" + juce::String (n) + ")...");
            });
        }
    }

    AudioEngine& engine;
    Keys keys;
    juce::ComboBox sound;
    juce::TextButton octDown { "-" }, octUp { "+" };
    juce::Slider volume;
    juce::Rectangle<int> octLabel, volLabel;
    int lowest = 36;   // C2: two octaves up from here
    int lastKey = -2;
    std::unique_ptr<juce::FileChooser> chooser;
};

/** KeysPanel in a small window of its own that stays on top while you work on PREP. */
class KeysWindow : public juce::DocumentWindow
{
public:
    KeysWindow (AudioEngine& e, std::function<void()> closed)
        : juce::DocumentWindow ("Centrifuge - Keys", Theme::bg, juce::DocumentWindow::closeButton), onClosed (std::move (closed))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new KeysPanel (e), true);
        setResizable (true, false);
        setAlwaysOnTop (true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }
    void closeButtonPressed() override { if (onClosed) juce::MessageManager::callAsync (onClosed); }
private:
    std::function<void()> onClosed;
};

} // namespace orbit
