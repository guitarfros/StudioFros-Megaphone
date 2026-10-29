// StudioFros - Megaphone
// Copyright (C) GuitarFros. Licensed under the GNU AGPLv3 (see LICENSE).
#include "MegaphoneDSP.h"

#include <algorithm>
#include <cmath>

namespace studiofros
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    inline double lerp (double a, double b, double t) { return a + (b - a) * t; }
    inline double clampd (double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

    // RBJ cookbook filters ---------------------------------------------------
    BiquadCoeffs makeLowpass (double sr, double f, double q)
    {
        f = clampd (f, 10.0, sr * 0.45);
        const double w0 = 2.0 * kPi * f / sr, cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q), a0 = 1.0 + alpha;
        BiquadCoeffs c;
        c.b0 = (1.0 - cw) * 0.5 / a0;
        c.b1 = (1.0 - cw) / a0;
        c.b2 = c.b0;
        c.a1 = -2.0 * cw / a0;
        c.a2 = (1.0 - alpha) / a0;
        return c;
    }

    BiquadCoeffs makeHighpass (double sr, double f, double q)
    {
        f = clampd (f, 10.0, sr * 0.45);
        const double w0 = 2.0 * kPi * f / sr, cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q), a0 = 1.0 + alpha;
        BiquadCoeffs c;
        c.b0 = (1.0 + cw) * 0.5 / a0;
        c.b1 = -(1.0 + cw) / a0;
        c.b2 = c.b0;
        c.a1 = -2.0 * cw / a0;
        c.a2 = (1.0 - alpha) / a0;
        return c;
    }

    BiquadCoeffs makePeak (double sr, double f, double q, double gainDb)
    {
        f = clampd (f, 10.0, sr * 0.45);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * f / sr, cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q), a0 = 1.0 + alpha / A;
        BiquadCoeffs c;
        c.b0 = (1.0 + alpha * A) / a0;
        c.b1 = -2.0 * cw / a0;
        c.b2 = (1.0 - alpha * A) / a0;
        c.a1 = -2.0 * cw / a0;
        c.a2 = (1.0 - alpha / A) / a0;
        return c;
    }

    inline double run (const BiquadCoeffs& c, BiquadState& s, double x)
    {
        const double y = c.b0 * x + s.z1;
        s.z1 = c.b1 * x - c.a1 * y + s.z2;
        s.z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    // xorshift32 white noise in [-1, 1)
    inline double whiteNoise (uint32_t& x)
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return (double) x / 2147483648.0 - 1.0;
    }
}

void MegaphoneEngine::prepare (double sampleRate, int numChannels)
{
    sampleRate_ = sampleRate > 1000.0 ? sampleRate : 44100.0;
    numChannels_ = std::max (1, std::min (numChannels, kMaxChannels));

    const int echoSize = (int) (sampleRate_ * 0.30) + 8;
    const int loopSize = (int) (sampleRate_ * 0.02) + 8;

    for (int c = 0; c < kMaxChannels; ++c)
    {
        channels_[c].echoLine.assign ((size_t) echoSize, 0.0f);
        channels_[c].loopLine.assign ((size_t) loopSize, 0.0f);
        channels_[c].rng = 0x1234567u + 7919u * (uint32_t) c;
    }

    envAttack_   = std::exp (-1.0 / (sampleRate_ * 0.005));
    envRelease_  = std::exp (-1.0 / (sampleRate_ * 0.300));
    compAttack_  = std::exp (-1.0 / (sampleRate_ * 0.003));
    compRelease_ = std::exp (-1.0 / (sampleRate_ * 0.090));

    echoHpC_  = makeHighpass (sampleRate_, 300.0, 0.7071);
    echoLpC_  = makeLowpass  (sampleRate_, 3200.0, 0.7071);
    noiseHpC_ = makeHighpass (sampleRate_, 500.0, 0.7071);
    noiseLpC_ = makeLowpass  (sampleRate_, 4500.0, 0.7071);
    dcC_      = makeHighpass (sampleRate_, 25.0, 0.7071);

    clickLength_ = (int) (sampleRate_ * 0.030);

    reset();
}

void MegaphoneEngine::reset()
{
    for (auto& ch : channels_)
    {
        ch.hp[0] = ch.hp[1] = ch.lp[0] = ch.lp[1] = {};
        ch.honk = ch.postLp = ch.dcBlock = ch.echoHp = ch.echoLp = ch.noiseHp = ch.noiseLp = {};
        std::fill (ch.echoLine.begin(), ch.echoLine.end(), 0.0f);
        std::fill (ch.loopLine.begin(), ch.loopLine.end(), 0.0f);
        ch.echoPos = ch.loopPos = 0;
    }

    inEnv_ = compEnv_ = 0.0;
    clickPos_ = 1 << 30;
    clickRequested_ = false;
    clickHp_ = {};
    firstUpdate_ = true;
}

