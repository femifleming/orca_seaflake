'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const Orca = require('../src/core/orca')

test('grid accepts inline BPM directive characters without parsing them as operators', () => {
  const Operator = function (orca, x, y, passive) { this.x = x; this.y = y; this.passive = passive; this.ports = {} }
  const library = Object.fromEntries('abcdefghijklmnopqrstuvwxyz0123456789'.split('').map(g => [g, Operator]))
  const orca = new Orca(library)
  orca.load(12, 1, '$bpm: 120a...')
  assert.equal(orca.format().split('\n')[0].slice(0, 10), '$bpm: 120a')
  assert.equal(orca.isAllowed('$'), true)
  assert.equal(orca.isAllowed(':'), true)
  assert.equal(orca.isAllowed(' '), true)
  assert.equal(orca.parse().length, 1)
  assert.equal(orca.parse()[0].x, 9)
})
