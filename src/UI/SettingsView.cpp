#include "SettingsView.h"
#include "Theme.h"
#include "ColourPicker.h"
#include "../Library/RekordboxMasterDb.h"

namespace orbit
{

class SettingsView::Content : public juce::Component, private juce::ChangeListener
{
public:
    Content (AppSettings& s, AudioEngine& e, std::function<void()> changed, std::function<void()> reload)
        : settings (s), engine (e), onChanged (std::move (changed)), onReload (std::move (reload)),
          deviceSelector (e.getDeviceManager(), 0, 0, 2, 12, false, false, true, false)
    {
        addSection ("AUDIO OUTPUT");
        add (&deviceSelector, 300, {});
        add (&deck1Out, 28, "Deck 1 plays on");
        add (&deck2Out, 28, "Deck 2 plays on");
        add (&sumToMaster, 28, {});
        sumToMaster.setButtonText ("Mix both decks onto outputs 1/2 (only when not using the XDJ-XZ mixer)");
        engine.getDeviceManager().addChangeListener (this);

        addSection ("REKORDBOX LIBRARY");
        add (&dbPath, 28, "master.db");
        add (&dbButtons, 30, {});
        dbButtons.addAndMakeVisible (browseDb);
        dbButtons.addAndMakeVisible (reloadDb);
        dbButtons.onResize = [this] { auto r = dbButtons.getLocalBounds(); browseDb.setBounds (r.removeFromLeft (110)); r.removeFromLeft (8); reloadDb.setBounds (r.removeFromLeft (150)); };
        add (&dbKey, 28, "Database key");
        add (&usbFolder, 28, "Extra export folder");

        addSection ("WAVEFORMS");
        const char* laneLabels[] = { "Deck 1 top lane", "Deck 1 bottom lane", "Deck 2 top lane", "Deck 2 bottom lane" };
        for (int i = 0; i < 4; ++i)
        {
            auto& row = laneRows[(size_t) i];
            row.style.addItemList ({ "Stems (overlaid, follow the stem knobs)", "3-Band", "RGB", "Single colour" }, 1);
            row.shape.addItemList ({ "Full waveform", "Half (bars grow up)", "Half (bars grow down)" }, 1);
            row.style.setSelectedId (settings.laneStyle[(size_t) i / 2][(size_t) i % 2] + 1, juce::dontSendNotification);
            row.shape.setSelectedId (settings.laneShape[(size_t) i / 2][(size_t) i % 2] + 1, juce::dontSendNotification);
            row.style.onChange = row.shape.onChange = [this, i]
            {
                auto& rw = laneRows[(size_t) i];
                settings.laneStyle[(size_t) i / 2][(size_t) i % 2] = rw.style.getSelectedId() - 1;
                settings.laneShape[(size_t) i / 2][(size_t) i % 2] = rw.shape.getSelectedId() - 1;
                onChanged();
            };
            add (&row, 28, laneLabels[i]);
        }
        overviewRow.style.addItemList ({ "3-Band", "RGB", "Single colour" }, 1);
        overviewRow.shape.addItemList ({ "Full waveform", "Half waveform" }, 1);
        overviewRow.style.setSelectedId (settings.overviewStyle, juce::dontSendNotification);
        overviewRow.shape.setSelectedId (settings.overviewShape + 1, juce::dontSendNotification);
        overviewRow.style.onChange = overviewRow.shape.onChange = [this]
        {
            settings.overviewStyle = overviewRow.style.getSelectedId();
            settings.overviewShape = overviewRow.shape.getSelectedId() - 1;
            onChanged();
        };
        add (&overviewRow, 28, "Overview (whole track)");
        add (&colourButton, 28, "Single colour");
        colourButton.setColour (juce::TextButton::buttonColourId, juce::Colour (settings.singleColour));
        colourButton.setButtonText ("CHOOSE COLOUR");
        colourButton.onClick = [this]
        {
            juce::Component::SafePointer<Content> safe (this);
            ColourPickerBubble::show (colourButton.getScreenBounds(), juce::Colour (settings.singleColour), [safe] (juce::Colour c)
            {
                if (safe == nullptr) return;
                safe->settings.singleColour = c.getARGB();
                safe->colourButton.setColour (juce::TextButton::buttonColourId, c);
                safe->onChanged();
            });
        };
        // Stem layer order (back to front) and opacity of each stem
        const char* posNames[] = { "Back", "2nd", "3rd", "Front" };
        for (int i = 0; i < 4; ++i)
        {
            auto& cb = layerRow.boxes[(size_t) i];
            for (int st = 0; st < 4; ++st)
                cb.addItem (juce::String (posNames[i]) + ":  " + (st == 0 ? "Drums" : st == 1 ? "Bass" : st == 2 ? "Inst" : "Vocals"), st + 1);
            cb.setSelectedId (settings.stemLayerOrder[(size_t) i] + 1, juce::dontSendNotification);
            cb.onChange = [this, i]
            {
                // Keep each stem once: swap with the layer that had the chosen stem.
                const int chosen = layerRow.boxes[(size_t) i].getSelectedId() - 1;
                auto& o = settings.stemLayerOrder;
                for (int j = 0; j < 4; ++j)
                    if (j != i && o[(size_t) j] == chosen) o[(size_t) j] = o[(size_t) i];
                o[(size_t) i] = chosen;
                for (int j = 0; j < 4; ++j)
                    layerRow.boxes[(size_t) j].setSelectedId (o[(size_t) j] + 1, juce::dontSendNotification);
                onChanged();
            };
        }
        add (&layerRow, 28, "Stem layers (back to front)");
        const char* stemLabels[] = { "Drums opacity (%)", "Bass opacity (%)", "Inst opacity (%)", "Vocals opacity (%)" };
        for (int st = 0; st < 4; ++st)
        {
            auto& sl = stemOpacity[(size_t) st];
            add (&sl, 28, stemLabels[st]);
            setupSlider (sl, 0.0, 100.0, 1.0, settings.stemOpacity[(size_t) st] * 100.0);
            sl.setColour (juce::Slider::trackColourId, Theme::stemColour (st));
            sl.onValueChange = [this, st] { settings.stemOpacity[(size_t) st] = (float) stemOpacity[(size_t) st].getValue() / 100.0f; onChanged(); };
        }
        add (&foldSkips, 28, {});
        foldSkips.setButtonText ("Fold skipped sections out of the scrolling waveform");
        foldSkips.setToggleState (settings.foldSkips, juce::dontSendNotification);
        foldSkips.onClick = [this] { settings.foldSkips = foldSkips.getToggleState(); onChanged(); };
        add (&playhead, 28, "Playhead position (%)");
        setupSlider (playhead, 10.0, 90.0, 1.0, settings.playheadPosition * 100.0);
        playhead.onValueChange = [this] { settings.playheadPosition = (float) playhead.getValue() / 100.0f; onChanged(); };
        add (&fps, 28, "Frame rate limit");
        add (&zoom, 28, "Visible seconds");

        addSection ("CONTROLLER");
        add (&quantize, 28, {});
        quantize.setButtonText ("Quantize on at start-up (each deck's QUANTIZE button turns it on / off)");
        quantize.setToggleState (settings.quantize, juce::dontSendNotification);
        quantize.onClick = [this] { settings.quantize = quantize.getToggleState(); onChanged(); };
        add (&jogTicks, 28, "Jog ticks / revolution");
        add (&leds, 28, {});
        leds.setButtonText ("Send LED feedback to the XDJ-XZ");

        addSection ("STEMS");
        add (&stemsOnLoad, 28, {});
        stemsOnLoad.setButtonText ("Separate stems automatically when a track is loaded");
        add (&stemsGpu, 28, {});
        stemsGpu.setButtonText ("Use the graphics card (DirectML) - much lower CPU use");
        add (&stemThreads, 28, "CPU threads (0 = auto)");
        add (&modelPath, 28, "Model file");
        add (&cachePath, 28, "Stem folder");
        add (&mp3Offset, 28, "MP3 grid offset (ms)");

        // --- values -------------------------------------------------------------
        dbPath.setText (settings.rekordboxDbPath.isNotEmpty() ? settings.rekordboxDbPath : defaultRekordboxDbFile().getFullPathName(), false);
        dbKey.setText (settings.rekordboxDbKey, false);
        dbKey.setTextToShowWhenEmpty ("built-in (leave empty)", Theme::textFaint);
        usbFolder.setText (settings.usbRoots.joinIntoString (";"), false);
        usbFolder.setTextToShowWhenEmpty ("optional: a folder containing PIONEER/rekordbox/export.pdb", Theme::textFaint);

        fps.addItemList ({ "30 fps", "60 fps", "120 fps", "144 fps" }, 1);
        fps.setSelectedId (settings.maxFps <= 30 ? 1 : settings.maxFps <= 60 ? 2 : settings.maxFps <= 120 ? 3 : 4, juce::dontSendNotification);
        setupSlider (zoom, 1.0, 30.0, 0.5, settings.visibleSeconds);
        setupSlider (jogTicks, 64.0, 4096.0, 1.0, settings.jogTicksPerRev);
        jogTicks.setSkewFactorFromMidPoint (720.0);
        leds.setToggleState (settings.ledFeedback, juce::dontSendNotification);
        stemsOnLoad.setToggleState (settings.stemsOnLoad, juce::dontSendNotification);
        stemsGpu.setToggleState (settings.stemsUseGpu, juce::dontSendNotification);
        setupSlider (stemThreads, 0.0, 32.0, 1.0, settings.stemThreads);
        modelPath.setText (settings.modelFile().getFullPathName(), false);
        cachePath.setText (settings.stemCacheDir().getFullPathName(), false);
        setupSlider (mp3Offset, -150.0, 150.0, 1.0, settings.mp3OffsetMs);

        // --- change handlers ------------------------------------------------------
        auto commitText = [this] (juce::TextEditor& t, std::function<void (const juce::String&)> set)
        {
            t.onReturnKey = t.onFocusLost = [this, &t, set] { set (t.getText().trim()); onChanged(); };
        };
        commitText (dbPath, [this] (auto& v) { settings.rekordboxDbPath = v == defaultRekordboxDbFile().getFullPathName() ? juce::String() : v; });
        commitText (dbKey, [this] (auto& v) { settings.rekordboxDbKey = v; });
        commitText (usbFolder, [this] (auto& v) { settings.usbRoots = juce::StringArray::fromTokens (v, ";", ""); settings.usbRoots.removeEmptyStrings(); });
        commitText (modelPath, [this] (auto& v) { settings.modelPath = v; });
        commitText (cachePath, [this] (auto& v) { settings.stemCachePath = v; });

        browseDb.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Choose rekordbox master.db", juce::File (dbPath.getText()), "*.db");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult() == juce::File()) return;
                dbPath.setText (fc.getResult().getFullPathName(), false);
                settings.rekordboxDbPath = fc.getResult().getFullPathName();
                onChanged();
                onReload();
            });
        };
        reloadDb.onClick = [this] { onReload(); };

        fps.onChange = [this] { const int v[] = { 30, 60, 120, 144 }; settings.maxFps = v[juce::jlimit (0, 3, fps.getSelectedId() - 1)]; onChanged(); };
        zoom.onValueChange = [this] { settings.visibleSeconds = zoom.getValue(); onChanged(); };
        jogTicks.onValueChange = [this] { settings.jogTicksPerRev = jogTicks.getValue(); onChanged(); };
        leds.onClick = [this] { settings.ledFeedback = leds.getToggleState(); onChanged(); };
        stemsOnLoad.onClick = [this] { settings.stemsOnLoad = stemsOnLoad.getToggleState(); onChanged(); };
        stemsGpu.onClick = [this] { settings.stemsUseGpu = stemsGpu.getToggleState(); onChanged(); };
        stemThreads.onValueChange = [this] { settings.stemThreads = (int) stemThreads.getValue(); onChanged(); };
        mp3Offset.onValueChange = [this] { settings.mp3OffsetMs = mp3Offset.getValue(); onChanged(); };

        auto routeChanged = [this]
        {
            settings.deckOutputs[0] = deck1Out.getSelectedId() - 2;
            settings.deckOutputs[1] = deck2Out.getSelectedId() - 2;
            settings.sumToMaster = sumToMaster.getToggleState() ? 1 : 0;
            onChanged();
        };
        deck1Out.onChange = routeChanged;
        deck2Out.onChange = routeChanged;
        sumToMaster.onClick = routeChanged;

        refreshRouting();
    }

    ~Content() override { engine.getDeviceManager().removeChangeListener (this); }

    /** Values that can also be changed from the play page. */
    void refreshValues()
    {
        foldSkips.setToggleState (settings.foldSkips, juce::dontSendNotification);
        playhead.setValue (settings.playheadPosition * 100.0, juce::dontSendNotification);
        zoom.setValue (settings.visibleSeconds, juce::dontSendNotification);
    }

    void refreshRouting()
    {
        int outs = 2;
        if (auto* dev = engine.getDeviceManager().getCurrentAudioDevice())
            outs = juce::jmax (2, dev->getActiveOutputChannels().getHighestBit() + 1);

        juce::StringArray names;
        if (auto* dev = engine.getDeviceManager().getCurrentAudioDevice())
            names = dev->getOutputChannelNames();

        for (auto* box : { &deck1Out, &deck2Out })
        {
            box->clear (juce::dontSendNotification);
            box->addItem ("Not routed", 1);
            for (int ch = 0; ch + 1 < outs; ch += 2)
            {
                juce::String label = "Outputs " + juce::String (ch + 1) + "/" + juce::String (ch + 2);
                if (ch + 1 < names.size())
                    label += "   (" + names[ch] + " / " + names[ch + 1] + ")";
                box->addItem (label, ch + 2);
            }
        }
        deck1Out.setSelectedId (engine.getDeckOutput (0) + 2, juce::dontSendNotification);
        deck2Out.setSelectedId (engine.getDeckOutput (1) + 2, juce::dontSendNotification);
        sumToMaster.setToggleState (engine.isSummingToMaster(), juce::dontSendNotification);
    }

    int layout (int width)
    {
        int y = 8;
        for (auto& row : rows)
        {
            if (row.comp == nullptr)
            {
                row.labelBounds = { 0, y + 14, width, 22 };
                y += 40;
                continue;
            }
            if (row.label.isNotEmpty())
            {
                row.labelBounds = { 0, y, 190, row.height };
                row.comp->setBounds (196, y, juce::jmin (620, width - 196), row.height);
            }
            else
                row.comp->setBounds (0, y, juce::jmin (816, width), row.height);
            y += row.height + 8;
        }
        return y + 20;
    }

    void paint (juce::Graphics& g) override
    {
        for (auto& row : rows)
        {
            if (row.comp == nullptr)
            {
                g.setColour (Theme::accent);
                g.setFont (Theme::font (12.5f, true));
                g.drawText (row.label, row.labelBounds, juce::Justification::centredLeft);
                g.setColour (Theme::line);
                g.fillRect (row.labelBounds.getX(), row.labelBounds.getBottom(), juce::jmin (816, getWidth()), 1);
            }
            else if (row.label.isNotEmpty())
            {
                g.setColour (Theme::textDim);
                g.setFont (Theme::font (13.5f));
                g.drawText (row.label, row.labelBounds, juce::Justification::centredLeft);
            }
        }
    }

