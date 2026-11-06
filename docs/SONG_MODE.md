# Song Mode Tracker Guide

Orca Seaflake features a built-in **Song Mode** canvas-rendered tracker sequencer inspired by hardware trackers (such as Elektron Octatrack and XOR NerdSEQ) and rendered directly onto Orca's canvas in pure monospace grid text.

It sequences through distinct Orca patches with per-slot frame lengths (`LEN`), multiplication factors (`MUL`), base tempos (`BPM`), dynamic tempo overrides via commands like `$bpm`, multiple play modes (`ORD`, `REV`, `RND`), and song file save/recall.

---

## Visual Design & Tracker Grid

The song tracker slides smoothly into view from the right side of the canvas with a 1-tile cyan edge divider and an orange header bar:

```
  SONG 01 STOP
MODE  ORD  LOOP ON
POS 0000/0016 SLOT 01

  # LEN MUL BPM PATCH
 01 016 001 120 . . . . R . . . . . . . .
 02 002 004 120 . . . . . . . R g . R g R . .
 03 008 002 140 . . D 4 . . . . . . . . .
 04 . . . . . . . . .
```

### Table Columns
- `#`: Slot index (`01` to `32`). A `>` cursor marks the currently playing slot.
- `LEN`: Base duration in frames (Orca ticks). For example, `1` = 1 frame, `16` = 16 frames.
- `MUL`: Multiplication factor. Multiplies `LEN` to determine total frames for that slot.
- `BPM`: Base tempo in beats-per-minute for that slot.
- `PATCH`: Plain-text preview snippet of the patch stored in that slot.

---

## Frame Duration: `LEN * MUL`

Slot duration in frames is calculated as:
$$\text{Duration} = \text{LEN} \times \text{MUL}\text{ frames}$$

1 frame corresponds to exactly 1 Orca tick.

Examples:
- If `LEN` is `1` and `MUL` is `1`: plays for **exactly 1 frame**.
- If `LEN` is `2` and `MUL` is `4`: plays for **8 frames** ($2 \times 4$).
- If `LEN` is `16` and `MUL` is `1`: plays for **16 frames** ($16 \times 1 = 1 \text{ bar of 16th notes}$).
- If `LEN` is `16` and `MUL` is `4`: plays for **64 frames** ($16 \times 4 = 4 \text{ bars}$).

---

## Base Tempo per Pattern & `$bpm` Override

1. **Base Tempo (`BPM`)**:
   - Each slot stores its own base tempo (e.g. `120`, `130`, `140`).
   - When transitioning to a slot, that slot's base tempo is applied to Orca's clock.

2. **Patch Command Override (`$bpm`)**:
   - If an Orca patch executes commands like `$bpm:140` or `$bpm140`, that BPM change **overrides** the base tempo while the patch plays in Song Mode.
   - Song Mode preserves the active override for the remaining frames of that slot.
   - If overridden, the active tempo appears in the header with an asterisk (e.g. `140*`).
   - When the next slot begins, that next slot's base tempo is loaded cleanly.

---

## Play Modes & Looping

- **`ORD` (Order)**: Plays slots in sequential order from slot `01` upward.
- **`REV` (Reverse)**: Plays slots in reverse order downward.
- **`RND` (Random)**: Shuffles all non-empty slots into random order per cycle.
- **`LOOP ON / OFF`**: When loop is `ON`, sequencing repeats continuously. When `OFF`, sequencing stops automatically after the last slot in the sequence finishes.

---

## Dedicated Song Mode Controls vs. Spacebar

- **Spacebar on Grid**: When typing on or navigating the Orca grid, `Space` is reserved for starting/pausing the Orca grid clock (identical to classic Orca).
- **Spacebar in Song Tracker**: When the song tracker is focused, `Space` starts/stops Song Mode playback.
- **Mouse Control**:
  - Click the top header bar (`SONG 01 STOP` / `PLAY`) to start or stop Song Mode at any time.
  - Click `MODE` to cycle modes (`ORD` $\rightarrow$ `REV` $\rightarrow$ `RND`).
  - Click `LOOP` to toggle looping (`ON` $\leftrightarrow$ `OFF`).
  - Click any cell under `LEN`, `MUL`, or `BPM` to select and edit that value.
  - Click to the left of the cyan divider line on the Orca grid to unfocus the song tracker.

---

## Keyboard Shortcuts (When Song Mode is Focused)

| Key | Action |
| --- | --- |
| `↑` / `↓` | Move between slots |
| `←` / `→` | Switch between `LEN`, `MUL`, and `BPM` fields |
| `0` - `9` | Type numbers into selected field |
| `+` / `-` (or `=`) | Increment / decrement value by 1 |
| `Enter` (`ENT`) | Store current Orca grid patch into selected slot |
| `L` | Load selected slot's patch and BPM onto the Orca grid |
| `Backspace` / `Del` | Clear selected slot |
| `Space` (`SPC`) | Play / Stop Song Mode sequencing |
| `M` | Cycle play mode (`ORD` $\rightarrow$ `REV` $\rightarrow$ `RND`) |
| `O` | Toggle loop (`ON` $\leftrightarrow$ `OFF`) |
| `[` / `]` | Select song bank (1 to 16) |
| `W` | Save current song to selected bank in local storage |
| `R` | Recall song from selected bank in local storage |
| `E` | Export song as a `.song` file (native macOS Save Dialog) |
| `I` | Import song from a `.song` file (native macOS Open Dialog) |
| `Escape` | Unfocus and slide song tracker away |

---

## Global Shortcuts & Commander Commands

| Action | Shortcut | Commander Command |
| --- | --- | --- |
| Toggle Song Tracker Drawer | `Cmd/Ctrl+T` or `Cmd/Ctrl+M` | `song` or `song:toggle` |
| Save Song File | `Cmd/Ctrl+Shift+E` | `song:save` |
| Open Song File | `Cmd/Ctrl+Shift+I` | `song:open` |
| Start Song Sequencing | - | `song:play` |
| Stop Song Sequencing | - | `song:stop` |
| Orca Clock Play/Pause | `Space` (when on grid) | `play` / `stop` |
| Drag & Drop Song File | Drag any `.song` file into window | - |
