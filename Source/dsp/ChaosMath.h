/*
    CHAOS REALM — Ultimate Unconventional Audio Processor
    ChaosMath.h — Shared, JUCE-independent DSP primitives.

    This single header is the foundation every one of the ten CHAOS REALM
    modules is built upon.  It is intentionally dependency-free (only the C++17
    standard library) so the whole DSP layer can be compiled and unit tested
    without a plugin host.

    Contents
    --------
      * Constants & scalar utilities   (denormal control, dB, clipping, interp)
      * Deterministic RNG + noise       (Xorshift128, white/pink)
      * Smoothing                       (OnePoleSmoother)
      * Filters                         (OnePole, Biquad, StateVariableFilter,
                                         DcBlocker)
      * Delay lines                     (DelayLine w/ linear + allpass interp)
      * Reverb building blocks          (AllpassDiffuser, CombFilter)
      * Chaotic systems                 (Lorenz, Rossler, LogisticMap)
      * FFT / STFT                      (radix-2 Cooley-Tukey, overlap-add STFT)

    Everything here is real-time safe after prepare()/allocate — no allocation,
    no locks, no exceptions on the audio thread.
*/
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>
#include <vector>

namespace chaos
{

// ============================================================================
//  Constants
// ============================================================================
constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;
constexpr float  kPiF   = 3.14159265358979323846f;
constexpr float  kTwoPiF= 6.28318530717958647692f;

// ============================================================================
//  Scalar utilities
// ============================================================================

/** Flush denormals / NaNs / Infs to a safe value.  Denormals cripple CPU
    performance in feedback structures; we scrub them aggressively. */
inline float sanitise (float x) noexcept
{
    if (!std::isfinite (x)) return 0.0f;
    // |x| < ~1e-15 -> treat as zero (kills denormals in long feedback tails)
    if (x < 1.0e-15f && x > -1.0e-15f) return 0.0f;
    return x;
}

template <typename T>
inline T clampT (T v, T lo, T hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

inline float clampf (float v, float lo, float hi) noexcept { return clampT (v, lo, hi); }

inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)  noexcept { return 20.0f * std::log10 (std::max (1.0e-9f, g)); }

inline float midiToFreq (float note) noexcept { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); }

