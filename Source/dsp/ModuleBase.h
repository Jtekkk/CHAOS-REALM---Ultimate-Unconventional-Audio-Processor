/*
    CHAOS REALM — Ultimate Unconventional Audio Processor
    ModuleBase.h — Abstract interface every processing module implements.

    Design contract (read this before writing a module)
    ----------------------------------------------------
      * A module is a pure-C++ object.  It must NOT depend on JUCE or any
        framework — only ChaosMath.h / AudioTypes.h and the STL.
      * Parameters are exchanged in NORMALISED 0..1 space.  The module maps
        those to its own internal units inside process().  Declare them once in
        buildParameters() (called from the base ctor helper) so the host and
        preset system can enumerate them.
      * process() is always fully WET and IN PLACE.  Dry/wet mixing and bypass
        are handled by ChaosEngine, so a module never keeps a dry copy itself.
      * prepare() may allocate.  process()/reset() must be allocation- and
        lock-free (real-time safe).
      * Given silence in, a module must eventually settle to silence (no
        self-oscillation / DC / NaN).  The stability test-suite enforces this.
*/
#pragma once

#include "AudioTypes.h"
#include "ChaosMath.h"

#include <memory>
#include <string>
#include <vector>

namespace chaos
{

class ModuleBase
{
public:
    virtual ~ModuleBase() = default;

    // -- Identity ----------------------------------------------------------
    virtual const char* getName() const = 0;
    virtual ModuleID    getID()   const = 0;

    // -- Lifecycle ---------------------------------------------------------
    /** Allocate & configure for the given stream.  Called before any process. */
    virtual void prepare (const ProcessContext& ctx)
    {
        context = ctx;
        smoothers.resize (params.size());
        for (size_t i = 0; i < params.size(); ++i)
        {
            smoothers[i].prepare (ctx.sampleRate, 15.0f);
            smoothers[i].snap (params[i]);
        }
        prepared = true;
        onPrepare (ctx);
    }

    /** Clear all internal state (buffers/filters) but keep parameters. */
    virtual void reset() { onReset(); }

    /** Process interleaved-by-channel audio in place, fully wet.
        buffers[ch][sample], ch < numChannels, sample < numSamples. */
    virtual void process (float* const* buffers, int numChannels, int numSamples) = 0;

    /** Reported latency in samples (0 unless the module buffers, e.g. STFT). */
    virtual int getLatencySamples() const { return 0; }

    // -- Parameters (normalised 0..1) --------------------------------------
    int getNumParameters() const noexcept { return (int) params.size(); }

    const ParameterInfo& getParameterInfo (int index) const { return paramInfo[(size_t) index]; }

    float getParameter (int index) const noexcept { return params[(size_t) index]; }

    void setParameter (int index, float value01) noexcept
    {
        value01 = clampf (value01, 0.0f, 1.0f);
        params[(size_t) index] = value01;
        if (prepared && (size_t) index < smoothers.size())
            smoothers[(size_t) index].setTarget (value01);
    }

    /** Immediately jump a parameter (no smoothing) — used on preset load. */
    void snapParameter (int index, float value01) noexcept
    {
        value01 = clampf (value01, 0.0f, 1.0f);
        params[(size_t) index] = value01;
        if (prepared && (size_t) index < smoothers.size())
            smoothers[(size_t) index].snap (value01);
    }

protected:
    // -- Hooks for subclasses ---------------------------------------------
    virtual void onPrepare (const ProcessContext&) {}
    virtual void onReset() {}

    /** Register a parameter; call from the subclass constructor.  Returns the
        parameter's index for convenient named access. */
    int addParameter (const ParameterInfo& info)
    {
        paramInfo.push_back (info);
        params.push_back (info.defaultValue);
        return (int) params.size() - 1;
    }

    /** Smoothed, per-sample value of parameter i (call once per sample). */
    float smoothed (int i) noexcept { return smoothers[(size_t) i].next(); }

    /** Latest smoothed value without advancing (for per-block use). */
    float smoothedValue (int i) const noexcept { return smoothers[(size_t) i].value(); }

    /** Raw target value of parameter i (no smoothing). */
    float raw (int i) const noexcept { return params[(size_t) i]; }

    ProcessContext context;
    bool prepared = false;

private:
    std::vector<ParameterInfo>  paramInfo;
    std::vector<float>          params;    // normalised 0..1
    std::vector<OnePoleSmoother> smoothers;
};

using ModulePtr = std::unique_ptr<ModuleBase>;

} // namespace chaos
