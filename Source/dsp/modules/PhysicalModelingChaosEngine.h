/*
    CHAOS REALM — Module 2: Physical Modeling Chaos Engine

    Sound-design intent
    -------------------
      * A non-linear waveguide / resonator (a delay-loop "body") is excited by
        the incoming audio and made to ring, so arbitrary input takes on the
        tonal character of a struck / bowed physical object.
      * A chaotic modulator (Lorenz / Rössler / Logistic map, user-selectable)
        continuously warps the resonator — its tuning (delay length) and its
        material damping — for organic, never-repeating detune and timbral drift.
      * A tanh non-linearity sits *inside* the feedback loop: it both bounds the
        loop (guaranteeing stability) and injects overtones, the way a real
        string/membrane stiffens and saturates when driven hard.

    Stability contract
    ------------------
      * Loop feedback is always < 0.97 and the in-loop tanh keeps the round-trip
        gain <= 1, so with silence in the resonator decays to silence.
      * A DC blocker in the loop stops the rectifying non-linearity from pumping
        DC into the delay.  Cutoffs are clamped to [20 Hz, 0.49*sr].

    Pure C++17, depends only on ChaosMath.h / ModuleBase.h (no JUCE).
*/
#pragma once

#include "../ChaosMath.h"
#include "../ModuleBase.h"

#include <array>
#include <vector>

namespace chaos
{

class PhysicalModelingChaosEngine : public ModuleBase
{
public:
    PhysicalModelingChaosEngine();

    const char* getName() const override { return "Physical Modeling Chaos Engine"; }
    ModuleID    getID()   const override { return ModuleID::PhysicalModelingChaosEngine; }

    void process (float* const* buffers, int numChannels, int numSamples) override;

protected:
    void onPrepare (const ProcessContext& ctx) override;
    void onReset() override;

private:
    // ---- parameter indices ----
    int pChaosSys = 0, pChaosRate = 0, pChaosDepth = 0, pMaterial = 0,
        pResonance = 0, pDrive = 0, pTone = 0;

    static constexpr int kNumChaosSystems = 3; // Lorenz, Rössler, Logistic map

    // One independent non-linear waveguide resonator + chaotic modulator per
    // channel (kept fully allocation-free once prepared).
    struct Voice
    {
        DelayLine   waveguide;          // the tuned resonator (delay loop)
        OnePole     damping;            // material / brightness element in the loop
        DcBlocker   loopDc;             // stops DC self-sustaining -> settles to silence
        OnePole     tone;               // post output filter
        DcBlocker   outDc;

        // Chaotic modulators — only the selected one is advanced per sample.
        Lorenz      lorenz;
        Rossler     rossler;
        LogisticMap logistic;
        float       mapPhase = 0.0f;    // sample & hold phase for the logistic map
        float       mapHold  = 0.0f;    // held logistic value (bipolar, [-1,1])

        float       baseDelay = 100.0f; // delay (samples) of the fundamental
    };

    std::vector<Voice> voices;
    float maxDelaySamples = 0.0f;

    /** Advance the selected chaotic system one step; returns a bounded [-1,1]
        modulation value. */
    float advanceChaos (Voice& v, int sys, float rate01) noexcept;
};

} // namespace chaos
