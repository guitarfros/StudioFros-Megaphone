// StudioFros - Megaphone
// Copyright (C) GuitarFros. Licensed under the GNU AGPLv3 (see LICENSE).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "FactoryPresets.h"

namespace studiofros
{

// Factory presets live in the code (read-only). User presets are small XML
// files (*.sfpreset) in the user's application-data folder, so they survive
// plugin updates and can be shared by copying the files.
class PresetManager
{
public:
    struct Entry
    {
        juce::String name;
        bool factory = true;
    };

    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    static juce::File getUserFolder();
    static constexpr const char* kExtension = ".sfpreset";

    std::vector<Entry> getAllPresets() const;   // factory first, then user (alphabetical)

    bool load (const Entry& entry);
    void loadInit();
    bool saveUser (const juce::String& name);   // creates or overwrites a user preset
    bool deleteUser (const juce::String& name);

    juce::String getCurrentName() const noexcept { return currentName; }
    bool isCurrentFactory() const noexcept { return currentFactory; }
    bool isModified() const;

    // Name is kept inside the plugin state so the DAW project remembers it.
    void syncFromState();

    static juce::String sanitiseName (const juce::String& raw);

private:
    void applyValues (const float* values);
    void currentValues (float* out) const;
    bool readUserFile (const juce::String& name, float* out) const;
    void setCurrent (const juce::String& name, bool factory, const float* reference);
    static int findFactory (const juce::String& name);

    juce::AudioProcessorValueTreeState& apvts;
    juce::String currentName { "Init" };
    bool currentFactory = true;
    float reference[kNumPresetParams] {};
};

} // namespace studiofros
