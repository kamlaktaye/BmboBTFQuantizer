# Bmbo BTF Quantizer (v2.0.0)

A polyphonic VCV Rack quantizer that loads **BTF** (Bmbo Tuning Format) files
and quantizes incoming 1V/oct CV to the loaded tuning.

## Building

This is a standard VCV Rack 2 plugin. You need the Rack SDK:

```bash
# From the Rack-SDK root, or with RACK_DIR pointing at it:
export RACK_DIR=/path/to/Rack-SDK
cd BmboBTFQuantizer
make
make install   # or: make dist, then unzip into Rack's plugins folder
```

Standard layout assumption (used by the default `RACK_DIR ?= ../..` in the
Makefile): this folder lives at `Rack-SDK/plugins/BmboBTFQuantizer`. If it
lives elsewhere, just export `RACK_DIR` explicitly as shown above.

## Architecture

```
plugin.hpp / plugin.cpp   Plugin registration (Rack boilerplate)

src/Interval.hpp           A single scale degree: ratio + cents, DSP-only
src/Scale.hpp               Parsed tuning: title, root Hz, structure, intervals
src/BTFParser.hpp/.cpp      Text -> Scale. No file I/O, no UI. Unit-testable.
src/Quantizer.hpp/.cpp      Scale -> nearest-neighbor voltage quantizer (DSP)
src/BmboBTFQuantizer.cpp    Module (DSP + file I/O) and Widget (UI) together
```

DSP code (`Interval`, `Scale`, `BTFParser`, `Quantizer`) has **zero**
dependency on VCV Rack or any GUI headers. The `Module` in
`BmboBTFQuantizer.cpp` is the only place that touches file I/O, and the
`BTFLoadButton` widget is the *only* place that touches `osdialog` (the
native file-picker library). This separation is what makes "open scale
support later" or "Scala import later" additive changes rather than
rewrites: they plug into `BTFParser`/`Scale`/`Quantizer` without touching
the Module or Widget structure.

### Signal flow (per polyphonic channel, per sample)

1. Compute `effectiveRoot` = (root Hz converted to 1V/oct, relative to C4)
   + Root CV input (if patched) + Root Transpose knob.
2. `relative = input - effectiveRoot`
3. `Quantizer::quantize(relative)` snaps the fractional-octave part to the
   nearest interval in the loaded scale (octave-repeating, per v0.1 scope),
   preserving whichever octave the input voltage was in.
4. `output = quantized + effectiveRoot + octaveShift`

If no tuning is loaded, the Quantizer passes CV through unchanged rather
than producing silence or garbage.

## BTF format (v2.0.0)

```
BTF 1.0

Title: Example
Root: 440
Structure: Closed

Intervals:
1/1
16/15
9/8
6/5
5/4
4/3
3/2
8/5
5/3
7/4
```

- Blank lines and lines starting with `#` or `;` are ignored anywhere.
- The first meaningful line must be a `BTF <version>` header.
- `Title:`, `Root:` (Hz), and `Structure:` (`Open` or `Closed`) are optional
  metadata lines, in any order, before `Intervals:`.
- Everything after `Intervals:` is one interval per line:
  - `num/den` or a bare integer → a ratio (e.g. `3/2`, `2`)
  - a number with a decimal point → cents (e.g. `701.955`)
- At least one interval is required.
- v0.1 only implements **Closed** (octave-repeating) quantization. `Open`
  scales are parsed and stored but quantized as if repeating at the octave;
  true non-repeating support is planned for a later version.

## Out of scope (v0.1)

No synth voices, oscillators, MIDI, sequencers, preset browser, internet
features, or extra DSP effects. This module only quantizes CV to a loaded
tuning.

## Planned for later versions

- True Open (non-repeating) scale quantization
- Morphing between two BTF files
- Scala (`.scl`) import, KBM support
- Alternate tuning displays
- Microtonal keyboard visualization
