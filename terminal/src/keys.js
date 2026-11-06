'use strict'

function isModifier (key = {}) {
  return Boolean(key.ctrl || key.meta)
}

function movementDistance (key = {}, arrow = {}, selectionMode = false) {
  return !selectionMode && (arrow.shift || key.ctrl || key.meta) ? 8 : 1
}

function shortcutAction (key = {}) {
  if (!isModifier(key) || !key.name) return null
  const name = String(key.name).toLowerCase()
  if (name === 'c') return 'copy'
  if (name === 'v') return 'paste'
  if (name === 'z') return key.shift ? 'redo' : 'undo'
  if (name === 's') return 'save'
  if (name === 'o') return 'open'
  if (name === 'n') return 'new'
  if (name === 't') return 'song'
  if (name === 'k') return 'command'
  if (name === '/') return 'comment'
  // Quit is intentionally Control+Q only. Control+C is never an exit key.
  if (key.ctrl && name === 'q') return 'quit'
  return null
}

module.exports = { isModifier, shortcutAction, movementDistance }
