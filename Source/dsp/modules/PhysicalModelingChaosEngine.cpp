/*
    CHAOS REALM — Module 2: Physical Modeling Chaos Engine  (implementation)

    Signal flow, per channel, per sample:

        in --> [drive] --> tanh -----------------.  (exciter injects energy)
                                                  v
        waveguide.read(delayLen) --> [damping LP] --> [loop DC block] --> tanh
              ^                                                            |
              |                                                            |
              '------------------ push( exc + feedback * shaped ) <--------'

        out = tone( delayed + a little exciter ) --> DC block --> softClip

    The delay length (pitch) and the damping cutoff are both warped every sample
    by the selected chaotic system, scaled by "Chaos Depth".
*/
#include "PhysicalModelingChaosEngine.h"

namespace chaos
{

PhysicalModelingChaosEngine::PhysicalModelingChaosEngine()
{
    // Parameters are normalised 0..1.  Defaults give an audible, characterful
    // but always-stable resonant body straight out of the box.

    // chaosSys — stepped choice of the chaotic modulator driving the resonator.
    ParameterInfo sys { "chaosSys", "Chaos System", 0.0f, "", 0.0f, 2.0f };
    sys.isStepped = true;
    sys.numSteps  = kNumChaosSystems;      // 0 = Lorenz, 1 = Rössler, 2 = Logistic
    pChaosSys = addParameter (sys);

    pChaosRate  = addParameter ({ "chaosRate",  "Chaos Rate",  0.30f, "%", 0.0f, 100.0f });
    pChaosDepth = addParameter ({ "chaosDepth", "Chaos Depth", 0.35f, "%", 0.0f, 100.0f });
    pMaterial   = addParameter ({ "material",   "Material",    0.55f, "%", 0.0f, 100.0f });
    pResonance  = addParameter ({ "resonance",  "Resonance",   0.60f, "%", 0.0f, 100.0f });
    pDrive      = addParameter ({ "drive",      "Drive",       0.40f, "%", 0.0f, 100.0f });
    pTone       = addParameter ({ "tone",       "Tone",        0.50f, "%", 0.0f, 100.0f });
}

void PhysicalModelingChaosEngine::onPrepare (const ProcessContext& ctx)
{
    const int    nch = std::max (1, ctx.numChannels);
    const double sr  = ctx.sampleRate;

    // Size the waveguide for the lowest fundamental the pitch modulation can
    // reach (~40 Hz, well below the ~110 Hz base after downward warp) plus
    // interpolation headroom.
    maxDelaySamples = (float) (sr / 40.0);

    voices.assign ((size_t) nch, Voice {});
    for (int c = 0; c < nch; ++c)
    {
        Voice& v = voices[(size_t) c];

        v.waveguide.prepare ((int) maxDelaySamples + 8);
        v.waveguide.reset();

        // Base fundamental ~110 Hz; detune stereo channels slightly for width.
        const float baseFreq = 110.0f * (1.0f + 0.006f * (float) c);
        v.baseDelay = clampf ((float) sr / baseFreq, 2.0f, maxDelaySamples - 2.0f);

        v.damping.reset();
        v.tone.reset();
        v.loopDc.reset();
        v.outDc.reset();

        v.lorenz.reset();
        v.rossler.reset();
        v.logistic.reset();
        v.logistic.setR (3.90f + 0.03f * (float) c);
        v.mapPhase = 0.15f * (float) c;
        v.mapHold  = 0.0f;

        // De-correlate the continuous chaos of each channel by evolving the
        // right channel's attractors into a different region of the orbit.
        // (prepare() may allocate/compute freely — this is not the audio thread.)
        for (int i = 0; i < c * 400; ++i)
        {
            v.lorenz.step();
            v.rossler.step();
        }
    }
}

void PhysicalModelingChaosEngine::onReset()
{
    for (auto& v : voices)
    {
        v.waveguide.reset();
        v.damping.reset();
        v.tone.reset();
        v.loopDc.reset();
        v.outDc.reset();
        v.lorenz.reset();
        v.rossler.reset();
        v.logistic.reset();
        v.mapPhase = 0.0f;
        v.mapHold  = 0.0f;
    }
}

float PhysicalModelingChaosEngine::advanceChaos (Voice& v, int sys, float rate01) noexcept
{
    float m = 0.0f;
    if (sys == 0)
    {
        // Lorenz — smooth, slowly wandering modulation.
        v.lorenz.setRate (0.0015f + rate01 * 0.0135f);   // dt in [0.0015, 0.015]
        v.lorenz.step();
        m = v.lorenz.outX();
    }
    else if (sys == 1)
    {
        // Rössler — more tonal, quasi-periodic chaos.
        v.rossler.setRate (0.004f + rate01 * 0.041f);    // dt in [0.004, 0.045]
        v.rossler.step();
        m = v.rossler.outX();
    }
    else
    {
        // Logistic map — stepped, stochastic-sounding sample & hold.
        v.mapPhase += 0.0004f + rate01 * rate01 * 0.045f;
        if (v.mapPhase >= 1.0f)
        {
            v.mapPhase -= 1.0f;
            v.mapHold = v.logistic.step() * 2.0f - 1.0f; // (0,1) -> (-1,1)
        }
        m = v.mapHold;
    }
    return clampf (m, -1.0f, 1.0f);
}

void PhysicalModelingChaosEngine::process (float* const* buffers, int numChannels, int numSamples)
{
    const int    nch = std::min (numChannels, (int) voices.size());
    const double sr  = context.sampleRate;
    const float  nyq = (float) sr * 0.49f;

    // Stepped chaos-system selector — discrete, read once per block.
    const int sys = (int) clampf (raw (pChaosSys) * (kNumChaosSystems - 1) + 0.5f,
                                  0.0f, (float) (kNumChaosSystems - 1));

    for (int n = 0; n < numSamples; ++n)
    {
        // Continuous parameters, smoothed once per sample.
        const float rate  = smoothed (pChaosRate);
        const float depth = smoothed (pChaosDepth);
        const float mat   = smoothed (pMaterial);
        const float res   = smoothed (pResonance);
        const float drv   = smoothed (pDrive);
        const float ton   = smoothed (pTone);

        // Map controls to internal units.
        const float driveGain = 0.5f + drv * 7.5f;                       // exciter input gain
        const float feedback  = clampf (0.70f + res * 0.25f, 0.0f, 0.95f); // loop gain, < 0.97
        // Material = damping brightness (dark -> bright), exponential sweep.
        const float matCut = clampf (300.0f * std::pow (40.0f, mat), 20.0f, nyq);
        // Tone = post-filter brightness.
        const float tonCut = clampf (500.0f * std::pow (30.0f, ton), 20.0f, nyq);

        for (int c = 0; c < nch; ++c)
        {
            Voice& v  = voices[(size_t) c];
            const float in = buffers[c][n];

            // ---- chaotic modulation source (bounded to [-1,1]) ----
            const float mod = advanceChaos (v, sys, rate);

            // Chaos warps the resonator pitch (delay length): up to +/- 0.7 oct.
            const float pitchMod = std::pow (2.0f, depth * mod * 0.7f);
            const float delayLen = clampf (v.baseDelay * pitchMod, 2.0f, maxDelaySamples - 2.0f);

            // Chaos also opens / closes the material damping filter.
            const float cutMod = clampf (matCut * (1.0f + 0.6f * depth * mod), 20.0f, nyq);
            v.damping.setCutoff (cutMod, sr, false); // low-pass = damping/brightness

            // ---- excitation: input driven through the loop non-linearity ----
            const float exc = fastTanh (in * driveGain);

            // ---- read resonator, damp it, keep it DC-free & bounded ----
            const float delayed = v.waveguide.readCubic (delayLen);
            const float damped  = v.damping.process (delayed);
            const float blocked = v.loopDc.process (damped);
            const float shaped  = fastTanh (blocked); // IN-LOOP non-linearity: bounds + overtones

            // ---- feed energy back into the waveguide loop ----
            const float loopIn = exc + feedback * shaped;
            v.waveguide.push (sanitise (loopIn));

            // ---- output: resonant body + a touch of the direct exciter ----
            v.tone.setCutoff (tonCut, sr, false);
            float y = v.tone.process (delayed + 0.25f * exc);
            y = v.outDc.process (y);
            y = softClip (y * 0.8f);                  // final safety bound

            buffers[c][n] = sanitise (y);
        }
    }
}

} // namespace chaos
