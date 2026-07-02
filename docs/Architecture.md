# CHAOS REALM — Architecture

## Design principle: a framework-free DSP core

The single most important decision in CHAOS REALM is that **all audio
processing lives in a pure-C++17 layer with zero dependency on JUCE** (or any
other framework). JUCE appears only in three files (`PluginProcessor`,
`PluginEditor`, `PresetManager`), which form a thin bridge to the host.

Benefits:

- The DSP compiles and **runs under any C++17 compiler**, so it is unit-tested
  in isolation — no plugin host, no GUI, no audio device required.
- Algorithms are portable and reusable outside a plugin context.
- The audio guarantees (finite, bounded, settles) are enforced by an automated
  suite that runs in seconds in CI.

```
Source/
├── PluginProcessor.{h,cpp}   ← JUCE: APVTS bridge, oversampling, PDC, scope
├── PluginEditor.{h,cpp}      ← JUCE: modular UI, spectrum analyzer
├── PresetManager.{h,cpp}     ← JUCE: parse + apply embedded preset bank
├── gui/ChaosLookAndFeel.h    ← JUCE: neon dark theme
└── dsp/                      ← ★ NO framework dependency ★
    ├── AudioTypes.h          ← ProcessContext, ParameterInfo, ModuleID
    ├── ChaosMath.h           ← FFT/STFT, filters, delays, chaos, RNG, smoothers
    ├── ModuleBase.h          ← the interface every module implements
    ├── Modulation.h          ← LFOs, envelope follower, macros, mod matrix
    ├── ChaosEngine.{h,cpp}   ← chain routing, per-module dry/wet, PDC, mod apply
    ├── PresetFactory.h       ← deterministic factory-preset generator
    └── modules/              ← the ten processors + AllModules.h aggregator
```

## Data flow per block

1. **Host → Processor.** `processBlock` receives the audio buffer.
2. **Parameter push.** Base parameter values are read from the APVTS (lock-free
   atomics) and pushed into each module via `setParameter` (normalised 0..1).
3. **Modulation config.** LFO rates/shapes, macros, and matrix routings are read
   from the APVTS into the `ModMatrix`.
4. **Oversampling up** (if enabled) via JUCE polyphase IIR.
5. **`ChaosEngine::process`:**
   - apply input gain, capture the master-dry copy;
   - advance modulation sources and add their offsets onto module parameters;
   - run each **enabled** module in routing order, each with latency-aligned
     per-module dry/wet;
   - apply master dry/wet and output gain.
6. **Oversampling down.**
7. **Scope publish** for the editor's spectrum analyzer (lock-free ring).

## The module contract (`ModuleBase`)

Every module:

- declares its parameters once in its constructor via `addParameter(...)`
  (values are always **normalised 0..1**; the module maps to real units itself);
- allocates in `onPrepare(ProcessContext)`, clears in `onReset()`;
- processes **fully wet, in place** in `process(float* const*, numCh, numSamples)`
  — dry/wet and bypass are the engine's job, never the module's;
- reads continuous parameters once per sample via `smoothed(i)` (built-in
  one-pole smoothing) and stepped ones via `raw(i)`;
- may report latency via `getLatencySamples()`.

The base class owns the parameter store and per-parameter smoothers, so modules
stay small and uniform.

## Latency & plugin-delay compensation

Only the Spectral Morphing Harmonizer buffers (its STFT frame). The engine holds
a per-module dry-alignment delay line sized to that module's reported latency,
so the module's own dry/wet stays phase-aligned, and the cumulative chain
latency is reported to the host. The mechanism is general — any future module
that reports latency is compensated automatically.

## Real-time safety

After `prepare()`, the audio path performs **no allocation, no locks, no
exceptions**. Denormals are scrubbed with `sanitise()` throughout, every
feedback loop contains a bounded non-linearity (`fastTanh`/`softClip`) with
feedback < ~0.95, and all filter cutoffs are clamped to `[20 Hz, 0.49·sr]`. The
stability suite proves finiteness, boundedness, and decay-to-silence for every
module and the full engine under noise, DC, and extreme/random parameters.

## Shared DSP primitives (`ChaosMath.h`)

- **FFT** — iterative radix-2 Cooley-Tukey (power-of-two), plus **RealFFT**, a
  real-input transform via a half-size complex FFT (used by the STFT to ~halve
  the Spectral module's cost).
- **STFT** — streaming WOLA with sqrt-Hann analysis/synthesis (COLA-correct at
  50 %/75 % overlap), one-frame latency, verified to reconstruct within 2 %.
- **Filters** — one-pole, RBJ biquad, TPT state-variable, DC blocker.
- **Delays** — power-of-two ring buffer with linear & cubic (Catmull-Rom) reads;
  Schroeder allpass diffuser; damped feedback comb.
- **Chaos** — Lorenz, Rössler, logistic map (all bounded/normalised).
- **Noise/RNG** — xorshift128 + pink filter.
- **Smoothing** — one-pole parameter smoother.

All are exercised by `tests/dsp_core_tests.cpp`.
