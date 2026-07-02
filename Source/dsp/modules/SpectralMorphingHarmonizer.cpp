/*
    CHAOS REALM — Module 1: Spectral Morphing Harmonizer  (implementation)

    See SpectralMorphingHarmonizer.h for the sound-design overview.  All work
    happens in the frequency domain inside processSpectrum(), which the STFT
    invokes once per analysis frame.  Every operator is either multiplicative
    or additive-from-the-existing-spectrum, so a silent (all-zero) spectrum
    stays silent — guaranteeing the settle-to-silence stability contract.
*/
#include "SpectralMorphingHarmonizer.h"

namespace chaos
{

SpectralMorphingHarmonizer::SpectralMorphingHarmonizer()
{
    // ---- fftsize: prepare-time CHOICE parameter (3 steps) ----------------
    // sel 0 -> 2048, 1 -> 4096, 2 -> 8192 (frame size).  Default = 4096.
    {
        ParameterInfo pi { "fftsize", "FFT Size", 0.5f, "smp", 2048.0f, 8192.0f };
        pi.isStepped = true;
        pi.numSteps  = 3;
        pFftSize = addParameter (pi);
    }

    // ---- continuous spectral operators (normalised 0..1) -----------------
    // Defaults chosen so the STFT round-trip is (near) identity: neutral shift
    // and formant sit at 0.5, harmonic / ghost / blur amounts sit at 0.
    pShift    = addParameter ({ "shift",    "Spectral Shift",  0.5f,  "%",  -100.0f, 100.0f });
    pHarm     = addParameter ({ "harmonic", "Harmonic Brush",  0.0f,  "%",     0.0f, 100.0f });
    pGhostInt = addParameter ({ "ghostInt", "Ghost Interval",  0.75f, "st",  -24.0f,  24.0f });
    pGhostAmt = addParameter ({ "ghostAmt", "Ghost Amount",    0.0f,  "%",     0.0f, 100.0f });
    pBlur     = addParameter ({ "blur",     "Spectral Blur",   0.0f,  "%",     0.0f, 100.0f });
    pFormant  = addParameter ({ "formant",  "Formant Tilt",    0.5f,  "%",  -100.0f, 100.0f });
}

// ============================================================================
//  Lifecycle
// ============================================================================
void SpectralMorphingHarmonizer::onPrepare (const ProcessContext& ctx)
{
    const int nch = std::max (1, ctx.numChannels);

    // fftsize is a prepare-time choice: read it RAW (never smoothed).
    const int sel = (int) clampf (raw (pFftSize) * 2.0f + 0.5f, 0.0f, 2.0f);
    fftOrder  = 11 + sel;             // 11 -> 2048, 12 -> 4096, 13 -> 8192
    frameSize = 1 << fftOrder;
    numBins   = frameSize / 2 + 1;

    // One STFT per channel, 75% overlap (overlap factor 4) at the chosen order.
    stfts.resize ((size_t) nch);
    for (auto& s : stfts) s.prepare (fftOrder, 4, ctx.sampleRate);

    // Pre-allocate all spectral scratch so the callback is allocation-free.
    mag0.assign    ((size_t) numBins, 0.0f);
    pha0.assign    ((size_t) numBins, 0.0f);
    workMag.assign ((size_t) numBins, 0.0f);
    workPha.assign ((size_t) numBins, 0.0f);
    scratch.assign ((size_t) numBins, 0.0f);

    // Build the spectral callback once (captures only 'this' -> no heap alloc
    // on the audio thread when passed to STFT::process).
    specCb = [this] (std::vector<std::complex<float>>& spec, int nB, float sr)
             { processSpectrum (spec, nB, sr); };
}

void SpectralMorphingHarmonizer::onReset()
{
    for (auto& s : stfts) s.reset();
    std::fill (mag0.begin(),    mag0.end(),    0.0f);
    std::fill (pha0.begin(),    pha0.end(),    0.0f);
    std::fill (workMag.begin(), workMag.end(), 0.0f);
    std::fill (workPha.begin(), workPha.end(), 0.0f);
    std::fill (scratch.begin(), scratch.end(), 0.0f);
}

// ============================================================================
//  Helpers
// ============================================================================
float SpectralMorphingHarmonizer::readInterp (const std::vector<float>& a, float idx, int n2) noexcept
{
    if (idx < 0.0f || idx > (float) n2) return 0.0f;
    const int   i0 = (int) std::floor (idx);
    const float f  = idx - (float) i0;
    const int   i1 = std::min (i0 + 1, n2);
    return a[(size_t) i0] * (1.0f - f) + a[(size_t) i1] * f;
}

// ============================================================================
//  Spectral operator chain (called by the STFT for each frame)
// ============================================================================
void SpectralMorphingHarmonizer::processSpectrum (std::vector<std::complex<float>>& spec,
                                                   int nBins, float sr)
{
    const int n2 = nBins - 1;   // highest unique bin index (== frameSize/2)

    // --- decompose into magnitude & phase, and track the peak magnitude ----
    float peak = 0.0f;
    for (int b = 0; b <= n2; ++b)
    {
        const std::complex<float> c = spec[(size_t) b];
        const float m = std::abs (c);
        mag0[(size_t) b] = m;
        pha0[(size_t) b] = std::arg (c);
        peak = std::max (peak, m);
    }
    // A bounded ceiling relative to the loudest input bin keeps the output
    // finite no matter how the operators stack (and is 0 for silence -> the
    // output is forced to silence, satisfying the settle contract).
    const float magLimit = peak * 16.0f + 1.0e-9f;

    // --- 1) SHIFT : resample magnitude & phase by a linear bin offset ------
    // offset == 0 at shift == 0.5 -> exact identity.
    const float offset = (mShift - 0.5f) * 2.0f * kMaxShiftBins;
    for (int b = 0; b <= n2; ++b)
    {
        const float src   = (float) b - offset;
        workMag[(size_t) b] = readInterp (mag0, src, n2);
        const int   sp    = (int) std::lround (src);      // carry nearest phase
        workPha[(size_t) b] = (sp >= 0 && sp <= n2) ? pha0[(size_t) sp] : 0.0f;
    }

    // --- 2) HARMONIC BRUSH : boost bins near multiples of a low fundamental -
    if (mHarm > 1.0e-4f)
    {
        const float binHz = sr / (float) frameSize;
        int fund = (int) std::lround (kHarmFundHz / std::max (1.0e-3f, binHz));
        fund = std::max (2, fund);
        const float boost = mHarm * 3.0f;                 // up to +4x at partials
        for (int center = fund; center <= n2; center += fund)
        {
            for (int j = -1; j <= 1; ++j)                 // small triangular window
            {
                const int bin = center + j;
                if (bin < 0 || bin > n2) continue;
                const float w = 1.0f - 0.4f * (float) std::abs (j);
                workMag[(size_t) bin] *= (1.0f + boost * w);
            }
        }
    }

    // --- 3) GHOST HARMONIC : add a transposed copy of the magnitude spectrum
    if (mGhostAmt > 1.0e-4f)
    {
        const float semis = (mGhostInt - 0.5f) * 2.0f * kMaxSemis;  // -24..+24 st
        const float ratio = std::pow (2.0f, semis / 12.0f);         // freq ratio
        const float g     = mGhostAmt * 0.9f;
        // Snapshot so we read a stable source while writing.
        for (int b = 0; b <= n2; ++b) scratch[(size_t) b] = workMag[(size_t) b];
        for (int b = 0; b <= n2; ++b)
        {
            const float src = (float) b / ratio;          // transpose up => lower src
            workMag[(size_t) b] += g * readInterp (scratch, src, n2);
        }
    }

    // --- 4) BLUR : moving-average smear of magnitudes across neighbour bins -
    if (mBlur > 1.0e-4f)
    {
        const int radius = (int) std::lround (mBlur * (float) kMaxBlurRadius);
        if (radius > 0)
        {
            for (int b = 0; b <= n2; ++b) scratch[(size_t) b] = workMag[(size_t) b];
            for (int b = 0; b <= n2; ++b)
            {
                const int lo = std::max (0,  b - radius);
                const int hi = std::min (n2, b + radius);
                float sum = 0.0f;
                for (int k = lo; k <= hi; ++k) sum += scratch[(size_t) k];
                const float avg = sum / (float) (hi - lo + 1);
                workMag[(size_t) b] = lerp (scratch[(size_t) b], avg, mBlur);
            }
        }
    }

    // --- 5) FORMANT : spectral-envelope tilt / warp (neutral at 0.5) --------
    const float tilt = (mFormant - 0.5f) * 2.0f;          // [-1, 1]
    if (std::abs (tilt) > 1.0e-4f)
    {
        const float expo = tilt * kMaxTilt;               // slope exponent
        const float invN = 1.0f / (float) (n2 + 1);
        for (int b = 0; b <= n2; ++b)
        {
            const float x = (float) (b + 1) * invN;       // (0, 1]
            float gain = std::pow (x, expo);              // tilts the magnitude slope
            gain = clampf (gain, 0.05f, 8.0f);
            workMag[(size_t) b] *= gain;
        }
    }

    // --- 6) clamp & recompose the complex spectrum -------------------------
    for (int b = 0; b <= n2; ++b)
    {
        float m = workMag[(size_t) b];
        if (! std::isfinite (m)) m = 0.0f;
        m = clampf (m, 0.0f, magLimit);
        spec[(size_t) b] = std::polar (m, workPha[(size_t) b]);
    }
    // The STFT engine restores Hermitian symmetry before the inverse transform.
}

// ============================================================================
//  Audio processing
// ============================================================================
void SpectralMorphingHarmonizer::process (float* const* buffers, int numChannels, int numSamples)
{
    const int nch = std::min (numChannels, (int) stfts.size());

    for (int n = 0; n < numSamples; ++n)
    {
        // Advance the parameter smoothers once per sample and capture the
        // latest values into members; the spectral callback (fired whenever a
        // frame completes inside STFT::process) reads these members.
        mShift    = smoothed (pShift);
        mHarm     = smoothed (pHarm);
        mGhostInt = smoothed (pGhostInt);
        mGhostAmt = smoothed (pGhostAmt);
        mBlur     = smoothed (pBlur);
        mFormant  = smoothed (pFormant);

        for (int c = 0; c < nch; ++c)
        {
            const float y = stfts[(size_t) c].process (buffers[c][n], specCb);
            buffers[c][n] = sanitise (y);
        }
    }
}

} // namespace chaos
