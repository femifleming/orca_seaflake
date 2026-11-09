'use strict'

const fs = require('node:fs')
const path = require('node:path')
const vm = require('node:vm')
const { bounds: getSelectionBounds, clearBlock, toggleBlockComment } = require('./selection')
const { performance } = require('node:perf_hooks')
const { shortcutAction, movementDistance, arrowDirection } = require('./keys')
const input = require('./input')
const screen = require('./screen')
const platform = require('./platform')
const { parseSong, serializeSong } = require('./song-file')

const ROOT = path.resolve(__dirname, '..')
const VERSION = require('../package.json').version
const SAVE_DIR = platform.saveDir()
const SONGS_DIR = path.join(SAVE_DIR, 'songs')
const { midi, error: midiLoadError } = platform.loadMidi()

// Stands in for midi.Output/Input when the native module could not load, so
// the grid, song mode and saving still work without MIDI.
class NoMidiPort {
  getPortCount () { return 0 }
  getPortName () { return '' }
  openPort () {}
  closePort () {}
  isPortOpen () { return false }
  sendMessage () {}
}
const undoStack = []
const redoStack = []
const HISTORY_LIMIT = 200
const state = { x: 0, y: 0, bpm: 120, playing: false, insert: false, message: '', file: null, dirty: false, prompt: false, promptValue: '', songOpen: false, songFocus: false, song: { slots: Array(32).fill(null), mode: 0, loop: true }, songFile: null, gridBpmDirective: null, songRow: 0, songField: 0, songBuffer: '', songPlaying: false, songOrder: [], songIndex: 0, songPosition: 0, gridW: 16, gridH: 8, selectionAnchor: null, selectionMode: false, clipboard: '' }

class CoreMidi {
  constructor () {
    // Opening the OS MIDI system can throw, e.g. on Linux without an ALSA
    // sequencer (/dev/snd/seq). Fall back to no MIDI instead of crashing.
    try {
      this.output = midi ? new midi.Output() : new NoMidiPort()
      this.input = midi ? new midi.Input() : new NoMidiPort()
    } catch (e) {
      this.openError = e
      this.output = new NoMidiPort()
      this.input = new NoMidiPort()
    }
    this.stack = []
    this.sequences = new Map()
    this.devices = []
    this.outputIndex = -1
    this.refresh()
  }
  refresh () {
    this.devices = []
    for (let i = 0; i < this.output.getPortCount(); i++) this.devices.push(this.output.getPortName(i))
    // Use the first output that actually opens; one may be busy or broken.
    if (this.devices.length && this.outputIndex < 0) {
      for (let i = 0; i < this.devices.length; i++) if (this.selectOutput(i)) break
    }
  }
  // Returns true when the port opened. Opening can fail, e.g. on Windows when
  // another program (often a DAW) already has the device open.
  selectOutput (i) {
    try { if (this.output.isPortOpen()) this.output.closePort() } catch {}
    this.outputIndex = -1
    this.failedPort = null
    if (i < 0 || !this.devices[i]) return false
    try {
      this.output.openPort(i)
      this.outputIndex = i
      return true
    } catch (e) {
      this.failedPort = this.devices[i]
      return false
    }
  }
  outputDevice () { return this.outputIndex >= 0 ? { name: this.devices[this.outputIndex] } : null }
  statusLabel () {
    if (!midi) return 'MIDI UNAVAILABLE (native module not built)'
    if (this.openError) return 'MIDI UNAVAILABLE (could not open the OS MIDI system)'
    const d = this.outputDevice()
    if (d) return `${this.outputIndex}: ${d.name}`
    if (this.devices.length) return 'no output open (Ctrl-K, midi: -1)'
    return 'no output found'
  }
  send (bytes) { if (this.output.isPortOpen()) this.output.sendMessage(bytes) }
  transpose (note, octave = 3) {
    const table = 'CcDdEFfGgAB'
    const names = Object.keys(this.transposeTable)
    const entry = this.transposeTable[note]
    if (!entry) return null
    const noteName = entry[0]
    const value = ['C','c','D','d','E','F','f','G','g','A','a','B'].indexOf(noteName)
    const oct = Math.max(0, Math.min(8, Number(octave) + Number(entry[1])))
    return { id: Math.max(0, Math.min(127, oct * 12 + value + 24)), value, note: noteName, octave: oct }
  }
  push (channel, octave, note, velocity, length) { return this.add({ channel, octave, note, velocity, length, isPlayed: false }) }
  pushAbsolute (channel, midiNote, velocity, length, pitchBend) { return this.add({ channel, midiNote, velocity, length, pitchBend, isPlayed: false }) }
  add (item) {
    const p = item.midiNote === undefined ? this.transpose(item.note, item.octave) : { id: item.midiNote }
    if (!p) return item
    this.stack.push(item)
    return item
  }
  trigger (item, down) {
    if (!this.output.isPortOpen()) return
    const p = item.midiNote === undefined ? this.transpose(item.note, item.octave) : { id: item.midiNote }
    if (!p) return
    const ch = Number(item.channel) & 15
    if (down && item.pitchBend !== undefined) {
      const bend = Math.max(0, Math.min(16383, Math.round(8192 + item.pitchBend * 4096)))
      this.send([0xE0 + ch, bend & 127, (bend >> 7) & 127])
    }
    this.send([(down ? 0x90 : 0x80) + ch, p.id, down ? Math.round((Number(item.velocity) / 16) * 127) : 0])
    if (!down && item.pitchBend !== undefined) this.send([0xE0 + ch, 0, 64])
  }
  runItem (item) {
    if (!item.isPlayed) { this.trigger(item, true); item.isPlayed = true }
    if (item.length-- <= 0) { this.trigger(item, false); this.stack = this.stack.filter(x => x !== item) }
  }
  run () { for (const item of [...this.stack]) this.runItem(item) }
  allNotesOff () { for (let ch = 0; ch < 16; ch++) this.send([0xB0 + ch, 123, 0]) }
  silence () { for (const item of this.stack) this.trigger(item, false); this.stack = []; for (const id of this.sequences.keys()) this.cancelSequence(id); this.allNotesOff() }
  scheduleSequence (id, sequence) {
    this.cancelSequence(id)
    const frameMs = 60000 / state.bpm / 4
    const duration = Math.max(1, sequence.duration) * frameMs
    const count = Math.max(0, Math.floor(sequence.repeats))
    const timers = []
    for (let i = 0; i < count; i++) {
      const p = count < 2 ? 0 : i / count
      let pos = p
      if (sequence.shape === 1) pos = 1 - Math.sqrt(1 - p)
      if (sequence.shape === 2) pos = Math.sqrt(p)
      if (sequence.shape === 3) pos = p < .5 ? Math.sqrt(p / 2) : 1 - Math.sqrt((1 - p) / 2)
      if (sequence.shape === 4) pos = p * sequence.pulsewidth
      timers.push(setTimeout(() => { this.trigger(sequence, true); setTimeout(() => this.trigger(sequence, false), Math.max(5, frameMs / 2)) }, Math.min(duration - 1, pos * duration)))
    }
    this.sequences.set(id, timers)
  }
  updateSequence () {}
  cancelSequence (id) { const t = this.sequences.get(id); if (t) t.forEach(clearTimeout); this.sequences.delete(id) }
  close () { this.silence(); try { this.output.closePort() } catch {}; try { this.input.closePort() } catch {} }
}

