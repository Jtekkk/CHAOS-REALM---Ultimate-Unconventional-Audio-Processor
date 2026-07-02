/*
    CHAOS REALM — Module 9: Quantum Modulation Processor  (implementation)
*/
#include "QuantumModulationProcessor.h"

namespace chaos
{

QuantumModulationProcessor::QuantumModulationProcessor()
{
    // Register parameters (normalised 0..1).  Defaults give an audibly active
    // but always-stable modulation.
    pSuperposition = addParameter ({ "superposition", "Superposition", 0.50f, "%",  0.0f, 100.0f });
    pEntangle      = addParameter ({ "entangle",      "Entanglement",  0.40f, "%",  0.0f, 100.0f });
    pTunnel        = addParameter ({ "tunnel",        "Tunneling",     0.20f, "%",  0.0f, 100.0f });
    pObserver      = addParameter ({ "observer",      "Observer",      0.40f, "%",  0.0f, 100.0f });
    pCollapse      = addParameter ({ "collapse",      "Collapse Rate", 0.35f, "Hz", 0.5f, 40.0f  });
    pSpread        = addParameter ({ "spread",        "Stereo Spread", 0.50f, "%",  0.0f, 100.0f });
    pCoherence     = addParameter ({ "coherence",     "Coherence",     0.50f, "%",  0.0f, 100.0f });
}

void QuantumModulationProcessor::onPrepare (const ProcessContext& ctx)
{
    const int    nch = std::max (1, ctx.numChannels);
    const double sr  = ctx.sampleRate;

    channels.assign ((size_t) nch, ChannelState {});
    for (auto& ch : channels)
    {
        // Two deliberately different superposition "states": a dark resonant
        // low-pass (A) and a bright resonant band-pass (B).  Blending between
        // them sweeps the spectral character.
        ch.split.setParams (800.0f,  0.707f, sr);   // crossover between the two bands
        ch.svfA.setParams  (500.0f,  1.20f,  sr);
        ch.svfB.setParams  (2500.0f, 2.50f,  sr);
        ch.reset();
    }

    // One-pole envelope coefficients: env = target + (env - target) * coeff.
    envCoeff = std::exp (-1.0f / (0.010f * (float) sr)); // ~10 ms band followers
    obsCoeff = std::exp (-1.0f / (0.050f * (float) sr)); // ~50 ms observer analysis

    logistic.setR (3.86f);   // deep in the chaotic regime
    logistic.reset();
    rng.setSeed (0x2BD1E995u);

    collapseClock  = 0.0f;
    tunnelClock    = 0.0f;
    weightTarget   = { { 0.5f, 0.5f } };
    gateLowTarget  = { { 1.0f, 1.0f } };
    gateHighTarget = { { 1.0f, 1.0f } };
    obsEnv         = 0.0f;
}

void QuantumModulationProcessor::onReset()
{
    for (auto& ch : channels) ch.reset();

    logistic.reset();
    collapseClock  = 0.0f;
    tunnelClock    = 0.0f;
    weightTarget   = { { 0.5f, 0.5f } };
    gateLowTarget  = { { 1.0f, 1.0f } };
    gateHighTarget = { { 1.0f, 1.0f } };
    obsEnv         = 0.0f;
}

void QuantumModulationProcessor::process (float* const* buffers, int numChannels, int numSamples)
{
    const int    nch = std::min (numChannels, (int) channels.size());
    if (nch <= 0) return;
    const float  sr  = (float) context.sampleRate;

    for (int n = 0; n < numSamples; ++n)
    {
        // ---- continuous parameters (smoothed once per sample) ----
        const float superposition = smoothed (pSuperposition);
        const float entangle      = smoothed (pEntangle);
        const float tunnelP       = smoothed (pTunnel);
        const float observer      = smoothed (pObserver);
        const float collapse      = smoothed (pCollapse);
        const float spread        = smoothed (pSpread);
        const float coherence     = smoothed (pCoherence);

        // ---- observer effect: analyse the input level ------------------------
        // A louder input is "measured" more often, which speeds up collapse.
        float inMag = 0.0f;
        for (int c = 0; c < nch; ++c) inMag += std::fabs (buffers[c][n]);
        inMag /= (float) nch;
        obsEnv = inMag + (obsEnv - inMag) * obsCoeff;

        // ---- superposition collapse: probabilistic weight jumps --------------
        float jumpsPerSec = 0.5f + collapse * collapse * 40.0f;      // base rate
        jumpsPerSec *= (1.0f + observer * obsEnv * 8.0f);            // observer scaling
        jumpsPerSec  = clampf (jumpsPerSec, 0.1f, 400.0f);

        collapseClock -= 1.0f;
        if (collapseClock <= 0.0f)
        {
            collapseClock += sr / jumpsPerSec;

            // Chaotic base target (LogisticMap) plus an independent Xorshift draw
            // used only to decorrelate the second channel.
            const float base  = logistic.step() * 2.0f - 1.0f;   // (-1, 1)
            const float indep = rng.nextBipolar();               // [-1, 1)
            const float swing = superposition * 0.5f;            // how far the blend swings

            weightTarget[0] = clampf (0.5f + swing * base, 0.0f, 1.0f);
            weightTarget[1] = clampf (0.5f + swing * (base * (1.0f - spread) + indep * spread),
                                      0.0f, 1.0f);
        }

        // ---- tunneling: per-band probabilistic gating ------------------------
        tunnelClock -= 1.0f;
        if (tunnelClock <= 0.0f)
        {
            const float gateRate = clampf (2.0f + collapse * 30.0f, 0.5f, 200.0f);
            tunnelClock += sr / gateRate;

            // Channel 0 decisions: a band is blocked with probability = tunnelP.
            const bool blkLo0 = rng.nextFloat() < tunnelP;
            const bool blkHi0 = rng.nextFloat() < tunnelP;
            gateLowTarget[0]  = blkLo0 ? 0.0f : 1.0f;
            gateHighTarget[0] = blkHi0 ? 0.0f : 1.0f;

            // Channel 1: with probability = spread take an independent decision,
            // otherwise mirror channel 0 (fully correlated at spread = 0).
            const bool blkLo1 = (rng.nextFloat() < spread) ? (rng.nextFloat() < tunnelP) : blkLo0;
            const bool blkHi1 = (rng.nextFloat() < spread) ? (rng.nextFloat() < tunnelP) : blkHi0;
            gateLowTarget[1]  = blkLo1 ? 0.0f : 1.0f;
            gateHighTarget[1] = blkHi1 ? 0.0f : 1.0f;
        }

        // ---- coherence: smoothing (crossfade) coefficients -------------------
        // Higher coherence -> slower, smoother relaxation of every switch, which
        // is also what keeps the probabilistic jumps click-free.
        const float weightMs = 3.0f + coherence * 300.0f;
        const float gateMs   = 2.0f + coherence * 60.0f;
        const float wCoeff   = std::exp (-1.0f / (weightMs * 0.001f * sr));
        const float gCoeff   = std::exp (-1.0f / (gateMs   * 0.001f * sr));

        for (int c = 0; c < nch; ++c)
        {
            const int    ci = (c < 2) ? c : 1;   // shared targets cover 2 channels
            ChannelState& ch = channels[(size_t) c];
            const float  x  = buffers[c][n];

            // Smooth all probabilistic switches toward their targets (never a
            // hard jump -> no clicks / instability).
            ch.weightCur   = weightTarget[(size_t) ci]   + (ch.weightCur   - weightTarget[(size_t) ci])   * wCoeff;
            ch.gateLowCur  = gateLowTarget[(size_t) ci]  + (ch.gateLowCur  - gateLowTarget[(size_t) ci])  * gCoeff;
            ch.gateHighCur = gateHighTarget[(size_t) ci] + (ch.gateHighCur - gateHighTarget[(size_t) ci]) * gCoeff;

            // ---- entanglement: split into two bands ----
            const auto  o  = ch.split.process (x);
            float lo = o.lp;
            float hi = o.hp;

            // Bounded amplitude followers per band.
            const float al = std::fabs (lo), ah = std::fabs (hi);
            ch.envLow  = al + (ch.envLow  - al) * envCoeff;
            ch.envHigh = ah + (ch.envHigh - ah) * envCoeff;

            // Cross-modulate band gains: energy in one band non-locally raises
            // or lowers the other.  Gains are clamped so the coupling is bounded
            // and collapses to unity (identity) when the input is silent.
            const float gLo = clampf (1.0f + entangle * 2.0f * (ch.envHigh - ch.envLow), 0.0f, 2.0f);
            const float gHi = clampf (1.0f + entangle * 2.0f * (ch.envLow  - ch.envHigh), 0.0f, 2.0f);
            lo *= gLo;
            hi *= gHi;

            // ---- tunneling: apply the smoothed per-band gates ----
            lo *= ch.gateLowCur;
            hi *= ch.gateHighCur;

            const float banded = lo + hi;

            // ---- superposition: blend the two parallel filter states ----
            const float a   = ch.svfA.processLP (banded);          // state A
            const float b   = ch.svfB.process (banded).bp * 0.7f;  // state B (resonant BP)
            const float sup = lerp (a, b, ch.weightCur);

            // ---- output conditioning ----
            float y = ch.dc.process (sup);
            y = clampf (y, -4.0f, 4.0f);   // hard safety bound (never reached in practice)
            buffers[c][n] = sanitise (y);
        }
    }
}

} // namespace chaos
