// StudioFros - Megaphone
// Copyright (C) GuitarFros. Licensed under the GNU AGPLv3 (see LICENSE).
//
// Stand-alone unit tests for the DSP engine and the preset tables.
// No framework needed: returns non-zero if any check fails.
#include "../Source/FactoryPresets.h"
#include "../Source/MegaphoneDSP.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace studiofros;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                                  \
    do {                                                                  \
        ++g_checks;                                                       \
        if (! (cond)) {                                                   \
            ++g_failures;                                                 \
            std::printf ("  FAIL  %s:%d  %s  -> ", __FILE__, __LINE__, #cond); \
            std::printf (__VA_ARGS__);                                    \
            std::printf ("\n");                                           \
        }                                                                 \
    } while (0)

static const double kPi = 3.14159265358979323846;

static EngineParams flatParams()
{
    EngineParams p;
    p.drive = 0.0f; p.honk = 0.0f; p.feedback = 0.0f; p.noise = 0.0f;
    p.mix = 1.0f; p.outputDb = 0.0f; p.comp = false; p.mode = 0; p.noiseSource = 1;
    p.lowCutHz = 500.0f; p.highCutHz = 4000.0f;
    return p;
}

static EngineParams fromPreset (const FactoryPreset& f)
{
    EngineParams p;
    p.drive = f.values[pDrive] / 100.0f;
    p.lowCutHz = f.values[pLowCut];
    p.highCutHz = f.values[pHighCut];
    p.honk = f.values[pHonk] / 100.0f;
    p.feedback = f.values[pFeedback] / 100.0f;
    p.noise = f.values[pNoise] / 100.0f;
    p.mix = f.values[pMix] / 100.0f;
    p.outputDb = f.values[pOutput];
    p.mode = (int) f.values[pMode];
    p.noiseSource = (int) f.values[pNoiseSource];
    p.comp = f.values[pComp] > 0.5f;
    return p;
}

static double rms (const std::vector<float>& v, size_t from = 0, size_t to = 0)
{
    if (to == 0 || to > v.size()) to = v.size();
    double acc = 0.0;
    for (size_t i = from; i < to; ++i) acc += (double) v[i] * v[i];
    return std::sqrt (acc / (double) (to - from));
}

static bool allFinite (const std::vector<float>& v)
{
    for (float x : v) if (! std::isfinite (x)) return false;
    return true;
}

static double maxAbs (const std::vector<float>& v)
{
    double m = 0.0;
    for (float x : v) m = std::max (m, (double) std::fabs (x));
    return m;
}

// Gain in dB of a sine through the engine (steady state)
static double sineGainDb (const EngineParams& p, double freq, double sr = 48000.0, double amp = 0.05)
{
    MegaphoneEngine e;
    e.prepare (sr, 1);
    e.setParams (p);

    const int n = (int) (sr * 1.0);
    std::vector<float> buf ((size_t) n);
    for (int i = 0; i < n; ++i) buf[(size_t) i] = (float) (amp * std::sin (2.0 * kPi * freq * i / sr));

    float* ch[1] = { buf.data() };
    e.process (ch, 1, n);

    const double outRms = rms (buf, (size_t) (n / 2));
    const double inRms = amp / std::sqrt (2.0);
    return 20.0 * std::log10 (outRms / inRms + 1e-12);
}

static std::vector<float> speechLikeNoise (int n, uint32_t seed, double level)
{
    std::vector<float> v ((size_t) n);
    uint32_t x = seed;
    for (int i = 0; i < n; ++i)
    {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        const double w = (double) x / 2147483648.0 - 1.0;
        const double am = 0.5 + 0.5 * std::sin (2.0 * kPi * 4.0 * i / 48000.0);
        v[(size_t) i] = (float) (w * level * am);
    }
    return v;
}

// ---------------------------------------------------------------------------
static void testPresets()
{
    std::printf ("presets\n");
    CHECK (kNumFactoryPresets == 12, "got %d", kNumFactoryPresets);

    std::set<std::string> names;
    for (int i = 0; i < kNumFactoryPresets; ++i)
    {
        const auto& fp = kFactoryPresets[i];
        CHECK (names.insert (fp.name).second, "duplicate name %s", fp.name);
        for (int k = 0; k < kNumPresetParams; ++k)
        {
            const auto& s = kParamSpecs[k];
            CHECK (fp.values[k] >= s.min && fp.values[k] <= s.max,
                   "preset %s param %s = %f out of [%f, %f]", fp.name, s.id, fp.values[k], s.min, s.max);
        }
    }

    // "Init" must equal the parameter defaults
    for (int k = 0; k < kNumPresetParams; ++k)
        CHECK (kFactoryPresets[0].values[k] == kParamSpecs[k].def, "Init differs from default for %s", kParamSpecs[k].id);

    // parameter ids are unique
    std::set<std::string> ids { kBypassId, kTriggerId };
    for (int k = 0; k < kNumPresetParams; ++k)
        CHECK (ids.insert (kParamSpecs[k].id).second, "duplicate id %s", kParamSpecs[k].id);
}

static void testBandPass()
{
    std::printf ("band-pass shape\n");
    const auto p = flatParams();
    const double ref = sineGainDb (p, 1000.0);
    const double g100 = sineGainDb (p, 100.0);
    const double g10k = sineGainDb (p, 10000.0);
    const double g2k = sineGainDb (p, 2000.0);

    std::printf ("    1k=%.1f dB  100Hz=%.1f dB  2k=%.1f dB  10k=%.1f dB\n", ref, g100, g2k, g10k);
    CHECK (ref > -4.0 && ref < 3.0, "passband gain %.2f", ref);
    CHECK (g100 < ref - 20.0, "100 Hz only %.1f dB below", ref - g100);
    CHECK (g10k < ref - 20.0, "10 kHz only %.1f dB below", ref - g10k);
    CHECK (std::fabs (g2k - ref) < 4.0, "mid band not flat: %.1f", g2k - ref);
}

static void testHonk()
{
    std::printf ("honk resonance\n");
    auto p = flatParams();
    const double g0 = sineGainDb (p, 2200.0);
    p.honk = 1.0f;
    const double g1 = sineGainDb (p, 2200.0);
    const double far = sineGainDb (p, 800.0) - sineGainDb (flatParams(), 800.0);
    std::printf ("    honk boost at 2.2k = %.1f dB, at 800 Hz = %.1f dB\n", g1 - g0, far);
    CHECK (g1 - g0 > 8.0, "boost %.1f", g1 - g0);
    CHECK (far < 4.0, "honk leaks to 800 Hz: %.1f", far);

    // Mode II moves the peak lower
    p.mode = 1;
    const double m2 = sineGainDb (p, 1500.0) - sineGainDb (flatParams(), 1500.0);
    std::printf ("    mode II boost at 1.5k = %.1f dB\n", m2);
    CHECK (m2 > 4.0, "mode II boost %.1f", m2);
}

static void testSaturation()
{
    std::printf ("drive adds harmonics but stays bounded\n");
    auto p = flatParams();
    p.drive = 1.0f;
    const double loud = sineGainDb (p, 1000.0, 48000.0, 0.5);
    std::printf ("    1k @ -6 dBFS with full drive -> %.1f dB gain\n", loud);
    CHECK (loud < 6.0, "output too hot: %.1f dB", loud);
}

static void testStability()
{
    std::printf ("stability with extreme settings and sample rates\n");
    const double rates[] = { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 };

    for (double sr : rates)
    {
        for (int mode = 0; mode < 2; ++mode)
        {
            for (int src = 0; src < 3; ++src)
            {
                EngineParams p;
                p.drive = 1.0f; p.honk = 1.0f; p.feedback = 1.0f; p.noise = 1.0f;
                p.mix = 1.0f; p.outputDb = 12.0f; p.comp = true;
                p.lowCutHz = 200.0f; p.highCutHz = 8000.0f; p.mode = mode; p.noiseSource = src;

                MegaphoneEngine e;
                e.prepare (sr, 2);
                e.setParams (p);
                e.triggerClick();

                const int n = (int) (sr * 3.0);
                std::vector<float> l ((size_t) n), r ((size_t) n);
                uint32_t x = 12345u;
                for (int i = 0; i < n; ++i)
                {
                    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                    l[(size_t) i] = (float) (((double) x / 2147483648.0 - 1.0) * 3.0);   // way over 0 dBFS
                    r[(size_t) i] = -l[(size_t) i];
                }
                float* ch[2] = { l.data(), r.data() };
                // odd block sizes on purpose
                int pos = 0;
                while (pos < n) { const int b = std::min (997, n - pos); float* c2[2] = { l.data() + pos, r.data() + pos }; e.process (c2, 2, b); pos += b; }
                (void) ch;

                CHECK (allFinite (l) && allFinite (r), "NaN/Inf sr=%.0f mode=%d src=%d", sr, mode, src);
                CHECK (maxAbs (l) < 20.0 && maxAbs (r) < 20.0, "unbounded %.2f sr=%.0f", maxAbs (l), sr);
            }
        }
    }
}

static void testSilenceAndNoFeedbackRunaway()
{
    std::printf ("silence stays silent, feedback tail dies\n");
    EngineParams p;
    p.drive = 0.8f; p.honk = 1.0f; p.feedback = 1.0f; p.noise = 0.0f; p.mix = 1.0f; p.comp = true;

    MegaphoneEngine e;
    e.prepare (48000.0, 1);
    e.setParams (p);

    // 0.5 s of loud tone, then 10 s of silence
    const int burst = 24000, tail = 480000;
    std::vector<float> buf ((size_t) (burst + tail), 0.0f);
    for (int i = 0; i < burst; ++i) buf[(size_t) i] = (float) (0.6 * std::sin (2.0 * kPi * 1800.0 * i / 48000.0));
    float* ch[1] = { buf.data() };
    e.process (ch, 1, burst + tail);

    CHECK (allFinite (buf), "non-finite");
    const double during = rms (buf, 0, (size_t) burst);
    const double endTail = rms (buf, (size_t) (burst + tail - 48000), (size_t) (burst + tail));
    std::printf ("    burst rms=%.3f, rms in last second of silence=%.2e\n", during, endTail);
    CHECK (during > 0.05, "no output during burst");
    CHECK (endTail < 1e-3, "tail did not decay: %.2e", endTail);

    // pure silence in, noise 0 -> exactly silent out
    EngineParams q = fromPreset (kFactoryPresets[0]);
    q.noise = 0.0f;
    MegaphoneEngine e2;
    e2.prepare (44100.0, 2);
    e2.setParams (q);
    std::vector<float> a (44100, 0.0f), b (44100, 0.0f);
    float* c2[2] = { a.data(), b.data() };
    e2.process (c2, 2, 44100);
    CHECK (maxAbs (a) < 1e-6 && maxAbs (b) < 1e-6, "silence not silent: %.2e", maxAbs (a));
}

static void testNoise()
{
    std::printf ("noise: hiss and click\n");
    const int n = 48000;

    auto render = [&] (int src, float noise, bool click) {
        EngineParams p = flatParams();
        p.noise = noise; p.noiseSource = src;
        MegaphoneEngine e;
        e.prepare (48000.0, 1);
        e.setParams (p);
        std::vector<float> buf ((size_t) n, 0.0f);
        float* ch[1] = { buf.data() };
        if (click) e.triggerClick();
        e.process (ch, 1, n);
        return buf;
    };

    // Burst: continuous hiss
    auto burst = render (2, 1.0f, false);
    const double hissRms = rms (burst, 24000);
    std::printf ("    hiss rms = %.4f\n", hissRms);
    CHECK (hissRms > 0.001 && hissRms < 0.2, "hiss level %.5f", hissRms);

    auto burstOff = render (2, 0.0f, false);
    CHECK (maxAbs (burstOff) < 1e-6, "noise 0 should be silent");

    // Click source: nothing without trigger, a short event with trigger
    auto clickNo = render (0, 1.0f, false);
    CHECK (maxAbs (clickNo) < 1e-6, "click source made noise without trigger");
    auto clickYes = render (0, 1.0f, true);
    const double early = rms (clickYes, 0, 1440);     // first 30 ms
    const double late = rms (clickYes, 24000);
    std::printf ("    click: first 30 ms rms=%.4f, later rms=%.2e\n", early, late);
    CHECK (early > 0.01, "click too quiet %.5f", early);
    CHECK (late < 1e-6, "click did not end %.2e", late);

    // Burst-only source ignores the trigger
    auto burstClick = render (2, 1.0f, true);
    auto burstNo = render (2, 1.0f, false);
    double diff = 0.0;
    for (size_t i = 0; i < 1440; ++i) diff = std::max (diff, (double) std::fabs (burstClick[i] - burstNo[i]));
    CHECK (diff < 1e-6, "burst source reacted to trigger");
}

static void testEchoAndLevels()
{
    std::printf ("slapback echo and level consistency\n");

    // single impulse-like click -> echo arrives about 85 ms later (mode I)
    EngineParams p = flatParams();
    p.feedback = 0.5f; p.honk = 0.0f; p.drive = 0.2f;
    MegaphoneEngine e;
    e.prepare (48000.0, 1);
    e.setParams (p);
    std::vector<float> buf (48000, 0.0f);
    for (int i = 0; i < 48; ++i) buf[(size_t) i] = (float) (0.5 * std::sin (2.0 * kPi * 1500.0 * i / 48000.0));
    float* ch[1] = { buf.data() };
    e.process (ch, 1, 48000);
    const double direct = rms (buf, 0, 400);
    const double gap = rms (buf, 1200, 3600);                // between source and echo
    const double echo = rms (buf, 4000 + 0, 4600);           // ~85 ms = 4080 samples
    std::printf ("    direct=%.4f gap=%.4f echo=%.4f\n", direct, gap, echo);
    CHECK (echo > gap * 3.0, "echo not found near 85 ms");

    // overall level should not explode or vanish with drive
    double lo = 1e9, hi = -1e9;
    for (float d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        EngineParams q;
        q.drive = d; q.noise = 0.0f; q.feedback = 0.0f; q.mix = 1.0f; q.comp = true;
        MegaphoneEngine e2;
        e2.prepare (48000.0, 1);
        e2.setParams (q);
        auto in = speechLikeNoise (96000, 777u, 0.3);
        float* c2[1] = { in.data() };
        e2.process (c2, 1, 96000);
        const double db = 20.0 * std::log10 (rms (in, 24000) + 1e-12);
        std::printf ("    drive %3.0f%% -> %.1f dBFS rms\n", d * 100.0f, db);
        lo = std::min (lo, db); hi = std::max (hi, db);
    }
    CHECK (hi - lo < 9.0, "level spread across drive = %.1f dB", hi - lo);
}

static void testDeterminismAndBlockSizes()
{
    std::printf ("block-size independence, mono/stereo\n");
    const EngineParams p = fromPreset (kFactoryPresets[3]);
    auto in = speechLikeNoise (48000, 4242u, 0.4);

    auto run = [&] (int block) {
        MegaphoneEngine e;
        e.prepare (48000.0, 1);
        e.setParams (p);
        std::vector<float> v = in;
        int pos = 0;
        while (pos < (int) v.size())
        {
            const int b = std::min (block, (int) v.size() - pos);
            float* c[1] = { v.data() + pos };
            e.process (c, 1, b);
            pos += b;
        }
        return v;
    };

    // Sub-blocks are 32 samples long, so blocks that are a multiple of 32 must match exactly
    auto a = run (512);
    auto b = run (32);
    double diff = 0.0;
    for (size_t i = 0; i < a.size(); ++i) diff = std::max (diff, (double) std::fabs (a[i] - b[i]));
    CHECK (diff < 1e-6, "different block sizes differ by %.2e", diff);
    CHECK (allFinite (run (1)), "block size 1");
    CHECK (allFinite (run (7)), "block size 7");

    // All factory presets render something sane
    for (int i = 0; i < kNumFactoryPresets; ++i)
    {
        MegaphoneEngine e;
        e.prepare (44100.0, 2);
        e.setParams (fromPreset (kFactoryPresets[i]));
        auto l = speechLikeNoise (44100, 99u + (uint32_t) i, 0.3);
        auto r = l;
        float* c[2] = { l.data(), r.data() };
        e.process (c, 2, 44100);
        const double lvl = rms (l, 10000);
        CHECK (allFinite (l) && lvl > 0.005 && lvl < 1.5, "preset %s level %.4f", kFactoryPresets[i].name, lvl);
    }
}

int main()
{
    testPresets();
    testBandPass();
    testHonk();
    testSaturation();
    testStability();
    testSilenceAndNoFeedbackRunaway();
    testNoise();
    testEchoAndLevels();
    testDeterminismAndBlockSizes();

    std::printf ("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
