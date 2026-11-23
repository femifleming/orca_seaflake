// Runs a patch through the original Orca Seaflake JS engine
// (orca-seaflake/terminal/src/core) and prints the grid after every frame.
// usage: node run_js.js <seaflake repo> <patch.orca> <frames> <w> <h>
'use strict'
const fs = require('fs'), path = require('path'), vm = require('vm')
const [repo, patchFile, framesArg, wArg, hArg] = process.argv.slice(2)
let rng = 0x5EAF1A4E >>> 0
function rnd () { // xorshift32, the same as sf_core.c
  rng ^= (rng << 13) >>> 0; rng >>>= 0
  rng ^= rng >>> 17
  rng ^= (rng << 5) >>> 0; rng >>>= 0
  return rng
}
const M = Object.create(Math); M.random = () => rnd() / 4294967296
const noop = () => {}
const client = { clock: { speed: { value: 120 }, scheduleBurst: noop, cancelBurst: noop },
  io: { midi: { push: noop, pushAbsolute: () => ({}), run: noop, runItem: noop, transpose: () => ({ id: 60, value: 0 }), scheduleSequence: noop, updateSequence: noop },
    cc: { stack: [], run: noop }, mono: { push: noop, run: noop }, udp: { push: noop, run: noop }, osc: { push: noop, run: noop } },
  commander: { trigger: noop } }
const ctx = vm.createContext({ console, Math: M, Number, String, Object, Array, Set, Map, client, isNaN, parseInt })
for (const f of ['transpose.js', 'operator.js', 'library.js', 'orca.js']) {
  let src = fs.readFileSync(path.join(repo, 'terminal/src/core', f), 'utf8')
  const name = { 'transpose.js': 'transposeTable', 'operator.js': 'Operator', 'library.js': 'library', 'orca.js': 'Orca' }[f]
  vm.runInContext(src + `\nglobalThis.${name} = ${name};`, ctx, { filename: f })
}
const w = +wArg, h = +hArg
const lines = fs.readFileSync(patchFile, 'utf8').replace(/\r/g, '').split('\n')
let s = ''
for (let y = 0; y < h; y++) s += ((lines[y] || '') + '.'.repeat(w)).slice(0, w).replace(/ /g, '.')
const orca = new ctx.Orca(ctx.library)
orca.load(w, h, s, 0)
for (let f = 0; f < +framesArg; f++) {
  orca.run()
  process.stdout.write(`frame ${f}\n${orca.format()}`)
}