void MegaphoneEngine::setParams (const EngineParams& p)
{
    EngineParams t = p;
    t.drive       = (float) clampd (t.drive, 0.0, 1.0);
    t.lowCutHz    = (float) clampd (t.lowCutHz, 20.0, 5000.0);
    t.highCutHz   = (float) clampd (t.highCutHz, 500.0, 18000.0);
    t.honk        = (float) clampd (t.honk, 0.0, 1.0);
    t.feedback    = (float) clampd (t.feedback, 0.0, 1.0);
    t.noise       = (float) clampd (t.noise, 0.0, 1.0);
    t.mix         = (float) clampd (t.mix, 0.0, 1.0);
    t.outputDb    = (float) clampd (t.outputDb, -60.0, 24.0);
    t.mode        = t.mode == 0 ? 0 : 1;
    t.noiseSource = (int) clampd (t.noiseSource, 0, 2);
    target_ = t;
}

void MegaphoneEngine::triggerClick()
{
    clickRequested_ = true;
}

double MegaphoneEngine::nextClick()
{
    if (clickPos_ >= clickLength_)
        return 0.0;

    const double t = (double) clickPos_ / sampleRate_;
    double n = whiteNoise (clickRng_);
    n = run (noiseHpC_, clickHp_, n);

    const double burst = n * std::exp (-t / 0.0012) * 1.6;
    const double ping  = std::sin (2.0 * kPi * 1700.0 * t) * std::exp (-t / 0.003) * 0.5;
    const double thump = std::sin (2.0 * kPi * 220.0 * t) * std::exp (-t / 0.006) * 0.4;

    ++clickPos_;
    return burst + ping + thump;
}

void MegaphoneEngine::updateSmoothed (int len)
{
    const EngineParams& t = target_;
    const double coef = 1.0 - std::exp (-(double) len / (sampleRate_ * 0.030));

    const double driveDbT = (double) t.drive * 34.0;
    const double outGainT = std::pow (10.0, (double) t.outputDb / 20.0);

    auto approach = [&] (double& v, double tgt)
    {
        if (firstUpdate_) v = tgt;
        else              v += (tgt - v) * coef;
    };

    approach (sDriveDb_, driveDbT);
    approach (sLow_, (double) t.lowCutHz);
    approach (sHigh_, (double) t.highCutHz);
    approach (sHonk_, (double) t.honk);
    approach (sFeedback_, (double) t.feedback);
    approach (sNoise_, (double) t.noise);
    approach (sMix_, (double) t.mix);
    approach (sOutGain_, outGainT);
    approach (sMode_, (double) t.mode);
    approach (sComp_, t.comp ? 1.0 : 0.0);
    firstUpdate_ = false;

    const double m = sMode_;

    // Band-pass (4th-order high-pass + 4th-order low-pass).
    // Mode I  : Butterworth, slightly resonant at the corners (harsh bullhorn).
    // Mode II : Linkwitz-Riley, wider and fuller (PA horn).
    const double low  = sLow_  * lerp (1.0, 0.7, m);
    const double high = sHigh_ * lerp (1.0, 1.25, m);
    const double q1 = lerp (0.5412, 0.5, m);
    const double q2 = lerp (1.3066, 0.5, m);

    hpC_[0] = makeHighpass (sampleRate_, low, q1);
    hpC_[1] = makeHighpass (sampleRate_, low, q2);
    lpC_[0] = makeLowpass  (sampleRate_, high, q1);
    lpC_[1] = makeLowpass  (sampleRate_, high, q2);

    // The "honk": horn resonance between 1.5 and 2.5 kHz
    honkC_ = makePeak (sampleRate_, lerp (2200.0, 1500.0, m), lerp (1.6, 1.0, m),
                       sHonk_ * lerp (12.0, 9.0, m));

    postLpC_ = makeLowpass (sampleRate_, high * 1.1, 0.7071);

    // Drive
    driveGain_ = std::pow (10.0, sDriveDb_ / 20.0);
    bias_ = lerp (0.18, 0.06, m) * std::min (1.0, (sDriveDb_ / 34.0) * 3.0);
    wetMakeup_ = 1.15 / std::pow (driveGain_, 0.45);

    // Echo / feedback
    echoDelaySamples_ = sampleRate_ * lerp (0.085, 0.130, m);
    loopDelaySamples_ = sampleRate_ * lerp (0.0032, 0.0046, m);
    echoLevel_   = sFeedback_ * 0.7;
    echoRecirc_  = sFeedback_ * 0.8;
    squealAmount_ = std::max (0.0, (sFeedback_ - 0.5) / 0.5) * 0.95;

    // Noise
    hissOn_  = t.noiseSource != 0;
    clickOn_ = t.noiseSource != 2;
    hissLevel_  = hissOn_  ? sNoise_ * 0.09 : 0.0;
    clickLevel_ = clickOn_ ? sNoise_ * 0.60 : 0.0;
}

