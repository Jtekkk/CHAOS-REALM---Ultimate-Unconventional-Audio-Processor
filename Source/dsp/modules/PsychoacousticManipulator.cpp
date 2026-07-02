/*
    CHAOS REALM — Module 3: Psychoacoustic Manipulator  (IMPLEMENTATION)

    See PsychoacousticManipulator.h for the sound-design intent and the
    stability contract.  All internal generators are gated by an input peak
    follower so that silence in yields silence out.
*/
#include "PsychoacousticManipulator.h"

namespace chaos
{

PsychoacousticManipulator::PsychoacousticManipulator()
{
    // Register parameters (normalised 0..1).  Defaults are deliberately mild so
    // the module is characterful yet neutral enough to insert anywhere.
    pBinaural   = addParameter ({ "binaural",   "Binaural",   0.25f, "%",  0.0f, 100.0f });
    pCarrier    = addParameter ({ "carrier",    "Carrier",    0.40f, "Hz", 60.0f, 640.0f });
    pBeat       = addParameter ({ "beat",       "Beat",       0.15f, "Hz", 0.0f,  30.0f });
    pShepard    = addParameter ({ "shepard",    "Shepard",    0.20f, "%",  0.0f, 100.0f });
    pMotion     = addParameter ({ "motion",     "Motion",     0.20f, "oct/s", 0.0f, 1.5f });
    pWidth      = addParameter ({ "width",      "Width",      0.30f, "%",  0.0f, 100.0f });
    pDissonance = addParameter ({ "dissonance", "Dissonance", 0.15f, "%",  0.0f, 100.0f });
}

void PsychoacousticManipulator::onPrepare (const ProcessContext& ctx)
{
    const int    nch = std::max (1, ctx.numChannels);
    const double sr  = ctx.sampleRate;

    // Exaggerated Haas widening (~30 ms) and a small micro-timing jitter (~3 ms).
    maxHaasSamples   = (float) (sr * 0.030);
    maxJitterSamples = (float) (sr * 0.003);

    // One modulated delay line per channel; sized for base + Haas + jitter + slack.
    const int lineLen = (int) (maxHaasSamples + maxJitterSamples) + 8;
    spatial.resize ((size_t) nch);
    for (auto& d : spatial) { d.prepare (lineLen); d.reset(); }

    jitterCur.assign    ((size_t) nch, 0.0f);
    jitterTarget.assign ((size_t) nch, 0.0f);
    jitterCount.assign  ((size_t) nch, 0);

    // Envelope release: ~80 ms one-pole decay (attack is instantaneous / peak).
    envRelCoef = std::exp (-1.0f / (0.080f * (float) sr));
    // Jitter smoothing: ~25 ms glide toward each new random target (click-free).
    jitterCoef = 1.0f - std::exp (-1.0f / (0.025f * (float) sr));
    // Draw a fresh jitter target roughly every 30 ms.
    jitterPeriod = std::max (1, (int) (sr * 0.030));

    // A modest low base note so N octaves stay comfortably in the audible range.
    fBase = 55.0f; // A1

    envFollow = 0.0f;
    binPhaseL = binPhaseR = 0.0f;
    shepPos   = 0.0f;
    shepPhase.fill (0.0f);
}

void PsychoacousticManipulator::onReset()
{
    for (auto& d : spatial) d.reset();
    std::fill (jitterCur.begin(),    jitterCur.end(),    0.0f);
    std::fill (jitterTarget.begin(), jitterTarget.end(), 0.0f);
    std::fill (jitterCount.begin(),  jitterCount.end(),  0);

    envFollow = 0.0f;
    binPhaseL = binPhaseR = 0.0f;
    shepPos   = 0.0f;
    shepPhase.fill (0.0f);
}

void PsychoacousticManipulator::process (float* const* buffers, int numChannels, int numSamples)
{
    const int    nch = std::min (numChannels, (int) spatial.size());
    if (nch <= 0) return;
    const float  sr      = (float) context.sampleRate;
    const float  nyquist = sr * 0.475f;

    // Fixed loudness of the injected illusions relative to the input envelope.
    constexpr float kBinGain  = 0.35f;  // per-ear binaural tone level
    constexpr float kShepGain = 0.35f;  // summed Shepard bank level

    const float sigma = (float) kPartials * 0.28f; // Gaussian width over octaves
    const float twoSig2 = 2.0f * sigma * sigma;

    for (int n = 0; n < numSamples; ++n)
    {
        // -- per-sample smoothed parameters -------------------------------
        const float binAmt   = smoothed (pBinaural);
        const float carrier01= smoothed (pCarrier);
        const float beat01   = smoothed (pBeat);
        const float shepAmt  = smoothed (pShepard);
        const float motion01 = smoothed (pMotion);
        const float width01  = smoothed (pWidth);
        const float diss01   = smoothed (pDissonance);

        // Map normalised controls to physical units.
        const float carrierHz = 60.0f * std::pow (2.0f, carrier01 * 3.4f); // ~60..640 Hz
        const float beatHz    = beat01 * 30.0f;                            // 0..30 Hz
        const float octPerSec = motion01 * 1.5f;                           // glide rate
        const float haas      = width01 * maxHaasSamples;

        // -- input & peak-follower envelope (the master gate) -------------
        const float inL = buffers[0][n];
        const float inR = (nch > 1) ? buffers[1][n] : inL;
        const float rect = std::max (std::fabs (inL), std::fabs (inR));
        // Instant attack, exponential release -> decays to 0 on silence.
        envFollow = std::max (rect, envFollow * envRelCoef);
        const float env = clampf (envFollow, 0.0f, 1.0f);

        // -- Shepard / Risset bank ---------------------------------------
        // Advance the global position; each partial rises octPerSec octaves,
        // wrapping (with ~0 amplitude at the extremes) for an endless glide.
        shepPos = wrap01 (shepPos + (octPerSec / (float) kPartials) / sr);
        float shepSum = 0.0f, weightSum = 0.0f;
        for (int k = 0; k < kPartials; ++k)
        {
            const float frac = wrap01 (shepPos + (float) k / (float) kPartials);
            const float oct  = frac * (float) kPartials;      // 0..kPartials octaves
            float freq = fBase * std::pow (2.0f, oct);
            freq = clampf (freq, 20.0f, nyquist);
            // Gaussian spectral window centred in the middle of the range.
            const float d = oct - (float) kPartials * 0.5f;
            const float w = std::exp (-(d * d) / twoSig2);
            shepPhase[(size_t) k] = wrap01 (shepPhase[(size_t) k] + freq / sr);
            shepSum   += w * std::sin (kTwoPiF * shepPhase[(size_t) k]);
            weightSum += w;
        }
        const float shepTone = (weightSum > 1.0e-6f) ? (shepSum / weightSum) : 0.0f;
        const float shep = shepTone * shepAmt * env * kShepGain; // gated, both ears

        // -- binaural oscillators ----------------------------------------
        binPhaseL = wrap01 (binPhaseL + carrierHz / sr);
        binPhaseR = wrap01 (binPhaseR + (carrierHz + beatHz) / sr);
        const float sinL = std::sin (kTwoPiF * binPhaseL);
        const float sinR = std::sin (kTwoPiF * binPhaseR);

        // -- spatial width (Haas) + dissonance jitter --------------------
        // Push dry input, then read back re-timed per channel.
        for (int c = 0; c < nch; ++c)
        {
            spatial[(size_t) c].push (buffers[c][n]);

            // Random-walk micro-timing jitter toward a fresh target every so often.
            if (--jitterCount[(size_t) c] <= 0)
            {
                jitterTarget[(size_t) c] = rng.nextFloat() * maxJitterSamples;
                jitterCount[(size_t) c]  = jitterPeriod;
            }
            jitterCur[(size_t) c] += (jitterTarget[(size_t) c] - jitterCur[(size_t) c]) * jitterCoef;
        }

        // Precedence-effect level tilt: the earlier (left) ear a touch louder.
        const float gL = 1.0f + 0.15f * width01;
        const float gR = 1.0f - 0.15f * width01;

        if (nch > 1)
        {
            // Left anchored (~0 delay), right delayed by the Haas time; both get
            // an independent jitter offset so the ears decorrelate.
            const float dL = 1.0f + diss01 * jitterCur[0];
            const float dR = 1.0f + haas + diss01 * jitterCur[1];
            const float spL = spatial[0].readLinear (dL) * gL;
            const float spR = spatial[1].readLinear (dR) * gR;

            buffers[0][n] = sanitise (spL + shep + binAmt * env * kBinGain * sinL);
            buffers[1][n] = sanitise (spR + shep + binAmt * env * kBinGain * sinR);
        }
        else
        {
            // Mono: no true binaural, but summing the two detuned sines yields a
            // monaural beat (amplitude modulation at the beat frequency).
            const float dM = 1.0f + diss01 * jitterCur[0];
            const float spM = spatial[0].readLinear (dM);
            const float monoBeat = 0.5f * (sinL + sinR);
            buffers[0][n] = sanitise (spM + shep + binAmt * env * kBinGain * monoBeat);
        }
    }
}

} // namespace chaos
