// StudioFros - Megaphone
// Copyright (C) GuitarFros. Licensed under the GNU AGPLv3 (see LICENSE).
#pragma once

#include "PluginProcessor.h"

class MegaphonePanel;

// Thin wrapper: the real UI lives in MegaphonePanel, drawn at a fixed design
// size and scaled to fit the window. When the final artwork arrives only the
// panel needs to change.
class MegaphoneEditor : public juce::AudioProcessorEditor
{
public:
    explicit MegaphoneEditor (MegaphoneProcessor&);
    ~MegaphoneEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

private:
    std::unique_ptr<MegaphonePanel> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MegaphoneEditor)
};
