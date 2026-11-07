'use strict'

// | (mute): bang it to write # down both sides of the block below, which mutes
// that block with classic # comments. Bang it again to remove them.

const test = require('node:test')
const assert = require('node:assert')
const fs = require('node:fs')
const path = require('node:path')
const vm = require('node:vm')

function engine (rows) {
  const sent = []
  const client = {
    io: {
      midi: { push: (...a) => { sent.push(a) }, pushAbsolute () {}, run () {}, runItem () {}, transpose: () => null, scheduleSequence () {}, updateSequence () {} },
      cc: { stack: { push () {} }, run () {} },
      mono: { push () {}, run () {} },
      udp: { push () {}, run () {} },
      osc: { push () {}, run () {} }
    },
    commander: { trigger () {} },
    clock: { speed: { value: 120 }, scheduleBurst () {}, cancelBurst () {} }
  }
  const ctx = vm.createContext({ Math, client })
  const dir = path.join(__dirname, '..', 'src', 'core')
  const names = { transpose: 'transposeTable', operator: 'Operator', library: 'library', orca: 'Orca' }
  for (const f of Object.keys(names)) vm.runInContext(fs.readFileSync(path.join(dir, f + '.js'), 'utf8') + `\nglobalThis.${names[f]} = ${names[f]}`, ctx)
  const orca = new ctx.Orca(ctx.library)
  const w = rows[0].length
  orca.load(w, rows.length, rows.join(''), 0)
  const grid = () => orca.s.match(new RegExp(`.{${w}}`, 'g'))
  return { orca, sent, grid, lib: ctx.library }
}

test('a bang hashes out both sides of the block', () => {
  const e = engine([
    '............',
    '...52|*.....',
    '......2C4...',
    '............',
    '............'
  ])
  e.orca.run()
  // muted on the same frame as the bang, so C never ran
  assert.deepStrictEqual(e.grid().slice(2, 5), [
    '.....#2C4..#',
    '.....#.....#',
    '............'
  ])
})

test('a second bang removes the hashes and the block runs again that frame', () => {
  const e = engine([
    '............',
    '...52|*.....',
    '.....#2C4..#',
    '.....#.....#'
  ])
  e.orca.run()
  assert.deepStrictEqual(e.grid().slice(2), ['......2C4...', '.......0....'])
})

test('a muted block stops running and resumes when unmuted', () => {
  const e = engine([
    '............',
    '...31|......',
    '......1C9...',
    '............'
  ])
  const counter = () => e.grid()[3][7]
  e.orca.run(); e.orca.run()
  assert.strictEqual(counter(), '1')
  e.orca.write(6, 1, '*'); e.orca.run()
  const frozen = counter()
  for (let i = 0; i < 4; i++) e.orca.run()
  assert.strictEqual(counter(), frozen, 'counter is frozen while muted')
  e.orca.write(6, 1, '*'); e.orca.run(); e.orca.run(); e.orca.run()
  assert.notStrictEqual(counter(), frozen, 'counter runs again after unmuting')
})

test('a muted MIDI note is silent until unmuted', () => {
  const e = engine([
    '............',
    '...62|*.....',
    '......D1....',
    '.......:03C.'
  ])
  e.orca.run()
  assert.strictEqual(e.grid()[2][5], '#')
  e.sent.length = 0
  for (let i = 0; i < 6; i++) e.orca.run()
  assert.strictEqual(e.sent.length, 0, 'no notes while muted')
  e.orca.write(6, 1, '*'); e.orca.run()
  for (let i = 0; i < 3; i++) e.orca.run()
  assert.ok(e.sent.length > 0, 'notes play again once unmuted')
})

test('a D above toggles it every 8 frames', () => {
  const e = engine([
    '.....D......',
    '............',
    '...21|......',
    '......abc...'
  ])
  const left = () => e.grid()[3][5]
  const states = []
  for (let f = 0; f < 17; f++) { e.orca.run(); states.push(left()) }
  assert.strictEqual(states[0], '#')
  assert.strictEqual(states[7], '#')
  assert.strictEqual(states[8], '.')
  assert.strictEqual(states[16], '#')
})

test('empty inlets mute 8 wide and 4 tall', () => {
  const e = engine([
    '..............',
    '...|*.........',
    '..............',
    '..............',
    '..............',
    '..............',
    '..............'
  ])
  e.orca.run()
  const g = e.grid()
  for (let y = 2; y <= 5; y++) assert.strictEqual(g[y][3] + g[y][12], '##', `row ${y}`)
  assert.strictEqual(g[6][3] + g[6][12], '..')
})

test('it does nothing without a bang', () => {
  const e = engine(['............', '...32|......', '......2C4...', '............'])
  for (let i = 0; i < 5; i++) e.orca.run()
  assert.ok(!e.orca.s.includes('#'))
})

test('the hash columns are shown as its ports', () => {
  const e = engine(['............', '...21|......', '............', '............'])
  const op = new e.lib['|'](e.orca, 5, 1, true)
  const cells = op.getPorts().map(p => `${p[0]},${p[1]}`)
  for (const c of ['3,1', '4,1', '5,2', '8,2']) assert.ok(cells.includes(c), c)
})

test('it can be built without a grid, as the operator guide does', () => {
  const e = engine(['....'])
  const op = new e.lib['|']()
  assert.strictEqual(op.name, 'mute')
})
