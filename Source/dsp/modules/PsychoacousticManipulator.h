/*
    CHAOS REALM — Module 3: Psychoacoustic Manipulator

    Sound-design intent
    -------------------
      * Binaural beats     — a sine in the left ear at the carrier frequency and
                             a sine in the right ear detuned by 0..30 Hz.  The
                             brain "hears" the difference tone (delta/theta/alpha
                             range) even though it is never physically present.
      * Shepard / Risset   — a bank of octave-spaced partials under a slowly
                             gliding Gaussian spectral window, producing the
                             classic endless-glissando auditory illusion.
      * Spatial width      — an exaggerated Haas (precedence-effect) inter-aural
                             delay plus a small level tilt to widen the image.
      * Dissonance         — a tiny per-channel random micro-timing jitter that
                             decorrelates the ears and adds an unsettling shimmer.

    Stability contract
    ------------------
      EVERY internal oscillator/tone (binaural + Shepard) is scaled by an inline
      one-pole PEAK FOLLOWER of the input.  Silence in therefore forces the
      envelope — and hence all generators — to zero, so the module always
      settles back to silence.  The Haas / jitter delays merely re-time the
      input, so they flush to silence on their own.

    Pure C++17, depends only on ChaosMath.h / ModuleBase.h (no JUCE).
*/
#pragma once

#include "../ChaosMath.h"
#include "../ModuleBase.h"

#include <array>
#include <vector>

namespace chaos
{

class PsychoacousticManipulator : public ModuleBase
{
public:
    PsychoacousticManipulator();

    const char* getName() const override { return "Psychoacoustic Manipulator"; }
    ModuleID    getID()   const override { return ModuleID::PsychoacousticManipulator; }

    void process (float* const* buffers, int numChannels, int numSamples) override;

protected:
    void onPrepare (const ProcessContext& ctx) override;
    void onReset() override;

private:
    // ---- parameter indices ----
    int pBinaural = 0, pCarrier = 0, pBeat = 0, pShepard = 0, pMotion = 0, pWidth = 0, pDissonance = 0;

    // ---- shared input envelope (one-pole peak follower) ----
    float envFollow  = 0.0f;   // current peak-follower state
    float envRelCoef = 0.0f;   // per-sample release coefficient (set in onPrepare)

    // ---- binaural beat oscillators (phases in turns, 0..1) ----
    float binPhaseL = 0.0f;
    float binPhaseR = 0.0f;

    // ---- Shepard / Risset partial bank ----
    static constexpr int kPartials = 6;          // octaves spanned by the bank
    std::array<float, kPartials> shepPhase { };   // per-partial phase (turns)
    float shepPos  = 0.0f;                         // global glissando position 0..1
    float fBase    = 55.0f;                        // lowest partial frequency (Hz)

    // ---- spatial width (Haas) + dissonance (jitter), per channel ----
    std::vector<DelayLine> spatial;    // one modulated delay line per channel
    std::vector<float> jitterCur;      // smoothed current jitter delay (samples)
    std::vector<float> jitterTarget;   // random target the jitter walks toward
    std::vector<int>   jitterCount;    // countdown until the next random target
    float jitterCoef  = 0.0f;          // per-sample jitter smoothing coefficient
    int   jitterPeriod = 1;            // samples between fresh jitter targets

    float maxHaasSamples   = 0.0f;     // ~30 ms of inter-aural delay
    float maxJitterSamples = 0.0f;     // ~3 ms of micro-timing wander

    Xorshift rng { 0x2B7E1516u };
};

} // namespace chaos