/** Cheap, stable tanh approximation (good to ~1e-3, monotonic, bounded). */
inline float fastTanh (float x) noexcept
{
    if (x < -3.0f) return -1.0f;
    if (x >  3.0f) return  1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** Smooth, bounded soft-clip (cubic below 1, hard beyond). */
inline float softClip (float x) noexcept
{
    if (x <= -1.0f) return -2.0f / 3.0f;
    if (x >=  1.0f) return  2.0f / 3.0f;
    return x - (x * x * x) / 3.0f;
}

/** Wrap a phase value into [0, 1). */
inline float wrap01 (float p) noexcept
{
    p -= std::floor (p);
    return p;
}

/** Equal-power crossfade weights for a mix value t in [0,1]. */
inline void equalPower (float t, float& dryGain, float& wetGain) noexcept
{
    const float a = t * 0.5f * kPiF;
    dryGain = std::cos (a);
    wetGain = std::sin (a);
}

// ============================================================================
//  Deterministic RNG (xorshift128) + noise generators
// ============================================================================
class Xorshift
{
public:
    explicit Xorshift (uint32_t seed = 0x9E3779B9u) { setSeed (seed); }

    void setSeed (uint32_t seed) noexcept
    {
        // Avoid the all-zero state; spl-mix the seed into four words.
        s[0] = seed ? seed : 0x1u;
        for (int i = 1; i < 4; ++i)
            s[i] = s[i - 1] * 1812433253u + 1u;
    }

    uint32_t nextUInt() noexcept
    {
        uint32_t t = s[3];
        const uint32_t sw = s[0];
        s[3] = s[2]; s[2] = s[1]; s[1] = sw;
        t ^= t << 11;
        t ^= t >> 8;
        s[0] = t ^ sw ^ (sw >> 19);
        return s[0];
    }

    /** Uniform float in [0, 1). */
    float nextFloat() noexcept
    {
        return (nextUInt() >> 8) * (1.0f / 16777216.0f); // 24-bit mantissa
    }

    /** Uniform float in [-1, 1). */
    float nextBipolar() noexcept { return nextFloat() * 2.0f - 1.0f; }

    /** Approximate standard-normal via central limit (sum of 4 uniforms). */
    float nextGaussian() noexcept
    {
        float sum = 0.0f;
        for (int i = 0; i < 4; ++i) sum += nextFloat();
        return (sum - 2.0f) * 1.2247448f; // scale to ~unit variance
    }

private:
    uint32_t s[4];
};

/** Paul Kellet's economical pink-noise filter (fed with white noise). */
class PinkNoise
{
public:
    void reset() noexcept { for (auto& v : b) v = 0.0f; }

    float process (float white) noexcept
    {
        b[0] = 0.99886f * b[0] + white * 0.0555179f;
        b[1] = 0.99332f * b[1] + white * 0.0750759f;
        b[2] = 0.96900f * b[2] + white * 0.1538520f;
        b[3] = 0.86650f * b[3] + white * 0.3104856f;
        b[4] = 0.55000f * b[4] + white * 0.5329522f;
        b[5] = -0.7616f * b[5] - white * 0.0168980f;
        const float out = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + white * 0.5362f;
        b[6] = white * 0.115926f;
        return out * 0.11f; // normalise roughly to +/-1
    }

private:
    float b[7] = {0,0,0,0,0,0,0};
};

// ============================================================================
//  Smoothing
// ============================================================================

/** One-pole exponential smoother for click-free parameter changes. */
class OnePoleSmoother
{
public:
    void prepare (double sampleRate, float timeMs = 20.0f) noexcept
    {
        sr = sampleRate;
        setTime (timeMs);
    }

    void setTime (float timeMs) noexcept
    {
        const float tc = std::max (0.0001f, timeMs) * 0.001f;
        coeff = std::exp (-1.0f / (tc * static_cast<float> (sr)));
    }

    void setTarget (float t) noexcept { target = t; }
    void snap (float v) noexcept      { target = current = v; }

    float next() noexcept
    {
        current = target + (current - target) * coeff;
        return current;
    }

    float value() const noexcept { return current; }

private:
    double sr = 44100.0;
    float coeff = 0.0f, current = 0.0f, target = 0.0f;
};

// ============================================================================
//  Filters
// ============================================================================

/** First-order one-pole low/high pass. */
class OnePole
{
public:
    void reset() noexcept { z = 0.0f; }

    /** Set cutoff (Hz).  highpass=false -> lowpass. */
    void setCutoff (float hz, double sampleRate, bool highpass = false) noexcept
    {
        const float x = std::exp (-kTwoPiF * clampf (hz, 1.0f, (float) sampleRate * 0.49f)
                                  / (float) sampleRate);
        a = 1.0f - x;
        b = x;
        hp = highpass;
    }

    float process (float in) noexcept
    {
        z = a * in + b * z;
        return hp ? in - z : z;
    }

private:
    float a = 1.0f, b = 0.0f, z = 0.0f;
    bool  hp = false;
};

/** DC blocker (leaky differentiator). */
class DcBlocker
{
public:
    void reset() noexcept { x1 = y1 = 0.0f; }
    float process (float x) noexcept
    {
        const float y = x - x1 + 0.9975f * y1;
        x1 = x; y1 = y;
        return y;
    }
private:
    float x1 = 0.0f, y1 = 0.0f;
};

/** Transposed Direct-Form-II biquad with RBJ cookbook coefficient helpers. */
class Biquad
{
public:
    enum class Type { LowPass, HighPass, BandPass, Notch, Peak, LowShelf, HighShelf };

    void reset() noexcept { z1 = z2 = 0.0f; }

    void setCoefficients (Type type, float freq, double sampleRate,
                          float Q = 0.7071f, float gainDb = 0.0f) noexcept
    {
        const float w0    = kTwoPiF * clampf (freq, 1.0f, (float) sampleRate * 0.49f)
                            / (float) sampleRate;
        const float cw    = std::cos (w0);
        const float sw    = std::sin (w0);
        const float alpha = sw / (2.0f * std::max (0.0001f, Q));
        const float A     = std::pow (10.0f, gainDb / 40.0f);

        float b0=1, b1=0, b2=0, a0=1, a1=0, a2=0;
        switch (type)
        {
            case Type::LowPass:
                b0 = (1 - cw) * 0.5f; b1 = 1 - cw; b2 = (1 - cw) * 0.5f;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::HighPass:
                b0 = (1 + cw) * 0.5f; b1 = -(1 + cw); b2 = (1 + cw) * 0.5f;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::BandPass:
                b0 = alpha; b1 = 0; b2 = -alpha;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::Notch:
                b0 = 1; b1 = -2 * cw; b2 = 1;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::Peak:
                b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
                a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
            case Type::LowShelf:
            {
                const float s = 2.0f * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) - (A - 1) * cw + s);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - s);
                a0 = (A + 1) + (A - 1) * cw + s;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - s; break;
            }
            case Type::HighShelf:
            {
                const float s = 2.0f * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) + (A - 1) * cw + s);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - s);
                a0 = (A + 1) - (A - 1) * cw + s;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - s; break;
            }
        }
        const float inv = 1.0f / a0;
        cb0 = b0 * inv; cb1 = b1 * inv; cb2 = b2 * inv;
        ca1 = a1 * inv; ca2 = a2 * inv;
    }

    float process (float in) noexcept
    {
        const float out = cb0 * in + z1;
        z1 = cb1 * in - ca1 * out + z2;
        z2 = cb2 * in - ca2 * out;
        return out;
    }

