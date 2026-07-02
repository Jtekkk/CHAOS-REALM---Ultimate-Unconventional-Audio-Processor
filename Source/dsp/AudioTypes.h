/*
    CHAOS REALM — Ultimate Unconventional Audio Processor
    AudioTypes.h — Fundamental, JUCE-independent audio types.

    The entire DSP layer of CHAOS REALM is deliberately free of any JUCE (or
    other framework) dependency.  This keeps the algorithms portable, unit
    testable in isolation, and re-usable outside of a plugin context.  The JUCE
    wrapper (PluginProcessor) is the *only* place that bridges these pure types
    to the host.
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chaos
{

/** Everything a module needs to know about the audio stream before it runs. */
struct ProcessContext
{
    double sampleRate   = 44100.0;
    int    maxBlockSize = 512;
    int    numChannels  = 2;
};

/** Describes a single user-facing parameter of a module.

    Modules always store and exchange parameter values in a normalised 0..1
    space (so the plugin's parameter bridge is uniform and trivial).  The
    display range / unit here exists purely so the UI and preset system can
    present a human-friendly value.  A module is free to map the normalised
    value to whatever internal curve it likes inside process().
*/
struct ParameterInfo
{
    std::string id;                 // stable id, unique within a module (e.g. "drive")
    std::string name;               // display name (e.g. "Drive")
    float       defaultValue = 0.f; // normalised 0..1
    std::string unit;               // display unit (e.g. "Hz", "%", "")
    float       minDisplay   = 0.f; // for UI text mapping
    float       maxDisplay   = 1.f;
    bool        isStepped    = false; // discrete (choice / integer) parameter?
    int         numSteps     = 0;     // when isStepped: number of distinct values

    ParameterInfo() = default;
    ParameterInfo (std::string pid, std::string pname, float def,
                   std::string punit = {}, float minD = 0.f, float maxD = 1.f)
        : id (std::move (pid)), name (std::move (pname)), defaultValue (def),
          unit (std::move (punit)), minDisplay (minD), maxDisplay (maxD) {}

    /** Map a normalised value to the display range (linear). */
    float toDisplay (float v01) const noexcept
    {
        return minDisplay + (maxDisplay - minDisplay) * v01;
    }
};

/** Stable identifiers for the ten CHAOS REALM modules.  The numeric order also
    defines their default position in the processing chain. */
enum class ModuleID : int
{
    SpectralMorphingHarmonizer = 0,
    PhysicalModelingChaosEngine,
    PsychoacousticManipulator,
    MicroTextureProcessor,
    NonLinearSpaceCreator,
    BiologicalEmulator,
    ElectromagneticFieldSimulator,
    TemporalDisintegrationEngine,
    QuantumModulationProcessor,
    SymbolicManipulator,
    NumModules
};

inline constexpr int kNumModules = static_cast<int> (ModuleID::NumModules);

} // namespace chaos
