/*
    CHAOS REALM — Core DSP primitive tests.
    Validates ChaosMath.h: FFT round-trip, STFT reconstruction, filter
    stability, chaotic-system boundedness, delay-line accuracy, RNG behaviour.
*/
#include "../Source/dsp/ChaosMath.h"
#include "TestFramework.h"

#include <complex>
#include <vector>

using namespace chaos;
using namespace chaostest;

static void testScalarUtils()
{
    section ("scalar utilities");
    checkClose (dbToGain (0.0f), 1.0f, 1e-5f, "0 dB == unity gain");
    checkClose (dbToGain (-6.0206f), 0.5f, 1e-3f, "-6 dB ~ 0.5");
    checkClose (fastTanh (0.0f), 0.0f, 1e-6f, "tanh(0)=0");
    check (fastTanh (100.0f) <= 1.0f && fastTanh (100.0f) >= 0.99f, "tanh saturates near 1");
    check (fastTanh (-100.0f) >= -1.0f && fastTanh (-100.0f) <= -0.99f, "tanh saturates near -1");
    checkClose (softClip (0.25f), 0.25f - 0.25f*0.25f*0.25f/3.0f, 1e-6f, "softClip cubic region");
    checkClose (midiToFreq (69.0f), 440.0f, 1e-3f, "A4 = 440 Hz");
    check (sanitise (1e-30f) == 0.0f, "denormal flushed to zero");
    check (sanitise (std::nanf ("")) == 0.0f, "NaN flushed to zero");
}

static void testFFTRoundTrip()
{
    section ("FFT round-trip");
    for (int order = 4; order <= 12; ++order)
    {
        FFT fft; fft.prepare (order);
        const int n = fft.size();
        std::vector<std::complex<float>> data ((size_t) n), original;
        Xorshift rng (1234u + (uint32_t) order);
        for (auto& c : data) c = { rng.nextBipolar(), rng.nextBipolar() };
        original = data;

        fft.transform (data, false);
        fft.transform (data, true);

        float maxErr = 0.0f;
        for (int i = 0; i < n; ++i)
            maxErr = std::max (maxErr, std::abs (data[(size_t) i] - original[(size_t) i]));
        checkClose (maxErr, 0.0f, 1e-4f, "IFFT(FFT(x)) == x, order " + std::to_string (order));
    }
}

static void testFFTImpulseAndSine()
{
    section ("FFT known signals");
    FFT fft; fft.prepare (10); // 1024
    const int n = fft.size();

    // Impulse -> flat magnitude spectrum of 1.
    std::vector<std::complex<float>> d ((size_t) n, {0.0f, 0.0f});
    d[0] = { 1.0f, 0.0f };
    fft.transform (d, false);
    float minMag = 1e9f, maxMag = 0.0f;
    for (auto& c : d) { const float m = std::abs (c); minMag = std::min (minMag, m); maxMag = std::max (maxMag, m); }
    checkClose (minMag, 1.0f, 1e-4f, "impulse spectrum flat (min)");
    checkClose (maxMag, 1.0f, 1e-4f, "impulse spectrum flat (max)");

    // Pure cosine at bin 16 -> energy concentrated at bins 16 and n-16.
    for (int i = 0; i < n; ++i)
        d[(size_t) i] = { std::cos (kTwoPiF * 16.0f * (float) i / (float) n), 0.0f };
    fft.transform (d, false);
    const float binMag = std::abs (d[16]);
    float otherMax = 0.0f;
    for (int i = 0; i < n; ++i)
        if (i != 16 && i != n - 16) otherMax = std::max (otherMax, std::abs (d[(size_t) i]));
    check (binMag > (float) n * 0.4f, "cosine energy lands in target bin");
    check (otherMax < binMag * 0.01f, "off-bin leakage is small");
}

