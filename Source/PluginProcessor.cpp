// StudioFros - Megaphone
// Copyright (C) GuitarFros. Licensed under the GNU AGPLv3 (see LICENSE).
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace studiofros;

juce::AudioProcessorValueTreeState::ParameterLayout MegaphoneProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    for (int i = 0; i < kNumPresetParams; ++i)
    {
        const auto& s = kParamSpecs[i];
        const juce::ParameterID pid { s.id, 1 };

        switch (s.kind)
        {
            case ParamKind::Float:
            {
                juce::NormalisableRange<float> range (s.min, s.max);
                if (s.skewCentre > 0.0f)
                    range.setSkewForCentre (s.skewCentre);

                params.push_back (std::make_unique<juce::AudioParameterFloat> (
                    pid, s.name, range, s.def,
                    juce::AudioParameterFloatAttributes().withLabel (juce::String (s.unit).trim())));
                break;
            }

            case ParamKind::Choice:
            {
                juce::StringArray choices;
                for (int c = 0; c < s.numChoices; ++c)
                    choices.add (s.choices[c]);
                params.push_back (std::make_unique<juce::AudioParameterChoice> (pid, s.name, choices, (int) s.def));
                break;
            }

            case ParamKind::Bool:
                params.push_back (std::make_unique<juce::AudioParameterBool> (pid, s.name, s.def > 0.5f));
                break;
        }
    }

    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kBypassId, 1 }, "Bypass", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kTriggerId, 1 }, "Trigger", false));

    return { params.begin(), params.end() };
}

MegaphoneProcessor::MegaphoneProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout()),
      presets (apvts)
{
    for (int i = 0; i < kNumPresetParams; ++i)
        raw[i] = apvts.getRawParameterValue (kParamSpecs[i].id);

    bypassParam  = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (kBypassId));
    triggerParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (kTriggerId));

    apvts.state.setProperty ("presetName", "Init", nullptr);
    apvts.state.setProperty ("presetFactory", true, nullptr);
}

bool MegaphoneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void MegaphoneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    dryBuffer.setSize (2, juce::jmax (samplesPerBlock, 512));
    bypassStep = (float) (1.0 / (0.010 * sampleRate));    // 10 ms crossfade
    lastTrigger = false;
    lastBypass = bypassParam != nullptr && bypassParam->get();
    bypassMix = lastBypass ? 1.0f : 0.0f;
    engineIdle = false;
}

void MegaphoneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, numSamples);

    if (numCh <= 0 || numSamples <= 0)
        return;

    // ---- parameters -> engine --------------------------------------------
    EngineParams p;
    p.drive       = raw[pDrive]->load() / 100.0f;
    p.lowCutHz    = raw[pLowCut]->load();
    p.highCutHz   = raw[pHighCut]->load();
    p.honk        = raw[pHonk]->load() / 100.0f;
    p.feedback    = raw[pFeedback]->load() / 100.0f;
    p.noise       = raw[pNoise]->load() / 100.0f;
    p.mix         = raw[pMix]->load() / 100.0f;
    p.outputDb    = raw[pOutput]->load();
    p.mode        = (int) (raw[pMode]->load() + 0.5f);
    p.noiseSource = (int) (raw[pNoiseSource]->load() + 0.5f);
    p.comp        = raw[pComp]->load() > 0.5f;
    engine.setParams (p);

    // Momentary TRIGGER button: rising edge -> button click
    const bool trig = triggerParam != nullptr && triggerParam->get();
    if (trig && ! lastTrigger)
        engine.triggerClick();
    lastTrigger = trig;

    // Bypass (the "microphone button" also clicks when the effect turns on)
    const bool byp = bypassParam != nullptr && bypassParam->get();
    if (lastBypass && ! byp)
    {
        engine.triggerClick();
        engineIdle = false;
    }
    lastBypass = byp;

    const float bypTarget = byp ? 1.0f : 0.0f;
    if (bypassMix == bypTarget && byp)
    {
        // fully bypassed: leave the audio untouched and clear the engine once
        if (! engineIdle)
        {
            engine.reset();
            engineIdle = true;
        }
        return;
    }

    // ---- processing --------------------------------------------------------
    const bool needCrossfade = (bypassMix != bypTarget);

    if (needCrossfade)
    {
        if (dryBuffer.getNumSamples() < numSamples)
            dryBuffer.setSize (2, numSamples, false, false, true);

        for (int c = 0; c < numCh; ++c)
            dryBuffer.copyFrom (c, 0, buffer, c, 0, numSamples);
    }

    engine.process (buffer.getArrayOfWritePointers(), numCh, numSamples);

    if (needCrossfade)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            if (bypassMix < bypTarget)      bypassMix = juce::jmin (bypTarget, bypassMix + bypassStep);
            else if (bypassMix > bypTarget) bypassMix = juce::jmax (bypTarget, bypassMix - bypassStep);

            for (int c = 0; c < numCh; ++c)
            {
                auto* out = buffer.getWritePointer (c);
                const float dry = dryBuffer.getReadPointer (c)[i];
                out[i] = out[i] * (1.0f - bypassMix) + dry * bypassMix;
            }
        }
    }
}

juce::AudioProcessorEditor* MegaphoneProcessor::createEditor()
{
    return new MegaphoneEditor (*this);
}

void MegaphoneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    apvts.state.setProperty ("presetName", presets.getCurrentName(), nullptr);
    apvts.state.setProperty ("presetFactory", presets.isCurrentFactory(), nullptr);

    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MegaphoneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            presets.syncFromState();
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MegaphoneProcessor();
}
