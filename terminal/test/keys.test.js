'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const { shortcutAction, movementDistance, arrowDirection } = require('../src/keys')

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

test('Shift, Ctrl and Option/Alt arrows leap eight cells; plain arrows move one', () => {
  assert.equal(movementDistance({ name: 'right', shift: true }), 8)
  assert.equal(movementDistance({ name: 'right', ctrl: true }), 8)
  assert.equal(movementDistance({ name: 'right', meta: true }), 8)
  assert.equal(movementDistance({ name: 'right' }), 1)
})

test('Option+Left/Right sent as ESC b / ESC f count as arrows', () => {
  assert.equal(arrowDirection({ name: 'b', meta: true }), 'left')
  assert.equal(arrowDirection({ name: 'f', meta: true }), 'right')
  assert.equal(arrowDirection({ name: 'b' }), null)
  assert.equal(arrowDirection({ name: 'up' }), 'up')
})