static void testRealFFT()
{
    section ("RealFFT vs complex FFT + round-trip");
    for (int order = 3; order <= 13; ++order)
    {
        const int n = 1 << order;
        RealFFT rf; rf.prepare (order);
        FFT full; full.prepare (order);
        Xorshift rng (555u + (uint32_t) order);

        std::vector<float> x ((size_t) n), y ((size_t) n);
        std::vector<std::complex<float>> spec ((size_t) (n / 2 + 1)), ref ((size_t) n);
        for (int i = 0; i < n; ++i) { x[(size_t) i] = rng.nextBipolar(); ref[(size_t) i] = { x[(size_t) i], 0.0f }; }

        rf.forward (x.data(), spec.data());
        full.transform (ref, false);

        float fErr = 0.0f;
        for (int k = 0; k <= n / 2; ++k) fErr = std::max (fErr, std::abs (spec[(size_t) k] - ref[(size_t) k]));
        check (fErr < 2.0e-3f, "RealFFT matches complex FFT, order " + std::to_string (order));

        rf.inverse (spec.data(), y.data());
        float rErr = 0.0f;
        for (int i = 0; i < n; ++i) rErr = std::max (rErr, std::fabs (y[(size_t) i] - x[(size_t) i]));
        check (rErr < 1.0e-4f, "RealFFT round-trip identity, order " + std::to_string (order));
    }
}

static void testSTFTReconstruction()
{
    section ("STFT overlap-add reconstruction");
    for (int overlap : { 2, 4 })
    {
        STFT stft; stft.prepare (10, overlap, 48000.0);
        const int latency = stft.latency();
        const int N = 20000;
        std::vector<float> in ((size_t) N), out ((size_t) N);
        Xorshift rng (77u);
        // A couple of sines + noise so we test broadband reconstruction.
        for (int i = 0; i < N; ++i)
            in[(size_t) i] = 0.5f * std::sin (kTwoPiF * 440.0f * (float) i / 48000.0f)
                           + 0.3f * std::sin (kTwoPiF * 1234.0f * (float) i / 48000.0f)
                           + 0.05f * rng.nextBipolar();

        // Identity spectral callback (pure passthrough).
        auto identity = [] (std::vector<std::complex<float>>&, int, float) {};
        for (int i = 0; i < N; ++i) out[(size_t) i] = stft.process (in[(size_t) i], identity);

        // Compare in[i] to out[i+latency] over a stable middle region.
        float maxErr = 0.0f, energy = 0.0f;
        for (int i = 2000; i < N - latency - 2000; ++i)
        {
            const float e = std::fabs (out[(size_t) (i + latency)] - in[(size_t) i]);
            maxErr = std::max (maxErr, e);
            energy += in[(size_t) i] * in[(size_t) i];
        }
        checkFinite (out, "STFT output finite (overlap " + std::to_string (overlap) + ")");
        check (maxErr < 0.02f, "STFT reconstructs within 2% (overlap "
               + std::to_string (overlap) + "), maxErr=" + std::to_string (maxErr));
    }
}

static void testFilters()
{
    section ("filter stability & response");
    const double sr = 48000.0;

    // Biquad lowpass: DC passes, Nyquist heavily attenuated, stays bounded.
    Biquad lp; lp.setCoefficients (Biquad::Type::LowPass, 1000.0f, sr, 0.707f);
    std::vector<float> resp;
    float dc = 0.0f;
    for (int i = 0; i < 4000; ++i) dc = lp.process (1.0f);
    checkClose (dc, 1.0f, 1e-2f, "LP passes DC at unity");

    lp.reset();
    std::vector<float> nyq;
    for (int i = 0; i < 4000; ++i) nyq.push_back (lp.process ((i & 1) ? 1.0f : -1.0f));
    checkBounded (nyq, 1.0f, "LP bounded on Nyquist input");
    check (std::fabs (nyq.back()) < 0.05f, "LP attenuates Nyquist");

    // SVF: sweep cutoff with noise, must stay finite & bounded.
    StateVariableFilter svf;
    Xorshift rng (9u);
    std::vector<float> svfOut;
    for (int i = 0; i < 20000; ++i)
    {
        const float f = 100.0f + 15000.0f * (0.5f + 0.5f * std::sin (i * 0.001f));
        svf.setParams (f, 4.0f, sr);
        svfOut.push_back (svf.processLP (rng.nextBipolar()));
    }
    checkFinite (svfOut, "SVF stays finite under fast cutoff modulation");
    checkBounded (svfOut, 20.0f, "SVF stays bounded under modulation");

    // DC blocker removes DC offset.
    DcBlocker dcb;
    float last = 0.0f;
    for (int i = 0; i < 8000; ++i) last = dcb.process (1.0f + 0.1f * std::sin (i * 0.01f));
    check (std::fabs (last) < 0.15f, "DC blocker removes constant offset");
}

