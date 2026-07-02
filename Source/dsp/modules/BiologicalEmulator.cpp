/*
    CHAOS REALM — Module 6: Biological Emulator  (implementation)
*/
#include "BiologicalEmulator.h"

namespace chaos
{

// C++17 requires an out-of-line definition for odr-used static constexpr members.
constexpr float BiologicalEmulator::kFormantHz[BiologicalEmulator::kNumSpecies]
                                              [BiologicalEmulator::kNumFormants];
constexpr float BiologicalEmulator::kFormantGain[BiologicalEmulator::kNumFormants];

BiologicalEmulator::BiologicalEmulator()
{
    // "species" is a discrete choice between the formant maps above.
    {
        ParameterInfo p { "species", "Species", 0.0f, "", 0.0f, 4.0f };
        p.isStepped = true;
        p.numSteps  = kNumSpecies;          // Human / Feline / Avian / Insectoid / Cetacean
        pSpecies = addParameter (p);
    }

    // Continuous parameters (all normalised 0..1).
    pFormantShift = addParameter ({ "formantShift", "Formant Shift", 0.50f, "x",  0.5f,  2.0f  });
    pNeuralRate   = addParameter ({ "neuralRate",   "Neural Rate",   0.40f, "%",  0.0f,  100.0f });
    pNeuralDepth  = addParameter ({ "neuralDepth",  "Neural Depth",  0.30f, "%",  0.0f,  100.0f });
    pResonance    = addParameter ({ "resonance",    "Resonance",     0.35f, "%",  0.0f,  100.0f });
    pEvolution    = addParameter ({ "evolution",    "Evolution",     0.25f, "%",  0.0f,  100.0f });
    pGrowl        = addParameter ({ "growl",        "Growl",         0.20f, "%",  0.0f,  100.0f });
}

void BiologicalEmulator::onPrepare (const ProcessContext& ctx)
{
    const int    nch = std::max (1, ctx.numChannels);
    const double sr  = ctx.sampleRate;
    const float  srf = (float) sr;

    chan.assign ((size_t) nch, ChannelState {});

    // Seed the morphing formant frequencies from the default species so the
    // first block already sounds correct (no zero-Hz glide from silence).
    const int species0 = (int) clampf (raw (pSpecies) * (kNumSpecies - 1) + 0.5f,
                                       0.0f, (float) (kNumSpecies - 1));
    for (int f = 0; f < kNumFormants; ++f)
        curFormantHz[(size_t) f] = kFormantHz[species0][f];

    driftCur.fill (0.0f);
    driftTarget.fill (0.0f);

    // Time constants (seconds) -> per-sample / per-control-block coefficients.
    membraneLeak = std::exp (-1.0f / (0.30f * srf));                 // membrane leak (~0.3 s)
    fireRelease  = std::exp (-1.0f / (0.06f * srf));                 // spike release (~60 ms)
    gateCoef     = 1.0f - std::exp (-1.0f / (0.0015f * srf));        // gate de-click (~1.5 ms)
    morphCoef    = 1.0f - std::exp (-(float) kCtrlInterval / (0.060f * srf)); // species glide
    driftCoef    = 1.0f - std::exp (-(float) kCtrlInterval / (0.800f * srf)); // evolution drift

    membrane = 0.0f;
    fireEnv  = 0.0f;
    gateSmooth = 1.0f;
    ctrlCounter = 0;      // force a coefficient refresh on the very first sample
    evoClock   = 0.0f;

    updateFormants (raw (pFormantShift), raw (pEvolution), species0, raw (pResonance), sr);
}

void BiologicalEmulator::onReset()
{
    for (auto& c : chan)
    {
        for (auto& f : c.formant) f.reset();
        c.body.reset();
        c.dc.reset();
        c.subSign = 1.0f;
        c.prevIn  = 0.0f;
    }
    membrane   = 0.0f;
    fireEnv    = 0.0f;
    gateSmooth = 1.0f;
    driftCur.fill (0.0f);
    driftTarget.fill (0.0f);
    ctrlCounter = 0;
    evoClock    = 0.0f;
}

/** Refresh formant / body filter coefficients (called at control rate). */
void BiologicalEmulator::updateFormants (float shift, float evolution,
                                         int species, float reso, double sr)
{
    // formantShift: normalised 0..1 -> multiplier 0.5x .. 2x (1x at centre).
    const float shiftMul = std::pow (2.0f, (shift - 0.5f) * 2.0f);
    const float nyq      = 0.49f * (float) sr;   // BOUNDED: never exceed Nyquist

    for (int f = 0; f < kNumFormants; ++f)
    {
        // Glide toward the selected species so switching morphs smoothly.
        const float target = kFormantHz[species][f];
        curFormantHz[(size_t) f] += (target - curFormantHz[(size_t) f]) * morphCoef;

        // Slow evolution drift: up to +/-25% around the nominal formant.
        const float driftMul = 1.0f + evolution * 0.25f * driftCur[(size_t) f];
        const float hz = clampf (curFormantHz[(size_t) f] * shiftMul * driftMul, 20.0f, nyq);

        for (auto& c : chan)
            c.formant[(size_t) f].setCoefficients (Biquad::Type::BandPass, hz, sr, kFormantQ);
    }

    // Cellular body resonance: an octave below the first formant, Q = resonance.
    const float bodyHz = clampf (curFormantHz[0] * 0.5f * shiftMul, 30.0f, nyq);
    const float bodyQ  = clampf (0.5f + reso * 9.5f, 0.5f, 10.0f);   // BOUNDED Q
    for (auto& c : chan)
        c.body.setParams (bodyHz, bodyQ, sr);
}

void BiologicalEmulator::process (float* const* buffers, int numChannels, int numSamples)
{
    const int    nch = std::min ({ numChannels, (int) chan.size(), 8 });
    if (nch <= 0) return;
    const double sr  = context.sampleRate;
    const float  srf = (float) sr;

    // "species" is a stepped parameter: read the discrete index for this block.
    const int species = (int) clampf (raw (pSpecies) * (kNumSpecies - 1) + 0.5f,
                                      0.0f, (float) (kNumSpecies - 1));

    for (int n = 0; n < numSamples; ++n)
    {
        // Advance every continuous smoother exactly once per sample.
        const float shift  = smoothed (pFormantShift);
        const float nRate  = smoothed (pNeuralRate);
        const float nDepth = smoothed (pNeuralDepth);
        const float reso   = smoothed (pResonance);
        const float evo    = smoothed (pEvolution);
        const float growl  = smoothed (pGrowl);

        // ---- control-rate updates: evolution walk + filter coefficients ----
        if (--ctrlCounter <= 0)
        {
            ctrlCounter = kCtrlInterval;

            // Bounded random walk toward a fresh target every ~80 ms.
            evoClock -= (float) kCtrlInterval;
            if (evoClock <= 0.0f)
            {
                evoClock += 0.08f * srf;
                for (int f = 0; f < kNumFormants; ++f)
                    driftTarget[(size_t) f] = clampf (driftTarget[(size_t) f]
                                                      + rng.nextBipolar() * 0.4f, -1.0f, 1.0f);
            }
            for (int f = 0; f < kNumFormants; ++f)
                driftCur[(size_t) f] += (driftTarget[(size_t) f] - driftCur[(size_t) f]) * driftCoef;

            updateFormants (shift, evo, species, reso, sr);
        }

        // Neuron excitability: how fast rectified energy charges the membrane.
        const float excite = 0.5f + nRate * nRate * 12.0f;   // "spikes per second" scale
        const float drive  = 1.0f + growl * 5.0f;            // growl saturation amount

        // ---- per-channel formant / body / growl processing ----
        std::array<float, 8> wet { {} };
        float monoDrive = 0.0f;

        for (int c = 0; c < nch; ++c)
        {
            ChannelState& cs = chan[(size_t) c];
            const float x = buffers[c][n];

            // Growl: soft saturation (throat grit) + an octave-down sub-harmonic.
            // Flip the sub sign on each rising zero crossing => half-frequency.
            if (cs.prevIn <= 0.0f && x > 0.0f) cs.subSign = -cs.subSign;
            cs.prevIn = x;

            float xg = fastTanh (x * drive) * (0.6f + 0.4f / drive); // level-compensated
            xg += cs.subSign * std::fabs (x) * growl * 0.4f;         // sub-harmonic

            // Vocal-tract formant bank (parallel band-passes, weighted sum).
            float voc = 0.0f;
            for (int f = 0; f < kNumFormants; ++f)
                voc += cs.formant[(size_t) f].process (xg) * kFormantGain[f];
            voc *= 1.7f;                                             // formant makeup

            // Cellular body resonance layer.
            const float body = cs.body.processBP (voc);
            float out = voc + body * (0.3f + reso * 0.4f) + xg * 0.2f;

            out = cs.dc.process (out);                               // kill sub-harmonic DC
            wet[(size_t) c] = out;
            monoDrive += std::fabs (xg);
        }
        monoDrive /= (float) nch;

        // ---- integrate-and-fire neuron (shared across channels) ----
        membrane += monoDrive * excite / srf;   // accumulate rectified energy
        membrane *= membraneLeak;               // leak toward rest
        if (membrane >= 1.0f)                    // threshold crossed -> fire
        {
            membrane -= 1.0f;
            fireEnv = 1.0f;                      // emit a spike / gate click
        }
        membrane = clampf (membrane, 0.0f, 4.0f);
        fireEnv *= fireRelease;                  // spike envelope decays

        // Gate: depth=0 -> transparent (1.0); depth=1 -> fully chopped by spikes.
        const float gateTarget = (1.0f - nDepth) + nDepth * fireEnv;
        gateSmooth += (gateTarget - gateSmooth) * gateCoef;

        // ---- apply gate and write in place (fully wet) ----
        for (int c = 0; c < nch; ++c)
            buffers[c][n] = sanitise (wet[(size_t) c] * gateSmooth);
    }
}

} // namespace chaos
