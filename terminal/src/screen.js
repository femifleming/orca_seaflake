'use strict'

// Terminal setup, drawing and cleanup.
//
// Drawing overwrites the screen in place instead of clearing it first, so the
// grid doesn't flicker while playing. Every line is clipped to the window
// width: a line that wraps pushes the whole screen up one row, which garbled
// the display in narrow windows.

const ENTER = [
  '\x1b[?1049h', // alternate screen: the shell's scrollback is left untouched
  '\x1b[?25l', // hide the cursor
  '\x1b[?2004h', // bracketed paste
  '\x1b[?1002h', // mouse: clicks and drags only (not every movement)
  '\x1b[?1006h' // mouse: SGR coordinates
].join('')

// Turn off every mouse mode, including ones older versions enabled (1000,
// 1003, 1015), so a terminal left in a bad state by a crash is fixed too.
const LEAVE = [
  '\x1b[?1000l', '\x1b[?1002l', '\x1b[?1003l', '\x1b[?1006l', '\x1b[?1015l',
  '\x1b[?2004l', '\x1b[0m', '\x1b[?25h', '\x1b[?1049l'
].join('')

let active = false
let stream = process.stdout

// Visible width of a string that may contain SGR colour codes.
function visibleLength (text) {
  return [...String(text).replace(/\x1b\[[0-9;]*m/g, '')].length
}

// Clip to `width` visible characters, keeping colour codes intact.
function fit (text, width) {
  let out = ''
  let seen = 0
  const re = /\x1b\[[0-9;]*m|[\s\S]/gu
  let m
  let clipped = false
  while ((m = re.exec(text))) {
    if (m[0].startsWith('\x1b')) { out += m[0]; continue }
    if (seen >= width) { clipped = true; continue }
    out += m[0]
    seen++
  }
  return clipped ? out + '\x1b[0m' : out
}

function enter (out = process.stdout) {
  stream = out
  if (!stream.isTTY) return
  active = true
  stream.write(ENTER)
}

function leave () {
  if (!active) return
  active = false
  try { stream.write(LEAVE) } catch {}
}

function render (lines, out = stream) {
  const cols = out.columns || 80
  const rows = out.rows || 24
  // One column of slack: writing into the last column can wrap in some terminals.
  const width = Math.max(1, cols - 1)
  const shown = out.isTTY ? lines.slice(0, Math.max(1, rows - 1)) : lines
  let frame = '\x1b[H'
  for (const line of shown) frame += fit(line, width) + '\x1b[K\n'
  frame += '\x1b[J'
  out.write(frame)
}

module.exports = { enter, leave, render, fit, visibleLength, ENTER, LEAVE }
