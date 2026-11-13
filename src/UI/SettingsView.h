#pragma once
#include "../App/AppSettings.h"
#include "../Audio/AudioEngine.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace orbit
{

class SettingsView : public juce::Component
{
public:
    /** stemsOnly: just the STEMS section (shown on the Stems page); otherwise everything else. */
    SettingsView (AppSettings& settings, AudioEngine& engine, std::function<void()> onChanged,
                  std::function<void()> onReloadLibrary, bool stemsOnly = false);
    int preferredHeight() const;
    ~SettingsView() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void refreshRouting();
    void refreshValues();
    /** Laptop mode: the XDJ-XZ screen link's status, shown under its switch. */
    inline static std::function<juce::String()> xzScreenStatus;

private:
    class Content;
    std::unique_ptr<Content> content;
    juce::Viewport viewport;
};

} // namespace orbit