const clock = {
  speed: { value: state.bpm },
  scheduleBurst (id, count, interval, pulse) { this.cancelBurst(id); const timers = []; for (let i=0;i<count;i++) timers.push(setTimeout(pulse, i*interval)); this.bursts.set(id,timers) },
  cancelBurst (id) { (this.bursts.get(id)||[]).forEach(clearTimeout); this.bursts.delete(id) },
  bursts: new Map()
}
const midiOut = new CoreMidi()
const io = { midi: midiOut, cc: { stack: [], run () {} }, mono: { push () {}, run () {} }, udp: { push () {}, run () {} }, osc: { push () {}, run () {} } }
const client = { clock, io, commander: { trigger () {} }, update () {} }

function loadEngine () {
  const context = vm.createContext({ console, Math, Number, String, Object, Array, Set, Map, Date, performance, setTimeout, clearTimeout, setInterval, clearInterval, client })
  for (const file of ['transpose.js', 'operator.js', 'library.js']) {
    let source = fs.readFileSync(path.join(ROOT, 'src/core', file), 'utf8')
    if (file === 'transpose.js') source += '\nglobalThis.transposeTable = transposeTable;'
    if (file === 'operator.js') source += '\nglobalThis.Operator = Operator;'
    if (file === 'library.js') source += '\nglobalThis.library = library;'
    vm.runInContext(source, context, { filename: file })
  }
  midiOut.transposeTable = context.transposeTable
  const source = fs.readFileSync(path.join(ROOT, 'src/core/orca.js'), 'utf8') + '\nglobalThis.Orca = Orca;'
  vm.runInContext(source, context, { filename: 'orca.js' })
  return { orca: new context.Orca(context.library), library: context.library }
}
const { orca } = loadEngine()
// The grid starts at the size of the terminal. It grows when the window grows
// but never shrinks, so making the window smaller (or opening a patch larger
// than the window) never loses cells: the view scrolls to follow the cursor.
function terminalGridSize () {
  return { w: Math.max(8, (process.stdout.columns || 80) - 4), h: Math.max(4, (process.stdout.rows || 24) - 8) }
}
function resizeGrid () {
  const size = terminalGridSize()
  const w = Math.max(size.w, orca.w), h = Math.max(size.h, orca.h)
  if (w === orca.w && h === orca.h) return
  const lines = []
  for (let y = 0; y < h; y++) {
    let line = ''
    for (let x = 0; x < w; x++) line += x < orca.w && y < orca.h ? (orca.glyphAt(x, y) || '.') : '.'
    lines.push(line)
  }
  orca.load(w, h, lines.join(''), orca.f)
  state.gridW = w; state.gridH = h
  state.x = Math.min(state.x, orca.w - 1); state.y = Math.min(state.y, orca.h - 1)
}
// Which part of the grid is on screen. Scrolls just enough to keep the
// cursor visible.
function gridView () {
  const cols = process.stdout.columns || 80
  const rows = process.stdout.rows || 24
  const w = Math.min(orca.w, Math.max(8, cols - 4))
  const h = Math.min(orca.h, Math.max(4, rows - 8))
  let vx = Math.min(state.viewX || 0, orca.w - w), vy = Math.min(state.viewY || 0, orca.h - h)
  if (state.x < vx) vx = state.x
  if (state.x >= vx + w) vx = state.x - w + 1
  if (state.y < vy) vy = state.y
  if (state.y >= vy + h) vy = state.y - h + 1
  state.viewX = Math.max(0, vx); state.viewY = Math.max(0, vy)
  return { x: state.viewX, y: state.viewY, w, h }
}
const initial = terminalGridSize()
orca.reset(initial.w, initial.h)
state.gridW = initial.w; state.gridH = initial.h

