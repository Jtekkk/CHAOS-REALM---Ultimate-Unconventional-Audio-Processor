/*
    CHAOS REALM — ChaosEngine.cpp
*/
#include "ChaosEngine.h"
#include "modules/AllModules.h"

namespace chaos
{

namespace
{
// Precomputed equal-power crossfade table — removes two trig calls per sample
// per active module from the audio hot path.  Indexed by the (smoothed) mix.
struct EqualPowerTable
{
    static constexpr int N = 2048;
    float dry[N + 1], wet[N + 1];
    EqualPowerTable()
    {
        for (int i = 0; i <= N; ++i)
        {
            const float a = ((float) i / (float) N) * 0.5f * kPiF;
            dry[i] = std::cos (a);
            wet[i] = std::sin (a);
        }
    }
};
const EqualPowerTable eqTable;

inline void equalPowerLUT (float t, float& dg, float& wg) noexcept
{
    const int idx = (int) (clampf (t, 0.0f, 1.0f) * (float) EqualPowerTable::N);
    dg = eqTable.dry[idx];
    wg = eqTable.wet[idx];
}
} // namespace

ModulePtr createModule (ModuleID id)
{
    switch (id)
    {
        case ModuleID::SpectralMorphingHarmonizer:   return std::make_unique<SpectralMorphingHarmonizer>();
        case ModuleID::PhysicalModelingChaosEngine:  return std::make_unique<PhysicalModelingChaosEngine>();
        case ModuleID::PsychoacousticManipulator:    return std::make_unique<PsychoacousticManipulator>();
        case ModuleID::MicroTextureProcessor:        return std::make_unique<MicroTextureProcessor>();
        case ModuleID::NonLinearSpaceCreator:        return std::make_unique<NonLinearSpaceCreator>();
        case ModuleID::BiologicalEmulator:           return std::make_unique<BiologicalEmulator>();
        case ModuleID::ElectromagneticFieldSimulator:return std::make_unique<ElectromagneticFieldSimulator>();
        case ModuleID::TemporalDisintegrationEngine: return std::make_unique<TemporalDisintegrationEngine>();
        case ModuleID::QuantumModulationProcessor:   return std::make_unique<QuantumModulationProcessor>();
        case ModuleID::SymbolicManipulator:          return std::make_unique<SymbolicManipulator>();
        default:                                     return nullptr;
    }
}

ChaosEngine::ChaosEngine()
{
    for (int i = 0; i < kNumModules; ++i)
    {
        modules[(size_t) i] = createModule (static_cast<ModuleID> (i));
        enabled[(size_t) i] = (i == static_cast<int> (ModuleID::MicroTextureProcessor)); // one on by default
        mixTarget[(size_t) i] = 1.0f;
        order[(size_t) i] = i;
    }
}

void ChaosEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    context = { sampleRate, maxBlockSize, numChannels };

    inputGain.prepare (sampleRate, 20.0f);  inputGain.snap (1.0f);
    outputGain.prepare (sampleRate, 20.0f); outputGain.snap (1.0f);
    masterMix.prepare (sampleRate, 20.0f);  masterMix.snap (1.0f);

    mod.prepare (sampleRate);

    totalLatency = 0;
    for (int i = 0; i < kNumModules; ++i)
    {
        modules[(size_t) i]->prepare (context);
        mixSmooth[(size_t) i].prepare (sampleRate, 15.0f);
        mixSmooth[(size_t) i].snap (mixTarget[(size_t) i]);

        const int lat = modules[(size_t) i]->getLatencySamples();
        auto& ds = drySync[(size_t) i];
        ds.latency = lat;
        ds.ch.clear();
        if (lat > 0)
        {
            ds.ch.resize ((size_t) numChannels);
            for (auto& d : ds.ch) { d.prepare (lat + 4); d.reset(); }
            totalLatency += lat;
        }
    }