float MegaphoneEngine::readLine (const std::vector<float>& line, int writePos, double delaySamples) const
{
    const int size = (int) line.size();
    double rp = (double) writePos - delaySamples;
    while (rp < 0.0) rp += (double) size;

    int i0 = (int) rp;
    const double fr = rp - (double) i0;
    if (i0 >= size) i0 -= size;
    int i1 = i0 + 1;
    if (i1 >= size) i1 = 0;

    return (float) ((double) line[(size_t) i0] * (1.0 - fr) + (double) line[(size_t) i1] * fr);
}

void MegaphoneEngine::process (float* const* ch, int numCh, int numSamples)
{
    numCh = std::min (numCh, numChannels_);
    if (numCh <= 0 || numSamples <= 0)
        return;

    if (clickRequested_)
    {
        clickPos_ = 0;
        clickRequested_ = false;
    }

    constexpr double kThreshold = 0.125;   // -18 dBFS
    constexpr double kCompMakeup = 1.6;

    int pos = 0;
    while (pos < numSamples)
    {
        const int len = std::min (kSubBlock, numSamples - pos);
        updateSmoothed (len);

        for (int i = 0; i < len; ++i)
        {
            const int idx = pos + i;

            // Linked detector
            double peak = 0.0;
            for (int c = 0; c < numCh; ++c)
                peak = std::max (peak, (double) std::fabs (ch[c][idx]));

            inEnv_   = peak + (peak > inEnv_ ? envAttack_ : envRelease_) * (inEnv_ - peak);
            compEnv_ = peak + (peak > compEnv_ ? compAttack_ : compRelease_) * (compEnv_ - peak);

            double compGain = kCompMakeup;
            if (compEnv_ > kThreshold)
                compGain = kCompMakeup * std::pow (kThreshold / compEnv_, 0.75);
            compGain = 1.0 + (compGain - 1.0) * sComp_;

            // Feedback "squeal" only engages while there is signal present,
            // so the effect never self-oscillates on silence.
            const double sqEnv = clampd ((inEnv_ - 0.002) * 20.0, 0.0, 1.0);
            const double squeal = squealAmount_ * sqEnv;

            const double hissAmp = hissLevel_ * (0.3 + 0.7 * std::min (1.0, inEnv_ * 4.0));
            const double click = nextClick() * clickLevel_;

            for (int c = 0; c < numCh; ++c)
            {
                Channel& chan = channels_[c];
                const double dry = (double) ch[c][idx];

                double s = dry * compGain;

                // Short acoustic loop (speaker -> microphone)
                s += squeal * (double) readLine (chan.loopLine, chan.loopPos, loopDelaySamples_);

                s = run (hpC_[0], chan.hp[0], s);
                s = run (hpC_[1], chan.hp[1], s);
                s = run (lpC_[0], chan.lp[0], s);
                s = run (lpC_[1], chan.lp[1], s);
                s = run (honkC_, chan.honk, s);

                double d = std::tanh (s * driveGain_ + bias_) - std::tanh (bias_);

                chan.loopLine[(size_t) chan.loopPos] = (float) d;
                if (++chan.loopPos >= (int) chan.loopLine.size())
                    chan.loopPos = 0;

                d = run (postLpC_, chan.postLp, d);
                d = run (dcC_, chan.dcBlock, d);

                double wet = d * wetMakeup_;

                // Slapback echo
                double e = (double) readLine (chan.echoLine, chan.echoPos, echoDelaySamples_);
                e = run (echoHpC_, chan.echoHp, e);
                e = run (echoLpC_, chan.echoLp, e);

                chan.echoLine[(size_t) chan.echoPos] = (float) (2.0 * std::tanh (0.5 * (wet + echoRecirc_ * e)));
                if (++chan.echoPos >= (int) chan.echoLine.size())
                    chan.echoPos = 0;

                wet += echoLevel_ * e;

                // Noise: loudspeaker hiss (follows the input) + button click
                if (hissAmp > 0.0)
                {
                    double w = whiteNoise (chan.rng);
                    w = run (noiseHpC_, chan.noiseHp, w);
                    w = run (noiseLpC_, chan.noiseLp, w);
                    wet += w * hissAmp * 1.5;
                }
                wet += click;

                const double out = (dry * (1.0 - sMix_) + wet * sMix_) * sOutGain_;
                ch[c][idx] = (float) out;
            }
        }

        pos += len;
    }
}

} // namespace studiofros