private:
    float cb0 = 1, cb1 = 0, cb2 = 0, ca1 = 0, ca2 = 0;
    float z1 = 0, z2 = 0;
};

/** Andrew Simper's TPT State-Variable Filter — cheap, stable at any cutoff,
    simultaneous LP/BP/HP outputs.  Ideal for modulated / chaotic sweeps. */
class StateVariableFilter
{
public:
    void reset() noexcept { ic1eq = ic2eq = 0.0f; }

    void setParams (float freq, float Q, double sampleRate) noexcept
    {
        const float f = clampf (freq, 10.0f, (float) sampleRate * 0.49f);
        g = std::tan (kPiF * f / (float) sampleRate);
        k = 1.0f / std::max (0.05f, Q);
        const float denom = 1.0f + g * (g + k);
        a1 = 1.0f / denom;
        a2 = g * a1;
        a3 = g * a2;
    }

    struct Outputs { float lp, bp, hp; };

    Outputs process (float v0) noexcept
    {
        const float v3 = v0 - ic2eq;
        const float v1 = a1 * ic1eq + a2 * v3;
        const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;
        return { v2, v1, v0 - k * v1 - v2 };
    }

    float processLP (float v) noexcept { return process (v).lp; }
    float processBP (float v) noexcept { return process (v).bp; }
    float processHP (float v) noexcept { return process (v).hp; }

private:
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    float ic1eq = 0, ic2eq = 0;
};

// ============================================================================
//  Delay line (power-of-two ring buffer, fractional read)
// ============================================================================
class DelayLine
{
public:
    /** maxDelaySamples is rounded up to a power of two. */
    void prepare (int maxDelaySamples)
    {
        int size = 4;
        while (size < maxDelaySamples + 4) size <<= 1;
        mask = size - 1;
        buffer.assign ((size_t) size, 0.0f);
        writePos = 0;
        apLast = 0.0f;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0; apLast = 0.0f;
    }