const SONG_FIELDS = ['len', 'mul', 'bpm']
const SONG_WIDTHS = [3, 3, 3]
const SONG_MODES = ['ORD', 'REV', 'RND']
const songLength = s => Math.max(1, s.len * s.mul)
const pad = (n, w) => String(n).padStart(w, '0')
function songOrder () {
  const order = state.song.slots.map((s, i) => s ? i : -1).filter(i => i >= 0)
  if (state.song.mode === 1) order.reverse()
  if (state.song.mode === 2) for (let i = order.length - 1; i > 0; i--) { const j = Math.floor(Math.random() * (i + 1)); [order[i], order[j]] = [order[j], order[i]] }
  return order
}
function storeSongSlot () {
  const i = state.songRow
  const old = state.song.slots[i]
  state.song.slots[i] = { len: old?.len ?? 16, mul: old?.mul ?? 1, bpm: old?.bpm ?? state.bpm, notes: old?.notes ?? '', p: orca.format() }
  state.message = `STORED SLOT ${pad(i + 1, 2)}`
}
function loadSongSlot (i) {
  const s = state.song.slots[i]
  if (!s) { state.message = 'EMPTY SLOT'; return false }
  recordUndo()
  openPatchText(s.p)
  setBpm(s.bpm)
  state.message = `LOADED SLOT ${pad(i + 1, 2)}`
  return true
}
function startSong () {
  state.songOrder = songOrder(); state.songIndex = 0; state.songPosition = 0
  if (!state.songOrder.length) { state.message = 'NO SONG SLOTS'; return }
  state.songPlaying = true
  const i = state.songOrder[0]
  loadSongSlot(i)
  state.message = `SONG PLAYING SLOT ${pad(i + 1, 2)}`
  playToggle(true)
}
function songTick () {
  if (!state.songPlaying) return
  const i = state.songOrder[state.songIndex]
  const s = state.song.slots[i]
  if (!s) { state.songPlaying = false; return }
  if (state.songPosition >= songLength(s)) {
    state.songIndex++
    if (state.songIndex >= state.songOrder.length) {
      if (!state.song.loop) { state.songPlaying = false; state.playing = false; clearInterval(timer); midiOut.silence(); return }
      state.songOrder = songOrder(); state.songIndex = 0
    }
    const next = state.songOrder[state.songIndex]
    state.songPosition = 0
    loadSongSlot(next)
  }
  state.songPosition++
}
function drawGrid () {
  const view = gridView()
  const lines = []
  lines.push(`\x1b[1;36m ORCA SEAFLAKE  /  TERMINAL ${VERSION}\x1b[0m`)
  lines.push(` ${state.playing ? '\x1b[32mPLAY' : '\x1b[33mSTOP'}\x1b[0m  BPM ${state.bpm}  FRAME ${orca.f}  ${pad(state.x, 3)},${pad(state.y, 3)}  GRID ${orca.w}x${orca.h}${state.selectionMode ? '  \x1b[36mSELECT\x1b[0m' : ''}  MIDI: ${midiOut.statusLabel()}`)
  // Arrows on the frame show there is more grid beyond the edge of the view
  const more = (cond, ch, fill) => cond ? `\x1b[33m${ch}\x1b[0m` : fill
  lines.push(' ┌' + '─'.repeat(view.w) + '┐' + more(view.y > 0, '▲', ''))
  for (let row = 0; row < view.h; row++) {
    const y = view.y + row
    let line = row === Math.floor(view.h / 2) ? more(view.x > 0, '◀', ' ') + '│' : ' │'
    for (let col = 0; col < view.w; col++) {
      const x = view.x + col
      const g = orca.glyphAt(x, y) || '.'
      const selected = isCellSelected(x, y)
      const cursor = x === state.x && y === state.y
      line += cursor ? `\x1b[7m${g}\x1b[0m` : selected ? `\x1b[7;36m${g}\x1b[0m` : (g === '.' ? (x % 8 === 0 && y % 8 === 0 ? '\x1b[90m+\x1b[0m' : '\x1b[90m.\x1b[0m') : `\x1b[1;37m${g}\x1b[0m`)
    }
    line += '│' + (row === Math.floor(view.h / 2) ? more(view.x + view.w < orca.w, '▶', '') : '')
    lines.push(line)
  }
  lines.push(' └' + '─'.repeat(view.w) + '┘' + more(view.y + view.h < orca.h, '▼', ''))
  lines.push(' Arrows move  Shift/Opt+Arrows leap 8  PgUp/PgDn/Home/End jump  Tab select  Ctrl-C/V copy/paste  < > tempo')
  const status = inletAtCursor() || state.message
  if (state.prompt) lines.push(` ${state.promptLabel || 'Path'}: ${state.promptValue}_  (Enter to confirm, Esc to cancel)`)
  else lines.push(' Ctrl-S save  Ctrl-O open  Ctrl-N new  Ctrl-T song  Ctrl-K command  Ctrl-Q quit  |  ' + status)
  screen.render(lines)
}
function drawSong () {
  const cols = process.stdout.columns || 80
  const rows = process.stdout.rows || 24
  const width = Math.max(24, cols - 4)
  const visible = Math.max(1, Math.min(32, rows - 10))
  const top = Math.max(0, Math.min(32 - visible, state.songRow - Math.floor(visible / 2)))
  const active = state.songPlaying ? state.songOrder[state.songIndex] : -1
  let out = ''
  const songName = state.songFile ? path.basename(state.songFile).replace(/\.(song|orca-song|json)$/i, '') : 'untitled'
  out += `\x1b[1;36m ORCA SEAFLAKE  /  SONG ${songName.toUpperCase()}\x1b[0m\n`
  out += ` ${state.songPlaying ? '\x1b[32mPLAY' : '\x1b[33mSTOP'}\x1b[0m  MODE ${SONG_MODES[state.song.mode]}  LOOP ${state.song.loop ? 'ON' : 'OFF'}  BPM ${state.bpm}\n`
  out += ' ┌' + '─'.repeat(width) + '┐\n'
  const heading = ' #  LEN MUL BPM  NOTES'.padEnd(width).slice(0, width)
  out += ' │' + heading + '│\n'
  for (let j = 0; j < visible; j++) {
    const i = top + j
    const slot = state.song.slots[i]
    const selected = i === state.songRow
    const marker = i === active ? '>' : selected ? '▶' : ' '
    const fields = slot ? [pad(slot.len, 3), pad(slot.mul, 3), pad(slot.bpm, 3)] : ['...', '...', '...']
    const line = `${marker}${pad(i + 1, 2)}  ${fields[0]} ${fields[1]} ${fields[2]}  ${slot?.notes || ''}`.padEnd(width).slice(0, width)
    out += ' │' + (selected ? `\x1b[7m${line}\x1b[0m` : line) + '│\n'
  }
  out += ' └' + '─'.repeat(width) + '┘\n'
  // Key in bold, what it does in plain text, in 15-column cells so the rows line up
  const key = (k, label) => `\x1b[1m${k}\x1b[0m ${label}` + ' '.repeat(Math.max(1, 15 - k.length - 1 - label.length))
  out += ' ' + [key('ENT', 'store'), key('L', 'load'), key('DEL', 'clear'), key('N', 'notes')].join('') + '\n'
  out += ' ' + [key('SPC', 'play'), key('M', 'mode'), key('R', 'loop'), key('0-9 +/-', 'edit')].join('') + '\n'
  out += ' ' + [key('S', 'save song'), key('O', 'open song'), key('ESC', 'close')].join('') + '\n'
  // While asking for a file name or notes, the bottom line shows the question and what is typed
  if (state.prompt) out += ` ${state.promptLabel || 'Path'}: ${state.promptValue}_  (Enter to confirm, Esc to cancel)\n`
  else out += ` Selected slot ${pad(state.songRow + 1, 2)}  Field ${SONG_FIELDS[state.songField].toUpperCase()}  ${state.message}` + '\n'
  screen.render(out.replace(/\n$/, '').split('\n'))
}
function draw () { if (state.songOpen) drawSong(); else drawGrid() }