    dryScratch.assign ((size_t) numChannels, std::vector<float> ((size_t) maxBlockSize, 0.0f));
    perModDry.assign ((size_t) numChannels, std::vector<float> ((size_t) maxBlockSize, 0.0f));
    chanPtrs.assign ((size_t) numChannels, nullptr);

    prepared = true;
}

void ChaosEngine::reset()
{
    for (auto& m : modules) if (m) m->reset();
    for (auto& ds : drySync) for (auto& d : ds.ch) d.reset();
    mod.reset();
}

void ChaosEngine::applyModulation()
{
    for (int i = 0; i < kNumModules; ++i)
    {
        auto& m = *modules[(size_t) i];
        const int np = m.getNumParameters();
        for (int p = 0; p < np; ++p)
        {
            const float off = mod.offsetFor (i, p);
            if (off != 0.0f)
            {
                // Re-apply base + modulation. Base is what the host set via
                // setParameter previously; we store it as the current target.
                const float base = m.getParameter (p);
                m.setParameter (p, clampf (base + off, 0.0f, 1.0f));
                // Note: base is preserved by the host each block, so this does
                // not accumulate — the host re-pushes base values every block.
            }
        }
    }
}

void ChaosEngine::process (float* const* buffers, int numChannels, int numSamples)
{
    if (! prepared) return;

    // --- input gain + master dry capture --------------------------------
    // Advance the shared gain smoother once per sample (n outer, c inner) so
    // every channel receives the same gain ramp.
    for (int n = 0; n < numSamples; ++n)
    {
        const float g = inputGain.next();
        for (int c = 0; c < numChannels; ++c)
        {
            const float v = buffers[c][n] * g;
            buffers[c][n] = v;
            dryScratch[(size_t) c][(size_t) n] = v;
        }
    }

    // --- modulation sources + apply to module params --------------------
    mod.updateSources (const_cast<const float* const*> (buffers), numChannels, numSamples);
    applyModulation();

    // --- run the chain in routing order ---------------------------------
    for (int pos = 0; pos < kNumModules; ++pos)
    {
        const int mi = order[(size_t) pos];
        if (! enabled[(size_t) mi]) continue;

        auto& m = *modules[(size_t) mi];
        auto& ds = drySync[(size_t) mi];

        // Capture this module's dry input (aligned to its latency if any).
        for (int c = 0; c < numChannels; ++c)
        {
            const float* x = buffers[c];
            float* dry = perModDry[(size_t) c].data();
            if (ds.latency > 0)
            {
                auto& dl = ds.ch[(size_t) c];
                for (int n = 0; n < numSamples; ++n) { dl.push (x[n]); dry[n] = dl.readLinear ((float) ds.latency); }
            }
            else
            {
                for (int n = 0; n < numSamples; ++n) dry[n] = x[n];
            }
        }

        // Fully-wet processing in place.
        for (int c = 0; c < numChannels; ++c) chanPtrs[(size_t) c] = buffers[c];
        m.process (chanPtrs.data(), numChannels, numSamples);

        // Equal-power dry/wet blend with smoothing.
        for (int n = 0; n < numSamples; ++n)
        {
            const float mix = mixSmooth[(size_t) mi].next();
            float dg, wg; equalPowerLUT (mix, dg, wg);
            for (int c = 0; c < numChannels; ++c)
            {
                float* x = buffers[c];
                const float dry = perModDry[(size_t) c][(size_t) n];
                x[n] = sanitise (dg * dry + wg * x[n]);
            }
        }
    }

    // --- master dry/wet + output gain -----------------------------------
    for (int n = 0; n < numSamples; ++n)
    {
        const float mm = masterMix.next();
        float dg, wg; equalPowerLUT (mm, dg, wg);
        const float og = outputGain.next();
        for (int c = 0; c < numChannels; ++c)
        {
            float* x = buffers[c];
            const float dry = dryScratch[(size_t) c][(size_t) n];
            x[n] = sanitise ((dg * dry + wg * x[n]) * og);
        }
    }
}

} // namespace chaos