    void push (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    /** Read 'delay' samples in the past with linear interpolation. */
    float readLinear (float delay) const noexcept
    {
        delay = clampf (delay, 0.0f, (float) mask - 1.0f);
        const float readPos = (float) writePos - delay;
        int i0 = (int) std::floor (readPos);
        const float frac = readPos - (float) i0;
        i0 &= mask;
        const int i1 = (i0 + 1) & mask;
        return lerp (buffer[(size_t) i0], buffer[(size_t) i1], frac);
    }

    /** Cubic (Catmull-Rom) interpolated read — smoother for pitch/modulation. */
    float readCubic (float delay) const noexcept
    {
        delay = clampf (delay, 1.0f, (float) mask - 2.0f);
        const float readPos = (float) writePos - delay;
        int i1 = (int) std::floor (readPos);
        const float f = readPos - (float) i1;
        const int i0 = (i1 - 1) & mask;
        const int i2 = (i1 + 1) & mask;
        const int i3 = (i1 + 2) & mask;
        i1 &= mask;
        const float a0 = buffer[(size_t) i0], a1 = buffer[(size_t) i1];
        const float a2 = buffer[(size_t) i2], a3 = buffer[(size_t) i3];
        const float c0 = a1;
        const float c1 = 0.5f * (a2 - a0);
        const float c2 = a0 - 2.5f * a1 + 2.0f * a2 - 0.5f * a3;
        const float c3 = 0.5f * (a3 - a0) + 1.5f * (a1 - a2);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }

    int size() const noexcept { return mask + 1; }

private:
    std::vector<float> buffer;
    int   mask = 0;
    int   writePos = 0;
    float apLast = 0.0f;
};

// ============================================================================
//  Reverb building blocks
// ============================================================================

/** Schroeder allpass diffuser. */
class AllpassDiffuser
{
public:
    void prepare (int maxSamples) { delay.prepare (maxSamples); delaySamples = (float) maxSamples * 0.5f; }
    void reset() noexcept { delay.reset(); }
    void setDelay (float samples) noexcept { delaySamples = std::max (1.0f, samples); }
    void setFeedback (float g) noexcept    { feedback = clampf (g, -0.98f, 0.98f); }

    float process (float in) noexcept
    {
        const float delayed = delay.readLinear (delaySamples);
        const float v = in + feedback * delayed;
        delay.push (v);
        return sanitise (delayed - feedback * v);
    }

private:
    DelayLine delay;
    float delaySamples = 100.0f;
    float feedback = 0.5f;
};

/** Feedback comb filter with a one-pole damping element in the loop. */
class CombFilter
{
public:
    void prepare (int maxSamples) { delay.prepare (maxSamples); delaySamples = (float) maxSamples * 0.5f; }
    void reset() noexcept { delay.reset(); damp = 0.0f; }
    void setDelay (float samples) noexcept   { delaySamples = std::max (1.0f, samples); }
    void setFeedback (float g) noexcept      { feedback = clampf (g, 0.0f, 0.999f); }
    void setDamping (float d) noexcept       { damping = clampf (d, 0.0f, 0.99f); }

    float process (float in) noexcept
    {
        const float y = delay.readLinear (delaySamples);
        damp = y * (1.0f - damping) + damp * damping;
        delay.push (sanitise (in + damp * feedback));
        return y;
    }

private:
    DelayLine delay;
    float delaySamples = 100.0f;
    float feedback = 0.7f, damping = 0.2f, damp = 0.0f;
};

// ============================================================================
//  Chaotic systems (bounded, normalised outputs in ~[-1, 1])
// ============================================================================

/** Lorenz attractor integrated with forward Euler.  x/y/z normalised. */
class Lorenz
{
public:
    void reset() noexcept { x = 0.1f; y = 0.0f; z = 0.0f; }
    void setRate (float dt) noexcept { h = clampf (dt, 0.0001f, 0.02f); }
    void setParams (float sigma_, float rho_, float beta_) noexcept
    { sigma = sigma_; rho = rho_; beta = beta_; }

    void step() noexcept
    {
        const float dx = sigma * (y - x);
        const float dy = x * (rho - z) - y;
        const float dz = x * y - beta * z;
        x += h * dx; y += h * dy; z += h * dz;
        // Guard against blow-up from extreme rates.
        if (!std::isfinite (x + y + z)) reset();
    }

    float outX() const noexcept { return clampf (x * 0.05f, -1.0f, 1.0f); }
    float outY() const noexcept { return clampf (y * 0.05f, -1.0f, 1.0f); }
    float outZ() const noexcept { return clampf ((z - 25.0f) * 0.05f, -1.0f, 1.0f); }

private:
    float x = 0.1f, y = 0.0f, z = 0.0f;
    float sigma = 10.0f, rho = 28.0f, beta = 8.0f / 3.0f;
    float h = 0.005f;
};

/** Rössler attractor — smoother, more tonal chaos than Lorenz. */
class Rossler
{
public:
    void reset() noexcept { x = 0.1f; y = 0.0f; z = 0.0f; }
    void setRate (float dt) noexcept { h = clampf (dt, 0.0001f, 0.05f); }
    void setParams (float a_, float b_, float c_) noexcept { a = a_; b = b_; c = c_; }

