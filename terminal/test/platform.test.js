'use strict'

const test = require('node:test')
const assert = require('node:assert')
const os = require('node:os')
const path = require('node:path')
const platform = require('../src/platform')

test('save dir defaults to ~/Documents/Orca Seaflake', () => {
  const prev = process.env.ORCA_SEAFLAKE_DIR
  delete process.env.ORCA_SEAFLAKE_DIR
  assert.strictEqual(platform.saveDir(), path.join(os.homedir(), 'Documents', 'Orca Seaflake'))
  if (prev !== undefined) process.env.ORCA_SEAFLAKE_DIR = prev
})

test('ORCA_SEAFLAKE_DIR overrides the save dir', () => {
  const prev = process.env.ORCA_SEAFLAKE_DIR
  process.env.ORCA_SEAFLAKE_DIR = 'some/dir'
  assert.strictEqual(platform.saveDir(), path.resolve('some/dir'))
  if (prev === undefined) delete process.env.ORCA_SEAFLAKE_DIR
  else process.env.ORCA_SEAFLAKE_DIR = prev
})

test('loadMidi never throws', () => {
  const r = platform.loadMidi()
  assert.ok(r.midi || r.error)
})

test('every OS has a MIDI setup hint', () => {
  assert.ok(platform.midiSetupHint().length > 0)
})
