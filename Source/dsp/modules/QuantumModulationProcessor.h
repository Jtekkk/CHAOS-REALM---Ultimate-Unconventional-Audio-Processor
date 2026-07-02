/*
    CHAOS REALM — Module 9: Quantum Modulation Processor

    Sound-design intent
    -------------------
    A modulation engine built out of loose "quantum" metaphors, all realised as
    honest, bounded DSP:

      * Superposition   — two parallel, differently-tuned filter "states" (A/B)
                          are blended by a weight that evolves probabilistically.
                          "superposition" sets how far the blend swings, "collapse"
                          how often the weight jumps (and how fast it relaxes).
      * Entanglement    — the signal is split into a low and a high band whose
                          amplitudes cross-modulate: energy in one band non-locally
                          drives the gain of the other.  "entangle" = coupling.
      * Tunneling       — each band is randomly gated (passes / is blocked) with a
                          probability set by "tunnel"; every transition is smoothed
                          into a click-free crossfade.
      * Observer effect — an input-level analysis scales the collapse (measurement)
                          rate, so louder input "collapses" the state faster.
      * spread          — decorrelates the probabilistic modulation between the two
                          channels for a wide stereo image.
      * coherence       — smooths all of the randomness (jump/relax time constants).

    Stability: every sub-block is a stable filter, a bounded envelope follower or a
    multiplicative gate, all fed by smoothed control signals — so with silence in
    the module settles to silence, and the output is always finite and bounded.

    Pure C++17, depends only on ChaosMath.h / ModuleBase.h.
*/
#pragma once

#include "../ChaosMath.h"
#include "../ModuleBase.h"

#include <array>
#include <vector>

namespace chaos
{

class QuantumModulationProcessor : public ModuleBase
{
public:
    QuantumModulationProcessor();

    const char* getName() const override { return "Quantum Modulation Processor"; }
    ModuleID    getID()   const override { return ModuleID::QuantumModulationProcessor; }

    void process (float* const* buffers, int numChannels, int numSamples) override;

protected:
    void onPrepare (const ProcessContext& ctx) override;
    void onReset() override;

private:
    // ---- parameter indices (all normalised 0..1, continuous) ----
    int pSuperposition = 0, pEntangle = 0, pTunnel = 0, pObserver = 0,
        pCollapse = 0, pSpread = 0, pCoherence = 0;

    // ---- per-channel processing state ----
    struct ChannelState
    {
        StateVariableFilter split;   // band splitter (LP + HP outputs)
        StateVariableFilter svfA;    // superposition state A (dark resonant LP)
        StateVariableFilter svfB;    // superposition state B (bright resonant BP)
        DcBlocker           dc;      // scrub any asymmetry-induced DC

        float weightCur    = 0.5f;   // smoothed A/B blend weight
        float gateLowCur   = 1.0f;   // smoothed low-band tunnel gate
        float gateHighCur  = 1.0f;   // smoothed high-band tunnel gate
        float envLow       = 0.0f;   // low-band amplitude follower (entanglement)
        float envHigh      = 0.0f;   // high-band amplitude follower (entanglement)

        void reset() noexcept
        {
            split.reset(); svfA.reset(); svfB.reset(); dc.reset();
            weightCur = 0.5f;
            gateLowCur = gateHighCur = 1.0f;
            envLow = envHigh = 0.0f;
        }
    };

    std::vector<ChannelState> channels;

    // ---- shared probabilistic modulation source ----
    Xorshift    rng { 0x2BD1E995u };
    LogisticMap logistic;                    // chaotic weight-target generator
    float collapseClock = 0.0f;              // samples until next weight collapse
    float tunnelClock   = 0.0f;              // samples until next tunnel event
    std::array<float, 2> weightTarget   { { 0.5f, 0.5f } }; // per-channel targets
    std::array<float, 2> gateLowTarget  { { 1.0f, 1.0f } };
    std::array<float, 2> gateHighTarget { { 1.0f, 1.0f } };
    float obsEnv = 0.0f;                     // input-level analysis (observer)

    // ---- sample-rate-dependent smoothing coefficients ----
    float envCoeff = 0.0f;                   // band envelope followers
    float obsCoeff = 0.0f;                   // observer envelope
};

} // namespace chaos
