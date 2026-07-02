# CHAOS REALM — User Manual

CHAOS REALM is an insert effect. Drop it on any track or bus, enable one or more
of the ten modules, and dial in the chaos. Every module is safe to insert at any
point in the chain — at its default settings it is stable and musical.

---

## The interface

```
┌───────────────────────────────────────────────────────────────────────┐
│ CHAOS REALM        [ < ] [ Preset browser ▾ ] [ > ]   In  Out  Master OS│  header
├───────────────────────────────────────────────────────────────────────┤
│ ░░░░░░░░░░░░░  live spectrum analyzer  ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │
├───────────────────────────────────────────────────────────────────────┤
│ ▌[on] Module 1 ……… Mix  ● ● ● ● ● ● ●   (rotary knobs, one per param)  │
│ ▌[on] Module 2 ……… Mix  ● ● ● ● ● ● ●                                  │  scrollable
│  …                                                                      │  module stack
└───────────────────────────────────────────────────────────────────────┘
```

- **Preset browser** — 512 factory presets in 14 categories. `<` / `>` step through them.
- **In / Out** — input & output trim (±24 dB).
- **Master** — global dry/wet.
- **OS** — oversampling: 1×, 2×, 4×, 8×, 16× (higher = cleaner non-linearities, more CPU & latency).
- Each **module panel** has an on/off switch, a **Mix** (per-module dry/wet), and its parameters as rotary knobs. Modules process **top to bottom** in the order shown.

---

## Global signal flow

```
Input → [In gain] → Module 1 → Module 2 → … → Module 10 → [Master dry/wet] → [Out gain] → Output
                     each with its own on/off + dry/wet blend
```

Modules that introduce latency (the Spectral Morphing Harmonizer) are
automatically delay-aligned, and the plugin reports the correct latency to your
host for plugin-delay compensation.

---

## The ten modules

### 1 · Spectral Morphing Harmonizer
FFT/STFT frequency-domain processing.

| Param | Range | What it does |
|-------|-------|--------------|
| FFT Size | 2048 / 4096 / 8192 | Frequency resolution (set before playback; sets latency) |
| Spectral Shift | −100…100 % | Shifts the magnitude spectrum up/down (0 = none) |
| Harmonic Brush | 0…100 % | Emphasises harmonic (integer-multiple) partials |
| Ghost Interval | −24…+24 st | Interval of the ghost-harmonic copy |
| Ghost Amount | 0…100 % | Level of the transposed ghost spectrum |
| Spectral Blur | 0…100 % | Smears magnitudes across neighbouring bins |
| Formant Tilt | −100…100 % | Tilts/warps the spectral envelope |

### 2 · Physical Modeling Chaos Engine
Non-linear waveguide resonator driven by deterministic chaos.

| Param | Range | What it does |
|-------|-------|--------------|
| Chaos System | Lorenz / Rössler / Logistic | Which chaotic oscillator modulates the resonator |
| Chaos Rate | 0…100 % | Speed of the chaotic modulation |
| Chaos Depth | 0…100 % | How much chaos warps pitch & damping |
| Material | 0…100 % | Damping / brightness of the resonator body |
| Resonance | 0…100 % | Loop feedback (ring/sustain) |
| Drive | 0…100 % | Excitation into the non-linearity |
| Tone | 0…100 % | Post-resonator tone |

### 3 · Psychoacoustic Manipulator
Auditory illusions. *All generators are gated by input level — silence stays silent.*

| Param | Range | What it does |
|-------|-------|--------------|
| Binaural | 0…100 % | Binaural-beat blend |
| Carrier | 60…640 Hz | Binaural carrier frequency |
| Beat | 0…30 Hz | L/R beat frequency |
| Shepard | 0…100 % | Shepard/Risset endless-tone blend |
| Motion | 0…1.5 oct/s | Shepard glide speed |
| Width | 0…100 % | Haas inter-aural width |
| Dissonance | 0…100 % | Micro-timing jitter between ears |

### 4 · Micro-Texture Processor
Granular + lo-fi textural degradation.

| Param | Range | What it does |
|-------|-------|--------------|
| Grain Density | 0…100 | Grains per second in the cloud |
| Grain Size | 5…400 ms | Length of each grain |
| Scatter | 0…100 % | Random position & pitch spread |
| SR Reduce | 1…50× | Sample-rate reduction (aliasing) |
| Bit Depth | 2…16 bit | Noise-shaped bit-crush |
| Temporal Smear | 0…100 % | Allpass diffusion (pre-echo/blur) |
| Stereo Spread | 0…100 % | Grain panning width |

### 5 · Non-Linear Space Creator
Modulated feedback-delay-network reverb.

| Param | Range | What it does |
|-------|-------|--------------|
| Size | 0…100 % | Room / network size |
| Decay | 0.2…12 s | Tail length (RT60) |
| Damping | 0…100 % | High-frequency absorption |
| Modulation | 0…100 % | Delay-length modulation (lush tail) |
| Diffusion | 0…100 % | Fractal input diffusion |
| Quantum | 0…100 % | Probabilistic decay variation |
| Pre-Delay | 0…200 ms | Delay before the reverb |

