/*
    CHAOS REALM — Ultimate Unconventional Audio Processor
    ChaosEngine.h — The modular processing chain.

    Owns the ten modules, their routing order, and per-module state (enable,
    dry/wet mix).  Handles:
      * arbitrary chain ordering
      * per-module equal-power dry/wet with automatic latency alignment
        (only the spectral module currently reports latency, but the mechanism
        is general)
      * global input/output gain and a master dry/wet
      * per-block application of the modulation matrix onto module parameters

    JUCE-free; the PluginProcessor drives it.
*/
#pragma once

#include "AudioTypes.h"
#include "ChaosMath.h"
#include "ModuleBase.h"
#include "Modulation.h"

#include <array>
#include <memory>
#include <vector>

namespace chaos
{

/** Factory: constructs a fresh module for the given id. Defined in the .cpp so
    the engine header does not need every module header. */
ModulePtr createModule (ModuleID id);

class ChaosEngine
{
public:
    ChaosEngine();

    // -- Lifecycle ---------------------------------------------------------
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    /** Process a block in place.  buffers[ch][sample]. */
    void process (float* const* buffers, int numChannels, int numSamples);

    int  getLatencySamples() const noexcept { return totalLatency; }

    // -- Per-module control (index is ModuleID order 0..9) -----------------
    ModuleBase& module (int i)             { return *modules[(size_t) i]; }
    const ModuleBase& module (int i) const { return *modules[(size_t) i]; }

    void setModuleEnabled (int i, bool on) noexcept { enabled[(size_t) i] = on; }
    bool isModuleEnabled (int i) const noexcept     { return enabled[(size_t) i]; }

    void setModuleMix (int i, float m) noexcept { mixTarget[(size_t) i] = clampf (m, 0.0f, 1.0f); }
    float getModuleMix (int i) const noexcept   { return mixTarget[(size_t) i]; }

    /** Chain position 'pos' will process module index 'moduleIndex'. */
    void setRouting (const std::array<int, kNumModules>& newOrder) noexcept { order = newOrder; }
    const std::array<int, kNumModules>& getRouting() const noexcept { return order; }

    // -- Global ------------------------------------------------------------
    void setInputGainDb (float db) noexcept  { inputGain.setTarget (dbToGain (db)); }
    void setOutputGainDb (float db) noexcept { outputGain.setTarget (dbToGain (db)); }
    void setMasterMix (float m) noexcept     { masterMix.setTarget (clampf (m, 0.0f, 1.0f)); }

    // -- Modulation --------------------------------------------------------
    ModMatrix& modMatrix() noexcept { return mod; }

private:
    void applyModulation();

    std::array<ModulePtr, kNumModules> modules;
    std::array<bool, kNumModules>  enabled;
    std::array<float, kNumModules> mixTarget;
    std::array<OnePoleSmoother, kNumModules> mixSmooth;
    std::array<int, kNumModules>   order;

    // Per-module dry-alignment delay (one DelayLine per channel) for modules
    // whose latency > 0.  Empty when a module reports zero latency.
    struct DrySync { std::vector<DelayLine> ch; int latency = 0; };
    std::array<DrySync, kNumModules> drySync;

    ModMatrix mod;

    OnePoleSmoother inputGain, outputGain, masterMix;

    std::vector<std::vector<float>> dryScratch;  // [ch][sample] master dry copy
    std::vector<std::vector<float>> perModDry;   // [ch][sample] per-module dry copy
    std::vector<float*> chanPtrs;

    ProcessContext context;
    int  totalLatency = 0;
    bool prepared = false;
};

} // namespace chaos
