# Controls

42 parameters: 5 for the whole box (and Auto Gain and the delay's Sync, appended last so older sessions keep their IDs), then six for each block (On, Flavor, three knobs and Mix; Width has no third knob, so five). Everything automates, everything except the switches and pickers accepts CLAP modulation, and everything can be typed into its box in the units it shows (`1.5k`, `250 ms`, `8 bit`, `3 s`, `-6 dB`, `fuzz`). In Bitwig there are seven remote-control pages: one per block and one for **Master**.

## The chain

Input → **Filter → Drive** → Modulation → Delay → Reverb → Width → Mix → Output. The **Filter position** picker (`FILTER → DRIVE` or `DRIVE → FILTER`) swaps the first two: a filter after the drive tames the fizz the drive makes (measured: 0.023 of the energy above 6 kHz when it comes first, 0.0001 after).

- **Input** −24…+12 dB, into the chain. The dry signal that Mix blends back in is not touched.
- **Mix** 0…100%: the whole chain against the dry. 0% is the dry, bit for bit.
- **Output** −24…+12 dB, the final level.
- **Auto Gain** matches the loudness of the effected signal to the dry's, so wet and dry compare at the same level (switch Bypass or sweep Mix and the volume stays put). It sits after the last block and before Mix and Output, so Mix still blends the matched signal against the dry and Output is still the final level. It measures the power of both channels over 0.4 s and glides to the gain that equalizes them over 150 ms, so it follows the level of the material rather than the waveform, and adapts in about half a second when you change a setting. It only learns while the dry is audible: when the input stops the last gain is held, so a reverb or delay tail is not pumped up or ducked as the input fades. The gain is limited to ±24 dB. Measured on noise, it brought a 300 Hz lowpass (−17.6 dB without) to −0.03 dB, a 90% overdrive (−6.1 dB) to 0.00 dB, and everything-on (−13.0 dB) to +0.05 dB. With every block off it changes nothing, bit for bit.
- **Bypass** takes the dry around everything (and Output with it), crossfaded over about 20 ms.

With every block off, Mix at 100% and the gains at 0 dB the signal passes untouched, bit for bit.

## What every block has

- **On.** Fades the block in or out over about 12 ms. A block that is off costs nothing and has no tail.
- **Flavor.** Three choices. Changing it while sound is passing ducks the block over a few milliseconds, switches, and fades back, so there is no click (the filter morphs between its three outputs instead, since it has all of them at once).
- **Three knobs**, relabeled by the flavor. Their values are stored the same whatever the flavor and shown in the flavor's own units.
- **Mix.** For Filter, Drive, Modulation and Width it crossfades the effect against the signal going into the block. For **Delay and Reverb** the dry stays at full level and Mix is how loud the effect is on top of it.

## Filter

A state-variable filter that stays stable under fast sweeps. One cutoff and resonance, three outputs.

| Flavor | What it does | Cutoff reads as |
| --- | --- | --- |
| **Lowpass** | Keeps what is below the cutoff. | Cutoff |
| **Highpass** | Removes what is below it. | Cutoff |
| **Bandpass** | Keeps a band; unity gain at the center, narrowing with resonance. | Center |

- **Cutoff** 20 Hz–20 kHz (logarithmic). **Resonance** 0–100%: Q from 0.71 to 8, a +18 dB peak at the cutoff at 100% on the 12 dB slope. **Slope** 12 or 24 dB per octave (the picker where the third knob would be). Measured with resonance 0 on 1 kHz: lowpass at 10 kHz −42.7 dB (12) and −85.5 dB (24); the cutoff itself is −3.0 dB.

## Drive

| Flavor | Character | Drive | Tone | Level |
| --- | --- | --- | --- | --- |
| **Overdrive** | Warm soft clipping, a little lopsided (so it makes even harmonics as well as odd), with a lift above 700 Hz into the clipper, which keeps the bass from flubbing as a screamer pedal does. | 0–46 dB into the clipper | low-pass, 700 Hz–16 kHz | −24…+12 dB |
| **Fuzz** | A hard clip that stops at +1 above and −0.8 below, with a high-pass at 90 Hz in front: fat, square-edged, uneven. | 20–65 dB | the same low-pass | −24…+12 dB |
| **Crush** | Bit-depth and sample-rate reduction. Not oversampled: the aliasing is the sound. | **Bits** 16 down to 2 | **Rate** full down to 1/32 | −24…+12 dB |

Overdrive and Fuzz are loudness-compensated against the amount of drive: the output level stays within ±0.3 dB of the input across the whole Drive range for a −18 dBFS sine. Both run at twice the sample rate with first-order antiderivative anti-aliasing, so what they make above the audio band does not fold back as inharmonic tones. At full drive on a pure tone, the strongest inharmonic component measures −60 dBc at 1 kHz, −50 dBc at 2.5 kHz and −45 dBc at 5 kHz (a hard-driven pure tone is the worst case there is). DC from the lopsided clipping is removed.

## Modulation

| Flavor | Character |
| --- | --- |
| **Chorus** | Two taps per ear around 10 and 15 ms, half a cycle apart, up to ±6 ms. The right ear runs a quarter of a cycle behind the left. |
| **Flanger** | One short tap sweeping exponentially around 1.5 ms (0.2 to 10 ms at full depth), with feedback. |
| **Phaser** | Six first-order allpass stages swept together, ±2 octaves around 1 kHz at full depth, with feedback. |

**Rate** 0.05–10 Hz (logarithmic). **Depth** 0–100%: how far the sweep goes (0 is a static comb or notch). **Feedback** 0–95% on the knob, scaled to suit the flavor: the loop gain tops out at 38% in the chorus, 84% in the flanger and 76% in the phaser, so every setting stays stable.

## Delay

| Flavor | Character |
| --- | --- |
| **Digital** | Each side repeats itself: clean, even. |
| **Ping-Pong** | The input goes into the left line; each line feeds the other, so echoes alternate, left first. |
| **Tape** | The time drifts (a slow wow and a fast flutter, a little different in each ear), and the loop is high-passed at 120 Hz and saturates, so each repeat is darker, looser and rounder. |

**Time** 20 ms–1 s (logarithmic; changing it glides over 50 ms, with the pitch glide an old echo has), or **Sync**. **Feedback** 0–90%. **Tone** is the cutoff of the low-pass in the feedback, so repeats lose their top as they fade: 400 Hz–12 kHz in Digital and Ping-Pong, 300 Hz–6 kHz in Tape.

### Tempo sync

The **Sync** menu beside the caption is *Free* (the Time knob rules) or a note value that follows the host's tempo: 1/32, 1/16, 1/8, 1/4, 1/2 and 1/1 (a bar of four beats), each with a triplet (T, a third shorter) and, from 1/16 to 1/4, a dotted version (half again as long), 12 values in all. While one is chosen the Time knob is dimmed, relabeled `TIME · 1/8.` and shows the time that comes to (`375 ms`, `1.00 s`); it is not in charge until Sync goes back to Free.

- The tempo comes from the host's transport, whether or not it is playing. Until the host has said, the delay assumes 120 BPM, and the first tempo it hears is taken at once rather than glided to.
- A tempo that changes later (or automation of the tempo) glides the delay time over about 50 ms, so a ramp is a smooth pitch bend of the repeats, as it is with the Time knob.
- The time is held between 20 ms and 4 s: a bar at 30 BPM is 8 s, so it plays as 4 s. The delay line holds 4 s.
- It works with all three delay flavors, and the reported tail follows the synced time. Measured: 1/8 at 120 BPM is an echo at 250.00 ms, a dotted quarter at 90 BPM at 1000.00 ms, and a triplet sixteenth at 140 BPM at 71.44 ms (71.43 expected), to the sample.

## Reverb

An eight-line feedback delay network with a Hadamard mix (the loop only loses what the gains take), modulated lines and damping. The flavors are three different spaces:

| Flavor | Lines | Diffusers | Tail brightness | Decay (Size 0–100%) |
| --- | --- | --- | --- | --- |
| **Room** | short (0.55×) | 2 | up to 14 kHz, damps to 3 kHz | 0.15–1.6 s |
| **Hall** | long (1.35×) | 3 | up to 11 kHz, damps to 1.2 kHz | 1–12 s |
| **Plate** | medium (0.85×) | 4, denser | up to 16 kHz, damps to 5.5 kHz | 0.4–5 s |

**Decay** is shown in seconds and is the RT60: measured around 1 kHz it lands within 10% of the number on the knob (room 0.51 s against 0.49 at 50%, hall 3.22 against 3.46, plate 1.44 against 1.41). **Damping** is how quickly the tail's high end dies. **Pre-delay** 0–100 ms. The two ears hear uncorrelated tails (left-right correlation −0.03).

## Width

| Flavor | Character | First knob |
| --- | --- | --- |
| **Stereo** | Mid/side: 0% is mono, 100% leaves the image alone, 200% doubles the side. The mono sum never changes. | **Width** 0–200% |
| **Haas** | Delays the right ear for a wide, offset feel. Check it in mono: it makes combs. | **Time** 0–30 ms |
| **Spread** | Four allpass sections per ear, different in each, smear the phase so a centered source fans out. Left-right correlation of a mono source 0.01 at 100%, at the same power. | **Amount** 0–100% |

**Mono Below** (all flavors) 0 (off), 20–400 Hz: what is below it is kept in the middle, by high-passing the side signal (a pure 60 Hz side signal is down 21 dB with 200 Hz set; 2 kHz is untouched). Width has no third knob.

## Starting points

- **Amp in a box:** Drive Overdrive 45%, Filter lowpass at 5 kHz placed *after* the drive, Reverb Plate at 20% mix.
- **Telephone:** Filter bandpass at 1.2 kHz, resonance 50%, then a touch of Overdrive.
- **Dub echo:** Delay Tape at 375 ms, feedback 60%, tone 40%, with a Filter highpass at 250 Hz in front.
- **Wide pad:** Modulation Chorus at 0.5 Hz, Reverb Hall at 6 s, Width Stereo at 130% with Mono Below 120 Hz.
- **Lo-fi loop:** Drive Crush at 8 bits and 1/4 rate, Filter lowpass at 6 kHz after it, Delay Digital at 25% mix.

`build/render_demo dir/` renders every flavor over a short phrase to WAV.
