'use strict'

// Reads and writes Song Mode files. Kept in step with clean()/pack() in
// desktop/sources/scripts/song.js so .song files open in both apps.
//
// Accepted inputs:
//   { orcaSong: 2, slots: [{ len, mul, bpm, p }] }   current format (both apps)
//   { orcaSong: 1, slots: [{ len, mul, bpm, p }] }   desktop exports before 0.8.0
//   { orcaSong: 1, slots: [{ bars, st, bpm, p }] }   oldest format (bars + 16ths)
//   { format: 'orca-seaflake-song', patterns: [...] } early Song Mode builds

const SLOTS = 32
const clamp = (v, min, max) => Math.max(min, Math.min(max, v))
const MODES = { forward: 0, ordered: 0, order: 0, reverse: 1, backward: 1, random: 2, shuffle: 2 }

function patchText (p) {
  if (Array.isArray(p)) return p.join('\n')
  return typeof p === 'string' ? p : ''
}

function slotFrom (s) {
  if (!s) return null
  const p = patchText(s.p)
  if (!p.length) return null
  let len = 16
  let mul = 1
  // Decide by the fields present, not the version number: desktop builds
  // before 0.8.0 wrote len/mul under orcaSong 1.
  if (s.len !== undefined) { len = s.len | 0; mul = s.mul !== undefined ? s.mul | 0 : 1 } else if (s.bars !== undefined) { len = (s.bars | 0) * 16 + Math.min(15, s.st | 0) }
  return {
    len: clamp(len, 1, 999),
    mul: clamp(mul, 1, 999),
    bpm: clamp(s.bpm | 0 || 120, 60, 300),
    notes: String(s.notes || ''),
    p
  }
}

function slotFromPattern (pattern, songTempo) {
  if (!pattern || typeof pattern.patch !== 'string' || !pattern.patch.length) return null
  let p = pattern.patch
  const w = pattern.width | 0
  // Early builds stored the grid as one long line of width × height cells.
  if (!p.includes('\n') && w > 0 && p.length > w) p = p.match(new RegExp(`.{1,${w}}`, 'g')).join('\n')
  return {
    len: clamp(pattern.steps | 0, 1, 999),
    mul: 1,
    bpm: clamp((pattern.tempo | 0) || (songTempo | 0) || 120, 60, 300),
    notes: String(pattern.name || ''),
    p
  }
}

function parseSong (j) {
  if (!j || typeof j !== 'object') throw new Error('Not a Seaflake song file')
  let slots
  let mode = clamp(j.mode | 0, 0, 2)
  if (Array.isArray(j.slots)) {
    slots = j.slots.slice(0, SLOTS).map(slotFrom)
  } else if (Array.isArray(j.patterns)) {
    slots = j.patterns.slice(0, SLOTS).map(p => slotFromPattern(p, j.tempo))
    if (typeof j.order === 'string' && j.order.toLowerCase() in MODES) mode = MODES[j.order.toLowerCase()]
  } else {
    throw new Error('Not a Seaflake song file')
  }
  while (slots.length < SLOTS) slots.push(null)
  return { slots, mode, loop: !!j.loop }
}

function serializeSong (song) {
  return JSON.stringify({
    orcaSong: 2,
    mode: song.mode,
    loop: song.loop ? 1 : 0,
    slots: song.slots.map(s => s && { len: s.len, mul: s.mul, bpm: s.bpm, notes: s.notes || '', p: s.p.split('\n') })
  }, null, 1)
}

module.exports = { parseSong, serializeSong, SLOTS }
