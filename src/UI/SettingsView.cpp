#include "SettingsView.h"
#include "../Stems/DemucsSeparator.h"
#include "Theme.h"
#include "ColourPicker.h"
#include "../Library/RekordboxMasterDb.h"
#include "../App/Features.h"
#include "../Audio/TrackAudio.h"

namespace orbit
{

class SettingsView::Content : public juce::Component, private juce::ChangeListener
{
public:
    Content (AppSettings& s, AudioEngine& e, std::function<void()> changed, std::function<void()> reload, bool stemsPage)
        : settings (s), engine (e), onChanged (std::move (changed)), onReload (std::move (reload)), stemsOnly (stemsPage),
          deviceSelector (e.getDeviceManager(), 0, 0, 2, 12, false, false, true, false)
    {
        addSection ("AUDIO OUTPUT");
        add (&deviceSelector, 300, {});
        add (&deck1Out, 28, "Deck 1 plays on");
        add (&deck2Out, 28, "Deck 2 plays on");
        add (&previewOut, 28, "PREP / previews play on");
        add (&sumToMaster, 28, {});
        sumToMaster.setButtonText ("Mix both decks onto outputs 1/2 (only when not using the XDJ-XZ mixer)");
        engine.getDeviceManager().addChangeListener (this);

        addSection ("LIBRARY");
        if (Features::rekordboxCollection)
        {
            add (&dbPath, 28, "rekordbox master.db");
            add (&dbButtons, 30, {});
            dbButtons.addAndMakeVisible (browseDb);
            dbButtons.addAndMakeVisible (reloadDb);
            dbButtons.onResize = [this] { auto r = dbButtons.getLocalBounds(); browseDb.setBounds (r.removeFromLeft (110)); r.removeFromLeft (8); reloadDb.setBounds (r.removeFromLeft (150)); };
            add (&dbKey, 28, "Database key");
        }
        add (&usbFolder, 28, "Extra USB export folder");
        add (&analysisRange, 28, "Tempo range (added music)");
        add (&keyNotation, 28, "Key notation");
        keyNotation.addItemList ({ "Standard (Fm, Ab, ...)", "Camelot (4A, 4B, ...)" }, 1);
        keyNotation.setSelectedId (settings.camelotKeys ? 2 : 1, juce::dontSendNotification);
        keyNotation.onChange = [this] { settings.camelotKeys = keyNotation.getSelectedId() == 2; onChanged(); };
        analysisRange.addItemList ({ "70 - 140 BPM", "88 - 175 BPM (house, techno, psytrance, drum & bass)", "100 - 200 BPM", "120 - 240 BPM" }, 1);
        analysisRange.setSelectedId (juce::jlimit (0, 3, settings.analysisRange) + 1, juce::dontSendNotification);
        analysisRange.setTooltip ("Music added from files or folders is analysed by Centrifuge: its tempo is looked for in this range");
        analysisRange.onChange = [this] { settings.analysisRange = analysisRange.getSelectedId() - 1; onChanged(); };

        addSection ("PLAY DECKS - WAVEFORMS");
        add (&nameOrder, 28, "Track name on the decks");
        nameOrder.addItemList ({ "Title, then artist", "Artist, then title" }, 1);
        nameOrder.setSelectedId (settings.artistFirst ? 2 : 1, juce::dontSendNotification);
        nameOrder.onChange = [this] { settings.artistFirst = nameOrder.getSelectedId() == 2; onChanged(); };
        const char* laneLabels[] = { "Deck 1 top lane", "Deck 1 bottom lane", "Deck 2 top lane", "Deck 2 bottom lane" };
        for (int i = 0; i < 4; ++i)
        {
            auto& row = laneRows[(size_t) i];
            fillLaneStyles (row.style, i % 2 == 1);
            row.shape.addItemList ({ "Full waveform", "Half (bars grow up)", "Half (bars grow down)" }, 1);
            row.style.setSelectedId (settings.laneStyle[(size_t) i / 2][(size_t) i % 2] + 2, juce::dontSendNotification);
            row.shape.setSelectedId (settings.laneShape[(size_t) i / 2][(size_t) i % 2] + 1, juce::dontSendNotification);
            row.style.onChange = row.shape.onChange = [this, i]
            {
                auto& rw = laneRows[(size_t) i];
                settings.laneStyle[(size_t) i / 2][(size_t) i % 2] = rw.style.getSelectedId() - 2;
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
        colourButton.getProperties().set ("psyched.swatch", true);
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
        add (&followDeck, 28, {});
        followDeck.setButtonText ("Single colour follows each deck's colour (click a deck's number to change it)");
        followDeck.setToggleState (settings.singleFollowsDeck, juce::dontSendNotification);
        followDeck.onClick = [this] { settings.singleFollowsDeck = followDeck.getToggleState(); onChanged(); };
        add (&foldSkips, 28, {});
        foldSkips.setButtonText ("Edited view on every deck: waveforms show the track as it will play (each deck's EDITED chip switches its own)");
        foldSkips.setToggleState (settings.editView[0] && settings.editView[1] && settings.editView[2], juce::dontSendNotification);
        foldSkips.onClick = [this] { const bool on = foldSkips.getToggleState(); settings.editView = { on, on, on }; onChanged(); };
        add (&markerSize, 28, "Overview marker size (%)");
        setupSlider (markerSize, 75.0, 300.0, 5.0, settings.markerScale * 100.0);
        markerSize.onValueChange = [this] { settings.markerScale = (float) markerSize.getValue() / 100.0f; onChanged(); };
        add (&playhead, 28, "Playhead position (%)");
        setupSlider (playhead, 10.0, 90.0, 1.0, settings.playheadPosition * 100.0);
        playhead.onValueChange = [this] { settings.playheadPosition = (float) playhead.getValue() / 100.0f; onChanged(); };
        add (&fps, 28, "Frame rate limit");
        add (&zoom, 28, "Visible seconds");

        addSection ("BROWSER");
        previewRow.style.addItemList ({ "3-Band", "RGB", "Single colour", "Stems (tracks with stems; others 3-band)" }, 1);
        previewRow.shape.addItemList ({ "Full waveform", "Half (bars grow up)" }, 1);
        previewRow.style.setSelectedId (settings.previewStyle, juce::dontSendNotification);
        previewRow.shape.setSelectedId (settings.previewShape + 1, juce::dontSendNotification);
        previewRow.style.onChange = previewRow.shape.onChange = [this]
        {
            settings.previewStyle = previewRow.style.getSelectedId();
            settings.previewShape = previewRow.shape.getSelectedId() - 1;
            onChanged();
        };
        add (&previewRow, 28, "Preview column waveform");
        add (&previewColour, 28, "Preview single colour");
        previewColour.getProperties().set ("psyched.swatch", true);
        previewColour.setColour (juce::TextButton::buttonColourId, juce::Colour (settings.previewColour));
        previewColour.setButtonText ("CHOOSE COLOUR");
        previewColour.onClick = [this]
        {
            juce::Component::SafePointer<Content> safe (this);
            ColourPickerBubble::show (previewColour.getScreenBounds(), juce::Colour (settings.previewColour), [safe] (juce::Colour c)
            {
                if (safe == nullptr) return;
                safe->settings.previewColour = c.getARGB();
                safe->previewColour.setColour (juce::TextButton::buttonColourId, c);
                safe->onChanged();
            });
        };

        setupSlider (historySec, 5.0, 300.0, 1.0, settings.historySeconds);
        historySec.setTextValueSuffix (" s");
        historySec.setTooltip ("A track goes into PLAY HISTORY (and its play count goes up) once it has played this long");
        historySec.onValueChange = [this] { settings.historySeconds = historySec.getValue(); onChanged(); };
        add (&historySec, 28, "Counts as played after");

        addSection ("RECORD");
        add (&recFolder, 28, "Recordings folder");
        recFolder.setText (settings.recordingsFolder().getFullPathName(), false);
        recFolder.onReturnKey = recFolder.onFocusLost = [this] { settings.recordFolder = recFolder.getText().trim(); onChanged(); };
        add (&recFormat, 28, "Format");
        recFormat.addItemList ({ "WAV (24-bit)", "FLAC (24-bit, lossless, smaller)", "MP3 320 kbps (needs lame.exe)" }, 1);
        recFormat.setSelectedId (settings.recordFormat + 1, juce::dontSendNotification);
        recFormat.onChange = [this] { settings.recordFormat = recFormat.getSelectedId() - 1; onChanged(); };
        add (&lamePath, 28, "LAME encoder (MP3)");
        lamePath.setText (settings.lamePath, false);
        lamePath.setTextToShowWhenEmpty ("path to lame.exe - only needed for MP3", Theme::textFaint);
        lamePath.onReturnKey = lamePath.onFocusLost = [this] { settings.lamePath = lamePath.getText().trim(); onChanged(); };
        add (&recSource, 28, "Record from");
        recSource.setTooltip ("Centrifuge's mix is the decks summed (not the XDJ-XZ's EQ, faders or FX). To record exactly what the XDJ-XZ "
                              "plays, choose the input pair its master comes in on (its USB recording channels)");
        refreshRecordSources();
        recSource.onChange = [this]
        {
            const int id = recSource.getSelectedId();
            settings.recordSource = id <= 1 ? -1 : (id - 2) * 2;
            onChanged();
        };
        add (&recArmed, 28, {});
        recArmed.setButtonText ("Arm: the recording starts with the first sound (and runs until you press REC again)");
        recArmed.setToggleState (settings.recordArmed, juce::dontSendNotification);
        recArmed.onClick = [this] { settings.recordArmed = recArmed.getToggleState(); onChanged(); };
        add (&recCue, 28, {});
        recCue.setButtonText ("Make a .cue file (track markers for each track played)");
        recCue.setToggleState (settings.recordCue, juce::dontSendNotification);
        recCue.onClick = [this] { settings.recordCue = recCue.getToggleState(); onChanged(); };
        add (&recSplit, 28, {});
        recSplit.setButtonText ("Split into a file per track after stopping (numbered, named artist - title)");
        recSplit.setToggleState (settings.recordSplit, juce::dontSendNotification);
        recSplit.onClick = [this] { settings.recordSplit = recSplit.getToggleState(); onChanged(); };

        addSection ("PREP - WAVEFORMS");
        for (int i = 0; i < 2; ++i)
        {
            auto& row = prepLaneRows[(size_t) i];
            fillLaneStyles (row.style, i == 1);
            row.shape.addItemList ({ "Full waveform", "Half (bars grow up)", "Half (bars grow down)" }, 1);
            row.style.setSelectedId (settings.prepLaneStyle[(size_t) i] + 2, juce::dontSendNotification);
            row.shape.setSelectedId (settings.prepLaneShape[(size_t) i] + 1, juce::dontSendNotification);
            row.style.onChange = row.shape.onChange = [this, i]
            {
                auto& rw = prepLaneRows[(size_t) i];
                settings.prepLaneStyle[(size_t) i] = rw.style.getSelectedId() - 2;
                settings.prepLaneShape[(size_t) i] = rw.shape.getSelectedId() - 1;
                onChanged();
            };
            add (&row, 28, i == 0 ? "PREP top lane" : "PREP second lane");
        }
        prepOverviewRow.style.addItemList ({ "3-Band", "RGB", "Single colour" }, 1);
        prepOverviewRow.shape.addItemList ({ "Full waveform", "Half waveform" }, 1);
        prepOverviewRow.style.setSelectedId (settings.prepOverviewStyle, juce::dontSendNotification);
        prepOverviewRow.shape.setSelectedId (settings.prepOverviewShape + 1, juce::dontSendNotification);
        prepOverviewRow.style.onChange = prepOverviewRow.shape.onChange = [this]
        {
            settings.prepOverviewStyle = prepOverviewRow.style.getSelectedId();
            settings.prepOverviewShape = prepOverviewRow.shape.getSelectedId() - 1;
            onChanged();
        };
        add (&prepOverviewRow, 28, "PREP overview");
        add (&prepColour, 28, "PREP colour");
        prepColour.getProperties().set ("psyched.swatch", true);
        prepColour.setColour (juce::TextButton::buttonColourId, juce::Colour (settings.deckColours[2]));
        prepColour.setButtonText ("CHOOSE COLOUR  (or click the P badge on PREP)");
        prepColour.onClick = [this]
        {
            juce::Component::SafePointer<Content> safe (this);
            ColourPickerBubble::show (prepColour.getScreenBounds(), juce::Colour (settings.deckColours[2]), [safe] (juce::Colour c)
            {
                if (safe == nullptr) return;
                safe->settings.deckColours[2] = c.getARGB();
                safe->prepColour.setColour (juce::TextButton::buttonColourId, c);
                safe->onChanged();
            });
        };

        addSection ("STEM COLOURS AND LAYERS");
        add (&stemColourRow, 30, "Stem colours");
        for (int st = 0; st < 4; ++st)
        {
            auto& b = stemColourRow.buttons[(size_t) st];
            b.getProperties().set ("psyched.swatch", true);
            b.setButtonText (juce::String (stemName (st)).toUpperCase());
            b.setColour (juce::TextButton::buttonColourId, juce::Colour (settings.stemColours[(size_t) st]));
            b.onClick = [this, st]
            {
                juce::Component::SafePointer<Content> safe (this);
                ColourPickerBubble::show (stemColourRow.buttons[(size_t) st].getScreenBounds(), juce::Colour (settings.stemColours[(size_t) st]), [safe, st] (juce::Colour c)
                {
                    if (safe == nullptr) return;
                    safe->settings.stemColours[(size_t) st] = c.getARGB();
                    safe->stemColourRow.buttons[(size_t) st].setColour (juce::TextButton::buttonColourId, c);
                    safe->onChanged();
                });
            };
        }
        stemColourRow.reset.onClick = [this]
        {
            for (int st = 0; st < 4; ++st)
            {
                settings.stemColours[(size_t) st] = Theme::defaultStemColours()[(size_t) st];
                stemColourRow.buttons[(size_t) st].setColour (juce::TextButton::buttonColourId, juce::Colour (settings.stemColours[(size_t) st]));
            }
            onChanged();
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
        addSection ("CONTROLLER");
        add (&quantize, 28, {});
        quantize.setButtonText ("Quantize on at start-up (each deck's QUANTIZE button turns it on / off)");
        quantize.setToggleState (settings.quantize, juce::dontSendNotification);
        quantize.onClick = [this] { settings.quantize = quantize.getToggleState(); onChanged(); };
        add (&jogTicks, 28, "Jog ticks / revolution");
        add (&leds, 28, {});
        leds.setButtonText ("Send LED feedback to the XDJ-XZ");
        add (&padColoursOn, 28, {});
        padColoursOn.setButtonText ("Hot cue pads light in their cue's colour (XDJ-XZ)");
        padColoursOn.setToggleState (settings.hotCuePadColours, juce::dontSendNotification);
        padColoursOn.onClick = [this] { settings.hotCuePadColours = padColoursOn.getToggleState(); onChanged(); };
        add (&jogScreens, 28, {});
        jogScreens.setButtonText ("XDJ-XZ jog screens show the track turning and the cue point; LOAD lights on decks that aren't playing");
        jogScreens.setToggleState (settings.jogDisplay, juce::dontSendNotification);
        jogScreens.onClick = [this] { settings.jogDisplay = jogScreens.getToggleState(); onChanged(); };
        add (&longPress, 28, "Long press time (s)");
        setupSlider (longPress, 0.25, 1.0, 0.05, settings.longPressSec);
        longPress.onValueChange = [this] { settings.longPressSec = longPress.getValue(); onChanged(); };

        addSection ("STREAMING TO THE XDJ-XZ SCREEN");
        add (&xzScreenOn, 28, {});
        xzScreenOn.setButtonText ("Show Centrifuge's decks on the XDJ-XZ screen (network cable from the computer to LINK, deck started from the Centrifuge loader stick)");
        xzScreenOn.setToggleState (settings.xzScreen, juce::dontSendNotification);
        xzScreenOn.onClick = [this] { settings.xzScreen = xzScreenOn.getToggleState(); onChanged(); };
        add (&xzScreenStatus, 22, "Status");
        add (&xzLead, 28, "Screen catch-up (ms)");
        setupSlider (xzLead, 0.0, 400.0, 5.0, settings.xzScreenLeadMs);
        xzLead.setTooltip ("The picture takes a moment to reach the XZ: its waveforms are drawn this far ahead so they line up with what you hear. "
                           "Raise it if the screen is behind the audio, lower it if it's ahead.");
        xzLead.onValueChange = [this] { settings.xzScreenLeadMs = xzLead.getValue(); onChanged(); };
        add (&xzScreenNote, 44, {});
        xzScreenNote.setText ("The deck's STREAM / STEMS / MODS buttons are hidden while it streams. Map them to the XZ's buttons in Mapping > "
                              "XDJ-XZ screen (streaming on / off, stem controls, mods panel), along with the browser on the XZ screen.",
                              juce::dontSendNotification);
        xzScreenNote.setColour (juce::Label::textColourId, Theme::textDim);
        xzScreenNote.setFont (Theme::font (12.5f));
        xzScreenNote.setJustificationType (juce::Justification::topLeft);

        addSection ("VINYL");
        for (int d = 0; d < 2; ++d)
        {
            const auto D = "Deck " + juce::String (d + 1) + " ";
            auto& v = vinyl[(size_t) d];
            add (&v.brakeOn, 28, {});
            v.brakeOn.setButtonText (D + "brake: pause slows to a stop like a turntable");
            add (&v.brake, 28, D + "brake time (s)");
            add (&v.startOn, 28, {});
            v.startOn.setButtonText (D + "start: play comes up to speed like a turntable");
            add (&v.start, 28, D + "start time (s)");
            add (&v.release, 28, D + "release after a scratch (s)");
            v.brakeOn.setToggleState (settings.brakeOn[(size_t) d], juce::dontSendNotification);
            v.startOn.setToggleState (settings.startOn[(size_t) d], juce::dontSendNotification);
            setupSlider (v.brake, 0.0, 3.0, 0.05, settings.brakeSec[(size_t) d]);
            setupSlider (v.start, 0.0, 3.0, 0.05, settings.startSec[(size_t) d]);
            setupSlider (v.release, 0.0, 3.0, 0.05, settings.releaseSec[(size_t) d]);
            v.brake.onValueChange = [this, d] { settings.brakeSec[(size_t) d] = vinyl[(size_t) d].brake.getValue(); onChanged(); };
            v.start.onValueChange = [this, d] { settings.startSec[(size_t) d] = vinyl[(size_t) d].start.getValue(); onChanged(); };
            v.release.onValueChange = [this, d] { settings.releaseSec[(size_t) d] = vinyl[(size_t) d].release.getValue(); onChanged(); };
            v.brakeOn.onClick = [this, d] { settings.brakeOn[(size_t) d] = vinyl[(size_t) d].brakeOn.getToggleState(); onChanged(); };
            v.startOn.onClick = [this, d] { settings.startOn[(size_t) d] = vinyl[(size_t) d].startOn.getToggleState(); onChanged(); };
        }
        add (&throwOn, 28, {});
        throwOn.setButtonText ("Backspin / throw: a fast spin plays out after you let go of the platter");
        throwOn.setToggleState (settings.throwOn, juce::dontSendNotification);
        throwOn.onClick = [this] { settings.throwOn = throwOn.getToggleState(); onChanged(); };
        add (&throwSpeed, 28, "Throw: spin speed needed (x normal)");
        setupSlider (throwSpeed, 0.2, 3.0, 0.05, settings.throwSpeed);
        throwSpeed.onValueChange = [this] { settings.throwSpeed = throwSpeed.getValue(); onChanged(); };
        add (&autoGain, 28, {});
        autoGain.setButtonText ("Auto gain: match each loaded track's loudness to the master deck");
        autoGain.setToggleState (settings.autoGain, juce::dontSendNotification);
        autoGain.onClick = [this] { settings.autoGain = autoGain.getToggleState(); onChanged(); };

        addSection ("STEMS");
        add (&stemsOnLoad, 28, {});
        stemsOnLoad.setButtonText ("Separate stems automatically when a track is loaded");
        add (&stemsGpu, 28, {});
        stemsGpu.setButtonText ("Use the graphics card (DirectML) - much lower CPU use");
        add (&stemFormat, 28, "Saved stems");
        stemFormat.addItem ("Compressed (Ogg Vorbis 160 kbps - small)", 1);
        stemFormat.addItem ("Original quality (FLAC 24-bit, the track's own sample rate - roughly 8-10x bigger)", 2);
        stemFormat.setSelectedId (settings.stemsLossless ? 2 : 1, juce::dontSendNotification);
        stemFormat.onChange = [this] { settings.stemsLossless = stemFormat.getSelectedId() == 2; onChanged(); };
        add (&qualityOnLoad, 28, "Quality when loading");
        add (&qualityBatch, 28, "Quality when pre-analysing");
        for (auto* q : { &qualityOnLoad, &qualityBatch })
            q->addItemList ({ "Fast - one pass of HTDemucs (quickest)",
                              "Better - two offset passes averaged (about 2x the time)",
                              "High - fine-tuned specialist models for drums, bass and vocals (about 3x)",
                              "Best - RoFormer vocals + fine-tuned drums and bass (cleanest vocals, slowest)" }, 1);
        qualityOnLoad.setSelectedId (juce::jlimit (0, 3, settings.stemQualityOnLoad) + 1, juce::dontSendNotification);
        qualityBatch.setSelectedId (juce::jlimit (0, 3, settings.stemQualityBatch) + 1, juce::dontSendNotification);
        qualityOnLoad.onChange = [this] { settings.stemQualityOnLoad = qualityOnLoad.getSelectedId() - 1; modelsRow.refresh(); onChanged(); };
        qualityBatch.onChange = [this] { settings.stemQualityBatch = qualityBatch.getSelectedId() - 1; modelsRow.refresh(); onChanged(); };
        add (&modelsRow, 30, "Models for High / Best");
        add (&stemThreads, 28, "CPU threads (0 = auto)");
        add (&modelPath, 28, "Model file");
        add (&cachePath, 28, "Stem folder");
        addSection ("LIBRARY (MORE)");
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
        previewOut.onChange = [this] { settings.previewOutput = previewOut.getSelectedId() <= 1 ? -1 : previewOut.getSelectedId() - 2; onChanged(); };
        deck1Out.onChange = routeChanged;
        deck2Out.onChange = routeChanged;
        sumToMaster.onClick = routeChanged;

        refreshRouting();
    }

    ~Content() override { engine.getDeviceManager().removeChangeListener (this); }

    /** Values that can also be changed from the play page. */
    void refreshValues()
    {
        foldSkips.setToggleState (settings.editView[0] && settings.editView[1] && settings.editView[2], juce::dontSendNotification);
        playhead.setValue (settings.playheadPosition * 100.0, juce::dontSendNotification);
        zoom.setValue (settings.visibleSeconds, juce::dontSendNotification);
        prepColour.setColour (juce::TextButton::buttonColourId, juce::Colour (settings.deckColours[2]));
        for (int i = 0; i < 4; ++i)
            laneRows[(size_t) i].style.setSelectedId (settings.laneStyle[(size_t) i / 2][(size_t) i % 2] + 2, juce::dontSendNotification);
        for (int i = 0; i < 2; ++i)
            prepLaneRows[(size_t) i].style.setSelectedId (settings.prepLaneStyle[(size_t) i] + 2, juce::dontSendNotification);
        for (int d = 0; d < 2; ++d)
        {
            vinyl[(size_t) d].brakeOn.setToggleState (settings.brakeOn[(size_t) d], juce::dontSendNotification);
            vinyl[(size_t) d].startOn.setToggleState (settings.startOn[(size_t) d], juce::dontSendNotification);
        }
        repaint();
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
        previewOut.clear (juce::dontSendNotification);
        previewOut.addItem ("Automatic (the first output pair the decks don't use)", 1);
        for (int ch = 0; ch + 1 < outs; ch += 2)
        {
            juce::String label = "Outputs " + juce::String (ch + 1) + "/" + juce::String (ch + 2);
            if (ch + 1 < names.size()) label += "   (" + names[ch] + " / " + names[ch + 1] + ")";
            previewOut.addItem (label, ch + 2);
        }
        previewOut.setSelectedId (settings.previewOutput >= 0 ? settings.previewOutput + 2 : 1, juce::dontSendNotification);
        deck1Out.setSelectedId (engine.getDeckOutput (0) + 2, juce::dontSendNotification);
        deck2Out.setSelectedId (engine.getDeckOutput (1) + 2, juce::dontSendNotification);
        sumToMaster.setToggleState (engine.isSummingToMaster(), juce::dontSendNotification);
    }

    int layout (int width)
    {
        int y = 8;
        bool hidden = false;
        for (auto& row : rows)
        {
            if (row.comp == nullptr)
            {
                hidden = settings.collapsedSections.contains (row.label);
                row.labelBounds = { 0, y + 14, juce::jmin (816, width), 22 };
                y += row.label.startsWith ("PREP") || row.label.startsWith ("PLAY") ? 48 : 40;
                if (row.label.startsWith ("PREP") || row.label.startsWith ("PLAY")) row.labelBounds.translate (0, 8);
                continue;
            }
            row.comp->setVisible (! hidden);
            if (hidden) continue;
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

    /** Section colours: PREP's sections in PREP's colour, the play decks' in deck 1's, the rest in the accent. */
    juce::Colour sectionColour (const juce::String& title) const
    {
        if (title.startsWith ("PREP")) return Theme::deckColour (2);
        if (title.startsWith ("PLAY")) return Theme::deckColour (0);
        return Theme::accent;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // click a section's title to fold it away / open it
        for (auto& row : rows)
            if (row.comp == nullptr && row.labelBounds.expanded (0, 6).contains (e.getPosition()))
            {
                auto& c = settings.collapsedSections;
                if (c.contains (row.label)) c.removeString (row.label); else c.add (row.label);
                settings.save();
                setSize (getWidth(), layout (getWidth()));
                repaint();
                return;
            }
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        bool over = false;
        for (auto& row : rows) over = over || (row.comp == nullptr && row.labelBounds.expanded (0, 6).contains (e.getPosition()));
        setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void paint (juce::Graphics& g) override
    {
        bool hidden = false;
        for (auto& row : rows)
        {
            if (row.comp == nullptr)
            {
                hidden = settings.collapsedSections.contains (row.label);
                const auto col = sectionColour (row.label);
                auto lb = row.labelBounds.toFloat();
                // the fold arrow: pointing down when open, right when folded
                auto ar = lb.removeFromLeft (18.0f).withSizeKeepingCentre (10.0f, 10.0f);
                juce::Path tri;
                if (hidden) tri.addTriangle (ar.getX() + 1.0f, ar.getY(), ar.getX() + 1.0f, ar.getBottom(), ar.getRight(), ar.getCentreY());
                else tri.addTriangle (ar.getX(), ar.getY() + 1.0f, ar.getRight(), ar.getY() + 1.0f, ar.getCentreX(), ar.getBottom());
                g.setColour (col);
                g.fillPath (tri);
                psyched::paintTitle (g, row.label, lb, juce::Justification::centredLeft, col, 11.0f);
                const bool major = row.label.startsWith ("PREP") || row.label.startsWith ("PLAY");
                g.setColour (major ? col.withAlpha (0.7f) : Theme::line);
                g.fillRect (row.labelBounds.getX(), row.labelBounds.getBottom(), juce::jmin (816, getWidth()), major ? 2 : 1);
            }
            else if (hidden) continue;
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

    // The Stems page shows only the STEMS section; the Settings page everything else.
    bool stemsOnly = false;
    bool inStems = false;
    bool showing() const { return inStems == stemsOnly; }
    void addSection (const juce::String& title)
    {
        inStems = title == "STEMS" || title == "STEM COLOURS AND LAYERS";
        if (showing()) rows.push_back ({ nullptr, 0, title, {} });
    }
    void add (juce::Component* c, int h, const juce::String& label)
    {
        if (! showing()) return;
        addAndMakeVisible (c);
        rows.push_back ({ c, h, label, {} });
        if (auto* t = dynamic_cast<juce::TextEditor*> (c)) t->setFont (Theme::font (13.5f));
    }
    void setupSlider (juce::Slider& s, double lo, double hi, double step, double value)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setScrollWheelEnabled (false);   // the mouse wheel scrolls the page, never changes a value
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
        s.setRange (lo, hi, step);
        s.setValue (value, juce::dontSendNotification);
    }
    void refreshRecordSources()
    {
        recSource.clear (juce::dontSendNotification);
        recSource.addItem ("Centrifuge's mix (the decks summed)", 1);
        const auto pairs = engine.inputPairNames();
        for (int i = 0; i < pairs.size(); ++i) recSource.addItem (pairs[i], i + 2);
        const int want = settings.recordSource < 0 ? 1 : settings.recordSource / 2 + 2;
        if (want > 1 && want - 2 >= pairs.size()) recSource.addItem ("Input " + juce::String (settings.recordSource + 1) + "/" + juce::String (settings.recordSource + 2) + " (not on this device)", want);
        recSource.setSelectedId (want, juce::dontSendNotification);
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        settings.audioDeviceState = engine.saveDeviceState();
        refreshRecordSources();
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
    std::array<PairRow, 2> prepLaneRows;
    PairRow prepOverviewRow, previewRow;
    juce::TextButton previewColour;
    juce::TextButton prepColour;
    struct StemColourRow : juce::Component
    {
        std::array<juce::TextButton, 4> buttons;
        juce::TextButton reset { "RESET" };
        StemColourRow() { for (auto& b : buttons) addAndMakeVisible (b); addAndMakeVisible (reset); }
        void resized() override
        {
            auto r = getLocalBounds();
            reset.setBounds (r.removeFromRight (70));
            r.removeFromRight (8);
            const int w = (r.getWidth() - 18) / 4;
            for (auto& b : buttons) { b.setBounds (r.removeFromLeft (w)); r.removeFromLeft (6); }
        }
    } stemColourRow;
    static void fillLaneStyles (juce::ComboBox& c, bool allowOff)
    {
        // ids are style + 2 (style -1 = off)
        if (allowOff) c.addItem ("Off (one lane)", 1);
        c.addItem ("Stems (overlaid, follow the stem knobs)", 2);
        c.addItem ("Stems (a row each: drums, bass, inst, vocals)", 6);
        c.addItem ("3-Band", 3);
        c.addItem ("RGB", 4);
        c.addItem ("Single colour", 5);
        const char* one[] = { "Drums only", "Bass only", "Inst only", "Vocals only" };
        for (int s = 0; s < 4; ++s) c.addItem (one[s], 7 + s);
    }
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
    juce::ComboBox deck1Out, deck2Out, previewOut, fps, analysisRange, keyNotation, nameOrder;
    juce::ToggleButton sumToMaster, leds, stemsOnLoad, stemsGpu, foldSkips, quantize, followDeck;
    juce::TextEditor dbPath, dbKey, usbFolder, modelPath, cachePath;
    Resizer dbButtons;
    juce::ComboBox qualityOnLoad, qualityBatch;

    /** Shows whether the High / Best models are installed, and where to get them. The models are too big
        for the main download: they come as separate zips on the "models" release page. */
    struct ModelsRow : juce::Component, private juce::Timer
    {
        static constexpr const char* releasePage = "https://github.com/StevelKnievel88/Centrifuge/releases/tag/models";

        ModelsRow (AppSettings& s, std::function<void()> changed) : settings (s), onChanged (std::move (changed))
        {
            addAndMakeVisible (status);
            addAndMakeVisible (get);
            addAndMakeVisible (folder);
            status.setFont (psyched::Fonts::value (12.0f));
            get.setColour (juce::TextButton::buttonOnColourId, psyched::Colours::pink);
            get.onClick = [] { juce::URL (releasePage).launchInDefaultBrowser(); };
            folder.onClick = []
            {
                auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
                exeDir.startAsProcess();
            };
            get.setTooltip ("Opens the models page: download the zip(s) and extract them into the Centrifuge folder (they go into models)");
            folder.setTooltip ("Opens the Centrifuge folder - extract the model zips here");
            refresh();
            startTimer (3000);   // notices models as soon as they've been extracted
        }

        /** Files the chosen quality levels need that aren't installed. */
        juce::StringArray missing() const
        {
            juce::StringArray m;
            for (int level : { settings.stemQualityOnLoad, settings.stemQualityBatch })
                for (auto& f : StemModels::required ((StemQuality) juce::jlimit (0, 3, level)))
                    if (! settings.findModel (f).existsAsFile()) m.addIfNotAlreadyThere (f);
            return m;
        }

        void refresh()
        {
            const auto m = missing();
            if (m != lastMissing && ! lastMissing.isEmpty() && m.size() < lastMissing.size() && onChanged)
                onChanged();   // new models arrived: reload the stem engine
            lastMissing = m;

            const bool needFt = m.contains (StemModels::ftDrums) || m.contains (StemModels::ftBass) || m.contains (StemModels::ftVocals);
            const bool needRo = m.contains (StemModels::roformer);
            get.getProperties().set ("psyched.solid", ! m.isEmpty());
            get.setButtonText (m.isEmpty() ? "MODELS PAGE" : "GET MODELS");
            juce::String text;
            if (m.isEmpty()) text = "Installed: everything the chosen qualities need";
            else
            {
                text = "Needs ";
                if (needFt) text << "Centrifuge-Models-FineTuned.zip (about 450 MB)";
                if (needFt && needRo) text << " + ";
                if (needRo) text << "Centrifuge-Models-RoFormer.zip (about 850 MB)";
                text << " - until then the next level down is used";
            }
            status.setText (text, juce::dontSendNotification);
            repaint();
        }

        void timerCallback() override { refresh(); }
        void resized() override
        {
            auto r = getLocalBounds();
            folder.setBounds (r.removeFromRight (130));
            r.removeFromRight (6);
            get.setBounds (r.removeFromRight (130));
            r.removeFromRight (10);
            status.setBounds (r);
        }

        AppSettings& settings;
        std::function<void()> onChanged;
        juce::Label status;
        juce::TextButton get { "GET MODELS" }, folder { "OPEN FOLDER" };
        juce::StringArray lastMissing;
    } modelsRow { settings, [this] { onChanged(); } };
    juce::TextButton browseDb { "BROWSE..." }, reloadDb { "RELOAD LIBRARY" };
    juce::Slider zoom, jogTicks, stemThreads, mp3Offset, playhead;
    struct VinylRow { juce::ToggleButton brakeOn, startOn; juce::Slider brake, start, release; };
    std::array<VinylRow, 2> vinyl;
    juce::ToggleButton throwOn, autoGain;
    juce::Slider throwSpeed, markerSize, longPress, xzLead;
    juce::ComboBox stemFormat;
    juce::ToggleButton padColoursOn, jogScreens, xzScreenOn;
    juce::Label xzScreenNote;
    struct StatusLabel : juce::Label, juce::Timer
    {
        StatusLabel() { setColour (juce::Label::textColourId, Theme::textDim); setFont (Theme::mono (12.5f)); startTimerHz (2); }
        void timerCallback() override { if (SettingsView::xzScreenStatus) setText (SettingsView::xzScreenStatus(), juce::dontSendNotification); }
    } xzScreenStatus;
    juce::Slider historySec;
    juce::TextEditor recFolder, lamePath;
    juce::ComboBox recFormat, recSource;
    juce::ToggleButton recArmed, recCue, recSplit;
    std::unique_ptr<juce::FileChooser> chooser;
};

SettingsView::SettingsView (AppSettings& s, AudioEngine& e, std::function<void()> changed, std::function<void()> reload, bool stemsOnly)
{
    content = std::make_unique<Content> (s, e, std::move (changed), std::move (reload), stemsOnly);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

SettingsView::~SettingsView() = default;

void SettingsView::refreshRouting() { content->refreshRouting(); }
void SettingsView::refreshValues() { content->refreshValues(); }

void SettingsView::paint (juce::Graphics&) {}

int SettingsView::preferredHeight() const
{
    return content->layout (juce::jmax (400, getWidth() - 40)) + 20;
}

void SettingsView::resized()
{
    viewport.setBounds (getLocalBounds().reduced (20, 10));
    const int w = viewport.getWidth() - viewport.getScrollBarThickness() - 4;
    content->setSize (w, content->layout (w));
}

} // namespace orbit