    void step() noexcept
    {
        const float dx = -y - z;
        const float dy = x + a * y;
        const float dz = b + z * (x - c);
        x += h * dx; y += h * dy; z += h * dz;
        if (!std::isfinite (x + y + z)) reset();
    }

    float outX() const noexcept { return clampf (x * 0.1f, -1.0f, 1.0f); }
    float outY() const noexcept { return clampf (y * 0.1f, -1.0f, 1.0f); }
    float outZ() const noexcept { return clampf (z * 0.04f - 1.0f, -1.0f, 1.0f); }

private:
    float x = 0.1f, y = 0.0f, z = 0.0f;
    float a = 0.2f, b = 0.2f, c = 5.7f;
    float h = 0.02f;
};

/** Logistic map — discrete chaos, great for stochastic-sounding gates. */
class LogisticMap
{
public:
    void reset() noexcept { x = 0.5f; }
    void setR (float r_) noexcept { r = clampf (r_, 2.5f, 4.0f); }

    float step() noexcept
    {
        x = r * x * (1.0f - x);
        if (!std::isfinite (x) || x <= 0.0f || x >= 1.0f) x = 0.5f;
        return x; // in (0,1)
    }

    float value() const noexcept { return x; }

private:
    float x = 0.5f, r = 3.9f;
};

// ============================================================================
//  FFT — iterative radix-2 Cooley-Tukey (in-place, power-of-two sizes)
// ============================================================================
class FFT
{
public:
    /** order == log2(size).  size = 1 << order. */
    void prepare (int order)
    {
        fftOrder = order;
        fftSize  = 1 << order;
        buildTwiddles();
        buildBitReversal();
    }

    int size()  const noexcept { return fftSize; }
    int order() const noexcept { return fftOrder; }