private:
    struct Row { juce::Component* comp; int height; juce::String label; juce::Rectangle<int> labelBounds; };

    void addSection (const juce::String& title) { rows.push_back ({ nullptr, 0, title, {} }); }
    void add (juce::Component* c, int h, const juce::String& label)
    {
        addAndMakeVisible (c);
        rows.push_back ({ c, h, label, {} });
        if (auto* t = dynamic_cast<juce::TextEditor*> (c)) t->setFont (Theme::font (13.5f));
    }
    void setupSlider (juce::Slider& s, double lo, double hi, double step, double value)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
        s.setRange (lo, hi, step);
        s.setValue (value, juce::dontSendNotification);
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        settings.audioDeviceState = engine.saveDeviceState();
        refreshRouting();
        onChanged();
    }

    struct Resizer : juce::Component
    {
        std::function<void()> onResize;
        void resized() override { if (onResize) onResize(); }
    };

    AppSettings& settings;
    AudioEngine& engine;
    std::function<void()> onChanged, onReload;
    std::vector<Row> rows;

    juce::AudioDeviceSelectorComponent deviceSelector;
    struct PairRow : juce::Component
    {
        juce::ComboBox style, shape;
        PairRow() { addAndMakeVisible (style); addAndMakeVisible (shape); }
        void resized() override
        {
            auto r = getLocalBounds();
            style.setBounds (r.removeFromLeft (r.getWidth() * 3 / 5));
            r.removeFromLeft (8);
            shape.setBounds (r);
        }
    };
    std::array<PairRow, 4> laneRows;
    struct LayerRow : juce::Component
    {
        std::array<juce::ComboBox, 4> boxes;
        LayerRow() { for (auto& b : boxes) addAndMakeVisible (b); }
        void resized() override
        {
            auto r = getLocalBounds();
            const int w = (r.getWidth() - 18) / 4;
            for (auto& b : boxes) { b.setBounds (r.removeFromLeft (w)); r.removeFromLeft (6); }
        }
    } layerRow;
    std::array<juce::Slider, 4> stemOpacity;
    PairRow overviewRow;
    juce::TextButton colourButton;
    juce::ComboBox deck1Out, deck2Out, fps;
    juce::ToggleButton sumToMaster, leds, stemsOnLoad, stemsGpu, foldSkips, quantize;
    juce::TextEditor dbPath, dbKey, usbFolder, modelPath, cachePath;
    Resizer dbButtons;
    juce::TextButton browseDb { "BROWSE..." }, reloadDb { "RELOAD LIBRARY" };
    juce::Slider zoom, jogTicks, stemThreads, mp3Offset, playhead;
    std::unique_ptr<juce::FileChooser> chooser;
};

SettingsView::SettingsView (AppSettings& s, AudioEngine& e, std::function<void()> changed, std::function<void()> reload)
{
    content = std::make_unique<Content> (s, e, std::move (changed), std::move (reload));
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

SettingsView::~SettingsView() = default;

void SettingsView::refreshRouting() { content->refreshRouting(); }
void SettingsView::refreshValues() { content->refreshValues(); }

void SettingsView::paint (juce::Graphics& g) { g.fillAll (Theme::bg); }

void SettingsView::resized()
{
    viewport.setBounds (getLocalBounds().reduced (20, 10));
    const int w = viewport.getWidth() - viewport.getScrollBarThickness() - 4;
    content->setSize (w, content->layout (w));
}

} // namespace orbit
