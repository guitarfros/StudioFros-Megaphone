// StudioFros - Megaphone
// Copyright (C) GuitarFros. Licensed under the GNU AGPLv3 (see LICENSE).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "MegaphoneDSP.h"
#include "ParameterSpecs.h"
#include "PresetManager.h"

class MegaphoneProcessor : public juce::AudioProcessor
{
public:
    MegaphoneProcessor();
    ~MegaphoneProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;
    studiofros::PresetManager presets;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    studiofros::MegaphoneEngine engine;

    std::atomic<float>* raw[studiofros::kNumPresetParams] {};
    juce::AudioParameterBool* bypassParam = nullptr;
    juce::AudioParameterBool* triggerParam = nullptr;

    juce::AudioBuffer<float> dryBuffer;
    bool lastTrigger = false;
    bool lastBypass = false;
    bool engineIdle = false;
    float bypassMix = 0.0f;          // 0 = effect, 1 = bypassed (smoothed)
    float bypassStep = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MegaphoneProcessor)
};
