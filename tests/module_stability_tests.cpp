/*
    CHAOS REALM — Module & engine stability test suite.

    Every module and the full engine must, for any parameter setting:
      * produce only finite samples (no NaN/Inf)
      * stay bounded (no runaway feedback)
      * settle towards silence when the input goes silent (no self-sustaining
        oscillation at default settings)

    These are the non-negotiable real-time-safety guarantees the DSP layer
    makes to the host.  Run after any module change.
*/
#include "../Source/dsp/ChaosEngine.h"
#include "../Source/dsp/modules/AllModules.h"
#include "TestFramework.h"

#include <vector>

using namespace chaos;
using namespace chaostest;

namespace
{
constexpr double kSR = 48000.0;
constexpr int    kBlock = 256;
constexpr int    kChannels = 2;

struct Buf
{
    std::vector<std::vector<float>> data;
    std::vector<float*> ptrs;
    Buf (int ch, int n) : data ((size_t) ch, std::vector<float> ((size_t) n, 0.0f)), ptrs ((size_t) ch)
    { for (int c = 0; c < ch; ++c) ptrs[(size_t) c] = data[(size_t) c].data(); }
    float* const* p() { return ptrs.data(); }
    void fillNoise (Xorshift& rng, float amp) { for (auto& ch : data) for (auto& s : ch) s = rng.nextBipolar() * amp; }
    void fillDC (float v)   { for (auto& ch : data) for (auto& s : ch) s = v; }
    void fillSilence()      { for (auto& ch : data) for (auto& s : ch) s = 0.0f; }
    float peak() const { float p = 0; for (auto& ch : data) for (float s : ch) p = std::max (p, std::fabs (s)); return p; }
    float rms()  const { double e = 0; int N = 0; for (auto& ch : data) for (float s : ch) { e += (double) s * s; ++N; } return N ? (float) std::sqrt (e / N) : 0.0f; }
    bool finite() const { for (auto& ch : data) for (float s : ch) if (!std::isfinite (s)) return false; return true; }
};

// Run 'seconds' of a supplied fill through a process functor; returns worst peak.
template <typename FillFn, typename ProcFn>
float runSeconds (double seconds, FillFn fill, ProcFn proc, bool& allFinite)
{
    const int blocks = (int) (seconds * kSR / kBlock);
    Buf b (kChannels, kBlock);
    float worst = 0.0f;
    allFinite = true;
    for (int i = 0; i < blocks; ++i)
    {
        fill (b);
        proc (b.p(), kChannels, kBlock);
        if (! b.finite()) allFinite = false;
        worst = std::max (worst, b.peak());
    }
    return worst;
}
} // namespace

static void stabilityBattery (ModuleBase& m, const std::string& name)
{
    section (name);
    ProcessContext ctx { kSR, kBlock, kChannels };
    m.prepare (ctx);
    Xorshift rng (0xBEEF1234u);
    const int np = m.getNumParameters();

    auto proc = [&] (float* const* p, int ch, int n) { m.process (p, ch, n); };

    // 1) Default params, moderate noise -> finite & bounded.
    {
        bool fin = true;
        const float peak = runSeconds (2.0, [&] (Buf& b) { b.fillNoise (rng, 0.5f); }, proc, fin);
        check (fin, name + ": finite under noise (defaults)");
        check (peak < 8.0f, name + ": bounded under noise (defaults), peak=" + std::to_string (peak));
    }

    // 2) Silence tail must settle (no self-oscillation at default settings).
    {
        bool fin = true;
        runSeconds (0.5, [&] (Buf& b) { b.fillNoise (rng, 0.7f); }, proc, fin); // excite
        // let it ring down
        float tailRms = 0.0f;
        const int blocks = (int) (4.0 * kSR / kBlock);
        Buf b (kChannels, kBlock);
        for (int i = 0; i < blocks; ++i)
        {
            b.fillSilence();
            m.process (b.p(), kChannels, kBlock);
            if (! b.finite()) fin = false;
            if (i > blocks - 8) tailRms = std::max (tailRms, b.rms());
        }
        check (fin, name + ": finite during silence tail");
        check (tailRms < 0.05f, name + ": settles toward silence, tailRMS=" + std::to_string (tailRms));
    }

    // 3) Extreme parameter settings (all min, all max) -> finite & bounded.
    for (float extreme : { 0.0f, 1.0f })
    {
        for (int p = 0; p < np; ++p) m.snapParameter (p, extreme);
        m.reset();
        bool fin = true;
        const float peak = runSeconds (1.5, [&] (Buf& b) { b.fillNoise (rng, 0.6f); }, proc, fin);
        const std::string tag = extreme == 0.0f ? "all-min" : "all-max";
        check (fin, name + ": finite (" + tag + ")");
        check (peak < 16.0f, name + ": bounded (" + tag + "), peak=" + std::to_string (peak));
    }

    // 4) Randomised parameter sweeps + DC input -> finite & bounded.
    {
        for (int p = 0; p < np; ++p) m.snapParameter (p, rng.nextFloat());
        m.reset();
        bool fin = true;
        const float peak = runSeconds (1.0, [&] (Buf& b) { b.fillDC (0.8f); }, proc, fin);
        check (fin, name + ": finite (random params, DC in)");
        check (peak < 16.0f, name + ": bounded (random params, DC in), peak=" + std::to_string (peak));
    }

    // Restore defaults for any later reuse.
    for (int p = 0; p < np; ++p) m.snapParameter (p, m.getParameterInfo (p).defaultValue);
    m.reset();
}

static void testEngineFull()
{
    section ("full engine — all modules enabled");
    ChaosEngine eng;
    eng.prepare (kSR, kBlock, kChannels);
    for (int i = 0; i < kNumModules; ++i) { eng.setModuleEnabled (i, true); eng.setModuleMix (i, 0.6f); }

    Xorshift rng (0x0EA7u);
    auto proc = [&] (float* const* p, int ch, int n) { eng.process (p, ch, n); };

    bool fin = true;
    const float peak = runSeconds (3.0, [&] (Buf& b) { b.fillNoise (rng, 0.4f); }, proc, fin);
    check (fin, "engine: finite with all 10 modules @ 60% mix");
    check (peak < 8.0f, "engine: bounded with all 10 modules, peak=" + std::to_string (peak));
    std::printf ("  engine reported latency: %d samples\n", eng.getLatencySamples());

    // Silence tail on the full chain.
    Buf b (kChannels, kBlock);
    const int blocks = (int) (5.0 * kSR / kBlock);
    float tailRms = 0.0f;
    for (int i = 0; i < blocks; ++i)
    {
        b.fillSilence();
        eng.process (b.p(), kChannels, kBlock);
        if (! b.finite()) fin = false;
        if (i > blocks - 8) tailRms = std::max (tailRms, b.rms());
    }
    check (fin, "engine: finite during silence tail");
    check (tailRms < 0.08f, "engine: chain settles toward silence, tailRMS=" + std::to_string (tailRms));
}

int main()
{
    std::printf ("CHAOS REALM — module & engine stability suite\n\n");
    for (int i = 0; i < kNumModules; ++i)
    {
        auto m = createModule (static_cast<ModuleID> (i));
        stabilityBattery (*m, m->getName());
    }
    testEngineFull();
    return summary();
}
