'use strict'

// A terminal input parser that understands every escape sequence the app can
// receive: arrows with any modifier, Home/End/PageUp/PageDown, function keys,
// CSI-u keys (iTerm2, kitty, WezTerm, foot), Option-as-Meta keys, SGR and X10
// mouse reports, bracketed paste and focus reports.
//
// Node's readline keypress parser does not know mouse reports or CSI-u, and
// splits them into single characters ("3", "5", ";", "M"...), which the grid
// then wrote as operators. Anything this parser does not recognise is
// swallowed whole, so stray sequences can never become typed glyphs.

const { StringDecoder } = require('node:string_decoder')

const ESC = '\x1b'
const PASTE_START = '\x1b[200~'
const PASTE_END = '\x1b[201~'

function key (name, str, mods = {}, sequence = str) {
  return { type: 'key', name, str, ctrl: !!mods.ctrl, meta: !!mods.meta, shift: !!mods.shift, sequence }
}

// xterm modifier parameter: 1 + (shift 1 | alt 2 | ctrl 4 | super/cmd 8)
function decodeModifier (param) {
  const m = Math.max(0, (Number(param) || 1) - 1)
  return { shift: !!(m & 1), meta: !!(m & 2) || !!(m & 8), ctrl: !!(m & 4) }
}

function charKey (ch, sequence = ch) {
  if (ch === '\r') return key('return', ch, {}, sequence)
  if (ch === '\n') return key('enter', ch, {}, sequence)
  if (ch === '\t') return key('tab', ch, {}, sequence)
  if (ch === '\x7f' || ch === '\x08') return key('backspace', ch, {}, sequence)
  if (ch === ' ') return key('space', ch, {}, sequence)
  if (ch === '\x00') return key('space', undefined, { ctrl: true }, sequence)
  if (ch === '\x1f') return key('/', undefined, { ctrl: true }, sequence)
  const code = ch.codePointAt(0)
  if (code >= 1 && code <= 26) return key(String.fromCharCode(code + 96), undefined, { ctrl: true }, sequence)
  if (code < 32) return key(undefined, undefined, {}, sequence)
  const lower = ch.toLowerCase()
  const isLetter = lower !== ch.toUpperCase()
  return key(isLetter ? lower : ch, ch, { shift: isLetter && ch !== lower }, sequence)
}

const CURSOR_FINALS = { A: 'up', B: 'down', C: 'right', D: 'left', H: 'home', F: 'end', E: 'clear' }
const SS3_FINALS = { ...CURSOR_FINALS, P: 'f1', Q: 'f2', R: 'f3', S: 'f4', M: 'enter' }
const TILDE_CODES = { 1: 'home', 2: 'insert', 3: 'delete', 4: 'end', 5: 'pageup', 6: 'pagedown', 7: 'home', 8: 'end', 11: 'f1', 12: 'f2', 13: 'f3', 14: 'f4', 15: 'f5', 17: 'f6', 18: 'f7', 19: 'f8', 20: 'f9', 21: 'f10', 23: 'f11', 24: 'f12' }

