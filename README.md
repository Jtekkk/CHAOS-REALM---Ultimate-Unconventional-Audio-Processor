# CHAOS REALM — Ultimate Unconventional Audio Processor

> A modular VST3/AU/Standalone plugin that unifies **ten** unconventional
> sound-manipulation engines into a single, freely-routable chaos machine.
> Built with JUCE on top of a **framework-independent, unit-tested DSP core**.

![status](https://img.shields.io/badge/DSP%20core-174%20checks%20passing-brightgreen)
![modules](https://img.shields.io/badge/modules-10-blueviolet)
![presets](https://img.shields.io/badge/factory%20presets-512-ff3d81)
![c++](https://img.shields.io/badge/C%2B%2B-17-00e5c8)

---

## What it is

CHAOS REALM takes the ten "outer boundary" audio concepts from the original
project vision and implements each as a self-contained DSP **module**.  All ten
share one interface and can be enabled, re-ordered, and dry/wet-blended in any
combination, then modulated by a global LFO / envelope / macro matrix.

| # | Module | What it does |
|---|--------|--------------|
| 1 | **Spectral Morphing Harmonizer** | STFT spectral shift, harmonic brush, ghost harmonics, spectral blur & formant warp |
| 2 | **Physical Modeling Chaos Engine** | Non-linear waveguide resonator driven by Lorenz / Rössler / logistic-map chaos |
| 3 | **Psychoacoustic Manipulator** | Binaural beats, Shepard/Risset tones, Haas width, micro-timing dissonance (all input-gated) |
| 4 | **Micro-Texture Processor** | Granular cloud, sample-rate reduction, noise-shaped bit-crush, temporal smear |
| 5 | **Non-Linear Space Creator** | Modulated 8-line Hadamard FDN reverb with fractal diffusion & probabilistic decay |
| 6 | **Biological Emulator** | Morphable vocal-tract formants, integrate-and-fire neural gate, evolving timbre |
| 7 | **Electromagnetic Field Simulator** | Magnetic hysteresis, RF hiss, induction crosstalk, plasma resonance, mains hum |
| 8 | **Temporal Disintegration Engine** | Overlapping time-folds, reversed look-ahead, tape decay, wow/flutter, timing smear |
| 9 | **Quantum Modulation Processor** | Superposition blend, band entanglement, probabilistic tunneling, observer effect |
| 10 | **Symbolic Manipulator** | Numerology/sacred-geometry resonator tunings, I-Ching pattern sequencer, alchemical elements |

### Highlights

- **Modular chain** — any subset of modules, each with independent dry/wet & bypass, **drag-to-reorder** (order saved with the project).
- **A/B compare & Randomize** — two full-state snapshots and a constrained, always-safe randomizer.
- **Latency-compensated** — modules that buffer (the spectral STFT) are dry-aligned automatically; the plugin reports correct PDC to the host.
- **Variable oversampling** — 1× to 16× via JUCE polyphase IIR.
- **Modulation matrix** — 4 LFOs (6 shapes incl. chaotic S&H), an envelope follower, 4 macros → any module parameter.
- **512 factory presets** across 14 categories, generated deterministically and embedded in the plugin.
- **Real-time safe** — no allocation / locks / exceptions on the audio thread after `prepare()`; denormals scrubbed throughout.

---

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│  JUCE layer  (Source/PluginProcessor, PluginEditor, Preset…)  │  <- host / UI
│    • APVTS parameter bridge   • oversampling   • preset bank   │
└───────────────────────────────┬──────────────────────────────┘
                                │  (thin, the only framework code)
┌───────────────────────────────▼──────────────────────────────┐
│  DSP core  (Source/dsp/*)   —  ZERO framework dependency       │
│                                                                │
│   ChaosEngine  ── routing, per-module dry/wet, PDC, modulation │
│      ├── ModuleBase  (interface every module implements)       │
│      ├── 10 × modules/*  (the processors above)                │
│      ├── Modulation  (LFOs / envelope / macros / matrix)       │
│      └── ChaosMath   (FFT/STFT, filters, delays, chaos, RNG)   │
└────────────────────────────────────────────────────────────────┘
```

Because the DSP layer has **no JUCE dependency**, it compiles and runs under any
C++17 compiler and is fully unit-tested on its own (see `tests/`).  The JUCE
layer is a thin bridge.  See [`docs/Architecture.md`](docs/Architecture.md).

---

## Building

### DSP tests (no JUCE, instant)

```bash
cmake -B build -DCHAOS_BUILD_PLUGIN=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

or directly with a compiler:

```bash
g++ -std=c++17 -O2 tests/dsp_core_tests.cpp -o core_tests && ./core_tests
g++ -std=c++17 -O2 tests/module_stability_tests.cpp Source/dsp/ChaosEngine.cpp \
    Source/dsp/modules/*.cpp -ISource -o stab && ./stab
```

### The full plugin (JUCE, VST3/AU/Standalone)

JUCE is fetched automatically by CMake. You need a C++17 toolchain, CMake ≥ 3.22,
and the usual JUCE platform dependencies:

- **Linux:** `libasound2-dev libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev libfreetype6-dev libglu1-mesa-dev`
- **macOS:** Xcode command-line tools
- **Windows:** Visual Studio 2022

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Artifacts appear under `build/CHAOSRealm_artefacts/`.

> The DSP core is validated in this repo's CI on every push. The plugin binary
> is built on a machine with the platform GUI/audio SDK dependencies above.

---

## Regenerating the preset bank

```bash
g++ -std=c++17 -O2 tools/generate_presets.cpp Source/dsp/ChaosEngine.cpp \
    Source/dsp/modules/*.cpp -I. -o genpresets
./genpresets 512 presets/FactoryPresets.xml
```

The bank is deterministic (fixed seed) so it stays reproducible and in sync
with the current parameter set.

---

## Testing

| Suite | What it guarantees |
|-------|--------------------|
| `chaos_core_tests` | FFT round-trip, STFT reconstruction, filter stability, chaos boundedness, delay accuracy, RNG statistics (48 checks) |
| `chaos_stability_tests` | Every module + the full engine is FINITE, BOUNDED, and SETTLES TO SILENCE under noise / DC / extreme & random parameters (104 checks) |

Both run in CI (`.github/workflows/ci.yml`).

---

## Documentation

- [`docs/Architecture.md`](docs/Architecture.md) — how the core, engine, and modules fit together.
- [`docs/UserManual.md`](docs/UserManual.md) — every parameter of every module, and how to use them.
- [`docs/DeveloperGuide.md`](docs/DeveloperGuide.md) — how to write your own module against `ModuleBase`.
- [`docs/Performance.md`](docs/Performance.md) — measured CPU / memory / latency vs. the stated targets.
- [`README_ORIGINAL_VISION.md`](README_ORIGINAL_VISION.md) — the original design brief this project realises.

---

## License / status

Reference implementation of the CHAOS REALM concept. The DSP algorithms are
original and framework-free; JUCE is fetched under its own license for the
plugin build.
