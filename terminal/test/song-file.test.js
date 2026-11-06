'use strict'

// .song files must open the same way in the terminal and the desktop app.
// The same real files are checked in desktop/scripts/test-song-mode.js.

const test = require('node:test')
const assert = require('node:assert')
const fs = require('node:fs')
const path = require('node:path')
const { parseSong, serializeSong } = require('../src/song-file')

const songs = path.join(__dirname, '..', '..', 'patches', 'songs')
const read = f => JSON.parse(fs.readFileSync(path.join(songs, f), 'utf8'))

test('current format (orcaSong 2) opens', () => {
  const s = parseSong(read('testingsequencing1.song'))
  assert.deepStrictEqual([s.slots[0].len, s.slots[0].mul, s.slots[0].bpm], [16, 3, 190])
  assert.strictEqual(s.slots.length, 32)
})

test('desktop exports from before 0.8.0 (orcaSong 1 with len/mul) keep their lengths', () => {
  const s = parseSong({ orcaSong: 1, mode: 0, loop: 1, slots: [{ len: 12, mul: 3, bpm: 140, p: ['..D4..'] }] })
  assert.deepStrictEqual([s.slots[0].len, s.slots[0].mul, s.slots[0].bpm], [12, 3, 140])
})

test('oldest format (bars + 16ths) converts to frames', () => {
  const s = parseSong({ orcaSong: 1, mode: 1, loop: 1, slots: [{ bars: 2, st: 4, bpm: 130, p: ['..D4..'] }] })
  assert.deepStrictEqual([s.slots[0].len, s.slots[0].mul, s.slots[0].bpm, s.mode], [36, 1, 130, 1])
})

test('oldest-format file saved by the desktop app opens', () => {
  const s = parseSong(read('untitled2.song'))
  assert.strictEqual(s.slots.filter(Boolean).length, 5)
  assert.deepStrictEqual([s.slots[0].len, s.slots[0].mul, s.slots[0].bpm], [17, 1, 90])
})

test('early pattern format (.orca-song) opens', () => {
  const s = parseSong(read('testingsongmode-26V04-811047.orca-song'))
  assert.strictEqual(s.slots.filter(Boolean).length, 6)
  assert.deepStrictEqual([s.slots[0].len, s.slots[0].bpm, s.slots[5].bpm], [12, 120, 60])
  const rows = s.slots[0].p.split('\n')
  assert.deepStrictEqual([rows.length, rows[0].length], [42, 78])
  assert.strictEqual(s.loop, true)
})

test('save then open gives back the same song', () => {
  const s = parseSong(read('testingsequencing1.song'))
  const again = parseSong(JSON.parse(serializeSong(s)))
  assert.deepStrictEqual(again, s)
  assert.strictEqual(JSON.parse(serializeSong(s)).orcaSong, 2)
})

test('rejects files that are not songs', () => {
  assert.throws(() => parseSong({ hello: 1 }), /Not a Seaflake song file/)
})
