// StudioFros - Megaphone
// Copyright (C) GuitarFros. Licensed under the GNU AGPLv3 (see LICENSE).
//
// Single source of truth for the plugin parameters. Plain C++ (no JUCE),
// so it is shared by the plugin, the presets and the unit tests.
#pragma once

namespace studiofros
{

enum class ParamKind { Float, Choice, Bool };

struct ParamSpec
{
    const char* id;       // stable identifier (never change it once released)
    const char* name;     // text shown in the DAW
    ParamKind kind;
    float min;
    float max;
    float def;            // default (for Choice: index)
    float skewCentre;     // > 0 : value shown in the middle of the knob travel
    const char* unit;     // suffix shown next to the value
    const char* const* choices;
    int numChoices;
};

// Order matters: it is the order used by presets.
enum ParamIndex
{
    pDrive = 0,
    pLowCut,
    pHighCut,
    pHonk,
    pFeedback,
    pNoise,
    pMix,
    pOutput,
    pMode,
    pNoiseSource,
    pComp,
    kNumPresetParams
};

inline constexpr const char* kModeChoices[]        = { "I", "II" };
inline constexpr const char* kNoiseSourceChoices[] = { "CLICK", "BOTH", "BURST" };

inline constexpr const char* kBypassId  = "bypass";
inline constexpr const char* kTriggerId = "trigger";

inline constexpr ParamSpec kParamSpecs[kNumPresetParams] = {
    // id           name         kind               min      max     def     centre  unit   choices  n
    { "drive",      "Drive",      ParamKind::Float,   0.0f,  100.0f,  40.0f,   0.0f, " %",  nullptr, 0 },
    { "lowcut",     "Low Cut",    ParamKind::Float, 200.0f, 1000.0f, 500.0f, 500.0f, " Hz", nullptr, 0 },
    { "highcut",    "High Cut",   ParamKind::Float, 2000.0f, 8000.0f, 4000.0f, 4000.0f, " Hz", nullptr, 0 },
    { "honk",       "Honk",       ParamKind::Float,   0.0f,  100.0f,  50.0f,   0.0f, " %",  nullptr, 0 },
    { "feedback",   "Feedback",   ParamKind::Float,   0.0f,  100.0f,   0.0f,   0.0f, " %",  nullptr, 0 },
    { "noise",      "Noise",      ParamKind::Float,   0.0f,  100.0f,  20.0f,   0.0f, " %",  nullptr, 0 },
    { "mix",        "Mix",        ParamKind::Float,   0.0f,  100.0f, 100.0f,   0.0f, " %",  nullptr, 0 },
    { "output",     "Output",     ParamKind::Float, -24.0f,   12.0f,   0.0f,   0.0f, " dB", nullptr, 0 },
    { "mode",       "Mode",       ParamKind::Choice,  0.0f,    1.0f,   0.0f,   0.0f, "",    kModeChoices, 2 },
    { "noisesrc",   "Noise Source", ParamKind::Choice, 0.0f,   2.0f,   1.0f,   0.0f, "",    kNoiseSourceChoices, 3 },
    { "comp",       "Comp",       ParamKind::Bool,    0.0f,    1.0f,   1.0f,   0.0f, "",    nullptr, 0 },
};

} // namespace studiofros
