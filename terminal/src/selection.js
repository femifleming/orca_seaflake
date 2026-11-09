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

// Mirrors Orca's Cursor.comment(): add a # at both ends of every selected
// row, or remove those edge markers when the first selected cell is already #.
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

module.exports = { bounds, clearBlock, toggleBlockComment }
