'use strict'

const fs = require('node:fs')
const path = require('node:path')
const vm = require('node:vm')
const readline = require('node:readline')
const { bounds: getSelectionBounds, clearBlock, normalizedArrow, toggleBlockComment } = require('./selection')
const { performance } = require('node:perf_hooks')
const { shortcutAction, movementDistance } = require('./keys')
const platform = require('./platform')

const ROOT = path.resolve(__dirname, '..')
const VERSION = require('../package.json').version
const SAVE_DIR = platform.saveDir()
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
const state = { x: 0, y: 0, bpm: 120, playing: false, insert: false, message: '', file: null, dirty: false, prompt: false, promptValue: '', songOpen: false, songFocus: false, song: { slots: Array(32).fill(null), mode: 0, loop: true }, gridBpmDirective: null, songRow: 0, songField: 0, songBuffer: '', songPlaying: false, songOrder: [], songIndex: 0, songPosition: 0, resizeCache: new Map(), gridW: 16, gridH: 8, selectionAnchor: null, selectionMode: false, clipboard: '' }

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
    if (this.devices.length && this.outputIndex < 0) this.selectOutput(0)
  }
  selectOutput (i) {
    try { if (this.output.isPortOpen()) this.output.closePort() } catch {}
    this.outputIndex = i
    if (i >= 0 && this.devices[i]) this.output.openPort(i)
  }
  outputDevice () { return this.outputIndex >= 0 ? { name: this.devices[this.outputIndex] } : null }
  statusLabel () {
    if (!midi) return 'MIDI UNAVAILABLE (native module not built)'
    if (this.openError) return 'MIDI UNAVAILABLE (could not open the OS MIDI system)'
    const d = this.outputDevice()
    return d ? `${this.outputIndex}: ${d.name}` : 'no output found'
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
// Size the grid to the terminal on startup. Grid contents are cached by
// coordinate when the terminal is resized, so shrinking never destroys cells.
function terminalGridSize () {
  return { w: Math.max(8, (process.stdout.columns || 80) - 4), h: Math.max(4, (process.stdout.rows || 24) - 8) }
}
function resizeGrid () {
  const size = terminalGridSize()
  if (size.w === orca.w && size.h === orca.h) return
  for (let y = 0; y < orca.h; y++) for (let x = 0; x < orca.w; x++) {
    state.resizeCache.set(`${x},${y}`, orca.glyphAt(x, y) || '.')
  }
  const lines = []
  for (let y = 0; y < size.h; y++) {
    let line = ''
    for (let x = 0; x < size.w; x++) line += state.resizeCache.get(`${x},${y}`) || (x < orca.w && y < orca.h ? orca.glyphAt(x, y) : '.')
    lines.push(line)
  }
  orca.load(size.w, size.h, lines.join(''), orca.f)
  state.gridW = size.w; state.gridH = size.h
  state.x = Math.min(state.x, orca.w - 1); state.y = Math.min(state.y, orca.h - 1)
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
  state.song.slots[i] = { len: old?.len ?? 64, mul: old?.mul ?? 1, bpm: old?.bpm ?? state.bpm, notes: old?.notes ?? '', p: orca.format() }
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
  const cols = process.stdout.columns || 80
  const rows = process.stdout.rows || 24
  const viewW = Math.min(orca.w, Math.max(8, cols - 4))
  state.viewStartX = 0
  const viewH = Math.min(orca.h, Math.max(4, rows - 8))
  let out = '\x1b[H\x1b[2J'
  out += `\x1b[1;36m ORCA SEAFLAKE  /  TERMINAL ${VERSION}\x1b[0m\n`
  out += ` ${state.playing ? '\x1b[32mPLAY' : '\x1b[33mSTOP'}\x1b[0m  BPM ${state.bpm}  FRAME ${orca.f}  GRID ${orca.w}x${orca.h}  MIDI: ${midiOut.statusLabel()}\n`
  out += ' ┌' + '─'.repeat(viewW) + '┐\n'
  for (let y = 0; y < viewH; y++) {
    out += ' │'
    for (let x = 0; x < viewW; x++) {
      const g = orca.glyphAt(x, y) || '.'
      const selected = isCellSelected(x, y)
      const cursor = x === state.x && y === state.y
      out += selected ? `\x1b[7;36m${g}\x1b[0m` : cursor ? `\x1b[7m${g}\x1b[0m` : (g === '.' ? '\x1b[90m.\x1b[0m' : `\x1b[1;37m${g}\x1b[0m`)
    }
    out += '│\n'
  }
  out += ' └' + '─'.repeat(viewW) + '┘\n'
  out += ` Arrows move  |  Shift/Ctrl+Arrows fast (8 cells)  |  Tab select  |  Mouse drag select  |  Ctrl-C copy Ctrl-V paste\n`
  const inlet = inletAtCursor()
  const status = inlet || state.message
  out += ' Ctrl-S save  Ctrl-O open  Ctrl-N new  Ctrl-T song mode  Ctrl-K command  Ctrl-Q quit  |  ' + (state.prompt ? `${state.promptLabel || 'Path'}: ${state.promptValue}` : status) + '\n'
  process.stdout.write(out)
}
function drawSong () {
  const cols = process.stdout.columns || 80
  const rows = process.stdout.rows || 24
  const width = Math.max(24, cols - 4)
  const visible = Math.max(1, Math.min(32, rows - 10))
  const top = Math.max(0, Math.min(32 - visible, state.songRow - Math.floor(visible / 2)))
  const active = state.songPlaying ? state.songOrder[state.songIndex] : -1
  let out = '\x1b[H\x1b[2J'
  out += `\x1b[1;36m ORCA SEAFLAKE  /  SONG MODE ${VERSION}\x1b[0m\n`
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
  out += ' Enter store | L load | Backspace clear | N edit notes for selected slot\n'
  out += ' Arrows select row/field | 0-9 edit | +/- adjust | Space play/stop | M mode | O loop\n'
  out += ' E export song | I import song | Ctrl-T return to Orca grid\n'
  out += ` Selected slot ${pad(state.songRow + 1, 2)}  Field ${SONG_FIELDS[state.songField].toUpperCase()}  ${state.message}` + '\n'
  process.stdout.write(out)
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
function moveCursor (dx, dy, extend = false, distance = 1) {
  if (extend && !state.selectionAnchor) state.selectionAnchor = { x: state.x, y: state.y }
  if (!extend) clearSelection()
  state.x = Math.max(0, Math.min(orca.w - 1, state.x + dx * distance))
  state.y = Math.max(0, Math.min(orca.h - 1, state.y + dy * distance))
}
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
    midiOut.silence(); midiOut.selectOutput(value)
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
  state.resizeCache.clear()
  const padded = Array.from({ length: targetH }, (_, y) => (lines[y] || '').padEnd(targetW, '.').slice(0, targetW)).join('')
  orca.load(targetW, targetH, padded); state.x = 0; state.y = 0
  for (let y = 0; y < orca.h; y++) for (let x = 0; x < orca.w; x++) state.resizeCache.set(`${x},${y}`, orca.glyphAt(x, y) || '.')
  resizeGrid()
}
function openFile (name) {
  openPatchText(fs.readFileSync(path.resolve(name), 'utf8'))
  state.file = path.resolve(name); state.dirty = false
  resetHistory()
  state.message = `Opened ${state.file}`
}
function beginOpenPrompt (label = 'Open patch') { state.prompt = true; state.promptLabel = label; state.promptValue = ''; draw() }
function shutdown () { clearInterval(timer); midiOut.close(); process.stdout.write('\x1b[0m\x1b[?1003l\x1b[?1006l\x1b[?2004l\x1b[?25h\n'); process.exit(0) }

readline.emitKeypressEvents(process.stdin)
if (process.stdin.isTTY) process.stdin.setRawMode(true)
process.stdout.write('\x1b[?25l\x1b[?2004h\x1b[?1003h\x1b[?1006h')
let mouseDragAnchor = null
process.stdin.on('data', chunk => {
  const input = chunk.toString('utf8')
  const re = /\x1b\[<(\d+);(\d+);(\d+)([Mm])/g
  let match
  while ((match = re.exec(input))) {
    if (state.songOpen || state.prompt) continue
    const button = Number(match[1]) & 3
    const col = Number(match[2]), row = Number(match[3])
    const x = col - 3 + (state.viewStartX || 0), y = row - 4
    if (x < 0 || x >= orca.w || y < 0 || y >= orca.h) continue
    if (match[4] === 'M' && button === 0) {
      if (!mouseDragAnchor) mouseDragAnchor = { x, y }
      state.selectionAnchor = { ...mouseDragAnchor }
      state.x = x; state.y = y
    } else if (match[4] === 'm' && mouseDragAnchor) {
      state.selectionAnchor = { ...mouseDragAnchor }
      state.x = x; state.y = y
      mouseDragAnchor = null
    } else if (match[4] === 'M' && (button === 1 || button === 2)) {
      state.x = x; state.y = y; clearSelection()
    }
    draw()
  }
})
process.stdin.on('keypress', (str, key) => {
  // Terminal bracketed-paste mode lets Cmd+V insert a rectangular text block.
  if (typeof str === 'string' && str.startsWith('\x1b[200~') && str.includes('\x1b[201~')) {
    const value = str.slice(6, str.lastIndexOf('\x1b[201~'))
    if (!state.songOpen && !state.prompt) pasteText(value)
    draw(); return
  }
  const arrow = normalizedArrow(key, str)
  const action = shortcutAction(key)
  if (action === 'quit') return shutdown()
  if (state.prompt) {
    if (key.name === 'escape' || (key.ctrl && key.name === 'c')) { state.prompt = false; state.message = state.promptAction === 'command' ? 'COMMAND CANCELLED' : 'Open cancelled'; state.promptAction = null; draw(); return }
    if (key.name === 'return' || key.name === 'enter') { try { const value = state.promptValue.trim(); if (state.promptAction === 'command') { runGridCommand(value) } else if (state.promptAction === 'song-note') { const slot = state.song.slots[state.songRow]; if (slot) { slot.notes = value; state.message = `NOTES UPDATED FOR SLOT ${pad(state.songRow + 1, 2)}` } } else if (state.promptAction === 'song-save') { fs.mkdirSync(path.dirname(path.resolve(value)), { recursive: true }); fs.writeFileSync(path.resolve(value), JSON.stringify({ orcaSong: 2, mode: state.song.mode, loop: state.song.loop ? 1 : 0, slots: state.song.slots.map(s => s && ({ len: s.len, mul: s.mul, bpm: s.bpm, notes: s.notes || '', p: s.p.split('\n') })) }, null, 1)); state.message = `SAVED ${value}` } else if (state.promptAction === 'song-open') { const j = JSON.parse(fs.readFileSync(path.resolve(value), 'utf8')); if (![1, 2].includes(j.orcaSong) || !Array.isArray(j.slots)) throw new Error('Not a Seaflake song file'); state.song = { mode: Math.max(0, Math.min(2, j.mode | 0)), loop: !!j.loop, slots: Array.from({length:32}, (_, i) => { const a = j.slots[i]; return a ? { len: Math.max(1, Math.min(999, j.orcaSong === 1 ? ((a.bars | 0) * 16 + Math.min(15, a.st | 0)) : (a.len | 0))), notes: String(a.notes || ''), mul: Math.max(1, Math.min(999, j.orcaSong === 1 ? 1 : (a.mul | 0))), bpm: Math.max(60, Math.min(300, a.bpm | 0)), p: Array.isArray(a.p) ? a.p.join('\n') : String(a.p || '') } : null }) }; state.message = `LOADED ${value}` } else openFile(value) } catch (e) { state.message = e.message }; state.prompt = false; state.promptAction = null; draw(); return }
    if (key.name === 'backspace') state.promptValue = state.promptValue.slice(0, -1)
    else if (str && str.length === 1 && !key.ctrl) state.promptValue += str
    draw(); return
  }
  // Control+C is exclusively copy. It never terminates the application.
  // Command shortcuts are accepted too when the terminal forwards them.
  if (action === 'copy') {
    if (!state.songOpen && !state.prompt) copySelection()
    else state.message = 'COPY IS AVAILABLE IN GRID MODE'
    draw(); return
  }
  if (!state.songOpen && action === 'undo') { undo(); draw(); return }
  if (!state.songOpen && action === 'redo') { redo(); draw(); return }
  if (action === 'command') { state.prompt = true; state.promptLabel = 'Command'; state.promptAction = 'command'; state.promptValue = ''; draw(); return }
  const songShortcut = action === 'song'
  if (songShortcut) { state.songOpen = !state.songOpen; state.songFocus = state.songOpen; draw(); return }
  if (state.songOpen) {
    if (key.name === 'escape') { state.songOpen = false; draw(); return }
    if (arrow.name === 'up') { state.songRow = Math.max(0, state.songRow - 1); state.songBuffer = '' }
    else if (arrow.name === 'down') { state.songRow = Math.min(31, state.songRow + 1); state.songBuffer = '' }
    else if (arrow.name === 'left') { state.songField = Math.max(0, state.songField - 1); state.songBuffer = '' }
    else if (arrow.name === 'right') { state.songField = Math.min(2, state.songField + 1); state.songBuffer = '' }
    else if (key.name === 'return' || key.name === 'enter') storeSongSlot()
    else if (key.name === 'backspace' || key.name === 'delete') { state.song.slots[state.songRow] = null; state.message = `CLEARED SLOT ${pad(state.songRow + 1, 2)}` }
    else if (key.name === 'space') { if (state.songPlaying) { state.songPlaying = false; playToggle(false) } else startSong() }
    else if (str === 'l' || str === 'L') loadSongSlot(state.songRow)
    else if (str === 'n' || str === 'N') { const slot = state.song.slots[state.songRow]; if (slot) { state.prompt = true; state.promptLabel = `Notes for slot ${pad(state.songRow + 1, 2)}`; state.promptAction = 'song-note'; state.promptValue = slot.notes || '' } else state.message = 'STORE A PATCH IN THIS SLOT FIRST' }
    else if (str === 'm' || str === 'M') state.song.mode = (state.song.mode + 1) % 3
    else if (str === 'o' || str === 'O') state.song.loop = !state.song.loop
    else if (str === 'e' || str === 'E') { state.prompt = true; state.promptLabel = 'Export song path'; state.promptAction = 'song-save'; state.promptValue = path.join(SAVE_DIR, 'untitled.song') }
    else if (str === 'i' || str === 'I') { state.prompt = true; state.promptLabel = 'Import song path'; state.promptAction = 'song-open'; state.promptValue = '' }
    else if (/^\d$/.test(str || '') && state.song.slots[state.songRow]) { const slot = state.song.slots[state.songRow], field = SONG_FIELDS[state.songField]; state.songBuffer = (state.songBuffer + str).slice(-SONG_WIDTHS[state.songField]); slot[field] = Number(state.songBuffer); state.message = `EDIT ${field.toUpperCase()} SLOT ${pad(state.songRow + 1, 2)}` }
    else if ((str === '+' || str === '=' || str === '-') && state.song.slots[state.songRow]) { const slot = state.song.slots[state.songRow], field = SONG_FIELDS[state.songField]; slot[field] = Math.max(field === 'bpm' ? 60 : 1, Math.min(field === 'bpm' ? 300 : 999, slot[field] + (str === '-' ? -1 : 1))); state.songBuffer = '' }
    draw(); return
  }
  // Tab starts a terminal-independent selection mode. In this mode ordinary
  // arrow keys extend the rectangle, even when the terminal strips Shift.
  if (key.name === 'tab' || str === '\t') {
    if (!state.selectionMode) {
      state.selectionMode = true
      state.selectionAnchor = { x: state.x, y: state.y }
      state.message = 'SELECTION MODE: ARROWS OR MOUSE DRAG EXTEND, TAB OR F2 EXITS'
    } else {
      state.selectionMode = false
      clearSelection()
      state.message = 'SELECTION MODE OFF'
    }
    draw(); return
  }
  if (key.name === 'f2') { state.selectionMode = false; clearSelection(); state.message = 'SELECTION MODE OFF'; draw(); return }
  if (key.name === 'escape') { if (state.selectionMode) { state.selectionMode = false; clearSelection(); state.message = 'SELECTION MODE OFF' }; draw(); return }
  const extendSelection = state.selectionMode || arrow.shift
  const travel = movementDistance(key, arrow, state.selectionMode)
  if (arrow.name === 'up') moveCursor(0, -1, extendSelection, travel)
  else if (arrow.name === 'down') moveCursor(0, 1, extendSelection, travel)
  else if (arrow.name === 'left') moveCursor(-1, 0, extendSelection, travel)
  else if (arrow.name === 'right') moveCursor(1, 0, extendSelection, travel)
  else if (action === 'paste') pasteClipboard()
  else if ((str === '#' && state.selectionAnchor) || action === 'comment') commentSelection()
  else if (key.name === 'space') return playToggle()
  else if (key.name === 'backspace' || key.name === 'delete' || str === '\x7f') {
    if (!deleteSelection()) edit('.')
  }
  else if (str === '.') edit('.')
  else if (str === '+') setBpm(state.bpm + 1)
  else if (str === '-') setBpm(state.bpm - 1)
  else if (action === 'save') save()
  else if (action === 'open') return beginOpenPrompt()
  else if (action === 'new') { const z = terminalGridSize(); orca.reset(z.w, z.h); state.file = null; state.dirty = false; state.resizeCache.clear(); state.gridBpmDirective = null; state.x = 0; state.y = 0; resetHistory() }
  else if (str && str.length === 1 && orca.isAllowed(str)) edit(str)
  draw()
})
process.stdout.on('resize', () => { resizeGrid(); draw() })
process.on('SIGTERM', shutdown)
process.on('exit', () => { try { midiOut.close() } catch {} })
if (!midi) state.message = `NO MIDI: native module failed to load. Run \`npm rebuild midi\` in the terminal folder. Editing still works.`
else if (midiOut.openError) state.message = `NO MIDI: ${midiOut.openError.message.split('\n')[0]}. Editing still works.`
else if (!midiOut.devices.length) state.message = `NO MIDI OUTPUTS FOUND. ${platform.midiSetupHint()}`
else state.message = `Ctrl-K then "midi: -1" lists MIDI outputs, "midi: 2" picks output 2`
try { if (process.argv[2]) openFile(process.argv[2]) } catch (e) { state.message = e.message }
draw()