static void testDelayLine()
{
    section ("delay line");
    DelayLine dl; dl.prepare (1000);
    // Push a ramp 0..499; convention is read-before-push, so read(D) returns
    // the sample from exactly D samples ago (read(1) == most recent push).
    for (int i = 0; i < 500; ++i) dl.push ((float) i);
    checkClose (dl.readLinear (1.0f), 499.0f, 1e-3f, "read(1) == most recent sample");
    checkClose (dl.readLinear (2.0f), 498.0f, 1e-3f, "read(2) == two samples ago");
    // Fractional delay interpolates linearly.
    checkClose (dl.readLinear (1.5f), 498.5f, 1e-3f, "fractional delay interpolates");

    // Cubic read of a smooth signal should be accurate.
    DelayLine d2; d2.prepare (2048);
    for (int i = 0; i < 2000; ++i) d2.push (std::sin (i * 0.01f));
    const float expected = std::sin ((2000 - 10.3f) * 0.01f);
    checkClose (d2.readCubic (10.3f), expected, 1e-3f, "cubic delay accurate on smooth signal");
}

static void testChaos()
{
    section ("chaotic systems bounded & finite");
    Lorenz lz; lz.reset(); lz.setRate (0.01f);
    std::vector<float> lzOut;
    for (int i = 0; i < 100000; ++i) { lz.step(); lzOut.push_back (lz.outX()); lzOut.push_back (lz.outZ()); }
    checkFinite (lzOut, "Lorenz finite over 100k steps");
    checkBounded (lzOut, 1.0f, "Lorenz outputs in [-1,1]");

    Rossler rs; rs.reset(); rs.setRate (0.03f);
    std::vector<float> rsOut;
    for (int i = 0; i < 100000; ++i) { rs.step(); rsOut.push_back (rs.outX()); rsOut.push_back (rs.outY()); }
    checkFinite (rsOut, "Rossler finite over 100k steps");
    checkBounded (rsOut, 1.0f, "Rossler outputs in [-1,1]");

    LogisticMap lm; lm.reset(); lm.setR (3.9f);
    bool inRange = true;
    for (int i = 0; i < 100000; ++i) { const float v = lm.step(); if (v <= 0.0f || v >= 1.0f) inRange = false; }
    check (inRange, "Logistic map stays in (0,1)");

    // Extreme rate should not blow up (guarded).
    Lorenz lz2; lz2.reset(); lz2.setRate (0.02f); lz2.setParams (10.0f, 200.0f, 2.6f);
    std::vector<float> hot;
    for (int i = 0; i < 50000; ++i) { lz2.step(); hot.push_back (lz2.outY()); }
    checkFinite (hot, "Lorenz guarded against blow-up at hot params");
}

static void testNoise()
{
    section ("RNG & noise");
    Xorshift rng (42u);
    double mean = 0.0; float lo = 1e9f, hi = -1e9f;
    const int N = 200000;
    for (int i = 0; i < N; ++i) { const float f = rng.nextFloat(); mean += f; lo = std::min (lo, f); hi = std::max (hi, f); }
    mean /= N;
    checkClose ((float) mean, 0.5f, 0.01f, "uniform RNG mean ~0.5");
    check (lo >= 0.0f && hi < 1.0f, "uniform RNG in [0,1)");

    PinkNoise pink;
    std::vector<float> po;
    for (int i = 0; i < 100000; ++i) po.push_back (pink.process (rng.nextBipolar()));
    checkFinite (po, "pink noise finite");
    checkBounded (po, 4.0f, "pink noise bounded");
}

static void testSmoother()
{
    section ("parameter smoother");
    OnePoleSmoother sm; sm.prepare (48000.0, 10.0f);
    sm.snap (0.0f); sm.setTarget (1.0f);
    float v = 0.0f;
    for (int i = 0; i < 48000; ++i) v = sm.next();
    checkClose (v, 1.0f, 1e-3f, "smoother converges to target");
    // Monotonic approach, never overshoots.
    sm.snap (0.0f); sm.setTarget (1.0f);
    float prev = -1.0f; bool mono = true;
    for (int i = 0; i < 2000; ++i) { const float x = sm.next(); if (x < prev - 1e-6f || x > 1.0001f) mono = false; prev = x; }
    check (mono, "smoother monotonic, no overshoot");
}

int main()
{
    std::printf ("CHAOS REALM — DSP core test suite\n\n");
    testScalarUtils();
    testFFTRoundTrip();
    testFFTImpulseAndSine();
    testRealFFT();
    testSTFTReconstruction();
    testFilters();
    testDelayLine();
    testChaos();
    testNoise();
    testSmoother();
    return summary();
}
