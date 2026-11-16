'use strict'

// Launches the real app, sends keystrokes through stdin, and checks that it
// draws, plays, saves and quits cleanly. Runs on macOS, Windows and Linux.

const test = require('node:test')
const assert = require('node:assert')
const fs = require('node:fs')
const os = require('node:os')
const path = require('node:path')
const { spawn, spawnSync } = require('node:child_process')

const bin = path.join(__dirname, '..', 'bin', 'orca-seaflake')
const version = require('../package.json').version

test('--version prints the package version', () => {
  const r = spawnSync(process.execPath, [bin, '--version'], { encoding: 'utf8' })
  assert.strictEqual(r.status, 0)
  assert.strictEqual(r.stdout.trim(), version)
})

test('--help explains usage', () => {
  const r = spawnSync(process.execPath, [bin, '--help'], { encoding: 'utf8' })
  assert.strictEqual(r.status, 0)
  assert.match(r.stdout, /Usage:/)
})

test('the MIDI module loads (native build succeeded)', () => {
  const r = spawnSync(process.execPath, [bin, '--midi'], { encoding: 'utf8' })
  assert.strictEqual(r.status, 0, r.stderr)
})

test('app opens a patch, plays, saves and quits with Ctrl-Q', async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'orca-seaflake-'))
  const patch = path.join(dir, 'smoke.orca')
  fs.writeFileSync(patch, '.D.....\n.......\n')
  const child = spawn(process.execPath, [bin, patch], { env: { ...process.env, ORCA_SEAFLAKE_DIR: dir }, stdio: ['pipe', 'pipe', 'pipe'] })
  let out = ''
  child.stdout.on('data', d => { out += d })
  child.stderr.on('data', d => { out += d })
  const wait = ms => new Promise(resolve => setTimeout(resolve, ms))
  const exited = new Promise(resolve => child.on('exit', code => resolve(code)))

  await wait(1500)
  child.stdin.write(' ') // play
  await wait(800)
  child.stdin.write('A') // edit a cell so the save has something new
  child.stdin.write('\x13') // Ctrl-S
  await wait(300)
  child.stdin.write('\x11') // Ctrl-Q
  const code = await Promise.race([exited, wait(10000).then(() => 'timeout')])
  if (code === 'timeout') child.kill()

  assert.strictEqual(code, 0, out.slice(-2000))
  assert.match(out, new RegExp(`TERMINAL ${version.replace(/\./g, '\\.')}`))
  assert.match(out, /PLAY/)
  assert.match(fs.readFileSync(patch, 'utf8'), /^A/)
})

// The second half types a patch in from an empty grid and listens for what
// the operators send. MIDI hardware is not needed: a missing MIDI output only
// changes the status line.
const dgram = require('node:dgram')
const BIN = bin
const UDP_PORT = 49261 // not Pilot's 49161: a running Pilot must not swallow the message
const OSC_PORT = 49262
const RIGHT = '\x1b[C'
const DOWN = '\x1b[B'
const HOME = '\x1b[H'

function listen (port) {
  return new Promise((resolve, reject) => {
    const socket = dgram.createSocket('udp4')
    const got = []
    socket.on('message', msg => got.push(msg))
    socket.on('error', reject)
    socket.bind(port, '127.0.0.1', () => resolve({ got, close: () => socket.close() }))
  })
}

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms))

test('app takes a typed patch, sends its UDP and OSC messages, saves and quits with Ctrl-Q', { timeout: 25000 }, async () => {
  const saveDir = fs.mkdtempSync(path.join(os.tmpdir(), 'orca-seaflake-smoke-'))
  const udp = await listen(UDP_PORT)
  const osc = await listen(OSC_PORT)
  const app = spawn(process.execPath, [BIN], { env: { ...process.env, ORCA_SEAFLAKE_DIR: saveDir }, stdio: ['pipe', 'pipe', 'pipe'] })
  let out = ''
  let err = ''
  app.stdout.on('data', d => { out += d })
  app.stderr.on('data', d => { err += d })
  const exited = new Promise(resolve => app.on('exit', code => resolve(code)))
  const type = async (text, wait = 40) => { app.stdin.write(text); await sleep(wait) }
  const frame = () => { const m = out.match(/FRAME (\d+)/g); return m ? Number(m[m.length - 1].slice(6)) : null }

  try {
    await sleep(1200)
    assert.match(out, /ORCA SEAFLAKE/, 'draws the header')
    assert.match(out, /┌/, 'draws the grid frame')

    // D writes its bang (*) into the cell below it, and an operator fires
    // when a * is next to it: D8 at (0,0) bangs ":04C" at (1,1), a MIDI note;
    // D1 at (0,2) bangs ";04C" at (1,3), UDP; D1 at (0,4) bangs "=a1" at
    // (1,5), OSC. Typing never moves the cursor, so Right follows each glyph.
    for (const row of ['D8', '.:04C', 'D1', '.;04C', 'D1', '.=a1']) {
      for (const g of row) { await type(g); await type(RIGHT) }
      await type(HOME + DOWN)
    }
    // Ctrl-K: point UDP and OSC at the test's own ports
    await type('\x0b'); await type(`udp: ${UDP_PORT}\r`, 100)
    assert.match(out, new RegExp(`UDP OUTPUT PORT ${UDP_PORT}`), 'udp: command answers')
    await type('\x0b'); await type(`osc: ${OSC_PORT}\r`, 100)
    assert.match(out, new RegExp(`OSC OUTPUT PORT ${OSC_PORT}`), 'osc: command answers')

    await type(' ', 600) // play
    const f0 = frame()
    await sleep(900)
    const f1 = frame()
    assert.ok(f0 !== null && f1 !== null && f1 > f0, `frames advance while playing (${f0} -> ${f1})`)
    await type(' ', 100) // stop

    assert.ok(udp.got.some(m => m.toString() === '04C'), `UDP message "04C" arrived (${udp.got.map(String).join(',')})`)
    const oscMsg = osc.got.find(m => m.toString('latin1').startsWith('/a\0'))
    assert.ok(oscMsg, 'OSC message to /a arrived')
    assert.equal(oscMsg.toString('latin1').slice(4, 8), ',i\0\0', 'OSC type tag: one int')
    assert.equal(oscMsg.readInt32BE(8), 1, 'OSC argument: value of "1"')

    await type('\x13', 200) // Ctrl-S
    const saved = fs.readdirSync(saveDir).filter(f => f.endsWith('.orca'))
    assert.equal(saved.length, 1, 'saved one patch')
    assert.ok(fs.readFileSync(path.join(saveDir, saved[0]), 'utf8').startsWith('D8'), 'the saved patch starts with D8')

    await type('\x11') // Ctrl-Q
    const code = await Promise.race([exited, sleep(5000).then(() => 'timeout')])
    assert.equal(code, 0, `exits with 0 (stderr: ${err.trim()})`)
    // The MIDI library (RtMidi) prints its own note when a computer has no
    // MIDI at all, as CI runners do: no ALSA sequencer, no openable Windows
    // port. Nothing else may reach stderr.
    const ours = err.split(/\r?\n/).filter(l => l.trim() && !/^(Midi(In|Out)\w*::|ALSA lib )/.test(l))
    assert.deepEqual(ours, [], 'nothing of ours on stderr')
  } finally {
    if (app.exitCode === null) app.kill('SIGKILL')
    udp.close(); osc.close()
    fs.rmSync(saveDir, { recursive: true, force: true })
  }
})
