# CHAOS REALM — Developer Guide

How to extend CHAOS REALM — primarily, how to write a new processing module.

## Prerequisites

Only a C++17 compiler is needed to develop and test modules; JUCE is *not*
required for the DSP layer.

```bash
# Build & run the DSP suites (fast, no JUCE)
g++ -std=c++17 -O2 tests/dsp_core_tests.cpp -o /tmp/core && /tmp/core
g++ -std=c++17 -O2 tests/module_stability_tests.cpp Source/dsp/ChaosEngine.cpp \
    Source/dsp/modules/*.cpp -ISource -o /tmp/stab && /tmp/stab
```

## Anatomy of a module

A module is a `chaos::ModuleBase` subclass. The reference implementation is
[`Source/dsp/modules/MicroTextureProcessor.{h,cpp}`](../Source/dsp/modules/MicroTextureProcessor.h)
— copy its shape.

```cpp
// MyModule.h
#pragma once
#include "../ChaosMath.h"
#include "../ModuleBase.h"

namespace chaos {
class MyModule : public ModuleBase {
public:
    MyModule();
    const char* getName() const override { return "My Module"; }
    ModuleID    getID()   const override { return ModuleID::MyModule; }
    void process (float* const* buffers, int numChannels, int numSamples) override;
protected:
    void onPrepare (const ProcessContext& ctx) override;
    void onReset() override;
private:
    int pDrive = 0, pTone = 0;
    std::vector<Biquad> tone;
};
} // namespace chaos
```

```cpp
// MyModule.cpp
#include "MyModule.h"
namespace chaos {

MyModule::MyModule() {
    // id, display name, default (0..1), unit, minDisplay, maxDisplay
    pDrive = addParameter ({ "drive", "Drive", 0.3f, "%",  0.f, 100.f });
    pTone  = addParameter ({ "tone",  "Tone",  0.5f, "Hz", 200.f, 12000.f });
}

void MyModule::onPrepare (const ProcessContext& ctx) {
    tone.assign ((size_t) ctx.numChannels, {});   // allocate here
    onReset();
}

void MyModule::onReset() { for (auto& b : tone) b.reset(); }

void MyModule::process (float* const* buffers, int numCh, int numSamples) {
    for (int n = 0; n < numSamples; ++n) {
        const float drive = smoothed (pDrive);                 // once per sample
        const float toneHz = getParameterInfo (pTone).toDisplay (smoothed (pTone));
        for (int c = 0; c < numCh; ++c) {
            tone[(size_t) c].setCoefficients (Biquad::Type::LowPass, toneHz, context.sampleRate);
            float x = fastTanh (buffers[c][n] * (1.f + drive * 8.f));
            buffers[c][n] = sanitise (tone[(size_t) c].process (x));
        }
    }
}
} // namespace chaos
```

## The rules (enforced by tests & review)

1. **No framework includes.** The `.h` includes only `../ChaosMath.h`,
   `../ModuleBase.h` (+ `<array>`/`<vector>` etc.); the `.cpp` includes only its
   own header.
2. **Parameters are normalised 0..1.** Declare them in the constructor with
   `addParameter`. Map to real units inside `process` via
   `getParameterInfo(i).toDisplay(v)`. For a discrete/stepped param, set
   `info.isStepped = true; info.numSteps = N;` before adding and read with
   `raw(i)`.
3. **Process fully wet, in place.** Never keep a dry copy — the engine handles
   dry/wet and bypass.
4. **Real-time safe.** Allocate only in `onPrepare`. `process`/`onReset` must not
   allocate, lock, or throw.
5. **Obey the stability contract:**
   - *Finite* — end every write with `sanitise(x)`.
   - *Bounded* — feedback gain < ~0.95, a `fastTanh`/`softClip` inside every
     feedback loop, filter cutoffs clamped to `[20 Hz, 0.49·sr]`.
   - *Settles to silence* — silence in ⇒ silence out within seconds. Any internal
     generator (oscillator/noise/hum) must be scaled by the input envelope.

## Registering the module

1. Add an entry to `ModuleID` in `Source/dsp/AudioTypes.h` (before `NumModules`).
2. Add the header to `Source/dsp/modules/AllModules.h`.
3. Add a `case` to `createModule()` in `Source/dsp/ChaosEngine.cpp`.
4. The CMake globs pick up `Source/dsp/modules/*.cpp` automatically.
5. The plugin's APVTS, editor panel, and preset factory all enumerate modules
   dynamically — no further wiring needed.
6. Run the stability suite: it automatically tests every module returned by
   `createModule`.

## Using the shared primitives

See [`Architecture.md`](Architecture.md) for the full `ChaosMath.h` catalogue:
`FFT`, `STFT`, `OnePole`, `Biquad`, `StateVariableFilter`, `DcBlocker`,
`DelayLine`, `AllpassDiffuser`, `CombFilter`, `Lorenz`, `Rossler`,
`LogisticMap`, `Xorshift`, `PinkNoise`, `OnePoleSmoother`, plus scalar helpers
(`sanitise`, `clampf`, `lerp`, `dbToGain`, `midiToFreq`, `fastTanh`, `softClip`,
`equalPower`).

## Testing your module

The stability suite (`tests/module_stability_tests.cpp`) runs a battery over
every module automatically. Add algorithm-specific correctness checks to
`tests/` as needed — see `dsp_core_tests.cpp` for the tiny assertion helpers in
`TestFramework.h`.

## Presets

`Source/dsp/PresetFactory.h` generates the factory bank by enumerating module
parameters, so new modules are covered automatically. Regenerate with:

```bash
g++ -std=c++17 -O2 tools/generate_presets.cpp Source/dsp/ChaosEngine.cpp \
    Source/dsp/modules/*.cpp -I. -o genpresets && ./genpresets 512 presets/FactoryPresets.xml
```