function patchSnapshot () { return { patch: orca.format(), frame: orca.f } }
function recordUndo () {
  const snapshot = patchSnapshot()
  if (undoStack[undoStack.length - 1]?.patch !== snapshot.patch) undoStack.push(snapshot)
  if (undoStack.length > HISTORY_LIMIT) undoStack.shift()
  redoStack.length = 0
}
function restorePatchSnapshot (snapshot) {
  const x = state.x, y = state.y
  openPatchText(snapshot.patch)
  orca.f = snapshot.frame
  state.x = Math.min(x, orca.w - 1)
  state.y = Math.min(y, orca.h - 1)
  state.dirty = true
  clearSelection()
}
function undo () {
  if (!undoStack.length) { state.message = 'NOTHING TO UNDO'; return }
  redoStack.push(patchSnapshot())
  restorePatchSnapshot(undoStack.pop())
  state.message = `UNDO (${undoStack.length} STEPS AVAILABLE)`
}
function redo () {
  if (!redoStack.length) { state.message = 'NOTHING TO REDO'; return }
  undoStack.push(patchSnapshot())
  restorePatchSnapshot(redoStack.pop())
  state.message = `REDO (${redoStack.length} STEPS AVAILABLE)`
}
function resetHistory () { undoStack.length = 0; redoStack.length = 0 }
function edit (glyph) { recordUndo(); clearSelection(); orca.write(state.x, state.y, glyph); state.dirty = true; if (state.insert) state.x = Math.min(orca.w - 1, state.x + 1) }
function selectionBounds () {
  return getSelectionBounds(state.selectionAnchor, { x: state.x, y: state.y })
}
function isCellSelected (x, y) {
  const b = selectionBounds()
  return !!b && x >= b.left && x <= b.right && y >= b.top && y <= b.bottom
}
function clearSelection () { state.selectionAnchor = null }
function selectedText () {
  const b = selectionBounds()
  if (!b) return ''
  const rows = []
  for (let y = b.top; y <= b.bottom; y++) {
    let line = ''
    for (let x = b.left; x <= b.right; x++) line += orca.glyphAt(x, y) || '.'
    rows.push(line)
  }
  return rows.join('\n')
}
function deleteSelection () {
  if (!state.selectionAnchor) return false
  const b = selectionBounds()
  recordUndo()
  const count = clearBlock(b, (x, y, glyph) => orca.write(x, y, glyph))
  state.dirty = true
  clearSelection()
  state.message = `DELETED ${b.right - b.left + 1}×${b.bottom - b.top + 1} BLOCK (${count} CELLS)`
  return true
}
function commentSelection () {
  if (!state.selectionAnchor) { state.message = 'SELECT A BLOCK FIRST'; return false }
  const b = selectionBounds()
  recordUndo()
  const result = toggleBlockComment(selectedText()).split('\n')
  for (let row = 0; row < result.length; row++) {
    for (let col = 0; col < result[row].length; col++) {
      orca.write(b.left + col, b.top + row, result[row][col])
    }
  }
  state.dirty = true
  state.message = result[0]?.[0] === '#' ? 'BLOCK COMMENTED' : 'BLOCK UNCOMMENTED'
  return true
}
function copySelection () {
  if (!state.selectionAnchor) { state.message = 'NO SELECTION'; return false }
  const value = selectedText()
  state.clipboard = value
  if (!platform.copyToClipboard(value)) state.message = 'COPIED TO INTERNAL CLIPBOARD (system clipboard unavailable)'
  else state.message = `COPIED ${selectionBounds().right - selectionBounds().left + 1}×${selectionBounds().bottom - selectionBounds().top + 1} BLOCK`
  return true
}
function pasteText (value) {
  if (!value) { state.message = 'CLIPBOARD EMPTY'; return }
  const lines = value.replace(/\r/g, '').replace(/\n$/, '').split('\n')
  recordUndo()
  for (let row = 0; row < lines.length && state.y + row < orca.h; row++) {
    for (let col = 0; col < lines[row].length && state.x + col < orca.w; col++) {
      orca.write(state.x + col, state.y + row, lines[row][col])
    }
  }
  state.dirty = true
  clearSelection()
  state.message = `PASTED ${lines.length} ROW${lines.length === 1 ? '' : 'S'}`
}
function pasteClipboard () {
  const system = platform.readClipboard()
  pasteText(system !== null ? system : state.clipboard)
}
function applyGridDirectives () {
  const lines = orca.format().split('\n')
  let bpm = null
  for (const line of lines) {
    for (const match of line.matchAll(/\$bpm:\s*(\d{1,3})/gi)) {
      const value = Number(match[1])
      if (value >= 60 && value <= 300) bpm = value
    }
  }
  if (bpm !== null && bpm !== state.gridBpmDirective) {
    state.gridBpmDirective = bpm
    if (bpm !== state.bpm) setBpm(bpm)
    state.message = `GRID BPM DIRECTIVE: ${bpm}`
  }
}
function inletAtCursor () {
  for (const operator of orca.parse()) {
    if (operator.x === state.x && operator.y === state.y) {
      return `${operator.glyph} ${operator.name.toUpperCase()} / ${operator.info || 'OPERATOR'}`
    }
    for (const [name, port] of Object.entries(operator.ports || {})) {
      if (operator.x + port.x !== state.x || operator.y + port.y !== state.y) continue
      const glyph = orca.glyphAt(state.x, state.y) || '.'
      const value = glyph === '.' || glyph === '*' ? (port.default || '.') : glyph
      const direction = port.output ? 'OUTLET' : 'INLET'
      return `${operator.glyph} ${operator.name.toUpperCase()} / ${direction} ${name.toUpperCase()} / VALUE ${value}`
    }
  }
  return ''
}

