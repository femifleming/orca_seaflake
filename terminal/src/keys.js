'use strict'

// How far a leap (fast travel) moves, matching the desktop app's 8x8 grid.
const LEAP = 8

function isModifier (key = {}) {
  return Boolean(key.ctrl || key.meta)
}

// The direction an arrow-like key moves the cursor, or null. Option+Left and
// Option+Right arrive as ESC b / ESC f in macOS Terminal and in iTerm2's
// "Natural Text Editing" preset, so those count as arrows too.
function arrowDirection (key = {}) {
  if (['up', 'down', 'left', 'right'].includes(key.name)) return key.name
  if (key.meta && !key.ctrl && key.name === 'b') return 'left'
  if (key.meta && !key.ctrl && key.name === 'f') return 'right'
  return null
}

// Any modifier on an arrow leaps 8 cells: Shift works in every terminal,
// Option/Alt and Ctrl wherever the terminal and OS pass them through (macOS
// uses Ctrl+arrows for Mission Control unless that is turned off).
function movementDistance (key = {}) {
  return key.shift || key.ctrl || key.meta ? LEAP : 1
}

function shortcutAction (key = {}) {
  if (!isModifier(key) || !key.name) return null
  const name = String(key.name).toLowerCase()
  if (name === 'c') return 'copy'
  if (name === 'v') return 'paste'
  if (name === 'z') return key.shift ? 'redo' : 'undo'
  if (name === 'y') return 'redo'
  if (name === 's') return 'save'
  if (name === 'o') return 'open'
  if (name === 'n') return 'new'
  if (name === 't') return 'song'
  if (name === 'k') return 'command'
  if (name === '/') return 'comment'
  if (name === 'a') return 'select-all'
  // Quit is intentionally Control+Q only. Control+C is never an exit key.
  if (key.ctrl && name === 'q') return 'quit'
  return null
}

module.exports = { LEAP, isModifier, shortcutAction, movementDistance, arrowDirection }
