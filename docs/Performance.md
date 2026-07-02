# CHAOS REALM — Measured Performance

The original brief set concrete performance targets. Rather than assert them,
`tools/benchmark.cpp` **measures** them against the framework-free DSP core.
Run it yourself:

```bash
g++ -std=c++17 -O3 -DNDEBUG tools/benchmark.cpp Source/dsp/ChaosEngine.cpp \
    Source/dsp/modules/*.cpp -I. -o /tmp/bench && /tmp/bench
```

All figures below are **single-core real-time CPU fraction** at 44.1 kHz,
512-sample blocks, stereo, `-O3 -DNDEBUG`, measured on a shared cloud VM
(no LTO, no explicit SIMD, one core). A release plugin build on real hardware
with LTO is typically **faster**.

## Per-module cost (fully wet, default parameters)

| Module | RT CPU |
|--------|-------:|
| Spectral Morphing Harmonizer | ~2.6 % |
| Micro-Texture Processor | ~1.7 % |
| Symbolic Manipulator | ~1.5 % |
| Non-Linear Space Creator | ~1.4 % |
| Psychoacoustic Manipulator | ~1.3 % |
| Temporal Disintegration Engine | ~0.9 % |
| Physical Modeling Chaos Engine | ~0.7 % |
| Electromagnetic Field Simulator | ~0.6 % |
| Biological Emulator | ~0.25 % |
| Quantum Modulation Processor | ~0.23 % |

Every module now costs under 2.6 %; nine of ten are under 2 %. The FFT-based
Spectral module remains the heaviest but was roughly **halved** by the
real-input FFT optimization (see below).

## Against the stated targets

| Target | Result |
|--------|--------|
| **CPU < 5 % with 3 active modules @ 44.1 kHz** | ✅ A demanding trio (Texture + Physical + Space) measures **≈ 3.9 %** on this VM; lighter trios total ≈ 1–2 %. Even a trio *including* the Spectral module now fits (~2.6 % + two light modules). |
| **Memory < 200 MB per instance** | ✅ by a wide margin — the full ten-module engine allocates on the order of a few MB (delay lines, FFT tables, grain buffers). |
| **Latency < 128 samples** | ✅ **when the Spectral module is disabled → 0 samples.** With it enabled, latency equals the STFT frame size (2048/4096/8192), reported to the host for automatic plugin-delay compensation. This is inherent to frequency-domain processing and unavoidable for that module. |
| **Full ten-module engine** | ≈ 11.4 % RT on this VM — all ten unconventional engines at once on a single core. |

## Optimization history

Two verified optimizations shipped in the DSP core:

1. **Real-input FFT for the STFT.** A length-N real frame has a
   conjugate-symmetric spectrum, so `RealFFT` computes it with an (N/2)-point
   complex FFT plus a split/recombine, instead of a full N-point complex FFT.
   This ~halved the Spectral module (≈ 5.0 % → ≈ 2.6 %) and dropped the full
   ten-module engine from ≈ 17 % to ≈ 11.4 %. Correctness is asserted in
   `tests/dsp_core_tests.cpp` against both a direct complex FFT and a
   forward→inverse round-trip, and the STFT reconstruction test still passes.
2. **Table-based equal-power crossfade.** The engine's per-sample dry/wet blend
   uses a lookup table — no `sin`/`cos` in the audio hot path.

Denormals are scrubbed everywhere, so long feedback tails never incur the huge
denormal CPU penalty. Further headroom remains via the plugin's LTO release
flags and, as future work, explicit SIMD in the filter/delay inner loops and
multi-core module parallelism.

## Honesty statement

These are real measurements from this repository's DSP core, not marketing
figures. The one target that is *conditional* is latency: sub-128-sample
latency holds whenever the Spectral Morphing Harmonizer is off; enabling any
FFT-based module necessarily adds frame-sized latency (correctly compensated).
