/*
    CHAOS REALM — Module 5: Non-Linear Space Creator

    Sound-design intent
    -------------------
      * A modulated Feedback-Delay-Network (FDN) reverb with fractal diffusion.
      * Input diffusion   : cascade of nested Schroeder allpasses ("diffusion").
      * Core              : 8-line FDN with mutually-prime delays scaled by
                            "size", mixed through an energy-preserving Hadamard
                            matrix (spectral radius <= 1) so the tail is dense
                            yet always bounded.
      * Damping           : per-line one-pole low-pass ("damping") darkens the
                            tail; per-line feedback gain from "decay" (< 0.95).
      * Modulation        : per-line LFOs wandered by a Rossler attractor
                            de-tune the delays for a lush, non-static tail.
      * Quantum           : probabilistic decay — a Xorshift/Logistic process
                            occasionally nudges a line's feedback (kept < 0.95).
      * Pre-delay         : a delay line before the network ("predelay").

    Pure C++17, ModuleBase subclass, depends only on ChaosMath.h / ModuleBase.h.
    At the default parameters the reverb rings OUT (RT60 ~ 1.9 s) and always
    settles to silence given silence in.
*/
#pragma once

#include "../ChaosMath.h"
#include "../ModuleBase.h"

#include <array>
#include <vector>

namespace chaos
{

class NonLinearSpaceCreator : public ModuleBase
{
public:
    NonLinearSpaceCreator();

    const char* getName() const override { return "Non-Linear Space Creator"; }
    ModuleID    getID()   const override { return ModuleID::NonLinearSpaceCreator; }

    void process (float* const* buffers, int numChannels, int numSamples) override;

protected:
    void onPrepare (const ProcessContext& ctx) override;
    void onReset() override;

private:
    // ---- parameter indices ----
    int pSize = 0, pDecay = 0, pDamping = 0, pModulation = 0,
        pDiffusion = 0, pQuantum = 0, pPredelay = 0;

    // ---- FDN core (8 mutually-prime lines) ----
    static constexpr int   kLines        = 8;
    static constexpr float kMaxModSamps  = 12.0f; // max delay-length modulation

    std::array<DelayLine, kLines> lines;
    std::array<float,     kLines> baseSamples   { }; // prime length * srRatio
    std::array<float,     kLines> dampState      { }; // per-line LP state
    std::array<float,     kLines> lfoPhase       { }; // modulation LFO phase
    std::array<float,     kLines> lfoInc         { }; // modulation LFO increment
    std::array<float,     kLines> quantumOffset  { }; // probabilistic gain nudge

    // ---- input diffusion (fractal allpass cascade, mono) ----
    static constexpr int kDiffStages = 4;
    std::array<AllpassDiffuser, kDiffStages> diffuser;

    // ---- pre-delay & output conditioning ----
    DelayLine              predelay;
    std::array<DcBlocker, 2> dcOut;

    // ---- modulation / chaos sources ----
    Rossler     rossler;                 // slow common drift for the LFOs
    Xorshift    rng { 0x2B7E1516u };      // quantum event trigger + line pick
    LogisticMap logistic;                // quantum nudge value

    float srRatio           = 1.0f;
    float maxPredelaySamples = 0.0f;
};

} // namespace chaos
