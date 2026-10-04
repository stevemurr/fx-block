# Design

## Signal flow

```
in ─ Input ─┬─ Filter ─ Drive ─ Modulation ─ Delay ─ Reverb ─ Width ─┬─ (Auto Gain) ─ Mix ─ Output ─ Bypass ─ out
            │      (or Drive ─ Filter)                              │
            └──────────────── dry ──────────────────────────────────┘
```

Stereo in, stereo out, no latency, no MIDI. All state is the 42 parameters; there is no hidden state to save.

## One block, three flavors

Every block is an `Fx` class (`Blocks.h`) with the same small interface, wrapped in a `Stage<Fx>` that supplies everything the six share:

- **On/off** as a crossfade, and a stage that is off passes the signal and keeps its effect cleared and idle, so an unused block costs nothing and leaves no tail.
- **Flavor changes** as a duck: the wet signal fades out over a few milliseconds, the effect switches flavor (clearing its state), and the wet fades back. Reverb and delay lines are different lengths per flavor, and the drive swaps its clipper, so a crossfade between the two would need both running; a duck is simple and inaudible. The filter is the exception (`ducks = false`): its lowpass, bandpass and highpass come from the same state, so it morphs between three weights over 10 ms with no discontinuity at all.
- **Mix.** Effects that return the whole signal (filter, drive, modulation, width) are crossfaded against their input; `additive` effects (delay, reverb) return only what they add, and the stage puts it on top of the dry at full level.

A flavor is data plus a small amount of code in one class. The three knobs are stored as the same three numbers for every flavor of a block, and `Parameters.h` says what each flavor makes of them (`flavorInfo`, and the real-unit mappings such as `reverbDecay`, `crushBits`, `haasMs`). The DSP, the host's value text, the editor's value text and the text parser all call the same functions, so a number on screen is the number heard, and a typed value reads back exactly.

## Parameters

IDs are persistent and laid out as 5 globals, then 6 per block (`blockParam(block, key)`), then Auto Gain and the delay's Sync (appended last, which is why they are not with the globals and the Delay block), so appending is safe and reordering is not. The Width block has no third knob; it keeps the slot so the arithmetic stays uniform and is simply not offered to the host (`exposedId`). State is `"FXBK"`, a version, a count and `(id, f64)` pairs: parameters missing from a session take their defaults and ids from a newer version are skipped, so adding parameters never breaks old sessions. Values are clamped on load; NaN is refused.

## Where the quality comes from

- **Oversampling without latency.** The distortion runs at 2× through a half-band built from two branches of allpass sections (3 + 2, `tools/gen_halfband.py`): the elliptic half-band whose poles sit on the imaginary axis gives the coefficients directly. It is an IIR, so it has no lookahead and the plugin reports no latency; measured, it is 80 dB down above 0.58 of the base rate and flat to 19.8 kHz at 48 kHz.
- **Anti-aliasing beyond oversampling.** A hard clip makes harmonics that fall off by only 6 dB an octave, more than 2× can stop (full drive on a 5 kHz tone folded back at −25 dBc with oversampling alone). Both clippers use first-order antiderivative anti-aliasing: instead of the curve at a sample, the average of the curve between this sample and the last, from the curve's closed-form integral (`ln cosh` for the tanh; a piecewise quadratic for the hard clip). That took the same tone to −45 dBc, and −60 dBc at 1 kHz.
- **Loudness that stays put.** Overdrive and fuzz are compensated by a table built when the plugin is prepared, from the rms of one cycle through the actual clipper at each gain, not a formula guessed for it. The level then holds within ±0.3 dB across the Drive range.
- **Reverb.** The decay on the knob is calibrated against the decay measured on the impulse response (a Schroeder backward integral of a band around 1 kHz): the lines' gains are set for 7% longer than named, which puts all three flavors within about 7%. A first version had measured 60% of the named time; that was the dry spike in the measurement, which is why the tests zero it.
- **Smoothing.** Every continuous parameter is smoothed per sample (20 ms for most, 50 ms for delay time, 8 ms for the filter cutoff in the log domain). The filter's coefficients are recomputed only while something is moving.
- **Exact ends.** The crossfades are written `(1-k)·a + k·b`, so Mix 0% and Bypass are the dry signal bit for bit, and a box with everything off is transparent bit for bit.

## Tests

Four suites, all in `ctest`, all run clean under AddressSanitizer and UBSan:

- **Parameters** — layout and ranges, the host-index mapping, sanitising, and that every parameter's text parses back to its value in every flavor (choices exactly), including what is refused.
- **Blocks** — filter slopes and resonance, the oversampler's passband and rejection, drive level and harmonics and aliasing at three pitches, crush quantisation and hold, modulation that moves and stays stable at full feedback, delay timing and darkening, reverb decay and decorrelation and pre-delay, width, and that toggling every block and switching every flavor under a sine never steps further than a click would.
- **Chain** — transparency, stability at 22.05–192 kHz with harsh settings, gains/mix/bypass, filter order, reported tails, reset determinism, and speed (best of ten passes, enforced only in optimised builds).
- **Plugin** — the CLAP interface through the real entry point: ports, parameters and flavor-aware text, state round-trips and version tolerance, truncated, failing and corrupted streams, remote pages, event timing, modulation, tails and the editor opening and closing.

`editor_snapshot --selftest` drives the editor with synthetic mouse events: switches, flavor pickers (and that they relabel the knobs), drag, double-click reset, typing, refusal of nonsense, balanced gestures. `clap-validator` passes.

## Not done, and not tested

Not auditioned in a DAW by anyone yet: the tests and the validator say the plugin is correct to the spec and the measurements, not that it sounds the way you want. Also not here:

- **Other tempo-synced things.** Only the delay follows the host's tempo; the modulation rate is in Hz.
- **Reorderable chain.** The order is fixed apart from the filter position.
- **A fourth flavor, presets, or a bypass per block that remembers its tail.** A block that is switched off is cut, not allowed to ring out.
- **Linux and Windows editors.** The DSP and plugin build anywhere; the editor is Cocoa. Without it the host's generic parameter UI works, and the remote-control pages are laid out for it.
- **Higher oversampling.** 2× with anti-aliasing is good; 4× would add a few dB more at the highest drive on high tones, at twice the drive's cost.

## Adding to it

A new flavor: add its name and caption in `Parameters.h`, handle it in the block's `process`, and add a test. A new block: add an `Fx` class and a `Stage<>` in `Chain`, extend the `Block` enum *at the end*, and its six parameters are appended automatically.
