/*
    CHAOS REALM — Module 1: Spectral Morphing Harmonizer

    Sound-design intent
    -------------------
      An FFT / STFT frequency-domain morphing engine.  A streaming
      Weighted-Overlap-Add STFT (75% overlap, one per channel) hands the
      per-frame magnitude/phase spectrum to a chain of spectral operators:

        * Shift    — linear magnitude/phase bin shift (inharmonic frequency
                     shift up or down; 0.5 = neutral).
        * Harmonic — a "harmonic brush" that boosts bins near integer
                     multiples of a low fundamental bin (adds a comb of
                     partials, emphasising a pitched core).
        * Ghost    — a scaled, bin-ratio-transposed copy of the magnitude
                     spectrum added back at a musical interval (semitones ->
                     frequency ratio): a spectral harmoniser / shimmer voice.
        * Blur     — a magnitude moving-average across neighbouring bins that
                     smears the spectrum (soft, cloud-like timbre).
        * Formant  — a spectral-envelope tilt / warp that reshapes the overall
                     magnitude slope (0.5 = neutral).

    Every operator is transparent at its default value, so with default
    parameters the STFT round-trip is (near) identity and silence in yields
    silence out.  The whole processor is FIR (no feedback), so it always
    settles to silence within one frame of latency.

    Pure C++17, depending only on ChaosMath.h / ModuleBase.h.  fftsize is a
    prepare-time choice parameter; the STFT frame size is reported as the
    module latency so the engine can dry-align.
*/
#pragma once

#include "../ChaosMath.h"
#include "../ModuleBase.h"

#include <complex>
#include <vector>

namespace chaos
{

class SpectralMorphingHarmonizer : public ModuleBase
{
public:
    SpectralMorphingHarmonizer();

    const char* getName() const override { return "Spectral Morphing Harmonizer"; }
    ModuleID    getID()   const override { return ModuleID::SpectralMorphingHarmonizer; }

    void process (float* const* buffers, int numChannels, int numSamples) override;

    /** STFT introduces exactly one frame of latency; report it for dry-align. */
    int getLatencySamples() const override { return frameSize; }

protected:
    void onPrepare (const ProcessContext& ctx) override;
    void onReset() override;

private:
    // ---- parameter indices ----
    int pFftSize = 0, pShift = 0, pHarm = 0, pGhostInt = 0, pGhostAmt = 0, pBlur = 0, pFormant = 0;

    // ---- fixed operator ranges ----
    static constexpr float kMaxShiftBins  = 60.0f;  // +/- linear bin shift
    static constexpr float kMaxSemis      = 24.0f;  // +/- ghost interval (semitones)
    static constexpr int   kMaxBlurRadius = 12;     // widest magnitude smear
    static constexpr float kMaxTilt       = 1.5f;   // formant slope exponent
    static constexpr float kHarmFundHz    = 110.0f; // fundamental for harmonic brush

    // ---- STFT engines (one per channel) ----
    std::vector<STFT> stfts;
    STFT::SpectralCallback specCb;      // built once in onPrepare (no RT alloc)

    int fftOrder  = 12;                 // log2 frame size
    int frameSize = 4096;               // STFT frame size (== latency)
    int numBins   = 2049;               // frameSize / 2 + 1

    // ---- per-block captured (smoothed) parameter values ----
    // Written once per sample in process(); read by the spectral callback.
    float mShift    = 0.5f;
    float mHarm     = 0.0f;
    float mGhostInt = 0.75f;
    float mGhostAmt = 0.0f;
    float mBlur     = 0.0f;
    float mFormant  = 0.5f;

    // ---- pre-allocated spectral scratch (sized numBins) ----
    std::vector<float> mag0, pha0, workMag, workPha, scratch;

    /** The spectral operator chain, invoked by the STFT for each frame. */
    void processSpectrum (std::vector<std::complex<float>>& spec, int nBins, float sr);

    /** Linear read of a magnitude array at a fractional bin index (0 if OOB). */
    static float readInterp (const std::vector<float>& a, float idx, int n2) noexcept;
};

} // namespace chaos
