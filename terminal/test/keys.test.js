'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const { shortcutAction, movementDistance } = require('../src/keys')

test('Ctrl+C and Command+C map only to copy, never quit', () => {
  assert.equal(shortcutAction({ ctrl: true, name: 'c' }), 'copy')
  assert.equal(shortcutAction({ meta: true, name: 'c' }), 'copy')
})

test('paste, undo, and redo accept Control and Command modifiers', () => {
  assert.equal(shortcutAction({ ctrl: true, name: 'v' }), 'paste')
  assert.equal(shortcutAction({ meta: true, name: 'v' }), 'paste')
  assert.equal(shortcutAction({ ctrl: true, name: 'z' }), 'undo')
  assert.equal(shortcutAction({ meta: true, name: 'z' }), 'undo')
  assert.equal(shortcutAction({ ctrl: true, shift: true, name: 'z' }), 'redo')
  assert.equal(shortcutAction({ meta: true, shift: true, name: 'z' }), 'redo')
})

test('quit is Control+Q only', () => {
  assert.equal(shortcutAction({ ctrl: true, name: 'q' }), 'quit')
  assert.equal(shortcutAction({ meta: true, name: 'q' }), null)
  assert.equal(shortcutAction({ ctrl: true, name: 'c' }), 'copy')
})

test('file and song shortcuts accept Control and Command', () => {
  for (const name of ['s', 'o', 'n', 't', 'k']) {
    assert.equal(shortcutAction({ ctrl: true, name }), ({ s: 'save', o: 'open', n: 'new', t: 'song', k: 'command' })[name])
    assert.equal(shortcutAction({ meta: true, name }), ({ s: 'save', o: 'open', n: 'new', t: 'song', k: 'command' })[name])
  }
})

test('Shift and Control/Command arrows fast-travel eight cells outside selection mode', () => {
  assert.equal(movementDistance({ shift: true }, { shift: true }, false), 8)
  assert.equal(movementDistance({ ctrl: true }, {}, false), 8)
  assert.equal(movementDistance({ meta: true }, {}, false), 8)
  assert.equal(movementDistance({}, {}, false), 1)
})

test('selection mode keeps arrow movement one cell at a time', () => {
  assert.equal(movementDistance({ shift: true }, { shift: true }, true), 1)
  assert.equal(movementDistance({ ctrl: true }, {}, true), 1)
})
