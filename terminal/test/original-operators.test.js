'use strict'

// The original Orca operators must behave exactly like classic Hundred Rabbits
// Orca (the released desktop app), including its defaults for empty inputs:
// C and D count to 8, I steps by 1 up to 36, U plays 1 of 8, Z moves by 1 and
// R picks below 36. Expected outputs below were recorded from that engine.

const test = require('node:test')
const assert = require('node:assert')
const fs = require('node:fs')
const path = require('node:path')
const vm = require('node:vm')

function engine () {
  const sent = []
  const client = {
    io: {
      midi: { push: (...a) => { sent.push(['midi', ...a]) }, pushAbsolute () {}, run () {}, runItem () {}, transpose: () => null, scheduleSequence () {}, updateSequence () {} },
      cc: { stack: { push: m => sent.push(['cc', m]) }, run () {} },
      mono: { push: (...a) => sent.push(['mono', ...a]), run () {} },
      udp: { push () {}, run () {} },
      osc: { push () {}, run () {} }
    },
    commander: { trigger () {} },
    clock: { speed: { value: 120 }, scheduleBurst () {}, cancelBurst () {} }
  }
  const ctx = vm.createContext({ Math, client })
  const dir = path.join(__dirname, '..', 'src', 'core')
  const exportName = { transpose: 'transposeTable', operator: 'Operator', library: 'library', orca: 'Orca' }
  for (const f of ['transpose', 'operator', 'library', 'orca']) {
    vm.runInContext(fs.readFileSync(path.join(dir, f + '.js'), 'utf8') + `\nglobalThis.${exportName[f]} = ${exportName[f]}`, ctx)
  }
  return { ctx, sent }
}

// Runs `frames` frames and returns the grid rows plus anything sent out.
function run (rows, frames = 1) {
  const { ctx, sent } = engine()
  const w = rows[0].length
  const orca = new ctx.Orca(ctx.library)
  orca.load(w, rows.length, rows.join(''), 0)
  const out = []
  for (let i = 0; i < frames; i++) { sent.length = 0; orca.run(); out.push(orca.s.match(new RegExp(`.{${w}}`, 'g'))) }
  return { grids: out, sent: [...sent] }
}
const below = (r, frame = 0) => r.grids[frame][2][2]

test('D on its own bangs every 8 frames', () => {
  const r = run(['.....', '..D..', '.....'], 17)
  const bangs = r.grids.map((g, f) => g[2][2] === '*' ? f : -1).filter(f => f >= 0)
  assert.deepStrictEqual(bangs, [0, 8, 16])
})
test('D with a rate of 2 bangs every 16 frames', () => {
  const r = run(['.....', '.2D..', '.....'], 17)
  const bangs = r.grids.map((g, f) => g[2][2] === '*' ? f : -1).filter(f => f >= 0)
  assert.deepStrictEqual(bangs, [0, 16])
})
test('C with an empty modulus counts 0 to 7', () => {
  const r = run(['.....', '..C..', '.....'], 9)
  assert.deepStrictEqual(r.grids.map((g, f) => below(r, f)), ['0', '1', '2', '3', '4', '5', '6', '7', '0'])
})
test('M with an empty side multiplies by 0', () => {
  assert.strictEqual(below(run(['.....', '.3M..', '.....'])), '0')
})
test('I on its own counts up by 1', () => {
  const r = run(['.....', '..I..', '.....'], 3)
  assert.deepStrictEqual([0, 1, 2].map(f => below(r, f)), ['1', '2', '3'])
})
test('I with an empty modulus wraps at 36', () => {
  const r = run(['.....', '..I..', '..y..'], 2)
  assert.deepStrictEqual([0, 1].map(f => below(r, f)), ['z', '0'])
})
test('U on its own bangs 1 of every 8 frames', () => {
  const r = run(['.....', '..U..', '.....'], 16)
  const bangs = r.grids.map((g, f) => g[2][2] === '*' ? f : -1).filter(f => f >= 0)
  assert.deepStrictEqual(bangs, [0, 8])
})
test('Z with an empty rate moves by 1', () => {
  const r = run(['.....', '..Z3.', '.....'], 4)
  assert.deepStrictEqual([0, 1, 2, 3].map(f => below(r, f)), ['1', '2', '3', '3'])
})
test('L with an empty side outputs nothing', () => {
  assert.strictEqual(below(run(['.....', '.5L..', '.....'])), '.')
})
test('R with an empty max picks from min up to 35', () => {
  const seen = new Set()
  for (let i = 0; i < 400; i++) {
    const v = parseInt(below(run(['.....', '.5R..', '.....'])), 36)
    assert.ok(v >= 5 && v <= 35, `got ${v}`)
    seen.add(v)
  }
  assert.ok(seen.size > 20)
})
test(': with an empty octave is silent', () => {
  assert.deepStrictEqual(run(['D........', '.........', ':0.C.....', '.........']).sent, [])
})
test(': with an empty channel is silent', () => {
  assert.deepStrictEqual(run(['D........', '.........', ':.3C.....', '.........']).sent, [])
})
test(': with all inputs sends a note (velocity f, length 1 by default)', () => {
  assert.deepStrictEqual(run(['D........', '.........', ':03C.....', '.........']).sent, [['midi', 0, 3, 'C', 15, 1]])
})
test('% with an empty octave is silent', () => {
  assert.deepStrictEqual(run(['D........', '.........', '%0.C.....', '.........']).sent, [])
})
test('! with an empty channel is silent', () => {
  assert.deepStrictEqual(run(['D........', '.........', '!.12.....', '.........']).sent, [])
})
test('? with an empty channel is silent', () => {
  assert.deepStrictEqual(run(['D........', '.........', '?.12.....', '.........']).sent, [])
})
