/*
    CHAOS REALM — Module 4: Micro-Texture Processor  (REFERENCE MODULE)

    Sound-design intent
    -------------------
      * Sub-sample granular processing with variable grain density
      * Sample-rate modulation / reduction (sample & hold decimation)
      * Dynamic bit-depth reduction with first-order noise shaping
      * Temporal smear (allpass diffusion) for pre-echo / post-rhythm blur

    This file is the canonical template for every CHAOS REALM module: a pure
    C++17 ModuleBase subclass depending only on ChaosMath.h / ModuleBase.h.
    Study the parameter registration in the constructor and the neutral-at-
    default behaviour (every sub-effect is transparent at its default value so
    the module can be safely inserted anywhere).
*/
#pragma once

#include "../ChaosMath.h"
#include "../ModuleBase.h"

#include <array>
#include <vector>

namespace chaos
{

class MicroTextureProcessor : public ModuleBase
{
public:
    MicroTextureProcessor();

    const char* getName() const override { return "Micro-Texture Processor"; }
    ModuleID    getID()   const override { return ModuleID::MicroTextureProcessor; }

    void process (float* const* buffers, int numChannels, int numSamples) override;

protected:
    void onPrepare (const ProcessContext& ctx) override;
    void onReset() override;

private:
    // ---- parameter indices ----
    int pDensity = 0, pSize = 0, pScatter = 0, pSrReduce = 0, pBits = 0, pSmear = 0, pStereo = 0;

    // ---- granular engine ----
    struct Grain
    {
        bool  active = false;
        float delay  = 0.0f;   // samples behind the write head
        float rate   = 1.0f;   // playback speed
        float phase  = 0.0f;   // 0..1 across the grain envelope
        float inc    = 0.0f;   // phase increment
        float pan    = 0.5f;   // 0 = L, 1 = R
    };
    static constexpr int kMaxGrains = 16;

    std::array<Grain, kMaxGrains> grains;
    std::vector<DelayLine> record;      // per-channel record buffer
    float grainClock = 0.0f;            // countdown to next grain (samples)
    Xorshift rng { 0x51F7A213u };

    // ---- sample-rate reduction (per channel) ----
    std::vector<float> srHold;
    std::vector<float> srPhase;         // fractional phase accumulator per ch

    // ---- bit reduction noise shaping (per channel) ----
    std::vector<float> bitError;

    // ---- temporal smear (per channel, cascade of allpasses) ----
    static constexpr int kSmearStages = 4;
    std::vector<std::array<AllpassDiffuser, kSmearStages>> smear;

    void triggerGrain();
    float maxRecordSamples = 0.0f;
};

} // namespace chaos