function tick () { if (!state.playing) return; if (state.songPlaying) songTick(); applyGridDirectives(); io.midi.run(); io.cc.run(); io.mono.run(); io.udp.run(); io.osc.run(); orca.run(); draw() }
let timer = null
function playToggle (forceOn = false) { state.playing = forceOn ? true : !state.playing; if (state.playing) { clearInterval(timer); timer = setInterval(tick, 60000 / state.bpm / 4) } else { state.songPlaying = false; clearInterval(timer); midiOut.silence() }; draw() }
function setBpm (n) { state.bpm = Math.max(60, Math.min(300, n)); clock.speed.value = state.bpm; if (state.playing) { clearInterval(timer); timer = setInterval(tick, 60000 / state.bpm / 4) } }
function runGridCommand (input) {
  const command = String(input || '').trim().replace(/^\$/, '')
  const match = command.match(/^([a-z]+)\s*:\s*(-?\d+)$/i)
  if (!match) { state.message = `UNKNOWN COMMAND: ${input}`; return false }
  const name = match[1].toLowerCase()
  const value = Number(match[2])
  if (name === 'bpm') {
    if (!Number.isFinite(value) || value < 60 || value > 300) { state.message = 'BPM MUST BE BETWEEN 60 AND 300'; return false }
    setBpm(value)
    state.message = `BPM SET TO ${state.bpm}`
    return true
  }
  if (name === 'apm') {
    if (value < 1 || value > 300) { state.message = 'APM MUST BE BETWEEN 1 AND 300'; return false }
    setBpm(value * 4)
    state.message = `APM SET TO ${value}`
    return true
  }
  if (name === 'midi') {
    midiOut.refresh()
    if (!midi) { state.message = `MIDI UNAVAILABLE: ${midiLoadError?.message?.split('\n')[0] || 'native module missing'}`; return false }
    if (!midiOut.devices.length) { state.message = `NO MIDI OUTPUTS. ${platform.midiSetupHint()}`; return false }
    if (value < 0 || value >= midiOut.devices.length) { state.message = `MIDI OUTPUTS: ${midiOut.devices.map((d, i) => `${i}=${d}`).join('  ')}`; return false }
    midiOut.silence()
    if (!midiOut.selectOutput(value)) { state.message = `COULD NOT OPEN ${midiOut.devices[value]}. IS ANOTHER APP USING IT?`; return false }
    state.message = `MIDI OUTPUT ${value}: ${midiOut.devices[value]}`
    return true
  }
  if (name === 'frame') { orca.f = Math.max(0, value); state.message = `FRAME SET TO ${orca.f}`; return true }
  if (name === 'rewind') { orca.f = Math.max(0, orca.f - Math.abs(value)); state.message = `REWOUND ${Math.abs(value)} FRAMES`; return true }
  if (name === 'skip') { orca.f += Math.abs(value); state.message = `SKIPPED ${Math.abs(value)} FRAMES`; return true }
  state.message = `UNKNOWN COMMAND: ${name}`
  return false
}

