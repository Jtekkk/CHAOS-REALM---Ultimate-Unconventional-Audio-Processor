/*
    CHAOS REALM — Module 10: Symbolic Manipulator  (implementation)
*/
#include "SymbolicManipulator.h"

namespace chaos
{

SymbolicManipulator::SymbolicManipulator()
{
    // Register parameters (normalised 0..1).  Defaults are gently characterful.
    pNumerology = addParameter ({ "numerology", "Numerology", 0.50f, "%",  0.0f, 100.0f });
    pGeometry   = addParameter ({ "geometry",   "Geometry",   0.30f, "%",  0.0f, 100.0f });

    // Divination selects one of kNumSeeds fixed patterns -> stepped / integer.
    ParameterInfo div { "divination", "Divination", 0.0f, "", 0.0f, (float) (kNumSeeds - 1) };
    div.isStepped = true;
    div.numSteps  = kNumSeeds;
    pDivination = addParameter (div);

    pElement   = addParameter ({ "element",   "Element",   0.25f, "%",  0.0f, 100.0f });  // earth->fire
    pRitual    = addParameter ({ "ritual",    "Ritual",    0.35f, "Hz", 0.5f, 16.0f  });  // step rate
    pIntensity = addParameter ({ "intensity", "Intensity", 0.50f, "%",  0.0f, 100.0f });
    pResonance = addParameter ({ "resonance", "Resonance", 0.50f, "%",  0.0f, 100.0f });
}

void SymbolicManipulator::regeneratePattern (uint32_t seed) noexcept
{
    // Deterministic 64-step divination pattern.  A small pentatonic-ish scale
    // keeps the re-tunings musical; occasional octave jumps add ritual drama.
    Xorshift rng (seed);
    static const float scale[7] = { 0.0f, 2.0f, 3.0f, 5.0f, 7.0f, 10.0f, 12.0f };

    for (int i = 0; i < kNumSteps; ++i)
    {
        const float r = rng.nextFloat();
        // ~22% rests; otherwise a mid-to-full gate level.
        pattern[(size_t) i].gate = (r < 0.22f) ? 0.0f : (0.35f + 0.65f * rng.nextFloat());

        int si = (int) (rng.nextFloat() * 7.0f);
        if (si > 6) si = 6;
        const float pick = rng.nextFloat();
        const float oct  = (pick < 0.20f) ? 12.0f : (pick > 0.90f ? -12.0f : 0.0f);
        pattern[(size_t) i].transpose = scale[(size_t) si] + oct;

        pattern[(size_t) i].fbMod = rng.nextFloat();
        pattern[(size_t) i].tone  = rng.nextFloat();
    }
}

void SymbolicManipulator::onPrepare (const ProcessContext& ctx)
{
    const int    nch = std::max (1, ctx.numChannels);
    const double sr  = ctx.sampleRate;

    // Comb needs room for the lowest tuning (~25 Hz); chorus needs ~50 ms.
    const int maxCombDelay  = (int) (sr / 25.0)  + 8;
    const int maxWaterDelay = (int) (sr * 0.05)  + 8;

    chans.assign ((size_t) nch, Channel {});
    for (int c = 0; c < nch; ++c)
    {
        auto& ch = chans[(size_t) c];
        for (int i = 0; i < kNumRes; ++i) { ch.combs[(size_t) i].prepare (maxCombDelay); ch.combs[(size_t) i].reset(); }
        ch.earthLP.reset(); ch.earthLP.setCutoff (700.0f,  sr, false); // warm LP
        ch.airHP.reset();   ch.airHP.setCutoff  (1800.0f, sr, true);   // bright HP
        ch.airDC.reset();
        ch.outDC.reset();
        ch.water.prepare (maxWaterDelay); ch.water.reset();
        ch.waterFb = 0.0f;
    }

    waterLfoPhase = 0.0f;
    seqStep = 0; seqPhase = 0.0f; gateSmooth = 0.0f;

    // Build the initial pattern from the current divination selection.
    const int sel = (int) clampf (raw (pDivination) * (float) (kNumSeeds - 1) + 0.5f,
                                  0.0f, (float) (kNumSeeds - 1));
    currentSeed = 0x5EED0000u + (uint32_t) sel * 0x9E3779B9u;
    regeneratePattern (currentSeed);

    transposeTarget = std::pow (2.0f, pattern[0].transpose / 12.0f);
    transposeSmooth = transposeTarget;
}

void SymbolicManipulator::onReset()
{
    for (auto& ch : chans)
    {
        for (auto& cf : ch.combs) cf.reset();
        ch.earthLP.reset();
        ch.airHP.reset();
        ch.airDC.reset();
        ch.outDC.reset();
        ch.water.reset();
        ch.waterFb = 0.0f;
    }
    waterLfoPhase = 0.0f;
    seqStep = 0; seqPhase = 0.0f; gateSmooth = 0.0f;
    transposeTarget = 1.0f; transposeSmooth = 1.0f;
}

void SymbolicManipulator::process (float* const* buffers, int numChannels, int numSamples)
{
    const int nch = std::min (numChannels, (int) chans.size());
    if (nch <= 0) return;

    const float srf = (float) context.sampleRate;

    // ---- divination: map the stepped param to an integer seed; regenerate the
    //      64-step pattern only when the selection changes (RT-safe, O(64)). ----
    const int sel = (int) clampf (raw (pDivination) * (float) (kNumSeeds - 1) + 0.5f,
                                  0.0f, (float) (kNumSeeds - 1));
    const uint32_t seed = 0x5EED0000u + (uint32_t) sel * 0x9E3779B9u;
    if (seed != currentSeed)
    {
        currentSeed = seed;
        regeneratePattern (seed);
        transposeTarget = std::pow (2.0f, pattern[(size_t) seqStep].transpose / 12.0f);
    }

    // ---- number-derived ratio sets (computed once per block) ----
    const float phi  = 1.6180339887f;
    const float fib [kNumRes] = { 1.0f, 2.0f, 3.0f, 5.0f, 8.0f, 13.0f };                 // Fibonacci multipliers
    const float gold[kNumRes] = { 1.0f, phi, phi * phi, phi * phi * phi,
                                  phi * phi * phi * phi, phi * phi * phi * phi * phi };   // golden-ratio powers
    const float prim[kNumRes] = { 1.0f, 2.0f, 3.0f, 5.0f, 7.0f, 11.0f };                 // prime harmonics
    const float geo [kNumRes] = { 1.0f, 1.4142136f, 1.6180340f, 1.7320508f,              // 1, sqrt2, phi, sqrt3,
                                  2.2360680f, 2.6180340f };                              // sqrt5, phi^2
    const float f0 = 98.0f; // fundamental (G2)

    // Click-free follower coefficients (fixed times, derived once per block).
    const float gateCoef  = std::exp (-1.0f / (0.004f * srf)); // ~4 ms gate slew
    const float transCoef = std::exp (-1.0f / (0.010f * srf)); // ~10 ms pitch slew

    for (int n = 0; n < numSamples; ++n)
    {
        const float numerology = smoothed (pNumerology);
        const float geometry   = smoothed (pGeometry);
        const float element    = smoothed (pElement);
        const float ritual     = smoothed (pRitual);
        const float intensity  = smoothed (pIntensity);
        const float resonance  = smoothed (pResonance);

        // ---- sequencer advance (ritual sets step rate: 0.5..16 steps/sec) ----
        const float stepRate = 0.5f + ritual * ritual * 15.5f;
        seqPhase += stepRate / srf;
        if (seqPhase >= 1.0f)
        {
            seqPhase -= 1.0f;
            seqStep = (seqStep + 1) % kNumSteps;
            transposeTarget = std::pow (2.0f, pattern[(size_t) seqStep].transpose / 12.0f);
        }
        const Step& st = pattern[(size_t) seqStep];

        gateSmooth      = st.gate          + (gateSmooth      - st.gate)          * gateCoef;
        transposeSmooth = transposeTarget  + (transposeSmooth - transposeTarget)  * transCoef;

        // ---- resonator morph position (0->fib, 0.5->gold, 1->prime) ----
        const float mm   = numerology * 2.0f;
        const int   mseg = (mm < 1.0f) ? 0 : 1;
        const float mf   = mm - (float) mseg;

        // ---- resonator feedback (< 0.95) & damping, modulated by the step ----
        float baseFb = 0.55f + resonance * 0.39f;         // 0.55 .. 0.94
        baseFb *= (0.85f + 0.15f * st.fbMod);
        baseFb  = clampf (baseFb, 0.0f, 0.94f);           // hard stability bound
        const float damp = clampf (0.15f + 0.5f * st.tone * (1.0f - geometry * 0.3f), 0.0f, 0.9f);

        // ---- chorus LFO advance (~0.6 Hz) ----
        waterLfoPhase += 0.6f / srf;
        if (waterLfoPhase >= 1.0f) waterLfoPhase -= 1.0f;

        // ---- alchemy: four-way element crossfade weights (earth/water/air/fire)
        const float e    = element * 3.0f;
        int         eidx = (int) e;
        if (eidx > 2) eidx = 2;
        const float ef = e - (float) eidx;
        const float wl = 1.0f - ef, wh = ef;
        float w0 = 0.0f, w1 = 0.0f, w2 = 0.0f, w3 = 0.0f;
        if      (eidx == 0) { w0 = wl; w1 = wh; }
        else if (eidx == 1) { w1 = wl; w2 = wh; }
        else                { w2 = wl; w3 = wh; }

        for (int c = 0; c < nch; ++c)
        {
            auto&       ch = chans[(size_t) c];
            const float x  = buffers[c][n];

            // ---- numerology + geometry resonator bank ----
            float resSum = 0.0f;
            for (int i = 0; i < kNumRes; ++i)
            {
                const float lo    = (mseg == 0) ? fib[i]  : gold[i];
                const float hi    = (mseg == 0) ? gold[i] : prim[i];
                float       ratio = lerp (lo, hi, mf);             // numerology morph
                ratio *= lerp (1.0f, geo[i], geometry);            // sacred-geometry spread
                float freq = f0 * ratio * transposeSmooth;         // divination transpose
                freq = clampf (freq, 25.0f, srf * 0.45f);
                const float delay = srf / freq;

                ch.combs[(size_t) i].setDelay (delay);
                ch.combs[(size_t) i].setFeedback (baseFb);
                ch.combs[(size_t) i].setDamping (damp);
                resSum += ch.combs[(size_t) i].process (x);
            }
            resSum *= (1.0f / (float) kNumRes);

            // Bound the resonant voice, then gate it with the divination step.
            float voice = fastTanh (resSum * 1.5f);
            voice *= gateSmooth;

            // ---- element: earth (warm low-pass) ----
            const float earth = ch.earthLP.process (voice);

            // ---- element: water (chorus/flange, modulated delay + light fb) ----
            const float lfo    = 0.5f - 0.5f * std::cos (kTwoPiF * (waterLfoPhase + 0.25f * (float) c));
            const float wdelay = (0.006f + 0.004f * lfo) * srf;    // 6..10 ms sweep
            const float wet    = ch.water.readLinear (wdelay);
            ch.water.push (sanitise (voice + ch.waterFb * 0.45f)); // fb 0.45 -> decays
            ch.waterFb = wet;
            const float water = 0.5f * voice + 0.7f * wet;

            // ---- element: air (bright high-pass + rectified octave shimmer) ----
            const float hp      = ch.airHP.process (voice);
            const float shimmer = ch.airDC.process (std::fabs (voice)); // octave-up, DC-free
            const float air     = hp + 0.6f * shimmer;

            // ---- element: fire (tanh drive, intrinsically bright) ----
            const float drive = 2.0f + intensity * 6.0f;
            const float fire  = fastTanh (voice * drive);

            // Interpolate the elemental outputs.
            const float elem = w0 * earth + w1 * water + w2 * air + w3 * fire;

            // Intensity is the overall wet character (neutral pass-through at 0).
            float out = lerp (x, elem, intensity);
            out = ch.outDC.process (out);

            buffers[c][n] = sanitise (out);
        }
    }
}

} // namespace chaos
