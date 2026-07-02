/*
    CHAOS REALM — Module 6: Biological Emulator

    Sound-design intent
    -------------------
      * Vocal-tract formant bank — a set of resonators tuned to the formant
        frequencies of different "species" (human vowel, feline, avian,
        insectoid, cetacean).  "formantShift" scales the whole set up/down so a
        source can be pushed toward tiny insect chatter or huge whale-song.
      * Integrate-and-fire neuron — rectified input energy charges a leaky
        membrane potential; when it crosses threshold the neuron "fires",
        emitting a gate pulse that tremolos / chops the output.  "neuralRate"
        sets excitability, "neuralDepth" sets how deep the gate cuts.
      * Cellular resonance — a mass-spring-damper (state-variable) resonator
        that adds low-body ring; "resonance" is its Q.
      * Evolution — a very slow bounded random walk that mutates the formant
        frequencies over time so the timbre never sits perfectly still.
      * Growl — a subtle saturating nonlinearity plus an octave-down
        sub-harmonic for animal-throat grit.

    Like every CHAOS REALM module this is pure C++17 depending only on
    ChaosMath.h / ModuleBase.h.  It contains no generator, so silence in always
    settles to silence out; every filter cutoff/Q is clamped so the structure is
    unconditionally stable.
*/
#pragma once

#include "../ChaosMath.h"
#include "../ModuleBase.h"

#include <array>
#include <vector>

namespace chaos
{

class BiologicalEmulator : public ModuleBase
{
public:
    BiologicalEmulator();

    const char* getName() const override { return "Biological Emulator"; }
    ModuleID    getID()   const override { return ModuleID::BiologicalEmulator; }

    void process (float* const* buffers, int numChannels, int numSamples) override;

protected:
    void onPrepare (const ProcessContext& ctx) override;
    void onReset() override;

private:
    // ---- parameter indices ----
    int pSpecies = 0, pFormantShift = 0, pNeuralRate = 0, pNeuralDepth = 0,
        pResonance = 0, pEvolution = 0, pGrowl = 0;

    // ---- formant bank layout ----
    static constexpr int kNumFormants = 4;
    static constexpr int kNumSpecies  = 5;   // human, feline, avian, insectoid, cetacean
    static constexpr int kCtrlInterval = 16; // samples between filter-coeff refreshes

    // Formant centre frequencies (Hz) per species — a rough vocal-tract map.
    static constexpr float kFormantHz[kNumSpecies][kNumFormants] =
    {
        {  730.0f, 1090.0f, 2440.0f, 3400.0f },   // human vowel "ah"
        {  850.0f, 1900.0f, 2800.0f, 3800.0f },   // feline
        { 2200.0f, 3600.0f, 5200.0f, 6800.0f },   // avian
        { 1400.0f, 2900.0f, 4200.0f, 5600.0f },   // insectoid
        {  280.0f,  560.0f, 1100.0f, 1900.0f }    // cetacean
    };
    // Relative loudness of each formant (upper formants quieter).
    static constexpr float kFormantGain[kNumFormants] = { 1.0f, 0.75f, 0.55f, 0.4f };
    static constexpr float kFormantQ = 6.0f;      // formant bandwidth

    // ---- per-channel state ----
    struct ChannelState
    {
        std::array<Biquad, kNumFormants> formant;   // parallel band-passes
        StateVariableFilter              body;       // cellular resonance
        DcBlocker                        dc;
        float subSign = 1.0f;                        // octave-down flip-flop
        float prevIn  = 0.0f;                        // zero-cross detector
    };
    std::vector<ChannelState> chan;

    // ---- shared formant morph state (glides when species changes) ----
    std::array<float, kNumFormants> curFormantHz { {} };

    // ---- integrate-and-fire neuron (mono control) ----
    float membrane   = 0.0f;   // leaky membrane potential
    float fireEnv    = 0.0f;   // firing envelope (1 on spike, decays)
    float gateSmooth = 1.0f;   // click-free gate multiplier

    // ---- evolution random walk ----
    Xorshift rng { 0xB105EEDu };
    std::array<float, kNumFormants> driftCur    { {} };
    std::array<float, kNumFormants> driftTarget { {} };
    float evoClock = 0.0f;

    // ---- control-rate bookkeeping & sr-derived coefficients ----
    int   ctrlCounter  = 0;
    float membraneLeak = 0.0f;
    float fireRelease  = 0.0f;
    float gateCoef     = 1.0f;
    float morphCoef    = 1.0f;
    float driftCoef    = 1.0f;

    void updateFormants (float shift, float evolution, int species, float reso, double sr);
};

} // namespace chaos
