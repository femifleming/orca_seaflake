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
