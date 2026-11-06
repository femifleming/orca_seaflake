'use strict'

function bounds (anchor, head) {
  if (!anchor || !head) return null
  return {
    left: Math.min(anchor.x, head.x),
    right: Math.max(anchor.x, head.x),
    top: Math.min(anchor.y, head.y),
    bottom: Math.max(anchor.y, head.y)
  }
}

// Mirrors Orca's Cursor.comment(): add a # at both ends of every selected
// row, or remove those edge markers when the first selected cell is already #.
// Node's readline parser does not expose modified arrow keys consistently
// across terminals. Inspect both the emitted character sequence and key object.
function normalizedArrow (key = {}, input = '') {
  const names = { A: 'up', B: 'down', C: 'right', D: 'left' }
  // node:readline emits `str` as undefined for escape-sequence keys on some
  // Node/macOS combinations. Prefer the complete sequence on `key.sequence`.
  const candidates = [String(key.sequence || ''), String(input || '')]
  for (const sequence of candidates) {
    // xterm/Terminal.app modified cursor keys: CSI 1;modifier + direction.
    // Also accept CSI ;modifier + direction and CSI modifier + direction.
    let match = sequence.match(/^\x1b\[(?:1;|;)(\d+)([ABCD])$/)
    if (!match) match = sequence.match(/^\x1b\[(\d+)([ABCD])$/)
    if (match) {
      const modifier = Number(match[1])
      return {
        name: names[match[2]] || null,
        shift: Boolean(key.shift) || (modifier & 1) === 1 || (modifier & 2) === 2
      }
    }
    // Application-cursor mode is an ordinary arrow, not a shifted arrow.
    match = sequence.match(/^\x1bO([ABCD])$/)
    if (match) return { name: names[match[1]] || null, shift: Boolean(key.shift) }
  }
  return {
    name: ['up', 'down', 'left', 'right'].includes(key.name) ? key.name : null,
    shift: Boolean(key.shift)
  }
}

function clearBlock (selection, write) {
  if (!selection || typeof write !== 'function') return 0
  let count = 0
  for (let y = selection.top; y <= selection.bottom; y++) {
    for (let x = selection.left; x <= selection.right; x++) {
      write(x, y, '.')
      count++
    }
  }
  return count
}

function toggleBlockComment (block) {
  const lines = String(block).replace(/\r/g, '').split('\n')
  const width = Math.max(0, ...lines.map(line => line.length))
  if (!width) return String(block)
  const marker = (lines[0] || '').charAt(0) === '#' ? '.' : '#'
  return lines.map(line => {
    const row = line.padEnd(width, '.')
    if (width === 1) return marker
    return marker + row.slice(1, -1) + marker
  }).join('\n')
}

module.exports = { bounds, clearBlock, normalizedArrow, toggleBlockComment }
