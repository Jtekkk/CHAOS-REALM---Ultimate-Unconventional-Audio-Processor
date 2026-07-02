/*
    CHAOS REALM — Ultimate Unconventional Audio Processor
    Modulation.h — Global modulation system.

    Provides the "advanced modulation system" from the project vision:
      * 4 LFOs with multiple shapes (incl. chaotic sample & hold)
      * 1 envelope follower driven by the plugin input
      * 4 user macro sources
      * A modulation matrix routing any source to any module parameter with a
        signed depth.

    The matrix produces a per-parameter additive offset in normalised space that
    the engine sums onto each module's base parameter value once per block.
    Everything is JUCE-free and real-time safe after prepare().
*/
#pragma once

#include "ChaosMath.h"

#include <array>
#include <vector>

namespace chaos
{

// ---------------------------------------------------------------------------
//  Low-Frequency Oscillator
// ---------------------------------------------------------------------------
class LFO
{
public:
    enum class Shape { Sine, Triangle, Saw, Square, SampleHold, Chaos, NumShapes };

    void prepare (double sr) noexcept { sampleRate = (float) sr; reset(); }
    void reset() noexcept
    {
        phase = 0.0f; shValue = rng.nextBipolar(); logistic.reset();
    }

    void setRateHz (float hz) noexcept   { rateHz = clampf (hz, 0.001f, 200.0f); }
    void setShape (Shape s) noexcept     { shape = s; }
    void setPhaseOffset (float p) noexcept { phaseOffset = wrap01 (p); }

    /** Advance by 'numSamples' and return the bipolar [-1,1] value at the end. */
    float advance (int numSamples) noexcept
    {
        const float inc = rateHz / sampleRate * (float) numSamples;
        const float prevPhase = phase;
        phase = wrap01 (phase + inc);
        // Detect wrap for sample & hold / chaos updates.
        if (phase < prevPhase)
        {
            shValue  = rng.nextBipolar();
            logistic.step();
        }
        return valueAt (wrap01 (phase + phaseOffset));
    }

    float current() const noexcept { return valueAt (wrap01 (phase + phaseOffset)); }

private:
    float valueAt (float p) const noexcept
    {
        switch (shape)
        {
            case Shape::Sine:       return std::sin (kTwoPiF * p);
            case Shape::Triangle:   return 4.0f * std::fabs (p - 0.5f) - 1.0f;
            case Shape::Saw:        return 2.0f * p - 1.0f;
            case Shape::Square:     return p < 0.5f ? 1.0f : -1.0f;
            case Shape::SampleHold: return shValue;
            case Shape::Chaos:      return logistic.value() * 2.0f - 1.0f;
            default:                return 0.0f;
        }
    }

    float sampleRate = 44100.0f;
    float rateHz = 1.0f, phase = 0.0f, phaseOffset = 0.0f;
    float shValue = 0.0f;
    Shape shape = Shape::Sine;
    Xorshift rng { 0xC0FFEEu };
    LogisticMap logistic;
};

// ---------------------------------------------------------------------------
//  Envelope follower (peak/RMS-ish, attack/release ballistics)
// ---------------------------------------------------------------------------
class EnvelopeFollower
{
public:
    void prepare (double sr) noexcept { sampleRate = (float) sr; setTimes (attackMs, releaseMs); env = 0.0f; }

    void setTimes (float atkMs, float relMs) noexcept
    {
        attackMs = atkMs; releaseMs = relMs;
        aCoef = std::exp (-1.0f / (0.001f * std::max (0.1f, atkMs) * sampleRate));
        rCoef = std::exp (-1.0f / (0.001f * std::max (0.1f, relMs) * sampleRate));
    }

    /** Feed a block, return the resulting envelope in [0,1]. */
    float processBlock (const float* const* buffers, int numCh, int numSamples) noexcept
    {
        for (int n = 0; n < numSamples; ++n)
        {
            float peak = 0.0f;
            for (int c = 0; c < numCh; ++c) peak = std::max (peak, std::fabs (buffers[c][n]));
            const float coef = peak > env ? aCoef : rCoef;
            env = peak + (env - peak) * coef;
        }
        return clampf (env, 0.0f, 1.0f);
    }

    float value() const noexcept { return clampf (env, 0.0f, 1.0f); }

private:
    float sampleRate = 44100.0f;
    float attackMs = 5.0f, releaseMs = 120.0f;
    float aCoef = 0.0f, rCoef = 0.0f, env = 0.0f;
};

// ---------------------------------------------------------------------------
//  Modulation matrix
// ---------------------------------------------------------------------------
class ModMatrix
{
public:
    static constexpr int kNumLFOs   = 4;
    static constexpr int kNumMacros = 4;
    static constexpr int kNumSlots  = 16;

    enum class Source { None, LFO1, LFO2, LFO3, LFO4, Envelope, Macro1, Macro2, Macro3, Macro4 };

    struct Slot
    {
        Source source = Source::None;
        int    destModule = -1;   // module index, -1 = disabled
        int    destParam  = -1;   // parameter index within that module
        float  depth = 0.0f;      // signed, [-1, 1]
    };

    void prepare (double sr)
    {
        for (auto& l : lfos) l.prepare (sr);
        env.prepare (sr);
    }

    void reset()
    {
        for (auto& l : lfos) l.reset();
    }

    LFO&              lfo (int i)           { return lfos[(size_t) i]; }
    EnvelopeFollower& envelope()            { return env; }
    void setMacro (int i, float v) noexcept { macros[(size_t) i] = clampf (v, 0.0f, 1.0f); }
    Slot& slot (int i)                      { return slots[(size_t) i]; }

    /** Advance all modulators for one block and evaluate their current values. */
    void updateSources (const float* const* buffers, int numCh, int numSamples) noexcept
    {
        for (auto& l : lfos) l.advance (numSamples);
        env.processBlock (buffers, numCh, numSamples);
    }

    float sourceValue (Source s) const noexcept
    {
        switch (s)
        {
            case Source::LFO1: return lfos[0].current();
            case Source::LFO2: return lfos[1].current();
            case Source::LFO3: return lfos[2].current();
            case Source::LFO4: return lfos[3].current();
            case Source::Envelope: return env.value() * 2.0f - 1.0f; // to bipolar
            case Source::Macro1: return macros[0] * 2.0f - 1.0f;
            case Source::Macro2: return macros[1] * 2.0f - 1.0f;
            case Source::Macro3: return macros[2] * 2.0f - 1.0f;
            case Source::Macro4: return macros[3] * 2.0f - 1.0f;
            default: return 0.0f;
        }
    }

    /** Sum the modulation offset (normalised) for a given module/param. */
    float offsetFor (int moduleIndex, int paramIndex) const noexcept
    {
        float sum = 0.0f;
        for (const auto& s : slots)
            if (s.source != Source::None && s.destModule == moduleIndex && s.destParam == paramIndex)
                sum += s.depth * sourceValue (s.source);
        return sum;
    }

private:
    std::array<LFO, kNumLFOs> lfos;
    EnvelopeFollower env;
    std::array<float, kNumMacros> macros { {0.0f, 0.0f, 0.0f, 0.0f} };
    std::array<Slot, kNumSlots> slots;
};

} // namespace chaos
