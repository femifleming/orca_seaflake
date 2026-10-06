# Operator reference

Orca is a two-dimensional grid sequencer. Place an operator glyph in the grid and put its operands in the cells around it. Most operators read a value to their left and right and write below; the tables below give the exact positions for each operator. A `.` input uses the documented default when one exists.

## Values and triggering

- Numeric operands use base 36: `0`–`9` mean 0–9 and `a`–`z` or `A`–`Z` mean 10–35. Numeric results wrap through this range where stated by the operator.
- Uppercase letter operators run every clock frame. Lowercase letter operators run when triggered by a neighboring `*` bang. Special-operator behavior is listed individually below.
- A bang is a `*` in one of the four cells directly adjacent to an operator. It triggers the operator during that tick; it is not one of the operator's numeric operands.
- The clock runs at the selected BPM. Use the Clock menu or its shortcuts to play, pause, and adjust tempo. The `D` and `C` operators use this clock's frame count.
- MIDI channels are zero-based (`0`–`f`, with valid MIDI channels `0`–`f` in the app's base-36 notation, limited to 0–15). Note names are letters; lowercase note glyphs are sharp spellings. Set up an output device in **MIDI → Next Output Device**.

## Build 04 additions

### `^` — probability gate

| Position | Operand | Default | Meaning |
|---|---|---:|---|
| Right | chance | `i` (18) | Base-36 chance from 0 to 35 |
| Below | output bang | — | Emits `*` when a neighboring bang is present and the random draw succeeds |

Chance is `chance / 35`; therefore `0` never passes and `z` always passes. With the default `i`, a bang passes a little over half the time. Put the downstream operator next to the output bang cell.

```text
^i       probability gate at 18/35
*^z      guaranteed pass whenever the input bang reaches the gate
```

### `&` — phase clock

| Position | Operand | Default | Meaning |
|---|---|---:|---|
| Left | rate | `1` | Frames per step (1–36) |
| Right | length | `4` | Steps in the repeating cycle (1–36) |
| Above | phase | `0` | Active step, zero-based (0–35, reduced modulo length) |
| Below | output bang | — | Bang on the selected phase |

It emits only when the current frame is on the `rate` boundary and the cycle step equals `phase`. For a four-step cycle, place `0`, `1`, `2`, or `3` above `&` to select the step.

```text
  2
1&4
 *
```

### `~` — timed burst

| Position | Operand | Default | Meaning |
|---|---|---:|---|
| Right | repeats | `4` | Number of bangs (0–35) |
| 2 cells right | division | `1` | Multiply the base clock interval |
| 3 cells right | multiplication | `1` | Divide the interval |
| Below | output bang | — | Emits each timed bang |

Trigger `~` with a neighboring bang. The interval is one quarter-note clock frame multiplied by `division` and divided by `multiplication`; values are base 36, clamped to at least 1 for division and multiplication. A burst is scheduled in real time and does not advance Orca's grid frame. Another trigger replaces the in-progress burst at that grid location.

```text
*~4       four bangs at the base frame interval
*~4 2 1   four bangs, each two frame intervals apart
```

The operands must be adjacent cells to the right of `~`; spaces above are only for readability.

### `+` — binary-scale definition

| Position | Operand | Default | Meaning |
|---|---|---:|---|
| Left | variable key | `0` | Variable that will hold the scale mask |
| Right, cells 1–12 | pitch flags | `101011010101` | One `0` or `1` for each chromatic pitch |

`+` writes a 12-character `0`/`1` mask into the selected Orca variable on each tick. Right-side cells represent `C, C♯, D, D♯, E, F, F♯, G, G♯, A, A♯, B` in that order. `1` includes a pitch and `0` omits it. The default mask is C major. Use `.` for an input's default.

```text
0+101011010101
```

This stores C major in variable `0`.

### `)` — MIDI note from a binary scale

| Position | Operand | Default | Meaning |
|---|---|---:|---|
| Right 1 | channel | `0` | MIDI channel, 0–15 |
| Right 2 | octave | `4` | Base octave, 0–8 |
| Right 3 | degree | `1` | One-based degree in the selected scale (1–35) |
| Right 4 | scale key | `0` | Variable containing a valid 12-bit mask |
| Right 5 | velocity | `f` | Base-36 velocity (0–16) |
| Right 6 | length | `1` | Note duration in clock ticks (0–32) |

Place a neighboring `*` to send the note. The degree counts only pitches marked `1` in the scale mask. Degrees beyond the scale's first octave continue into higher octaves. The scale key must resolve to exactly twelve `0`/`1` characters.

```text
*)0470f1
```

This triggers channel 0, octave 4, scale degree 7 from the mask stored in variable `0`, at velocity `f` and length 1.

### `(` — just-intonation MIDI note

| Position | Operand | Default | Meaning |
|---|---|---:|---|
| Right 1 | channel | `0` | MIDI channel, 0–15 |
| Right 2 | octave | `4` | Base octave, 0–8 |
| Right 3 | note | — | Note letter; lowercase is sharp |
| Right 4 | velocity | `f` | Base-36 velocity (0–16) |
| Right 5 | length | `1` | Note duration in clock ticks (0–32) |

Trigger with a neighboring `*`. The note is tuned to the app's twelve-ratio just-intonation table using channel pitch bend; the bend is reset when the note is released. Connect a MIDI output that accepts pitch bend for the tuning adjustment to be audible.

```text
*(04Cz1
```

This triggers a C note at octave 4 on channel 0 with velocity `z` and length 1, using the just-intonation ratio for its pitch class.

### `@` — shaped MIDI sequence

`@` reads **eight** cells to its right and requires channel, octave, and note values. Its eight inputs include separate duration and pulse-width controls.

| Offset right | Operand | Default | Range / meaning |
|---:|---|---:|---|
| 1 | channel | `0` | MIDI channel, 0–15 |
| 2 | octave | `4` | Octave, 0–8 |
| 3 | note | — | Note letter; lowercase is sharp |
| 4 | velocity | `f` | Base-36 velocity, 0–16 |
| 5 | shape | `1` | Envelope curve, 1–4 |
| 6 | repeats | `1` | Number of notes in the sequence, 0–35 |
| 7 | duration | `1` | Total sequence length in clock frames, 1–36 |
| 8 | pulse width | `i` | Shape 4's progress multiplier, 1–35 |

Trigger `@` with a neighboring `*` to start/restart the sequence. Without a bang, changes to its operands update an already-running sequence at that grid location. The sequence uses real-time timers and does not change the Orca frame counter.

The shape values are eased curves across the sequence: `1` rises slowly at first, `2` rises quickly at first, `3` eases in and out, and `4` advances linearly scaled by pulse width. The pulse-width value is divided by 36 internally. Velocity is shaped across the sequence and kept between 1 and the supplied maximum. Repeats below 1 do not start a sequence.

```text
*@04Cz141i
```

This starts four notes on channel 0, octave 4, C, with maximum velocity `z`, shape 1, across one clock frame, using the default pulse width.

### Other MIDI output operators

These are part of Build 04's expanded MIDI set. Each is bang-triggered and reads its operands to the right.

| Glyph | Name | Operand order |
|---|---|---|
| `:` | MIDI note | channel, octave, note, velocity, length |
| `%` | Monophonic MIDI note | channel, octave, note, velocity, length; replaces the current note on its channel |
| `!` | MIDI control change | channel, controller number, value |
| `?` | MIDI pitch bend | channel, LSB, MSB |

For `:` and `%`, velocity defaults to `f`, length to `1`, octave to `4`, and channel to `0`. Note letters use the app's transpose table; lowercase notes are sharps. CC values and pitch-bend LSB/MSB inputs are mapped from base-36 `0`–`z` to MIDI `0`–`127`. Channel values above 15 are ignored.

```text
*:04Cz1    MIDI note
*%04Cz1    monophonic MIDI note
*!007      CC controller 0, value 7
*?080      pitch bend with both data bytes at 0
```

## Core alphabet operators

Uppercase forms evaluate every frame; lowercase forms evaluate on a bang.

| Glyph | Name | Inputs and result |
|---|---|---|
| `A` | add | Left + right; result below |
| `B` | subtract | Absolute difference of left and right; result below |
| `C` | clock | Left = rate (default 1), right = modulus (default `z`); outputs frame/rate modulo modulus below |
| `D` | delay | Left = rate (default 1), right = modulus (default 1); bangs below on the frame modulo `(rate × modulus)` boundary |
| `E` | east | Moves one cell east; bangs if blocked |
| `F` | if | Bangs below when left and right glyphs match |
| `G` | generator | Left 3 = x offset, left 2 = y offset, left 1 = length; copies the following operands to the target row |
| `H` | halt | Reads and locks the operand below |
| `I` | increment | Left = step (default 1), right = modulus (default `z`); increments the value below |
| `J` | jumper | Outputs the glyph above below itself |
| `K` | konkat | Left = count; reads that many variable keys on its right and writes their values below |
| `L` | lesser | Outputs the smaller of left and right below |
| `M` | multiply | Left × right; result below |
| `N` | north | Moves one cell north; bangs if blocked |
| `O` | read | Left 2 = x offset, left 1 = y offset; outputs the referenced grid glyph below |
| `P` | push | Left 2 = key, left 1 = list length, right = value; writes the value below at horizontal slot `key modulo length` |
| `Q` | query | Left 3 = x offset, left 2 = y offset, left 1 = count; reads that many grid cells and writes results below |
| `R` | random | Left/right = inclusive base-36 range (defaults `0`–`z`); result below |
| `S` | south | Moves one cell south; bangs if blocked |
| `T` | track | Left 2 = index key, left 1 = list length; selects the eastward operand at `key modulo length` and outputs it below |
| `U` | uclid | Left = step count (default 1), right = cycle length (default 4); bangs below on a Euclidean rhythm |
| `V` | variable | Left = write value, right = read key; reads/writes the selected variable and outputs below |
| `W` | west | Moves one cell west; bangs if blocked |
| `X` | write | Left 2 = x offset, left 1 = y offset, right = value; writes the value at the selected grid location |
| `Y` | jymper | Outputs the glyph to the left below itself |
| `Z` | lerp | Left = transition rate, right = target, below = current value; moves the current value toward target |

## Other built-in operators

| Glyph | Name | Behavior |
|---|---|---|
| `*` | bang | Triggers neighboring bang-sensitive operators; the bang is consumed during evaluation |
| `#` | comment | Locks the rest of the row until the next `#` |
| `=` | OSC | Bang-triggered; reads an OSC path at right 1 and message data after it until `.` |
| `;` | UDP | Bang-triggered; sends the cells to its right up to `.` (up to 36 glyphs) |
| `$` | self | Bang-triggered ORCA command, read from the cells to its right up to `.` |

## Keyboard and app controls

Use **View → Toggle Guide** (`Cmd/Ctrl+G`) for the in-app operator list. The command prompt (`Cmd/Ctrl+K`) supports commands such as `osc:` and `udp:` to choose output ports. **MIDI → Next Output Device** cycles available MIDI outputs; **MIDI → Refresh Devices** (`Cmd/Ctrl+Shift+M`) rescans MIDI hardware. `Cmd/Ctrl+P` triggers the operator at the cursor. `Cmd/Ctrl+F` advances one frame while paused.
