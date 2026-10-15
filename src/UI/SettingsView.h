#pragma once
#include "../App/AppSettings.h"
#include "../Audio/AudioEngine.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace orbit
{

class SettingsView : public juce::Component
{
public:
    SettingsView (AppSettings& settings, AudioEngine& engine, std::function<void()> onChanged,
                  std::function<void()> onReloadLibrary);
    ~SettingsView() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void refreshRouting();
    void refreshValues();

private:
    class Content;
    std::unique_ptr<Content> content;
    juce::Viewport viewport;
};

} // namespace orbit
