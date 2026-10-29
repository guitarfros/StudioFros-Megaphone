// StudioFros - Megaphone
// Copyright (C) GuitarFros. Licensed under the GNU AGPLv3 (see LICENSE).
//
// Audio engine. Plain C++17 with no JUCE dependency, so it can be unit tested
// on its own.
//
// Signal flow (per channel):
//
//   in -> [compressor] -> (+ squeal loop) -> HP x2 -> LP x2 -> HONK peak ->
//         DRIVE (asymmetric tanh) -> post low-pass + DC block -> wet
//   wet -> [slapback echo with feedback] -> + noise (hiss / button click)
//   mix(dry, wet) -> output gain
#pragma once

#include <cstdint>
#include <vector>

namespace studiofros
{

struct EngineParams
{
    float drive       = 0.4f;    // 0..1
    float lowCutHz    = 500.0f;
    float highCutHz   = 4000.0f;
    float honk        = 0.5f;    // 0..1
    float feedback    = 0.0f;    // 0..1
    float noise       = 0.2f;    // 0..1
    float mix         = 1.0f;    // 0..1
    float outputDb    = 0.0f;
    int   mode        = 0;       // 0 = I (bullhorn), 1 = II (PA horn)
    int   noiseSource = 1;       // 0 = click, 1 = both, 2 = burst
    bool  comp        = true;
};

struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

struct BiquadState
{
    double z1 = 0.0, z2 = 0.0;
};

class MegaphoneEngine
{
public:
    MegaphoneEngine() = default;

    void prepare (double sampleRate, int numChannels);
    void reset();

    // Thread-safety: call from the audio thread only (or while not processing).
    void setParams (const EngineParams& p);
    void triggerClick();

    void process (float* const* channels, int numChannels, int numSamples);

    static constexpr int kMaxChannels = 2;

private:
    static constexpr int kSubBlock = 32;

    struct Channel
    {
        BiquadState hp[2], lp[2], honk, postLp, dcBlock, echoHp, echoLp, noiseHp, noiseLp;
        std::vector<float> echoLine, loopLine;
        int echoPos = 0, loopPos = 0;
        uint32_t rng = 0x1234567u;
    };

    void updateSmoothed (int blockLength);
    float readLine (const std::vector<float>& line, int writePos, double delaySamples) const;
    double nextClick();

    double sampleRate_ = 44100.0;
    int numChannels_ = 2;

    EngineParams target_;
    Channel channels_[kMaxChannels];

    // smoothed values
    double sDriveDb_ = 13.0, sLow_ = 500.0, sHigh_ = 4000.0, sHonk_ = 0.5, sFeedback_ = 0.0;
    double sNoise_ = 0.2, sMix_ = 1.0, sOutGain_ = 1.0, sMode_ = 0.0, sComp_ = 1.0;
    bool firstUpdate_ = true;

    // coefficients, recomputed every sub-block
    BiquadCoeffs hpC_[2], lpC_[2], honkC_, postLpC_, dcC_, echoHpC_, echoLpC_, noiseHpC_, noiseLpC_;
    double driveGain_ = 1.0, bias_ = 0.0, wetMakeup_ = 1.0;
    double echoDelaySamples_ = 4000.0, loopDelaySamples_ = 150.0;
    double echoLevel_ = 0.0, echoRecirc_ = 0.0, squealAmount_ = 0.0;
    double hissLevel_ = 0.0, clickLevel_ = 0.0;
    bool hissOn_ = true, clickOn_ = true;

    // envelopes (linked across channels)
    double inEnv_ = 0.0, compEnv_ = 0.0;
    double envAttack_ = 0.0, envRelease_ = 0.0, compAttack_ = 0.0, compRelease_ = 0.0;

    // click generator
    int clickPos_ = 1 << 30;
    int clickLength_ = 0;
    uint32_t clickRng_ = 0x9e3779b9u;
    BiquadState clickHp_;
    bool clickRequested_ = false;
};

} // namespace studiofros
