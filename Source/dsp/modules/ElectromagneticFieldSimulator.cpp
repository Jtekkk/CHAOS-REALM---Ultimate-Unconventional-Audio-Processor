/*
    CHAOS REALM — Module 7: Electromagnetic Field Simulator  (IMPLEMENTATION)
*/
#include "ElectromagneticFieldSimulator.h"

namespace chaos
{

ElectromagneticFieldSimulator::ElectromagneticFieldSimulator()
{
    // Register parameters (normalised 0..1).  Defaults are deliberately gentle:
    // only mild magnetic coloration is active at start; every additive generator
    // (rf / hum) and the resonance / crosstalk sit at zero so the module can be
    // inserted anywhere and settles cleanly to silence.
    pFlux       = addParameter ({ "flux",       "Flux",         0.25f, "%",  0.0f,   100.0f });
    pRf         = addParameter ({ "rf",         "RF Noise",     0.00f, "%",  0.0f,   100.0f });
    pCrosstalk  = addParameter ({ "crosstalk",  "Crosstalk",    0.00f, "%",  0.0f,   100.0f });
    pPlasmaFreq = addParameter ({ "plasmafreq", "Plasma Freq",  0.35f, "Hz", 80.0f,  10240.0f });
    pPlasmaRes  = addParameter ({ "plasmares",  "Plasma Res",   0.00f, "Q",  0.7f,   20.0f });
    pIonize     = addParameter ({ "ionize",     "Ionize",       0.00f, "%",  0.0f,   100.0f });
    pHum        = addParameter ({ "hum",        "Mains Hum",    0.00f, "%",  0.0f,   100.0f });
}

void ElectromagneticFieldSimulator::onPrepare (const ProcessContext& ctx)
{
    const int    nch = std::max (1, ctx.numChannels);
    const double sr  = ctx.sampleRate;

    // Per-channel magnetic hysteresis state.
    hist.assign ((size_t) nch, 0.0f);
    histDc.assign ((size_t) nch, DcBlocker {});
    for (auto& d : histDc) d.reset();

    // Per-channel plasma resonators.
    plasma.assign ((size_t) nch, StateVariableFilter {});
    for (auto& p : plasma) p.reset();

    // Per-channel RF band-shaping (fixed broadband band around ~3 kHz).
    rfBand.assign ((size_t) nch, Biquad {});
    for (auto& b : rfBand)
    {
        b.reset();
        b.setCoefficients (Biquad::Type::BandPass, 3000.0f, sr, 0.7f);
    }
    crackle.assign ((size_t) nch, 0.0f);
    crackleDecay = std::exp (-1.0f / (0.004f * (float) sr)); // ~4 ms crackle tails

    // Per-channel induction crosstalk: a short delay + a coupling high-pass.
    xtalkSamples = std::max (1.0f, 0.0004f * (float) sr);    // ~0.4 ms coupling lag
    xtalk.resize ((size_t) nch);
    for (auto& d : xtalk) { d.prepare ((int) (0.005 * sr) + 8); d.reset(); }
    xtalkHp.assign ((size_t) nch, OnePole {});
    for (auto& h : xtalkHp) { h.reset(); h.setCutoff (500.0f, sr, true); }

    // Shared generators / envelope follower.
    envFollow  = 0.0f;
    envRelease = std::exp (-1.0f / (0.060f * (float) sr));   // ~60 ms release
    humPhase   = 0.0f;
    humInc     = kHumFreq / (float) sr;
    ionPhase   = 0.0f;
    ionInc     = 0.0f;
}

void ElectromagneticFieldSimulator::onReset()
{
    std::fill (hist.begin(), hist.end(), 0.0f);
    for (auto& d : histDc)  d.reset();
    for (auto& p : plasma)  p.reset();
    for (auto& b : rfBand)  b.reset();
    std::fill (crackle.begin(), crackle.end(), 0.0f);
    for (auto& d : xtalk)   d.reset();
    for (auto& h : xtalkHp) h.reset();

    envFollow = 0.0f;
    humPhase  = 0.0f;
    ionPhase  = 0.0f;
}

void ElectromagneticFieldSimulator::process (float* const* buffers, int numChannels, int numSamples)
{
    const int    nch = std::min (numChannels, (int) hist.size());
    if (nch <= 0) return;
    const double sr  = context.sampleRate;
    const float  nyq = (float) (sr * 0.45);

    for (int n = 0; n < numSamples; ++n)
    {
        // ---- smoothed parameters (once per sample) ----
        const float flux    = smoothed (pFlux);
        const float rfAmt   = smoothed (pRf);
        const float xtalkA  = smoothed (pCrosstalk);
        const float pFreqN  = smoothed (pPlasmaFreq);
        const float pResN   = smoothed (pPlasmaRes);
        const float ionAmt  = smoothed (pIonize);
        const float humAmt  = smoothed (pHum);

        // Capture the dry inputs before we overwrite the buffers (crosstalk and
        // the envelope both need the untouched signal).
        float in[2] = { 0.0f, 0.0f };
        for (int c = 0; c < nch; ++c) in[c] = buffers[c][n];

        // ---- shared input peak-envelope follower (silence-gate) ----
        float peak = 0.0f;
        for (int c = 0; c < nch; ++c) peak = std::max (peak, std::fabs (in[c]));
        if (peak > envFollow) envFollow = peak;                       // instant attack
        else                  envFollow = peak + (envFollow - peak) * envRelease;
        const float env = clampf (envFollow, 0.0f, 4.0f);

        // ---- shared generators advanced once per sample ----
        // Mains hum: fundamental + 2nd/3rd harmonics.
        humPhase += humInc; if (humPhase >= 1.0f) humPhase -= 1.0f;
        const float humTone = std::sin (kTwoPiF * humPhase)
                            + 0.35f * std::sin (kTwoPiF * 2.0f * humPhase)
                            + 0.18f * std::sin (kTwoPiF * 3.0f * humPhase);

        // Ionise ring-mod oscillator (sheen frequency tracks plasma tuning).
        const float ionFreq = 200.0f + pFreqN * pFreqN * 4000.0f;
        ionInc = ionFreq / (float) sr;
        ionPhase += ionInc; if (ionPhase >= 1.0f) ionPhase -= 1.0f;
        const float ionOsc = std::sin (kTwoPiF * ionPhase);

        // Plasma resonance tuning (exponential sweep) + resonance amount.
        const float plasmaFreq = clampf (80.0f * std::pow (2.0f, pFreqN * 7.0f), 20.0f, nyq);
        const float plasmaQ    = 0.7f + pResN * pResN * 20.0f;

        // Feed the crosstalk delays with the dry signal.
        for (int c = 0; c < nch; ++c) xtalk[(size_t) c].push (in[c]);

        for (int c = 0; c < nch; ++c)
        {
            float x = in[c];

            // ---- magnetic hysteresis: asymmetric saturation WITH memory ----
            // y = tanh(drive*(x + k*lastY) + bias); the k term is the flux memory,
            // the small bias breaks symmetry -> even harmonics.  fastTanh bounds
            // the state, so the loop is always stable & finite.
            const float drive  = 1.0f + flux * flux * 6.0f;
            const float k      = 0.12f + flux * 0.28f;
            const float bias   = flux * 0.10f;
            const float pre    = drive * (x + k * hist[(size_t) c]) + bias;
            float       y      = fastTanh (pre);
            hist[(size_t) c]   = y;                       // update flux memory
            y = histDc[(size_t) c].process (y);           // strip the bias DC
            x = y * (1.0f / drive);                       // makeup ~ unity small-signal

            // ---- plasma resonance: swept high-Q bandpass (signal-driven) ----
            plasma[(size_t) c].setParams (plasmaFreq, plasmaQ, sr);
            float bp = plasma[(size_t) c].processBP (x);
            // Ionise: ring-modulate the resonance for a metallic sheen.
            bp = lerp (bp, bp * ionOsc, ionAmt);
            x += pResN * bp * 0.5f;

            // ---- induction crosstalk: filtered/delayed OTHER channel ----
            if (nch > 1)
            {
                const int other   = c ^ 1;                // 0<->1 (stereo)
                float     coupled = xtalk[(size_t) other].readLinear (xtalkSamples);
                coupled = xtalkHp[(size_t) c].process (coupled); // inductive HP coupling
                x += xtalkA * coupled * 0.8f;
            }

            // ---- RF interference: env-gated broadband noise + AM crackle ----
            const float band = rfBand[(size_t) c].process (rng.nextBipolar());
            crackle[(size_t) c] *= crackleDecay;          // decay any active burst
            if (rng.nextFloat() < (0.0004f + rfAmt * 0.004f))
                crackle[(size_t) c] = rng.nextFloat();    // fire a new crackle burst
            const float rfSig = band * (0.4f + 2.0f * crackle[(size_t) c]);
            x += rfAmt * env * rfSig * 0.5f;

            // ---- mains hum: env-gated ----
            x += humAmt * env * humTone * 0.25f;

            // ---- field saturation ceiling: keeps the bus bounded (~unity for
            //      small signals, hard-bounded to +/-2.5 when resonances stack) ----
            x = 2.5f * fastTanh (x * 0.4f);

            buffers[c][n] = sanitise (x);
        }
    }
}

} // namespace chaos
