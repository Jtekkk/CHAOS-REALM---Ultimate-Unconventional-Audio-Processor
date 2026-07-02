/*
    CHAOS REALM — Module 8: Temporal Disintegration Engine  (IMPLEMENTATION)

    See TemporalDisintegrationEngine.h for the sound-design overview.  The audio
    path per sample/channel is:

        input --> global feedback inject --> record buffer
              --> overlapping multi-tap read (fold)
              --> reverse-segment blend (reverse)
              --> tape-memory feedback loop (decay)
              --> age darkening + saturation
              --> DC block --> output (also stored as the global feedback state)

    All feedback coefficients are held below 0.9 and pass through fastTanh, and
    every write is scrubbed with chaos::sanitise, guaranteeing a finite, bounded
    signal that decays to silence.
*/
#include "TemporalDisintegrationEngine.h"

namespace chaos
{

TemporalDisintegrationEngine::TemporalDisintegrationEngine()
{
    // Parameters are normalised 0..1; the display ranges are purely cosmetic.
    // Defaults are moderate so the module is characterful but neutral enough to
    // be inserted anywhere without running away.
    pFold     = addParameter ({ "fold",     "Fold",     0.25f, "%", 0.0f, 100.0f });
    pReverse  = addParameter ({ "reverse",  "Reverse",  0.00f, "%", 0.0f, 100.0f });
    pDecay    = addParameter ({ "decay",    "Decay",    0.35f, "%", 0.0f, 100.0f });
    pFlutter  = addParameter ({ "flutter",  "Flutter",  0.20f, "%", 0.0f, 100.0f });
    pSmear    = addParameter ({ "smear",    "Smear",    0.20f, "%", 0.0f, 100.0f });
    pAge      = addParameter ({ "age",      "Age",      0.30f, "%", 0.0f, 100.0f });
    pFeedback = addParameter ({ "feedback", "Feedback", 0.20f, "%", 0.0f, 100.0f });
}

void TemporalDisintegrationEngine::onPrepare (const ProcessContext& ctx)
{
    const int    nch = std::max (1, ctx.numChannels);
    const double sr  = ctx.sampleRate;

    // Record buffer holds ~0.5 s of past for the overlapping taps.
    recordSamples = (float) (sr * 0.5);

    // Reverse segment: replay the last ~0.15 s backwards.
    segSamples = (int) (sr * 0.15) + 1;

    // Tape-memory base delay (~90 ms) plus head-room for wow/flutter excursion.
    tapeBase       = (float) (sr * 0.09);
    maxModSamples  = (float) (sr * 0.004);   // peak +/- wow+flutter excursion
    smearRange     = (float) (sr * 0.003);   // peak smear jitter excursion

    // LFO increments (radians/sample): wow ~0.7 Hz, flutter ~6.3 Hz.
    wowInc  = kTwoPiF * 0.7f  / (float) sr;
    flutInc = kTwoPiF * 6.3f  / (float) sr;

    // Fixed, mutually-overlapping tap offsets (seconds -> samples).
    const float tapSec[kNumTaps] = { 0.011f, 0.043f, 0.081f, 0.137f };
    for (int i = 0; i < kNumTaps; ++i)
        tapBase[(size_t) i] = tapSec[i] * (float) sr;

    channels.resize ((size_t) nch);
    for (int c = 0; c < nch; ++c)
    {
        Channel& ch = channels[(size_t) c];

        ch.record.prepare ((int) recordSamples + 8);
        ch.record.reset();

        // Tape delay needs room for base + modulation.
        ch.tape.prepare ((int) (tapeBase + maxModSamples) + 8);
        ch.tape.reset();

        // Filters start bright; age/process moves them per-sample.
        ch.tapeLP.setCutoff (8000.0f, sr, false);
        ch.tapeLP.reset();
        ch.ageLP.setCutoff (18000.0f, sr, false);
        ch.ageLP.reset();
        ch.dc.reset();

        ch.revBuf.assign ((size_t) segSamples, 0.0f);
        ch.revWrite = 0;
        ch.revRead  = (float) (segSamples - 1);

        // De-correlate the two channels' modulators for a wider image.
        ch.wowPhase  = (float) c * 0.37f;
        ch.flutPhase = (float) c * 1.13f;
        ch.globalFb  = 0.0f;

        ch.jitter.fill (0.0f);
        ch.jitterTarget.fill (0.0f);
        ch.jitterClock = 0;
    }
}

void TemporalDisintegrationEngine::onReset()
{
    for (auto& ch : channels)
    {
        ch.record.reset();
        ch.tape.reset();
        ch.tapeLP.reset();
        ch.ageLP.reset();
        ch.dc.reset();

        std::fill (ch.revBuf.begin(), ch.revBuf.end(), 0.0f);
        ch.revWrite = 0;
        ch.revRead  = (float) (segSamples - 1);

        ch.globalFb = 0.0f;
        ch.jitter.fill (0.0f);
        ch.jitterTarget.fill (0.0f);
        ch.jitterClock = 0;
    }
}

void TemporalDisintegrationEngine::process (float* const* buffers, int numChannels, int numSamples)
{
    const int    nch = std::min (numChannels, (int) channels.size());
    const double sr  = context.sampleRate;

    for (int n = 0; n < numSamples; ++n)
    {
        // --- continuous parameters: advance the smoothers once per sample ---
        const float fold     = smoothed (pFold);
        const float reverse  = smoothed (pReverse);
        const float decay    = smoothed (pDecay);
        const float flutter  = smoothed (pFlutter);
        const float smear    = smoothed (pSmear);
        const float age      = smoothed (pAge);
        const float feedback = smoothed (pFeedback);

        // Feedback coefficients — HARD capped below 0.9 for the stability
        // contract (both loops are also tanh-limited below).
        const float tapeFb    = clampf (decay    * 0.86f, 0.0f, 0.86f);
        const float globalFbG = clampf (feedback * 0.82f, 0.0f, 0.82f);

        // Age moves both damping filters darker and drives the saturation.
        const float tapeCut = 8000.0f - age * 6000.0f;   // 8k -> 2k Hz
        const float ageCut  = 18000.0f - age * 15500.0f; // 18k -> 2.5k Hz
        const float ageDrive = 1.0f + age * 3.0f;

        for (int c = 0; c < nch; ++c)
        {
            Channel& ch = channels[(size_t) c];
            const float in = buffers[c][n];

            // ---- global feedback injection (tanh-limited, < 0.9) ----
            const float inj = in + globalFbG * fastTanh (ch.globalFb);
            ch.record.push (sanitise (inj));

            // ---- wow & flutter: slow sag + fast jitter of read positions ----
            ch.wowPhase  += wowInc;
            ch.flutPhase += flutInc;
            if (ch.wowPhase  > kTwoPiF) ch.wowPhase  -= kTwoPiF;
            if (ch.flutPhase > kTwoPiF) ch.flutPhase -= kTwoPiF;
            const float wow  = std::sin (ch.wowPhase);
            const float flut = std::sin (ch.flutPhase);
            const float mod  = (0.7f * wow + 0.3f * flut) * flutter * maxModSamples;

            // ---- probability smear: sample-&-hold stochastic tap jitter ----
            if (--ch.jitterClock <= 0)
            {
                // Re-roll targets a few hundred times per second.
                ch.jitterClock = 128 + (int) (rng.nextFloat() * 256.0f);
                for (int t = 0; t < kNumTaps; ++t)
                    ch.jitterTarget[(size_t) t] = rng.nextBipolar() * smearRange;
            }
            // Glide toward the held targets (scaled by smear amount).
            for (int t = 0; t < kNumTaps; ++t)
                ch.jitter[(size_t) t] += (ch.jitterTarget[(size_t) t] * smear
                                          - ch.jitter[(size_t) t]) * 0.002f;

            // ---- overlapping multi-tap read (cross-faded by fold) ----
            // Tap 0 is always present; higher taps fade in with fold so that
            // more overlapping temporal regions blend together as it opens.
            float tapMix = 0.0f, wSum = 0.0f;
            for (int t = 0; t < kNumTaps; ++t)
            {
                const float w = (t == 0) ? 1.0f : fold;
                if (w <= 1.0e-4f) continue;
                float d = tapBase[(size_t) t] + mod + ch.jitter[(size_t) t];
                d = clampf (d, 1.0f, recordSamples - 2.0f);
                tapMix += w * ch.record.readCubic (d);
                wSum   += w;
            }
            tapMix /= (wSum > 1.0e-6f ? wSum : 1.0f);

            // fold=0 -> dry injected signal; fold=1 -> full overlapping blend.
            const float folded = lerp (inj, tapMix, fold);

            // ---- reverse / broken causality ----
            // Capture forward, replay the segment with a backward pointer, and
            // window the read position so wrap-arounds don't click.
            ch.revBuf[(size_t) ch.revWrite] = folded;
            ch.revWrite = (ch.revWrite + 1) % segSamples;

            int   ri   = (int) ch.revRead;
            if (ri < 0) ri = 0; else if (ri >= segSamples) ri = segSamples - 1;
            const float revEnv = std::sin (kPiF * (float) ri / (float) segSamples);
            const float rev    = ch.revBuf[(size_t) ri] * revEnv;

            ch.revRead -= 1.0f;               // travel backwards through time
            if (ch.revRead < 0.0f) ch.revRead += (float) segSamples;

            const float smeared = lerp (folded, rev, reverse);

            // ---- tape-memory feedback loop (decay), tanh + LP damped ----
            const float tapeDly = clampf (tapeBase + mod, 1.0f,
                                          tapeBase + maxModSamples);
            float tapeOut = ch.tape.readLinear (tapeDly);
            ch.tapeLP.setCutoff (clampf (tapeCut, 200.0f, (float) sr * 0.45f), sr, false);
            tapeOut = ch.tapeLP.process (tapeOut);

            // Saturate the loop input (bounded), then write.  The fresh input
            // is scaled by (1 - tapeFb) so the resonant loop has ~unity DC gain
            // instead of 1/(1-fb) — essential so the *global* feedback loop that
            // wraps this one cannot see an amplified tail and run away.
            const float loopIn = fastTanh (smeared * (1.0f - tapeFb) + tapeFb * tapeOut);
            ch.tape.push (sanitise (loopIn));

            // More decay -> more of the disintegrating tail in the output.
            float wet = lerp (smeared, tapeOut, 0.35f + 0.6f * decay);

            // ---- age: darken then saturate a "worn" copy, blend by age ----
            // The saturator is normalised by 1/ageDrive so its small-signal gain
            // stays at unity (it only ever compresses, never amplifies) — this
            // sits inside the global feedback loop, so extra gain here would
            // break the "settles to silence" contract.
            ch.ageLP.setCutoff (clampf (ageCut, 300.0f, (float) sr * 0.45f), sr, false);
            const float dark = ch.ageLP.process (wet);
            wet = lerp (wet, fastTanh (dark * ageDrive) * (1.0f / ageDrive), age);

            // Keep the long feedback tails free of DC / denormals.
            wet = ch.dc.process (wet);

            // Store the (bounded) output as next sample's global feedback state.
            ch.globalFb = wet;

            buffers[c][n] = sanitise (wet);
        }
    }
}

} // namespace chaos
