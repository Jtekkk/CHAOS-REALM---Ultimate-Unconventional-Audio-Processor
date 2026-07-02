/*
    CHAOS REALM — Module 7: Electromagnetic Field Simulator

    Sound-design intent
    -------------------
      * Magnetic hysteresis  — asymmetric saturating transfer WITH memory,
        driven by "flux", giving tape / transformer-like harmonics.
      * RF interference      — broadband bandpassed noise + amplitude-modulated
        crackle, injected proportional to "rf" AND the input envelope.
      * Induction crosstalk  — a filtered, delayed copy of the OTHER channel
        bleeds into each channel by "crosstalk" (guarded for mono).
      * Plasma resonance     — a swept high-Q resonant bandpass whose frequency
        follows "plasmaFreq"/"plasmaRes", with optional sine ring-modulation
        for an ionised sheen ("ionize").
      * Mains hum            — 50/60 Hz + harmonics, envelope-gated by "hum".

    Stability contract: every additive noise / hum / tone generator is scaled by
    an input peak-envelope follower, so silence in => silence out.  Signal-driven
    stages (hysteresis, plasma resonance, crosstalk) settle to silence naturally.
*/
#pragma once

#include "../ChaosMath.h"
#include "../ModuleBase.h"

#include <array>
#include <vector>

namespace chaos
{

class ElectromagneticFieldSimulator : public ModuleBase
{
public:
    ElectromagneticFieldSimulator();

    const char* getName() const override { return "Electromagnetic Field Simulator"; }
    ModuleID    getID()   const override { return ModuleID::ElectromagneticFieldSimulator; }

    void process (float* const* buffers, int numChannels, int numSamples) override;

protected:
    void onPrepare (const ProcessContext& ctx) override;
    void onReset() override;

private:
    // ---- parameter indices (all continuous, normalised 0..1) ----
    int pFlux = 0, pRf = 0, pCrosstalk = 0, pPlasmaFreq = 0, pPlasmaRes = 0, pIonize = 0, pHum = 0;

    // ---- magnetic hysteresis (per channel) ----
    std::vector<float>     hist;     // last output -> transfer memory term
    std::vector<DcBlocker> histDc;   // removes the asymmetric bias DC

    // ---- plasma resonance (per channel) ----
    std::vector<StateVariableFilter> plasma;

    // ---- RF interference (per channel) ----
    std::vector<Biquad> rfBand;      // shapes the broadband noise into an RF band
    std::vector<float>  crackle;     // sparse amplitude-modulation crackle state
    float crackleDecay = 0.0f;       // per-sample decay of a crackle burst

    // ---- induction crosstalk (per channel) ----
    std::vector<DelayLine> xtalk;    // short delay holding the dry channel signal
    std::vector<OnePole>   xtalkHp;  // inductive coupling emphasises highs
    float xtalkSamples = 8.0f;

    // ---- shared generators / envelope ----
    Xorshift rng { 0x0EF1E1D5u };
    float envFollow  = 0.0f;         // input peak-envelope follower
    float envRelease = 0.0f;         // release coefficient of the follower
    float humPhase   = 0.0f, humInc = 0.0f;   // mains hum oscillator
    float ionPhase   = 0.0f, ionInc = 0.0f;   // ionise ring-mod oscillator

    static constexpr float kHumFreq = 60.0f;  // mains fundamental (Hz)
};

} // namespace chaos