function save () {
  fs.mkdirSync(SAVE_DIR, { recursive: true })
  const name = state.file || path.join(SAVE_DIR, `untitled-${Date.now()}.orca`)
  fs.writeFileSync(name, `${orca.format()}`)
  state.file = name; state.dirty = false; state.message = `Saved ${name}`
}
function openPatchText (raw) {
  state.gridBpmDirective = null
  const lines = raw.replace(/\r/g, '').trimEnd().split('\n')
  const w = Math.max(1, ...lines.map(x => x.length)); const h = lines.length
  const size = terminalGridSize()
  const targetW = Math.max(w, size.w), targetH = Math.max(h, size.h)
  const padded = Array.from({ length: targetH }, (_, y) => (lines[y] || '').padEnd(targetW, '.').slice(0, targetW)).join('')
  orca.load(targetW, targetH, padded); state.x = 0; state.y = 0; state.viewX = 0; state.viewY = 0
  state.gridW = orca.w; state.gridH = orca.h
}
function openFile (name) {
  openPatchText(fs.readFileSync(path.resolve(name), 'utf8'))
  state.file = path.resolve(name); state.dirty = false
  resetHistory()
  state.message = `Opened ${state.file}`
}
function beginPrompt (label, action, value = '') { state.prompt = true; state.promptLabel = label; state.promptAction = action; state.promptValue = value }
function finishPrompt () {
  try {
    const value = state.promptValue.trim()
    if (state.promptAction === 'command') runGridCommand(value)
    else if (state.promptAction === 'song-note') {
      const slot = state.song.slots[state.songRow]
      if (slot) { slot.notes = value; state.message = `NOTES UPDATED FOR SLOT ${pad(state.songRow + 1, 2)}` }
    } else if (state.promptAction === 'song-save') {
      const file = path.resolve(expandHome(/\.(song|orca-song|json)$/i.test(value) ? value : value + '.song'))
      fs.mkdirSync(path.dirname(file), { recursive: true })
      fs.writeFileSync(file, serializeSong(state.song))
      state.songFile = file; state.message = `SAVED ${file}`
    } else if (state.promptAction === 'song-open') {
      const file = path.resolve(expandHome(value))
      state.song = parseSong(JSON.parse(fs.readFileSync(file, 'utf8')))
      state.songFile = file; state.message = `OPENED ${file}`
    } else if (value) openFile(expandHome(value))
    else state.message = 'Open cancelled'
  } catch (e) { state.message = e.message }
  state.prompt = false; state.promptAction = null
}
// Paths typed or dragged into the prompt: expand ~, and strip the quotes or
// backslash escapes terminals add when a file is dropped onto the window.
function expandHome (value) {
  let v = String(value).trim()
  if (/^(['"]).*\1$/.test(v)) v = v.slice(1, -1)
  else v = v.replace(/\\(.)/g, '$1')
  if (v === '~' || v.startsWith('~/') || v.startsWith('~\\')) v = path.join(require('node:os').homedir(), v.slice(1))
  return v
}

let shuttingDown = false
function restoreTerminal () {
  screen.leave()
  try { if (process.stdin.isTTY) process.stdin.setRawMode(false) } catch {}
}
function shutdown (code = 0) {
  if (shuttingDown) return
  shuttingDown = true
  clearInterval(timer)
  try { midiOut.close() } catch {}
  restoreTerminal()
  process.exit(code)
}

// Grid-mode cursor movement
function moveTo (x, y, extend) {
  if (extend && !state.selectionAnchor) state.selectionAnchor = { x: state.x, y: state.y }
  if (!extend) clearSelection()
  state.x = Math.max(0, Math.min(orca.w - 1, x))
  state.y = Math.max(0, Math.min(orca.h - 1, y))
}
function jumpKey (key, extend) {
  const page = gridView().h
  const corner = key.ctrl || key.meta
  if (key.name === 'pageup') moveTo(state.x, state.y - page, extend)
  else if (key.name === 'pagedown') moveTo(state.x, state.y + page, extend)
  else if (key.name === 'home') moveTo(0, corner ? 0 : state.y, extend)
  else if (key.name === 'end') moveTo(orca.w - 1, corner ? orca.h - 1 : state.y, extend)
  else return false
  return true
}

let mouseDragAnchor = null
function handleMouse (e) {
  if (state.songOpen || state.prompt || e.wheel) return
  const view = gridView()
  // Grid cells start at screen column 3 and row 4 (1-based).
  const col = e.x - 3, row = e.y - 4
  const inside = col >= 0 && col < view.w && row >= 0 && row < view.h
  // Clicks outside the grid are ignored; a drag that leaves it scrolls the view.
  if (!inside && !mouseDragAnchor) return
  const x = Math.max(0, Math.min(orca.w - 1, view.x + col))
  const y = Math.max(0, Math.min(orca.h - 1, view.y + row))
  if (e.release) {
    if (!mouseDragAnchor) return
    state.x = x; state.y = y
    // A click without a drag just moves the cursor.
    if (mouseDragAnchor.x === x && mouseDragAnchor.y === y && !state.selectionMode) clearSelection()
    else state.selectionAnchor = { ...mouseDragAnchor }
    mouseDragAnchor = null
  } else if (e.button === 0) {
    if (!e.drag || !mouseDragAnchor) mouseDragAnchor = { x, y }
    state.selectionAnchor = { ...mouseDragAnchor }
    state.x = x; state.y = y
  } else return
  draw()
}

function handlePaste (text) {
  if (state.prompt) state.promptValue += text.replace(/\r?\n[\s\S]*$/, '')
  else if (!state.songOpen) pasteText(text)
  draw()
}

function handleKey (key) {
  const str = key.str
  const action = shortcutAction(key)
  if (action === 'quit') return shutdown()
  if (state.prompt) {
    if (key.name === 'escape' || (key.ctrl && key.name === 'c')) { state.prompt = false; state.message = state.promptAction === 'command' ? 'COMMAND CANCELLED' : 'Cancelled'; state.promptAction = null; draw(); return }
    if (key.name === 'return' || key.name === 'enter') { finishPrompt(); draw(); return }
    if (key.name === 'backspace') state.promptValue = state.promptValue.slice(0, -1)
    else if (str && !key.ctrl && !key.meta && [...str].length === 1) state.promptValue += str
    draw(); return
  }
  // Control+C is exclusively copy. It never terminates the application.
  // Command shortcuts are accepted too when the terminal forwards them.
  if (action === 'copy') {
    if (!state.songOpen) copySelection()
    else state.message = 'COPY IS AVAILABLE IN GRID MODE'
    draw(); return
  }
  if (!state.songOpen && action === 'undo') { undo(); draw(); return }
  if (!state.songOpen && action === 'redo') { redo(); draw(); return }
  if (action === 'command') { beginPrompt('Command', 'command'); draw(); return }
  if (action === 'song') { state.songOpen = !state.songOpen; state.songFocus = state.songOpen; draw(); return }
  const direction = arrowDirection(key)
  if (state.songOpen) {
    const step = movementDistance(key)
    if (key.name === 'escape') { state.songOpen = false; draw(); return }
    if (direction === 'up') { state.songRow = Math.max(0, state.songRow - step); state.songBuffer = '' }
    else if (direction === 'down') { state.songRow = Math.min(31, state.songRow + step); state.songBuffer = '' }
    else if (direction === 'left') { state.songField = Math.max(0, state.songField - 1); state.songBuffer = '' }
    else if (direction === 'right') { state.songField = Math.min(2, state.songField + 1); state.songBuffer = '' }
    else if (key.name === 'pageup' || key.name === 'home') { state.songRow = key.name === 'home' ? 0 : Math.max(0, state.songRow - 8); state.songBuffer = '' }
    else if (key.name === 'pagedown' || key.name === 'end') { state.songRow = key.name === 'end' ? 31 : Math.min(31, state.songRow + 8); state.songBuffer = '' }
    else if (key.name === 'return' || key.name === 'enter') storeSongSlot()
    else if (key.name === 'backspace' || key.name === 'delete') { state.song.slots[state.songRow] = null; state.message = `CLEARED SLOT ${pad(state.songRow + 1, 2)}` }
    else if (key.name === 'space') { if (state.songPlaying) { state.songPlaying = false; playToggle(false) } else startSong() }
    else if (action) { /* other shortcuts do nothing in Song Mode */ }
    else if (str === 'l' || str === 'L') loadSongSlot(state.songRow)
    else if (str === 'n' || str === 'N') { const slot = state.song.slots[state.songRow]; if (slot) beginPrompt(`Notes for slot ${pad(state.songRow + 1, 2)}`, 'song-note', slot.notes || ''); else state.message = 'STORE A PATCH IN THIS SLOT FIRST' }
    else if (str === 'm' || str === 'M') state.song.mode = (state.song.mode + 1) % 3
    else if (str === 'r' || str === 'R') state.song.loop = !state.song.loop
    else if (str === 's' || str === 'S') beginPrompt('Save song as', 'song-save', state.songFile || path.join(SONGS_DIR, 'untitled.song'))
    else if (str === 'o' || str === 'O') beginPrompt('Open song', 'song-open', state.songFile ? path.dirname(state.songFile) + path.sep : SONGS_DIR + path.sep)
    else if (/^\d$/.test(str || '') && state.song.slots[state.songRow]) { const slot = state.song.slots[state.songRow], field = SONG_FIELDS[state.songField]; state.songBuffer = (state.songBuffer + str).slice(-SONG_WIDTHS[state.songField]); slot[field] = Number(state.songBuffer); state.message = `EDIT ${field.toUpperCase()} SLOT ${pad(state.songRow + 1, 2)}` }
    else if ((str === '+' || str === '=' || str === '-') && state.song.slots[state.songRow]) { const slot = state.song.slots[state.songRow], field = SONG_FIELDS[state.songField]; slot[field] = Math.max(field === 'bpm' ? 60 : 1, Math.min(field === 'bpm' ? 300 : 999, slot[field] + (str === '-' ? -1 : 1))); state.songBuffer = '' }
    draw(); return
  }
  // Tab starts a terminal-independent selection mode. In this mode arrows
  // (and leaps, PgUp/PgDn, Home/End) grow the rectangle.
  if (key.name === 'tab' && !key.shift) {
    if (!state.selectionMode) {
      state.selectionMode = true
      state.selectionAnchor = { x: state.x, y: state.y }
      state.message = 'SELECTION MODE: ARROWS OR MOUSE DRAG EXTEND, TAB OR ESC EXITS'
    } else {
      state.selectionMode = false
      clearSelection()
      state.message = 'SELECTION MODE OFF'
    }
    draw(); return
  }
  if (key.name === 'f2' || key.name === 'escape') {
    if (state.selectionMode || state.selectionAnchor) { state.selectionMode = false; clearSelection(); state.message = 'SELECTION MODE OFF' }
    draw(); return
  }
  const extend = state.selectionMode
  if (direction) {
    const d = movementDistance(key)
    const [dx, dy] = { up: [0, -1], down: [0, 1], left: [-1, 0], right: [1, 0] }[direction]
    moveTo(state.x + dx * d, state.y + dy * d, extend)
  } else if (jumpKey(key, extend)) { /* moved */ }
  else if (action === 'paste') pasteClipboard()
  else if (action === 'select-all') { state.selectionAnchor = { x: 0, y: 0 }; state.x = orca.w - 1; state.y = orca.h - 1; state.message = 'SELECTED ALL' }
  else if ((str === '#' && state.selectionAnchor) || action === 'comment') commentSelection()
  else if (key.name === 'space' && !key.ctrl && !key.meta) return playToggle()
  else if (key.name === 'backspace' || key.name === 'delete') {
    if (!deleteSelection()) edit('.')
  }
  else if (action === 'save') save()
  else if (action === 'open') beginPrompt('Open patch', 'open')
  else if (action === 'new') { recordUndo(); const z = terminalGridSize(); orca.reset(z.w, z.h); state.file = null; state.dirty = false; state.gridBpmDirective = null; state.x = 0; state.y = 0; state.viewX = 0; state.viewY = 0; clearSelection(); state.selectionMode = false; state.message = 'NEW GRID' }
  else if (key.ctrl || key.meta) { /* unbound shortcut: never type it */ }
  else if (str === '>') { setBpm(state.bpm + 1); state.message = `BPM ${state.bpm}` }
  else if (str === '<') { setBpm(state.bpm - 1); state.message = `BPM ${state.bpm}` }
  else if (str === '.') edit('.')
  else if (str && [...str].length === 1 && orca.isAllowed(str)) edit(str)
  draw()
}

// A crash must never leave the terminal in raw/mouse-reporting mode, or every
// later mouse movement types escape codes into the shell.
process.on('uncaughtException', e => {
  restoreTerminal()
  try { midiOut.close() } catch {}
  console.error(`Orca Seaflake crashed: ${e && e.stack ? e.stack : e}`)
  process.exit(1)
})
for (const signal of ['SIGTERM', 'SIGHUP', 'SIGINT']) process.on(signal, () => shutdown(0))
process.on('exit', () => { restoreTerminal(); try { midiOut.close() } catch {} })

if (process.stdin.isTTY) process.stdin.setRawMode(true)
process.stdin.resume()
screen.enter(process.stdout)
input.attach(process.stdin, e => {
  if (shuttingDown) return
  if (e.type === 'mouse') handleMouse(e)
  else if (e.type === 'paste') handlePaste(e.text)
  else if (e.type === 'key') handleKey(e)
})
process.stdout.on('resize', () => { resizeGrid(); draw() })
if (!midi) state.message = `NO MIDI: native module failed to load. Run \`npm rebuild midi\` in the terminal folder. Editing still works.`
else if (midiOut.devices.length && !midiOut.outputDevice()) state.message = `COULD NOT OPEN ANY MIDI OUTPUT (another app may be using it). Ctrl-K then "midi: -1" to retry.`
else if (midiOut.openError) state.message = `NO MIDI: ${midiOut.openError.message.split('\n')[0]}. Editing still works.`
else if (!midiOut.devices.length) state.message = `NO MIDI OUTPUTS FOUND. ${platform.midiSetupHint()}`
else state.message = `Ctrl-K then "midi: -1" lists MIDI outputs, "midi: 2" picks output 2`
try { if (process.argv[2]) openFile(process.argv[2]) } catch (e) { state.message = e.message }
draw()
