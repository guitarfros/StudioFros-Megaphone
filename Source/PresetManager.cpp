// StudioFros - Megaphone
// Copyright (C) GuitarFros. Licensed under the GNU AGPLv3 (see LICENSE).
#include "PresetManager.h"

namespace studiofros
{

namespace
{
    const juce::Identifier kPresetNameProp ("presetName");
    const juce::Identifier kPresetFactoryProp ("presetFactory");
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    for (int i = 0; i < kNumPresetParams; ++i)
        reference[i] = kParamSpecs[i].def;
}

juce::File PresetManager::getUserFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif

    return dir.getChildFile ("GuitarFros")
              .getChildFile ("StudioFros Megaphone")
              .getChildFile ("Presets");
}

juce::String PresetManager::sanitiseName (const juce::String& raw)
{
    auto s = juce::File::createLegalFileName (raw.trim()).trim();
    if (s.length() > 40)
        s = s.substring (0, 40).trim();
    return s;
}

int PresetManager::findFactory (const juce::String& name)
{
    for (int i = 0; i < kNumFactoryPresets; ++i)
        if (name.equalsIgnoreCase (kFactoryPresets[i].name))
            return i;
    return -1;
}

std::vector<PresetManager::Entry> PresetManager::getAllPresets() const
{
    std::vector<Entry> list;

    for (int i = 0; i < kNumFactoryPresets; ++i)
        list.push_back ({ kFactoryPresets[i].name, true });

    auto files = getUserFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + kExtension);

    juce::StringArray names;
    for (auto& f : files)
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();

    for (auto& n : names)
        list.push_back ({ n, false });

    return list;
}

void PresetManager::applyValues (const float* values)
{
    for (int i = 0; i < kNumPresetParams; ++i)
    {
        if (auto* p = apvts.getParameter (kParamSpecs[i].id))
        {
            const float v = juce::jlimit (kParamSpecs[i].min, kParamSpecs[i].max, values[i]);
            p->setValueNotifyingHost (p->convertTo0to1 (v));
        }
    }
}

void PresetManager::currentValues (float* out) const
{
    for (int i = 0; i < kNumPresetParams; ++i)
    {
        if (auto* p = apvts.getParameter (kParamSpecs[i].id))
            out[i] = p->convertFrom0to1 (p->getValue());
        else
            out[i] = kParamSpecs[i].def;
    }
}

void PresetManager::setCurrent (const juce::String& name, bool factory, const float* ref)
{
    currentName = name;
    currentFactory = factory;
    for (int i = 0; i < kNumPresetParams; ++i)
        reference[i] = ref[i];

    apvts.state.setProperty (kPresetNameProp, name, nullptr);
    apvts.state.setProperty (kPresetFactoryProp, factory, nullptr);
}

bool PresetManager::readUserFile (const juce::String& name, float* out) const
{
    auto file = getUserFolder().getChildFile (name + kExtension);
    if (! file.existsAsFile())
        return false;

    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName ("StudioFrosPreset"))
        return false;

    // start from defaults so presets from older versions still load
    for (int i = 0; i < kNumPresetParams; ++i)
        out[i] = kParamSpecs[i].def;

    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
    {
        const auto id = e->getStringAttribute ("id");
        for (int i = 0; i < kNumPresetParams; ++i)
            if (id == kParamSpecs[i].id)
                out[i] = juce::jlimit (kParamSpecs[i].min, kParamSpecs[i].max, (float) e->getDoubleAttribute ("value", kParamSpecs[i].def));
    }
    return true;
}

bool PresetManager::load (const Entry& entry)
{
    float values[kNumPresetParams];

    if (entry.factory)
    {
        const int idx = findFactory (entry.name);
        if (idx < 0)
            return false;
        for (int i = 0; i < kNumPresetParams; ++i)
            values[i] = kFactoryPresets[idx].values[i];
        applyValues (values);
        setCurrent (kFactoryPresets[idx].name, true, values);
        return true;
    }

    if (! readUserFile (entry.name, values))
        return false;

    applyValues (values);
    setCurrent (entry.name, false, values);
    return true;
}

void PresetManager::loadInit()
{
    load ({ kFactoryPresets[0].name, true });
}

bool PresetManager::saveUser (const juce::String& rawName)
{
    auto name = sanitiseName (rawName);
    if (name.isEmpty())
        return false;

    // never shadow a factory preset
    if (findFactory (name) >= 0)
        name = name + " (user)";

    float values[kNumPresetParams];
    currentValues (values);

    juce::XmlElement root ("StudioFrosPreset");
    root.setAttribute ("name", name);
    root.setAttribute ("version", 1);

    for (int i = 0; i < kNumPresetParams; ++i)
    {
        auto* e = root.createNewChildElement ("PARAM");
        e->setAttribute ("id", kParamSpecs[i].id);
        e->setAttribute ("value", (double) values[i]);
    }

    auto folder = getUserFolder();
    if (! folder.createDirectory().wasOk())
        return false;

    if (! folder.getChildFile (name + kExtension).replaceWithText (root.toString()))
        return false;

    setCurrent (name, false, values);
    return true;
}

bool PresetManager::deleteUser (const juce::String& name)
{
    auto file = getUserFolder().getChildFile (name + kExtension);
    if (! file.existsAsFile())
        return false;

    const bool ok = file.deleteFile();
    if (ok && ! currentFactory && currentName == name)
    {
        // keep the sound as it is, but it no longer belongs to a saved preset
        setCurrent ("Init", true, kFactoryPresets[0].values);
    }
    return ok;
}

bool PresetManager::isModified() const
{
    float now[kNumPresetParams];
    currentValues (now);

    for (int i = 0; i < kNumPresetParams; ++i)
    {
        const float range = kParamSpecs[i].max - kParamSpecs[i].min;
        if (std::abs (now[i] - reference[i]) > range * 0.002f)
            return true;
    }
    return false;
}

void PresetManager::syncFromState()
{
    const auto name = apvts.state.getProperty (kPresetNameProp, "Init").toString();
    const bool factory = (bool) apvts.state.getProperty (kPresetFactoryProp, true);

    float values[kNumPresetParams];

    if (factory)
    {
        const int idx = findFactory (name);
        if (idx >= 0)
        {
            for (int i = 0; i < kNumPresetParams; ++i)
                values[i] = kFactoryPresets[idx].values[i];
            currentName = kFactoryPresets[idx].name;
            currentFactory = true;
            for (int i = 0; i < kNumPresetParams; ++i)
                reference[i] = values[i];
            return;
        }
    }
    else if (readUserFile (name, values))
    {
        currentName = name;
        currentFactory = false;
        for (int i = 0; i < kNumPresetParams; ++i)
            reference[i] = values[i];
        return;
    }

    // preset not found (e.g. project opened on another computer):
    // keep the name, but treat the current sound as its reference
    currentValues (values);
    currentName = name;
    currentFactory = factory;
    for (int i = 0; i < kNumPresetParams; ++i)
        reference[i] = values[i];
}

} // namespace studiofros
