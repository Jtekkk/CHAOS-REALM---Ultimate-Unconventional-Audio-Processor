/*
    CHAOS REALM — tools/benchmark.cpp
    Framework-free performance benchmark for the DSP core.  Measures the
    single-core real-time CPU fraction of each module and of representative
    engine configurations, plus a per-instance memory estimate and the reported
    latency.  These numbers validate the project's stated performance targets.

    Build & run (from repo root):
      g++ -std=c++17 -O3 -DNDEBUG tools/benchmark.cpp Source/dsp/ChaosEngine.cpp \
          Source/dsp/modules/*.cpp -I. -o /tmp/bench && /tmp/bench
*/
#include "Source/dsp/ChaosEngine.h"
#include "Source/dsp/modules/AllModules.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace chaos;
using Clock = std::chrono::steady_clock;

namespace
{
constexpr double kSR = 44100.0;   // the target reference rate
constexpr int    kBlock = 512;
constexpr int    kCh = 2;
constexpr double kSeconds = 20.0; // audio duration processed per measurement

struct Buffers
{
    std::vector<std::vector<float>> d;
    std::vector<float*> p;
    Buffers() : d ((size_t) kCh, std::vector<float> ((size_t) kBlock)), p ((size_t) kCh)
    { for (int c = 0; c < kCh; ++c) p[(size_t) c] = d[(size_t) c].data(); }
    void refill (Xorshift& rng) { for (auto& ch : d) for (auto& s : ch) s = 0.3f * rng.nextBipolar(); }
    float* const* ptr() { return p.data(); }
};

// Return real-time CPU fraction (%) for a per-block process functor.
template <typename Proc>
double measure (Proc proc)
{
    const int blocks = (int) (kSeconds * kSR / kBlock);
    Buffers b; Xorshift rng (0x1234u);
    // Warm up.
    for (int i = 0; i < 64; ++i) { b.refill (rng); proc (b.ptr(), kCh, kBlock); }
    const auto t0 = Clock::now();
    for (int i = 0; i < blocks; ++i) { b.refill (rng); proc (b.ptr(), kCh, kBlock); }
    const auto t1 = Clock::now();
    const double procSec = std::chrono::duration<double> (t1 - t0).count();
    const double audioSec = (double) blocks * kBlock / kSR;
    return 100.0 * procSec / audioSec;
}

long readVmRssKb()
{
    std::ifstream f ("/proc/self/status");
    std::string key; long val = 0; std::string unit;
    while (f >> key)
    {
        if (key == "VmRSS:") { f >> val >> unit; return val; }
        std::string rest; std::getline (f, rest);
    }
    return -1;
}
} // namespace

int main()
{
    std::printf ("CHAOS REALM — performance benchmark\n");
    std::printf ("  sample rate %.0f Hz, block %d, %d ch, %.0f s per test, single core\n\n",
                 kSR, kBlock, kCh, kSeconds);

    // ---- Per-module CPU (module fully wet, default params) ----
    std::printf ("Per-module real-time CPU (default params):\n");
    for (int i = 0; i < kNumModules; ++i)
    {
        auto m = createModule (static_cast<ModuleID> (i));
        m->prepare ({ kSR, kBlock, kCh });
        const double cpu = measure ([&] (float* const* p, int c, int n) { m->process (p, c, n); });
        std::printf ("  %-34s %6.2f %%\n", m->getName(), cpu);
    }

    // ---- Engine: 3 active modules (the stated target scenario) ----
    {
        ChaosEngine eng; eng.prepare (kSR, kBlock, kCh);
        for (int i = 0; i < kNumModules; ++i) eng.setModuleEnabled (i, false);
        // A representative mix: texture + resonator + space.
        eng.setModuleEnabled ((int) ModuleID::MicroTextureProcessor, true);
        eng.setModuleEnabled ((int) ModuleID::PhysicalModelingChaosEngine, true);
        eng.setModuleEnabled ((int) ModuleID::NonLinearSpaceCreator, true);
        const double cpu = measure ([&] (float* const* p, int c, int n) { eng.process (p, c, n); });
        std::printf ("\nEngine, 3 active modules (Texture+Physical+Space): %.2f %%  [target < 5%%]\n", cpu);
    }

    // ---- Engine: all 10 modules ----
    long rssBefore = readVmRssKb();
    auto engFull = std::make_unique<ChaosEngine>();
    engFull->prepare (kSR, kBlock, kCh);
    for (int i = 0; i < kNumModules; ++i) engFull->setModuleEnabled (i, true);
    long rssAfter = readVmRssKb();
    {
        const double cpu = measure ([&] (float* const* p, int c, int n) { engFull->process (p, c, n); });
        std::printf ("Engine, all 10 modules active:                     %.2f %%\n", cpu);
        std::printf ("Engine reported latency:                           %d samples @44.1k (%.1f ms)\n",
                     engFull->getLatencySamples(), 1000.0 * engFull->getLatencySamples() / kSR);
        std::printf ("  (latency comes from the Spectral module's STFT; 0 samples when it is disabled)\n");
    }

    if (rssBefore > 0 && rssAfter > 0)
        std::printf ("\nApprox. per-instance memory (RSS delta preparing full engine): %.1f MB  [target < 200 MB]\n",
                     (rssAfter - rssBefore) / 1024.0);

    std::printf ("\nNote: measured with -O3 on this host; a release plugin build with LTO and\n");
    std::printf ("SIMD-friendly flags will typically be faster. Multi-core is not used here.\n");
    return 0;
}
