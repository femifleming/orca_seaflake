'use strict'

// The original Orca operators must behave exactly like upstream Hundred Rabbits
// Orca, including when inputs are left empty. Expected outputs below were
// recorded from upstream's engine (github.com/hundredrabbits/Orca).

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

test('C with an empty modulus outputs nothing', () => {
  assert.strictEqual(below(run(['.....', '.4C..', '.....'])), '.')
})
test('M with an empty side multiplies by 0', () => {
  assert.strictEqual(below(run(['.....', '.3M..', '.....'])), '0')
})
test('I with empty step stays at 0', () => {
  const r = run(['.....', '..I..', '.....'], 3)
  assert.deepStrictEqual([0, 1, 2].map(f => below(r, f)), ['0', '0', '0'])
})
test('U with empty step never bangs', () => {
  const r = run(['.....', '..U..', '.....'], 4)
  assert.ok([0, 1, 2, 3].every(f => below(r, f) === '.'))
})
test('Z with empty rate does not move', () => {
  const r = run(['.....', '..Z3.', '.....'], 3)
  assert.deepStrictEqual([0, 1, 2].map(f => below(r, f)), ['0', '0', '0'])
})
test('R with an empty side picks within 0..left', () => {
  for (let i = 0; i < 200; i++) {
    const v = parseInt(below(run(['.....', '.5R..', '.....'])), 36)
    assert.ok(v >= 0 && v <= 5, `got ${v}`)
  }
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
