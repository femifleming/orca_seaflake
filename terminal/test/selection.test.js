'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const { bounds, clearBlock, normalizedArrow, toggleBlockComment } = require('../src/selection')

test('selection bounds expand in both axes from a fixed anchor', () => {
  const anchor = { x: 3, y: 4 }
  const afterHorizontal = bounds(anchor, { x: 7, y: 4 })
  const afterVertical = bounds(anchor, { x: 7, y: 1 })
  assert.deepEqual(afterHorizontal, { left: 3, right: 7, top: 4, bottom: 4 })
  assert.deepEqual(afterVertical, { left: 3, right: 7, top: 1, bottom: 4 })
})

test('block comment adds hashes to both sides of every selected row', () => {
  assert.equal(toggleBlockComment('ABCDE\n12345\n.....'), '#BCD#\n#234#\n#...#')
})

test('block comment toggles the boundary hashes back off', () => {
  assert.equal(toggleBlockComment('#BCD#\n#234#'), '.BCD.\n.234.')
})


test('modified down-arrow sequences are recognized as shifted arrows', () => {
  assert.deepEqual(normalizedArrow({ name: 'down' }, '\x1b[1;2B'), { name: 'down', shift: true })
  assert.deepEqual(normalizedArrow({ name: 'down' }, '\x1b[;2B'), { name: 'down', shift: true })
  assert.deepEqual(normalizedArrow({ name: 'down', shift: true }, '\x1b[B'), { name: 'down', shift: true })
})

test('unmodified down arrow remains a cursor move', () => {
  assert.deepEqual(normalizedArrow({ name: 'down' }, '\x1b[B'), { name: 'down', shift: false })
})

test('Shift+Down is recognized from Node readline key.sequence when str is undefined', () => {
  assert.deepEqual(
    normalizedArrow({ sequence: '\x1b[1;2B', name: 'down', shift: true }, undefined),
    { name: 'down', shift: true }
  )
})

test('application-cursor down is not mistaken for Shift+Down', () => {
  assert.deepEqual(
    normalizedArrow({ sequence: '\x1bOB', name: 'down', shift: false }, undefined),
    { name: 'down', shift: false }
  )
})

test('backspace clears every cell in the rectangular selection and nothing outside it', () => {
  const cells = new Map()
  const selection = { left: 2, right: 4, top: 1, bottom: 2 }
  const count = clearBlock(selection, (x, y, glyph) => cells.set(`${x},${y}`, glyph))
  assert.equal(count, 6)
  assert.deepEqual([...cells.keys()].sort(), ['2,1', '2,2', '3,1', '3,2', '4,1', '4,2'])
  assert.ok([...cells.values()].every(glyph => glyph === '.'))
})

test('clearing without a selection does not write anything', () => {
  let writes = 0
  assert.equal(clearBlock(null, () => { writes++ }), 0)
  assert.equal(writes, 0)
})
