// StudioFros - Megaphone
// Copyright (C) GuitarFros. Licensed under the GNU AGPLv3 (see LICENSE).
#pragma once

#include "ParameterSpecs.h"

namespace studiofros
{

struct FactoryPreset
{
    const char* name;
    // Same order as ParamIndex, in real units (%, Hz, dB, choice index, 0/1)
    float values[kNumPresetParams];
};

// Mode: 0 = I, 1 = II.  NoiseSource: 0 = CLICK, 1 = BOTH, 2 = BURST.
inline constexpr FactoryPreset kFactoryPresets[] = {
    //                   drive  low   high   honk  fb   noise mix   out   mode nsrc comp
    { "Init",              { 40, 500, 4000,  50,   0,  20, 100,  0.0f, 0, 1, 1 } },
    { "Classic Bullhorn",  { 55, 500, 4000,  60,  10,  20, 100,  0.0f, 0, 1, 1 } },
    { "Police PA",         { 45, 400, 5000,  45,  25,  15, 100,  0.0f, 1, 1, 1 } },
    { "Protest Rally",     { 80, 550, 3800,  75,  15,  25, 100, -2.0f, 0, 1, 1 } },
    { "Stadium Announcer", { 35, 350, 5500,  40,  55,  10, 100,  0.0f, 1, 0, 1 } },
    { "Toy Megaphone",     { 60, 800, 6500,  70,   5,  30, 100, -2.0f, 0, 1, 0 } },
    { "Broken Speaker",    { 100, 600, 3000, 80,  20,  70, 100, -4.0f, 0, 2, 1 } },
    { "Walkie Crunch",     { 70, 700, 2800,  55,   0,  40, 100, -2.0f, 0, 2, 1 } },
    { "Street Preacher",   { 50, 450, 4200,  55,  30,  45, 100,  0.0f, 1, 2, 1 } },
    { "Feedback Squeal",   { 60, 500, 4000,  80,  85,  15, 100, -3.0f, 0, 0, 1 } },
    { "Subtle Grit",       { 30, 400, 6000,  30,   0,  10,  35,  0.0f, 1, 0, 0 } },
    { "Guitar Horn",       { 50, 300, 5000,  55,  15,   0,  70,  0.0f, 1, 0, 1 } },
};

inline constexpr int kNumFactoryPresets = (int) (sizeof (kFactoryPresets) / sizeof (kFactoryPresets[0]));

} // namespace studiofros
