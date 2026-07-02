/*
    CHAOS REALM — Module 4: Micro-Texture Processor  (REFERENCE IMPLEMENTATION)
*/
#include "MicroTextureProcessor.h"

namespace chaos
{

MicroTextureProcessor::MicroTextureProcessor()
{
    // Register parameters (normalised 0..1). Defaults chosen so the module is
    // gently characterful but never unstable.
    pDensity  = addParameter ({ "density",  "Grain Density", 0.35f, "",   0.0f, 100.0f });
    pSize     = addParameter ({ "size",     "Grain Size",    0.40f, "ms", 5.0f, 400.0f });
    pScatter  = addParameter ({ "scatter",  "Scatter",       0.25f, "%",  0.0f, 100.0f });
    pSrReduce = addParameter ({ "srreduce", "SR Reduce",     0.20f, "x",  1.0f, 50.0f  });
    pBits     = addParameter ({ "bits",     "Bit Depth",     0.85f, "bit",2.0f, 16.0f  });
    pSmear    = addParameter ({ "smear",    "Temporal Smear",0.20f, "%",  0.0f, 100.0f });
    pStereo   = addParameter ({ "stereo",   "Stereo Spread", 0.50f, "%",  0.0f, 100.0f });
}

void MicroTextureProcessor::onPrepare (const ProcessContext& ctx)
{
    const int nch = std::max (1, ctx.numChannels);
    maxRecordSamples = (float) (ctx.sampleRate * 1.2); // 1.2 s of grain material

    record.resize ((size_t) nch);
    for (auto& r : record) { r.prepare ((int) maxRecordSamples + 8); r.reset(); }

    srHold.assign ((size_t) nch, 0.0f);
    srPhase.assign ((size_t) nch, 0.0f);
    bitError.assign ((size_t) nch, 0.0f);

    smear.resize ((size_t) nch);
    for (int c = 0; c < nch; ++c)
    {
        // Mutually-prime allpass lengths keep the diffusion dense & metallic-free.
        const float base[kSmearStages] = { 0.0047f, 0.0071f, 0.0113f, 0.0173f };
        for (int s = 0; s < kSmearStages; ++s)
        {
            int len = (int) (base[s] * (float) ctx.sampleRate) + 1 + c * 3;
            smear[(size_t) c][(size_t) s].prepare (len + 4);
            smear[(size_t) c][(size_t) s].setDelay ((float) len);
            smear[(size_t) c][(size_t) s].setFeedback (0.7f);
        }
    }

    for (auto& g : grains) g.active = false;
    grainClock = 0.0f;
}

void MicroTextureProcessor::onReset()
{
    for (auto& r : record) r.reset();
    for (auto& c : smear) for (auto& s : c) s.reset();
    std::fill (srHold.begin(), srHold.end(), 0.0f);
    std::fill (srPhase.begin(), srPhase.end(), 0.0f);
    std::fill (bitError.begin(), bitError.end(), 0.0f);
    for (auto& g : grains) g.active = false;
    grainClock = 0.0f;
}

void MicroTextureProcessor::triggerGrain()
{
    // Find a free voice.
    for (auto& g : grains)
    {
        if (! g.active)
        {
            const float scatter = raw (pScatter);
            const float sizeMs  = getParameterInfo (pSize).toDisplay (raw (pSize));
            const float lenSamp = clampf (sizeMs * 0.001f * (float) context.sampleRate,
                                          16.0f, maxRecordSamples * 0.5f);

            // Start reading a little way into the recorded past, scattered.
            const float scatterRange = scatter * maxRecordSamples * 0.4f;
            g.delay = clampf (lenSamp + rng.nextFloat() * scatterRange,
                              2.0f, maxRecordSamples - 2.0f);
            // Pitch scatter: up to +/- one octave, scaled by scatter.
            const float semis = (rng.nextBipolar()) * scatter * 12.0f;
            g.rate  = std::pow (2.0f, semis / 12.0f);
            g.phase = 0.0f;
            g.inc   = 1.0f / lenSamp;
            g.pan   = 0.5f + rng.nextBipolar() * 0.5f * raw (pStereo);
            g.pan   = clampf (g.pan, 0.0f, 1.0f);
            g.active = true;
            return;
        }
    }
}

void MicroTextureProcessor::process (float* const* buffers, int numChannels, int numSamples)
{
    const int nch = std::min (numChannels, (int) record.size());
    const double sr = context.sampleRate;

    for (int n = 0; n < numSamples; ++n)
    {
        const float density = smoothed (pDensity);
        const float srAmt   = smoothed (pSrReduce);
        const float bitsN   = smoothed (pBits);
        const float smearN  = smoothed (pSmear);

        // Push input into the record buffers.
        for (int c = 0; c < nch; ++c) record[(size_t) c].push (buffers[c][n]);

        // ---- granular scheduling ----
        // Grain rate: 1..~400 grains/sec mapped from density.
        const float grainsPerSec = 1.0f + density * density * 400.0f;
        grainClock -= 1.0f;
        if (grainClock <= 0.0f)
        {
            triggerGrain();
            const float period = (float) sr / std::max (1.0f, grainsPerSec);
            grainClock += period * (0.7f + 0.6f * rng.nextFloat());
        }

        // ---- render grains per channel ----
        std::array<float, 8> grainOut { };
        for (auto& g : grains)
        {
            if (! g.active) continue;
            const float env = 0.5f - 0.5f * std::cos (kTwoPiF * g.phase); // Hann
            for (int c = 0; c < nch; ++c)
            {
                const float s = record[(size_t) c].readCubic (g.delay);
                // Constant-power pan.
                const float pan = (c == 0) ? (1.0f - g.pan) : g.pan;
                grainOut[(size_t) c] += s * env * (nch > 1 ? pan : 1.0f);
            }
            g.delay -= (1.0f - g.rate);         // advance read at playback rate
            g.delay  = clampf (g.delay, 1.0f, maxRecordSamples - 2.0f);
            g.phase += g.inc;
            if (g.phase >= 1.0f) g.active = false;
        }

        // Grain gain compensation (Hann overlap of a dense cloud ~ 1).
        const float grainGain = 1.6f;

        for (int c = 0; c < nch; ++c)
        {
            const float dry = buffers[c][n];
            // Crossfade dry <-> granular by density so density=0 is transparent.
            float x = lerp (dry, grainOut[(size_t) c] * grainGain, clampf (density * 1.3f, 0.0f, 1.0f));

            // ---- sample-rate reduction (sample & hold) ----
            // step = 1 (transparent) .. ~50 as srAmt rises.
            const float step = 1.0f + srAmt * srAmt * 49.0f;
            srPhase[(size_t) c] += 1.0f;
            if (srPhase[(size_t) c] >= step)
            {
                srPhase[(size_t) c] -= step;
                srHold[(size_t) c] = x;
            }
            x = srHold[(size_t) c];

            // ---- bit-depth reduction w/ noise shaping ----
            // bitsN=1 -> 16 bits (transparent-ish); bitsN=0 -> 2 bits.
            const float bits  = 2.0f + bitsN * 14.0f;
            const float levels = std::pow (2.0f, bits) - 1.0f;
            const float shaped = x + bitError[(size_t) c];
            const float q = std::round (shaped * 0.5f * levels) / (0.5f * levels);
            bitError[(size_t) c] = shaped - q;   // first-order error feedback
            x = q;

            // ---- temporal smear (allpass diffusion) ----
            if (smearN > 0.001f)
            {
                float d = x;
                for (int s = 0; s < kSmearStages; ++s)
                {
                    smear[(size_t) c][(size_t) s].setFeedback (0.4f + 0.55f * smearN);
                    d = smear[(size_t) c][(size_t) s].process (d);
                }
                x = lerp (x, d, smearN);
            }

            buffers[c][n] = sanitise (x);
        }
    }
}

} // namespace chaos