function decodeCsi (params, final, sequence) {
  // SGR mouse: CSI < button ; col ; row M (press/drag/wheel) or m (release)
  if (params.startsWith('<') && (final === 'M' || final === 'm')) {
    const [b, x, y] = params.slice(1).split(';').map(Number)
    return mouseEvent(b, x, y, final === 'm', sequence)
  }
  // Private/other prefixes (?, >, =) are terminal replies, never keys.
  if (/^[?>=<]/.test(params)) return { type: 'ignore', sequence }
  const parts = params.split(';').map(p => p.split(':')[0])
  if (final === 'I' || final === 'O') return { type: 'ignore', sequence } // focus in / out
  if (final === 'Z') return key('tab', undefined, { shift: true }, sequence)
  // rxvt shifted arrows: CSI a / b / c / d
  if (!params && 'abcd'.includes(final)) return key(CURSOR_FINALS['ABCD'['abcd'.indexOf(final)]], undefined, { shift: true }, sequence)
  if (CURSOR_FINALS[final] || 'PQRS'.includes(final)) {
    const name = CURSOR_FINALS[final] || ({ P: 'f1', Q: 'f2', R: 'f3', S: 'f4' })[final]
    // CSI 1;mod A, or the older CSI mod A form
    const mod = parts.length > 1 ? parts[1] : (parts[0] && parts[0] !== '1' ? parts[0] : 1)
    return key(name, undefined, decodeModifier(mod), sequence)
  }
  if (final === '~') {
    const code = Number(parts[0])
    if (code === 27 && parts.length >= 3) return csiU(Number(parts[2]), parts[1], sequence) // xterm modifyOtherKeys
    const name = TILDE_CODES[code]
    return name ? key(name, undefined, decodeModifier(parts[1]), sequence) : { type: 'ignore', sequence }
  }
  if (final === 'u') return csiU(Number(parts[0]), parts[1], sequence)
  // rxvt modified keys use $ (shift) and ^ (ctrl) as final bytes
  if ((final === '$' || final === '^') && TILDE_CODES[Number(parts[0])]) {
    return key(TILDE_CODES[Number(parts[0])], undefined, final === '$' ? { shift: true } : { ctrl: true }, sequence)
  }
  return { type: 'ignore', sequence }
}

// CSI codepoint ; modifier u
function csiU (code, modParam, sequence) {
  const mods = decodeModifier(modParam)
  if (!Number.isFinite(code)) return { type: 'ignore', sequence }
  const named = { 13: 'return', 9: 'tab', 27: 'escape', 127: 'backspace', 8: 'backspace', 32: 'space' }[code]
  if (named) return key(named, mods.ctrl || mods.meta ? undefined : String.fromCodePoint(code), mods, sequence)
  if (code >= 57344 && code <= 63743) return { type: 'ignore', sequence } // kitty private-use (lone modifiers etc.)
  let ch = String.fromCodePoint(code)
  const lower = ch.toLowerCase()
  if (mods.shift && lower !== ch.toUpperCase()) ch = ch.toUpperCase()
  return key(lower, mods.ctrl || mods.meta ? undefined : ch, mods, sequence)
}

function mouseEvent (b, x, y, release, sequence) {
  return {
    type: 'mouse',
    x,
    y,
    button: b & 3,
    drag: !!(b & 32),
    wheel: !!(b & 64),
    shift: !!(b & 4),
    meta: !!(b & 8),
    ctrl: !!(b & 16),
    release,
    sequence
  }
}

// Parse as much of `buf` as possible. Returns the events and any incomplete
// tail to keep for the next chunk. With `flush`, a trailing lone ESC becomes
// the Escape key and any other incomplete sequence is dropped.
function parse (buf, flush = false) {
  const events = []
  let i = 0
  while (i < buf.length) {
    const ch = buf[i]
    if (ch !== ESC) {
      const cp = buf.codePointAt(i)
      const s = String.fromCodePoint(cp)
      events.push(charKey(s))
      i += s.length
      continue
    }
    const token = parseEscape(buf, i, flush)
    if (!token) break // incomplete; wait for more input
    events.push(token.event)
    i = token.end
  }
  return { events, rest: buf.slice(i) }
}

function parseEscape (buf, i, flush) {
  const next = buf[i + 1]
  if (next === undefined) return flush ? { event: key('escape', undefined, {}, ESC), end: i + 1 } : null
  if (next === '[') return parseCsi(buf, i, flush)
  if (next === 'O') {
    const f = buf[i + 2]
    if (f === undefined) return flush ? { event: key('o', 'O', { meta: true, shift: true }, ESC + 'O'), end: i + 2 } : null
    const seq = buf.slice(i, i + 3)
    // rxvt ctrl-arrows: ESC O a/b/c/d
    if ('abcd'.includes(f)) return { event: key(CURSOR_FINALS['ABCD'['abcd'.indexOf(f)]], undefined, { ctrl: true }, seq), end: i + 3 }
    const name = SS3_FINALS[f]
    return { event: name ? key(name, name === 'enter' ? '\r' : undefined, {}, seq) : { type: 'ignore', sequence: seq }, end: i + 3 }
  }
  if (next === ESC) {
    // ESC ESC [A etc: Option/Alt + key in Terminal.app and older xterms
    if (buf[i + 2] === undefined && !flush) return null
    if (buf[i + 2] === '[' || buf[i + 2] === 'O') {
      const inner = parseEscape(buf, i + 1, flush)
      if (!inner) return null
      if (inner.event.type === 'key') inner.event.meta = true
      inner.event.sequence = buf.slice(i, inner.end)
      return inner
    }
    return { event: key('escape', undefined, { meta: true }, ESC + ESC), end: i + 2 }
  }
  // ESC + character: Option-as-Meta / Alt + key
  const cp = buf.codePointAt(i + 1)
  const s = String.fromCodePoint(cp)
  const event = charKey(s, ESC + s)
  event.meta = true
  return { event, end: i + 1 + s.length }
}