### 6 · Biological Emulator
Vocal-tract & organism modelling.

| Param | Range | What it does |
|-------|-------|--------------|
| Species | Human / Feline / Avian / Insectoid / Cetacean | Formant set |
| Formant Shift | 0.5…2× | Scales all formant frequencies |
| Neural Rate | 0…100 % | Integrate-and-fire neuron excitability |
| Neural Depth | 0…100 % | Neural gate/tremolo depth |
| Resonance | 0…100 % | Cellular resonance Q |
| Evolution | 0…100 % | Slow timbral mutation |
| Growl | 0…100 % | Sub-harmonic / non-linearity |

### 7 · Electromagnetic Field Simulator
Electromagnetic artefacts. *Noise & hum generators are input-gated.*

| Param | Range | What it does |
|-------|-------|--------------|
| Flux | 0…100 % | Magnetic hysteresis saturation |
| RF Noise | 0…100 % | Broadband RF interference / crackle |
| Crosstalk | 0…100 % | Inter-channel induction bleed |
| Plasma Freq | 80…10240 Hz | Plasma resonance sweep frequency |
| Plasma Res | 0.7…20 Q | Plasma resonance sharpness |
| Ionize | 0…100 % | Ring-modulated ionised sheen |
| Mains Hum | 0…100 % | 50/60 Hz hum + harmonics |

### 8 · Temporal Disintegration Engine
Time-domain smearing & tape decay.

| Param | Range | What it does |
|-------|-------|--------------|
| Fold | 0…100 % | Overlapping time-region blend |
| Reverse | 0…100 % | Reversed segment playback |
| Decay | 0…100 % | Tape feedback decay |
| Flutter | 0…100 % | Wow & flutter depth |
| Smear | 0…100 % | Stochastic timing jitter |
| Age | 0…100 % | Tape tone / saturation |
| Feedback | 0…100 % | Global feedback amount |

### 9 · Quantum Modulation Processor
Probabilistic, quantum-metaphor modulation.

| Param | Range | What it does |
|-------|-------|--------------|
| Superposition | 0…100 % | Blend swing between two parallel states |
| Entanglement | 0…100 % | Cross-coupling of frequency bands |
| Tunneling | 0…100 % | Probabilistic band gating |
| Observer | 0…100 % | How strongly input analysis drives processing |
| Collapse Rate | 0.5…40 Hz | Rate of probabilistic state jumps |
| Stereo Spread | 0…100 % | L/R decorrelation |
| Coherence | 0…100 % | Smoothing of the randomness |

### 10 · Symbolic Manipulator
Esoteric / symbolic sound mapping.

| Param | Range | What it does |
|-------|-------|--------------|
| Numerology | 0…100 % | Morphs resonator tunings across number sets (Fibonacci → φ → primes) |
| Geometry | 0…100 % | Sacred-geometry comb tunings (φ, √2, √3, √5) |
| Divination | 16 seeds | I-Ching/tarot pattern-sequencer seed |
| Element | Earth→Water→Air→Fire | Alchemical processing crossfade |
| Ritual | 0.5…16 Hz | Pattern-sequencer rate |
| Intensity | 0…100 % | Wet character |
| Resonance | 0…100 % | Resonator Q / feedback |

---

## Modulation

CHAOS REALM has a global modulation matrix:

- **4 LFOs** — each with a rate (0.01–20 Hz) and a shape (Sine, Triangle, Saw, Square, Sample & Hold, Chaos).
- **1 Envelope follower** — tracks the plugin input level.
- **4 Macros** — assignable control knobs.
- **6 matrix slots** — each routes a **source** (an LFO, the envelope, or a macro) to any **destination** (any parameter of any module) with a signed **depth** (−100…+100 %).

Modulation is added on top of the parameter's set value each block, so automation
and modulation coexist.

---

## Presets

512 factory presets ship embedded in the plugin, grouped into 14 categories
(Spectral Morph, Chaos Engines, Mind Games, Micro Textures, Impossible Spaces,
Creatures, Electromagnetic, Time Smear, Quantum States, Occult, Hybrids, Rituals,
Total Chaos, and Init). Use the header browser or the `<` / `>` buttons. Your DAW
also saves/restores the full plugin state per project.

---

## Tips

- Start from **Init / Clean**, enable one module, and explore before stacking.
- **Order matters** — a reverb (Space) before vs. after a texture module sounds very different. (Chain order is the panel order.)
- Push **oversampling** to 4×+ when using heavy non-linear modules (Physical Modeling, Electromagnetic) to tame aliasing.
- Use **per-module Mix** for parallel-style blending instead of full wet.
- Assign an **LFO → module parameter** for evolving, generative textures.
