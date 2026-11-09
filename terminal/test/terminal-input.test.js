'use strict'

// Drives the real app with the byte sequences terminals actually send, then
// saves and checks the grid. Guards against mouse reports typing glyphs and
// against fast travel (leap) breaking.

const test = require('node:test')
const assert = require('node:assert')
const fs = require('node:fs')
const os = require('node:os')
const path = require('node:path')
const { spawn } = require('node:child_process')

const bin = path.join(__dirname, '..', 'bin', 'orca-seaflake')
const wait = ms => new Promise(resolve => setTimeout(resolve, ms))

async function session (patchText, steps) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'orca-seaflake-'))
  const patch = path.join(dir, 'p.orca')
  fs.writeFileSync(patch, patchText)
  const child = spawn(process.execPath, [bin, patch], { env: { ...process.env, ORCA_SEAFLAKE_DIR: dir }, stdio: ['pipe', 'pipe', 'pipe'] })
  let out = ''
  child.stdout.on('data', d => { out += d })
  child.stderr.on('data', d => { out += d })
  const exited = new Promise(resolve => child.on('exit', code => resolve(code)))
  await wait(1200)
  for (const s of steps) { child.stdin.write(s); await wait(80) }
  child.stdin.write('\x13') // Ctrl-S
  await wait(200)
  child.stdin.write('\x11') // Ctrl-Q
  const code = await Promise.race([exited, wait(10000).then(() => 'timeout')])
  if (code === 'timeout') child.kill()
  assert.strictEqual(code, 0, out.slice(-2000))
  return fs.readFileSync(patch, 'utf8').split('\n')
}

const empty = Array(10).fill('.'.repeat(40)).join('\n') + '\n'

test('mouse movement, clicks and wheel never write into the grid', async () => {
  const rows = await session(empty, [
    '\x1b[<35;10;5M\x1b[<35;11;5M\x1b[<35;12;6M', // any-motion reports
    '\x1b[<64;5;5M\x1b[<65;5;5M', // wheel
    '\x1b[<0;3;4M\x1b[<0;3;4m', // click on cell 0,0
    '\x1b[M #$' // X10 report
  ])
  assert.ok(rows.every(r => /^\.*$/.test(r)), rows.join('\n'))
})

test('Shift, Option and Ctrl arrows leap eight cells', async () => {
  const rows = await session(empty, [
    '\x1b[1;2C', 'A', // Shift+Right -> x 8
    '\x1b[1;2B', 'B', // Shift+Down from x 8 -> y 8
    '\x1b[1;3A', '\x1bb', 'C', // Option+Up, Option+Left (ESC b) -> 0,0
    '\x1b[1;5C', '\x1b[1;5C', 'D', // Ctrl+Right twice -> x 16
    '\x1b\x1b[D', 'E' // ESC ESC [D (Terminal.app Option+Left) -> x 8
  ])
  assert.strictEqual(rows[0][8], 'E', rows.join('\n'))
  assert.strictEqual(rows[0][0], 'C')
  assert.strictEqual(rows[0][16], 'D')
  assert.strictEqual(rows[8][8], 'B')
})

test('End, Home and PageDown jump across the grid; + writes the operator', async () => {
  const rows = await session(empty, ['\x1b[F', '+', '\x1b[H', '\x1b[6~', 'x'])
  assert.strictEqual(rows[0][rows[0].length - 1], '+', rows.join('\n'))
  assert.ok(rows.slice(1).some(r => r[0] === 'x'), rows.join('\n'))
})

test('bracketed paste places a block at the cursor', async () => {
  const rows = await session(empty, ['\x1b[200~AB\nCD\x1b[201~'])
  assert.strictEqual(rows[0].slice(0, 2), 'AB')
  assert.strictEqual(rows[1].slice(0, 2), 'CD')
})
