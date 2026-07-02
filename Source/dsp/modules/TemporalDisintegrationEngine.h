/*
    CHAOS REALM — Module 8: Temporal Disintegration Engine

    Sound-design intent
    -------------------
      Time itself is the raw material here.  The module treats the incoming
      stream as a memory that is decaying, folding back on itself and losing
      causality:

        * Fold     — several overlapping delay taps read the same recorded
                     past at different offsets and are cross-faded together, so
                     distinct moments smear into one another.
        * Reverse  — short segments are captured and replayed with a backward
                     travelling read pointer, breaking causality (sounds arrive
                     before they "happened").
        * Decay    — a tape-memory feedback delay with tanh saturation and a
                     low-pass in the loop; each pass is darker and more worn.
        * Flutter  — wow (slow) + flutter (fast) LFO modulation of the read
                     positions, the classic sagging-tape pitch instability.
        * Smear    — stochastic (Xorshift) timing jitter on the tap positions,
                     a probabilistic blurring of when each moment is heard.
        * Age      — extra darkening + saturation, the sound of a memory that
                     has been recalled too many times.
        * Feedback — a global, tanh-limited regeneration path so the whole
                     disintegration can slowly consume itself.

    Stability contract
    ------------------
      Every feedback coefficient is kept strictly below 0.9 and every feedback
      loop runs through chaos::fastTanh, so the output is always finite,
      bounded, and settles to silence once the input stops.

    Depends only on ChaosMath.h / ModuleBase.h (pure C++17, no JUCE).
*/
#pragma once

#include "../ChaosMath.h"
#include "../ModuleBase.h"

#include <array>
#include <vector>

namespace chaos
{

class TemporalDisintegrationEngine : public ModuleBase
{
public:
    TemporalDisintegrationEngine();

    const char* getName() const override { return "Temporal Disintegration Engine"; }
    ModuleID    getID()   const override { return ModuleID::TemporalDisintegrationEngine; }

    void process (float* const* buffers, int numChannels, int numSamples) override;

protected:
    void onPrepare (const ProcessContext& ctx) override;
    void onReset() override;

private:
    // ---- parameter indices ----
    int pFold = 0, pReverse = 0, pDecay = 0, pFlutter = 0, pSmear = 0, pAge = 0, pFeedback = 0;

    // ---- multi-tap overlapping delay ----
    static constexpr int kNumTaps = 4;
    std::array<float, kNumTaps> tapBase { };   // base tap delays (samples), shared per channel

    // ---- per-channel disintegration state ----
    struct Channel
    {
        DelayLine record;                       // source for the overlapping taps
        DelayLine tape;                         // tape-memory feedback delay
        OnePole   tapeLP;                       // tone / damping inside the tape loop
        OnePole   ageLP;                        // output darkening ("age")
        DcBlocker dc;                           // keep the long tails DC-free

        std::vector<float> revBuf;              // captured segment for reverse playback
        int   revWrite = 0;                     // forward-moving capture pointer
        float revRead  = 0.0f;                  // backward-moving playback pointer

        float wowPhase  = 0.0f;                 // slow pitch-sag LFO phase
        float flutPhase = 0.0f;                 // fast flutter LFO phase
        float globalFb  = 0.0f;                 // global (tanh-limited) feedback state

        std::array<float, kNumTaps> jitter       { }; // current smear offset per tap
        std::array<float, kNumTaps> jitterTarget { }; // sample-&-held target per tap
        int   jitterClock = 0;                  // countdown to next jitter update
    };

    std::vector<Channel> channels;
    Xorshift rng { 0x7A11C0DEu };

    // ---- prepared, sample-rate dependent sizes ----
    float recordSamples = 0.0f;   // length of the tap record buffer
    int   segSamples    = 0;      // length of the reverse segment buffer
    float tapeBase      = 0.0f;   // base tape-memory delay (samples)
    float wowInc        = 0.0f;   // slow LFO phase increment (rad/sample)
    float flutInc       = 0.0f;   // fast LFO phase increment (rad/sample)
    float maxModSamples = 0.0f;   // peak wow+flutter delay excursion (samples)
    float smearRange    = 0.0f;   // peak smear jitter excursion (samples)
};

} // namespace chaos