function parseCsi (buf, i, flush) {
  let j = i + 2
  // X10 mouse: ESC [ M Cb Cx Cy (three raw bytes, each offset by 32)
  if (buf[j] === 'M') {
    if (buf.length < j + 4) return flush ? { event: { type: 'ignore', sequence: buf.slice(i) }, end: buf.length } : null
    const b = buf.charCodeAt(j + 1) - 32, x = buf.charCodeAt(j + 2) - 32, y = buf.charCodeAt(j + 3) - 32
    return { event: mouseEvent(b, x, y, (b & 3) === 3 && !(b & 64), buf.slice(i, j + 4)), end: j + 4 }
  }
  // Linux console function keys: ESC [ [ A
  if (buf[j] === '[') {
    if (buf[j + 1] === undefined) return flush ? { event: { type: 'ignore', sequence: buf.slice(i) }, end: buf.length } : null
    return { event: { type: 'ignore', sequence: buf.slice(i, j + 2) }, end: j + 2 }
  }
  const start = j
  while (j < buf.length && /[0-9;:<=>?]/.test(buf[j])) j++
  while (j < buf.length && /[ -/]/.test(buf[j])) j++
  if (j >= buf.length) return flush ? { event: { type: 'ignore', sequence: buf.slice(i) }, end: buf.length } : null
  const final = buf[j]
  const sequence = buf.slice(i, j + 1)
  if (!/[@-~$^]/.test(final)) return { event: { type: 'ignore', sequence }, end: j + 1 }
  const params = buf.slice(start, j)
  if (sequence === PASTE_START) {
    const endAt = buf.indexOf(PASTE_END, j + 1)
    if (endAt < 0) {
      if (!flush) return null
      return { event: { type: 'paste', text: buf.slice(j + 1) }, end: buf.length }
    }
    return { event: { type: 'paste', text: buf.slice(j + 1, endAt) }, end: endAt + PASTE_END.length }
  }
  if (sequence === PASTE_END) return { event: { type: 'ignore', sequence }, end: j + 1 }
  return { event: decodeCsi(params, final, sequence), end: j + 1 }
}

// Wire the parser to a stream. Incomplete sequences wait briefly for the rest
// to arrive; a lone ESC becomes the Escape key after that pause. An unfinished
// paste waits longer, since large pastes arrive in several chunks.
function attach (stream, onEvent, { escapeDelay = 40, pasteDelay = 500 } = {}) {
  let pending = ''
  let timer = null
  // Keeps a multi-byte character that is split across two reads in one piece.
  const decoder = new StringDecoder('utf8')
  const emit = events => { for (const e of events) if (e.type !== 'ignore') onEvent(e) }
  const flush = () => { timer = null; const r = parse(pending, true); pending = r.rest; emit(r.events) }
  stream.on('data', chunk => {
    if (timer) { clearTimeout(timer); timer = null }
    const r = parse(pending + (typeof chunk === 'string' ? chunk : decoder.write(chunk)))
    pending = r.rest
    emit(r.events)
    if (pending) timer = setTimeout(flush, pending.startsWith(PASTE_START) ? pasteDelay : escapeDelay)
  })
}

module.exports = { parse, attach, decodeModifier }