    /** In-place complex FFT.  inverse=false: forward; true: inverse (scaled by 1/N). */
    void transform (std::vector<std::complex<float>>& data, bool inverse) const
    {
        const int n = fftSize;
        // Bit-reversal permutation.
        for (int i = 0; i < n; ++i)
        {
            const int j = bitRev[(size_t) i];
            if (j > i) std::swap (data[(size_t) i], data[(size_t) j]);
        }
        // Butterflies.
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1;
            const int step = n / len;
            for (int i = 0; i < n; i += len)
            {
                int k = 0;
                for (int j = 0; j < half; ++j)
                {
                    std::complex<float> w = twiddles[(size_t) k];
                    if (inverse) w = std::conj (w);
                    const auto u = data[(size_t) (i + j)];
                    const auto v = data[(size_t) (i + j + half)] * w;
                    data[(size_t) (i + j)]        = u + v;
                    data[(size_t) (i + j + half)] = u - v;
                    k += step;
                }
            }
        }
        if (inverse)
        {
            const float inv = 1.0f / (float) n;
            for (auto& c : data) c *= inv;
        }
    }

private:
    void buildTwiddles()
    {
        twiddles.resize ((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
        {
            const float ang = -kTwoPiF * (float) i / (float) fftSize;
            twiddles[(size_t) i] = { std::cos (ang), std::sin (ang) };
        }
    }

    void buildBitReversal()
    {
        bitRev.resize ((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
        {
            int x = i, r = 0;
            for (int b = 0; b < fftOrder; ++b) { r = (r << 1) | (x & 1); x >>= 1; }
            bitRev[(size_t) i] = r;
        }
    }

    int fftOrder = 0, fftSize = 1;
    std::vector<std::complex<float>> twiddles;
    std::vector<int> bitRev;
};

/** Hann window (periodic). */
inline void makeHannWindow (std::vector<float>& w, int size)
{
    w.resize ((size_t) size);
    for (int i = 0; i < size; ++i)
        w[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPiF * (float) i / (float) size);
}

// ============================================================================
//  RealFFT — real-input FFT via a half-size complex FFT.
//
//  A length-N real signal (N a power of two) has a conjugate-symmetric
//  spectrum, so only N/2+1 bins are unique.  Computing it with a full N-point
//  complex FFT wastes ~half the work.  RealFFT instead runs an (N/2)-point
//  complex FFT and split/recombines, roughly halving the transform cost — the
//  single biggest CPU lever for the STFT-based Spectral module.
//
//  Correctness is asserted by tests/dsp_core_tests.cpp against both a direct
//  DFT and a full complex FFT.
// ============================================================================
class RealFFT
{
public:
    /** order == log2(N), where N is the REAL length.  N must be >= 4. */
    void prepare (int order)
    {
        realOrder = order;
        N = 1 << order;
        M = N / 2;                       // internal complex FFT size
        half.prepare (order - 1);        // (N/2)-point complex FFT
        buffer.assign ((size_t) M, {0.0f, 0.0f});
        // Recombination twiddles W_N^k = exp(-2πi k / N), k = 0..M.
        tw.resize ((size_t) (M + 1));
        for (int k = 0; k <= M; ++k)
        {
            const float a = -kTwoPiF * (float) k / (float) N;
            tw[(size_t) k] = { std::cos (a), std::sin (a) };
        }
    }

    int size() const noexcept { return N; }

    /** Forward: real[N] -> spectrum[0..N/2] (N/2+1 unique complex bins). */
    void forward (const float* in, std::complex<float>* spec) const
    {
        // Pack pairs of reals into a half-length complex sequence.
        for (int n = 0; n < M; ++n)
            buffer[(size_t) n] = { in[2 * n], in[2 * n + 1] };
        half.transform (buffer, false);

        // Split into even/odd DFTs and recombine.
        for (int k = 0; k <= M; ++k)
        {
            const auto Ck  = buffer[(size_t) (k % M)];
            const auto Cmk = buffer[(size_t) ((M - k) % M)];
            const auto cCmk = std::conj (Cmk);
            const std::complex<float> Xe = 0.5f * (Ck + cCmk);
            const std::complex<float> Xo = std::complex<float> (0.0f, -0.5f) * (Ck - cCmk);
            spec[(size_t) k] = Xe + tw[(size_t) k] * Xo;
        }
    }

    /** Inverse: spectrum[0..N/2] -> real[N].  spec[0] and spec[N/2] should be
        real; any imaginary part there is ignored. */
    void inverse (const std::complex<float>* spec, float* out) const
    {
        for (int k = 0; k < M; ++k)
        {
            const auto Xk  = spec[(size_t) k];
            const auto Xmk = std::conj (spec[(size_t) (M - k)]);
            const std::complex<float> Xe = 0.5f * (Xk + Xmk);
            // Undo the forward twiddle (conjugate) and the -i factor.
            const std::complex<float> Xo = std::complex<float> (0.0f, 0.5f)
                                         * std::conj (tw[(size_t) k]) * (Xk - Xmk);
            buffer[(size_t) k] = Xe + Xo;
        }
        half.transform (buffer, true); // inverse (scaled by 1/M)
        for (int n = 0; n < M; ++n)
        {
            out[2 * n]     = buffer[(size_t) n].real();
            out[2 * n + 1] = buffer[(size_t) n].imag();
        }
    }

private:
    FFT half;
    int realOrder = 0, N = 0, M = 0;
    mutable std::vector<std::complex<float>> buffer;
    std::vector<std::complex<float>> tw;
};

/** Streaming Weighted-Overlap-Add (WOLA) STFT engine.

    Feed it one sample at a time via process(); once a full hop has been
    collected it windows a frame, forward-transforms, invokes the user
    'spectral callback' with the complex spectrum, transforms back and
    overlap-adds into the output.  The callback receives a mutable spectrum and
    the number of unique bins (0..N/2 inclusive); it should only touch those and
    the engine restores Hermitian symmetry before the inverse transform.

    Analysis and synthesis both use a sqrt-Hann window so the *product* is a
    Hann window, which satisfies the Constant-Overlap-Add (COLA) condition at
    50% and 75% overlap.  A single scalar normalisation therefore yields
    (near) perfect reconstruction for an identity callback.  The processing
    latency is exactly one frame (N samples).

    Implementation follows the classic FIFO/accumulator structure
    (cf. S. Bernsee's WOLA): input is collected in a length-N FIFO shifted by
    the hop each frame, output is overlap-added into a length-2N accumulator
    whose head H samples are drained into a small output FIFO.
*/
class STFT
{
public:
    using SpectralCallback = std::function<void (std::vector<std::complex<float>>& spectrum,
                                                 int numBins, float sampleRate)>;

    /** fftOrder: log2 frame size.  overlap: 2 (50%) or 4 (75%). */
    void prepare (int fftOrder, int overlap, double sr)
    {
        rfft.prepare (fftOrder);
        size = rfft.size();
        hop  = size / std::max (2, overlap);
        sampleRate = (float) sr;
        inFifoLatency = size - hop;

        // sqrt-Hann window: analysis * synthesis == Hann (COLA-compliant).
        window.resize ((size_t) size);
        for (int i = 0; i < size; ++i)
        {
            const float h = 0.5f - 0.5f * std::cos (kTwoPiF * (float) i / (float) size);
            window[(size_t) i] = std::sqrt (h);
        }

        // Steady-state overlap-add gain of the *product* window (== Hann),
        // measured at the frame centre.  This is position-independent by COLA.
        float C = 0.0f;
        for (int m = -overlap; m <= overlap; ++m)
        {
            const int j = size / 2 - m * hop;
            if (j >= 0 && j < size) C += window[(size_t) j] * window[(size_t) j];
        }
        winNorm = C > 1.0e-6f ? 1.0f / C : 1.0f;

        inFifo.assign ((size_t) size, 0.0f);
        outFifo.assign ((size_t) size, 0.0f);
        outAccum.assign ((size_t) (2 * size), 0.0f);
        realFrame.assign ((size_t) size, 0.0f);
        spectrum.assign ((size_t) (size / 2 + 1), {0.0f, 0.0f});
        rover = inFifoLatency;
    }

    void reset() noexcept
    {
        std::fill (inFifo.begin(),  inFifo.end(),  0.0f);
        std::fill (outFifo.begin(), outFifo.end(), 0.0f);
        std::fill (outAccum.begin(),outAccum.end(),0.0f);
        rover = inFifoLatency;
    }

    int latency()   const noexcept { return size; } // one full frame
    int frameSize() const noexcept { return size; }
    int hopSize()   const noexcept { return hop; }

    /** Process a single sample; returns the reconstructed sample delayed by
        latency() samples. */
    float process (float in, const SpectralCallback& cb)
    {
        inFifo[(size_t) rover] = in;
        const float out = outFifo[(size_t) (rover - inFifoLatency)];
        ++rover;
        if (rover >= size)
        {
            rover = inFifoLatency;
            processFrame (cb);
        }
        return out;
    }

private:
    void processFrame (const SpectralCallback& cb)
    {
        // Windowed analysis into the real frame, then a half-size real FFT.
        for (int i = 0; i < size; ++i)
            realFrame[(size_t) i] = inFifo[(size_t) i] * window[(size_t) i];

        rfft.forward (realFrame.data(), spectrum.data());

        cb (spectrum, size / 2 + 1, sampleRate);

        // DC and Nyquist must be real for a purely real inverse.
        spectrum[0].imag (0.0f);
        spectrum[(size_t) (size / 2)].imag (0.0f);

        rfft.inverse (spectrum.data(), realFrame.data());

        // Windowed overlap-add.
        for (int i = 0; i < size; ++i)
            outAccum[(size_t) i] += window[(size_t) i] * realFrame[(size_t) i] * winNorm;

        // Drain the head hop into the output FIFO, then shift.
        for (int i = 0; i < hop; ++i) outFifo[(size_t) i] = outAccum[(size_t) i];
        std::move (outAccum.begin() + hop, outAccum.end(), outAccum.begin());
        std::fill (outAccum.end() - hop, outAccum.end(), 0.0f);

        // Shift the input FIFO left by one hop.
        std::move (inFifo.begin() + hop, inFifo.end(), inFifo.begin());
    }

    RealFFT rfft;
    int size = 0, hop = 0, inFifoLatency = 0, rover = 0;
    float sampleRate = 44100.0f;
    float winNorm = 1.0f;
    std::vector<float> window, inFifo, outFifo;
    std::vector<float> outAccum;
    std::vector<float> realFrame;
    std::vector<std::complex<float>> spectrum;
};

} // namespace chaos
