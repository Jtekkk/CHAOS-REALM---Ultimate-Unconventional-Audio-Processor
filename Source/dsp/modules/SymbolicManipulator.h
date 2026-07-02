/*
    CHAOS REALM — Module 10: Symbolic Manipulator

    Sound-design intent  (esoteric / symbolic sound mapping)
    --------------------------------------------------------
      * Numerology   — a bank of feedback comb resonators tuned to number-derived
                       frequency ratios (Fibonacci multipliers, golden-ratio phi
                       powers, prime-harmonic series).  "numerology" morphs the
                       whole bank between those three ratio sets.
      * Sacred geom. — the resonator tunings are additionally spread/scaled by
                       geometric constants (phi, sqrt2, sqrt3, sqrt5) so the bank
                       fans out along irrational intervals.  "geometry" dials how
                       far the geometric spread is applied.
      * Divination   — a 64-step pseudo-random pattern (seeded Xorshift, I-Ching /
                       tarot flavoured) advancing at a rate set by "ritual".  Each
                       step gates, re-tunes (transpose) and modulates (feedback /
                       damping) the resonator voice.  "divination" chooses the seed
                       (a stepped parameter -> integer seed -> a fixed pattern).
      * Alchemy      — the resonator voice is routed through four elemental
                       archetypes crossfaded by "element":
                          0.00 earth : warm low-pass
                          0.33 water : chorus / flange (modulated delay)
                          0.66 air   : bright high-pass + rectified shimmer
                          1.00 fire  : tanh drive / bright saturation
                       Adjacent element outputs are interpolated.
      * "intensity"  — overall wet character (internal dry<->wet blend + fire drive).
      * "resonance"  — resonator feedback / Q (hard-bounded below 0.95).

    Pure C++17, depends only on ChaosMath.h / ModuleBase.h.  prepare() allocates;
    process()/reset() are real-time safe.  Given silence in, every feedback path
    (comb fb < 0.95, chorus fb 0.45) decays, so the module settles to silence.
*/
#pragma once

#include "../ChaosMath.h"
#include "../ModuleBase.h"

#include <array>
#include <vector>

namespace chaos
{

class SymbolicManipulator : public ModuleBase
{
public:
    SymbolicManipulator();

    const char* getName() const override { return "Symbolic Manipulator"; }
    ModuleID    getID()   const override { return ModuleID::SymbolicManipulator; }

    void process (float* const* buffers, int numChannels, int numSamples) override;

protected:
    void onPrepare (const ProcessContext& ctx) override;
    void onReset() override;

private:
    // ---- parameter indices ----
    int pNumerology = 0, pGeometry = 0, pDivination = 0, pElement = 0,
        pRitual = 0, pIntensity = 0, pResonance = 0;

    // ---- fixed sizes ----
    static constexpr int kNumRes   = 6;   // resonators in the bank
    static constexpr int kNumSteps = 64;  // divination sequence length (I-Ching)
    static constexpr int kNumSeeds = 16;  // distinct seeds selectable by divination

    // ---- divination sequencer ----
    struct Step
    {
        float gate      = 0.0f;   // 0 = rest, else output level for the step
        float transpose = 0.0f;   // semitone offset applied to the whole bank
        float fbMod     = 0.0f;   // 0..1 feedback modulation
        float tone      = 0.0f;   // 0..1 damping / tone modulation
    };
    std::array<Step, kNumSteps> pattern {};
    uint32_t currentSeed     = 0;
    int      seqStep         = 0;
    float    seqPhase        = 0.0f;   // 0..1 progress toward the next step
    float    gateSmooth      = 0.0f;   // click-free gate follower
    float    transposeTarget = 1.0f;   // multiplicative pitch factor for the step
    float    transposeSmooth = 1.0f;   // click-free transpose follower

    // ---- per-channel DSP state ----
    struct Channel
    {
        std::array<CombFilter, kNumRes> combs;   // numerology resonator bank
        OnePole   earthLP;                       // element: warm low-pass
        OnePole   airHP;                         // element: bright high-pass
        DcBlocker airDC;                         // shimmer DC removal
        DcBlocker outDC;                         // final DC guard
        DelayLine water;                         // element: chorus/flange delay
        float     waterFb = 0.0f;                // chorus feedback state
    };
    std::vector<Channel> chans;
    float waterLfoPhase = 0.0f;                  // shared chorus LFO (0..1)

    void regeneratePattern (uint32_t seed) noexcept;
};

} // namespace chaos
