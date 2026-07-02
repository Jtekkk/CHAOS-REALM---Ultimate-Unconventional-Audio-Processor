/*
    CHAOS REALM — Module 5: Non-Linear Space Creator  (IMPLEMENTATION)

    Modulated 8-line FDN reverb with fractal input diffusion.  Stability rests
    on three pillars:
      1) an orthonormal (energy-preserving) Hadamard feedback matrix, so the
         mixing stage never adds energy (spectral radius == 1);
      2) a per-line feedback gain strictly < 0.95, so every loop decays;
      3) sanitise() on every value written into a delay, so denormals / NaNs in
         the long tail are scrubbed.
    Together these guarantee a FINITE, BOUNDED output that SETTLES TO SILENCE.
*/
#include "NonLinearSpaceCreator.h"

namespace chaos
{

NonLinearSpaceCreator::NonLinearSpaceCreator()
{
    // Register parameters (normalised 0..1).  Defaults give a lush hall that
    // rings out with an RT60 of roughly 1.9 s and always decays to silence.
    pSize       = addParameter ({ "size",       "Size",       0.50f, "%",  0.0f, 100.0f });
    pDecay      = addParameter ({ "decay",      "Decay",      0.60f, "s",  0.2f, 12.0f  });
    pDamping    = addParameter ({ "damping",    "Damping",    0.35f, "%",  0.0f, 100.0f });
    pModulation = addParameter ({ "modulation", "Modulation", 0.25f, "%",  0.0f, 100.0f });
    pDiffusion  = addParameter ({ "diffusion",  "Diffusion",  0.60f, "%",  0.0f, 100.0f });
    pQuantum    = addParameter ({ "quantum",    "Quantum",    0.20f, "%",  0.0f, 100.0f });
    pPredelay   = addParameter ({ "predelay",   "Pre-Delay",  0.10f, "ms", 0.0f, 200.0f });
}

void NonLinearSpaceCreator::onPrepare (const ProcessContext& ctx)
{
    const double sr = ctx.sampleRate;
    srRatio = (float) (sr / 44100.0);

    // Eight mutually-prime base delay lengths (samples @ 44.1 kHz) — primes keep
    // the modal density high and free of ringing at rational ratios.
    const float primes[kLines] =
        { 1129.0f, 1327.0f, 1523.0f, 1721.0f, 1933.0f, 2129.0f, 2333.0f, 2521.0f };

    for (int i = 0; i < kLines; ++i)
    {
        baseSamples[i] = primes[i] * srRatio;

        // Allow head-room for the largest "size" (1.65x) plus LFO modulation.
        const int maxLen = (int) (primes[i] * srRatio * 1.7f)
                           + (int) kMaxModSamps + 64;
        lines[i].prepare (maxLen);
        lines[i].reset();

        dampState[i]     = 0.0f;
        quantumOffset[i] = 0.0f;

        // Distinct, non-harmonic LFO rates (0.13 .. 0.71 Hz) with spread phases.
        lfoPhase[i] = (float) i * 0.7f;
        const float freq = 0.13f + 0.083f * (float) i;
        lfoInc[i] = kTwoPiF * freq / (float) sr;
    }

    // Fractal input diffusion: nested allpasses with mutually-prime lengths.
    const float dsec[kDiffStages] = { 0.00417f, 0.00631f, 0.00937f, 0.01277f };
    for (int s = 0; s < kDiffStages; ++s)
    {
        const int len = (int) (dsec[s] * (float) sr) + 1;
        diffuser[(size_t) s].prepare (len + 8);
        diffuser[(size_t) s].setDelay ((float) len);
        diffuser[(size_t) s].setFeedback (0.55f);
        diffuser[(size_t) s].reset();
    }

    // Pre-delay: up to 200 ms ahead of the network.
    maxPredelaySamples = (float) (sr * 0.2);
    predelay.prepare ((int) maxPredelaySamples + 64);
    predelay.reset();

    for (auto& dc : dcOut) dc.reset();

    // Chaos sources.  A very small Rossler step rate yields a slow (~1 Hz) drift.
    rossler.reset();
    rossler.setRate (0.0001f);
    logistic.reset();
    logistic.setR (3.9f);
    rng.setSeed (0x2B7E1516u);
}

void NonLinearSpaceCreator::onReset()
{
    for (auto& l : lines) l.reset();
    for (int i = 0; i < kLines; ++i)
    {
        dampState[i]     = 0.0f;
        quantumOffset[i] = 0.0f;
        lfoPhase[i]      = (float) i * 0.7f;
    }
    for (auto& d : diffuser) d.reset();
    predelay.reset();
    for (auto& dc : dcOut) dc.reset();
    rossler.reset();
    logistic.reset();
}

void NonLinearSpaceCreator::process (float* const* buffers, int numChannels, int numSamples)
{
    const int    nch = std::min (numChannels, 2);
    const double sr  = context.sampleRate;

    // Fixed +/- sign patterns give decorrelated left / right output taps.
    static const float signL[kLines] = { +1,-1,+1,-1,+1,-1,+1,-1 };
    static const float signR[kLines] = { +1,+1,-1,-1,+1,+1,-1,-1 };
    const float outScale = 1.0f / std::sqrt ((float) kLines);
    const float hNorm    = 1.0f / std::sqrt ((float) kLines);

    for (int n = 0; n < numSamples; ++n)
    {
        // ---- smoothed parameters (advance once per sample) ----
        const float size    = smoothed (pSize);
        const float decay   = smoothed (pDecay);
        const float damping = smoothed (pDamping);
        const float modAmt  = smoothed (pModulation);
        const float diff    = smoothed (pDiffusion);
        const float quantum = smoothed (pQuantum);
        const float preAmt  = smoothed (pPredelay);

        // ---- input: sum to mono, then pre-delay ----
        const float in = (nch >= 2) ? 0.5f * (buffers[0][n] + buffers[1][n])
                                    : buffers[0][n];
        predelay.push (in);
        const float preSamples = clampf (preAmt * maxPredelaySamples,
                                         0.0f, maxPredelaySamples);
        float x = predelay.readLinear (preSamples);

        // ---- fractal input diffusion (blend controlled by "diffusion") ----
        {
            float xd = x;
            for (int s = 0; s < kDiffStages; ++s)
                xd = diffuser[(size_t) s].process (xd);
            x = lerp (x, xd, diff);
        }

        // ---- modulation & decay controls (shared across the 8 lines) ----
        rossler.step();
        const float drift = rossler.outX();               // slow common wander

        const float baseG     = clampf (0.72f + 0.22f * decay, 0.0f, 0.92f);
        const float sizeScale = 0.35f + 1.3f * size;      // 0.35 .. 1.65

        // Shared one-pole damping coefficient (log-scaled cutoff).
        const float dampHz = clampf (18000.0f * std::pow (0.06f, damping),
                                     400.0f, 18000.0f);
        const float dampCoeff = 1.0f - std::exp (-kTwoPiF * dampHz / (float) sr);

        // Quantum: occasionally nudge one line's feedback (bounded to +/-0.04).
        if (quantum > 0.001f && rng.nextFloat() < quantum * 0.0009f)
        {
            const int li = (int) (rng.nextUInt() & 7u);
            quantumOffset[(size_t) li] =
                (logistic.step() - 0.5f) * 2.0f * 0.04f * quantum;
        }

        // ---- read + damp each delay line ----
        std::array<float, kLines> y { };
        for (int i = 0; i < kLines; ++i)
        {
            lfoPhase[i] += lfoInc[i];
            if (lfoPhase[i] >= kTwoPiF) lfoPhase[i] -= kTwoPiF;

            const float depth = modAmt * kMaxModSamps * (1.0f + 0.25f * drift);
            float d = baseSamples[i] * sizeScale + std::sin (lfoPhase[i]) * depth;
            const float hi = (float) lines[i].size() - 4.0f;
            d = clampf (d, kMaxModSamps + 8.0f, hi);

            const float rd = lines[i].readCubic (d);
            dampState[i] += dampCoeff * (rd - dampState[i]);  // one-pole LP
            y[i] = dampState[i];
        }

        // ---- energy-preserving Hadamard mix + bounded feedback ----
        // Fast Walsh-Hadamard transform (entries +/-1), scaled by 1/sqrt(8) to
        // make it orthonormal, then attenuated per line so the loop decays.
        std::array<float, kLines> v = y;
        for (int len = 1; len < kLines; len <<= 1)
            for (int i0 = 0; i0 < kLines; i0 += (len << 1))
                for (int j = 0; j < len; ++j)
                {
                    const float a = v[(size_t) (i0 + j)];
                    const float b = v[(size_t) (i0 + j + len)];
                    v[(size_t) (i0 + j)]       = a + b;
                    v[(size_t) (i0 + j + len)] = a - b;
                }

        for (int i = 0; i < kLines; ++i)
        {
            const float gi = clampf (baseG + quantumOffset[i], 0.0f, 0.94f);
            const float fb = v[i] * hNorm * gi;
            lines[i].push (sanitise (x + fb));            // inject input into all
        }

        // ---- decorrelated stereo output tap ----
        float wl = 0.0f, wr = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            wl += signL[i] * y[i];
            wr += signR[i] * y[i];
        }
        wl = dcOut[0].process (wl * outScale);
        wr = dcOut[1].process (wr * outScale);

        if (nch >= 2)
        {
            buffers[0][n] = sanitise (wl);
            buffers[1][n] = sanitise (wr);
        }
        else
        {
            buffers[0][n] = sanitise (0.5f * (wl + wr));
        }
    }
}

} // namespace chaos
