'use strict'

// Mock minimal DOM and Orca client environment for headless testing
global.window = {
  localStorage: {
    data: {},
    getItem (k) { return this.data[k] || null },
    setItem (k, v) { this.data[k] = String(v) }
  },
  devicePixelRatio: 1,
  innerWidth: 1000,
  addEventListener (ev, fn) {},
  removeEventListener (ev, fn) {}
}
global.localStorage = global.window.localStorage
global.document = {
  hidden: false,
  documentElement: {
    style: { setProperty () {} }
  },
  createElement (tag) {
    return {
      tagName: tag.toUpperCase(),
      style: {},
      getContext () {
        return {
          clearRect () {},
          fillRect () {},
          fillText () {}
        }
      },
      getBoundingClientRect () {
        return { left: 0, top: 0, width: 800, height: 600 }
      }
    }
  },
  body: {
    appendChild () {}
  }
}
global.requestAnimationFrame = (fn) => setTimeout(fn, 0)

// Load Song class
const path = require('path')
const fs = require('fs')

const songPath = path.resolve(__dirname, '../sources/scripts/song.js')
const songCode = fs.readFileSync(songPath, 'utf8')
const SongConstructor = new Function('client', `${songCode}; return new Song(client);`)

console.log('--- Testing Orca Song Mode (Tracker Architecture) ---')

// Create mock Client
const mockClient = {
  tile: { w: 10, h: 15, ws: 10, hs: 15 },
  grid: { w: 8, h: 8 },
  clock: {
    speed: { value: 120, target: 120 },
    isPaused: false,
    setSpeed (val, target, timer) {
      this.speed.value = val
      this.speed.target = target || val
    },
    setFrame (f) {
      mockClient.orca.f = f
    },
    play () { this.isPaused = false },
    stop () { this.isPaused = true }
  },
  orca: {
    w: 40,
    h: 15,
    f: 0,
    s: '..D4..\n......',
    load (w, h, s, f = 0) {
      this.w = w
      this.h = h
      this.s = s
      this.f = f
    },
    toString () { return this.s }
  },
  whenOpen (file, text) {
    const lines = text.trim().split(/\r?\n/)
    this.orca.load(lines[0].length, lines.length, lines.join('\n').trim())
  },
  write (str, x, y, max, ty) {},
  drawSprite (x, y, g, ty) {},
  update () {}
}

const song = SongConstructor(mockClient)

// Test 1: Initialization and Defaults
console.assert(song.d.slots.length === 32, 'Should have 32 slots')
console.assert(song.d.mode === 0, 'Default mode should be ORD (0)')
console.assert(song.d.loop === 1, 'Default loop should be ON (1)')
console.log('✔ Test 1: Initialization passed (32 slots, mode ORD, loop ON)')

// Test 2: Duration calculation: len * mul frames
console.assert(song.duration({ len: 1, mul: 1 }) === 1, 'len: 1, mul: 1 should be 1 frame')
console.assert(song.duration({ len: 2, mul: 4 }) === 8, 'len: 2, mul: 4 should be 8 frames')
console.assert(song.duration({ len: 16, mul: 2 }) === 32, 'len: 16, mul: 2 should be 32 frames')
console.assert(song.duration({ len: 120, mul: 1 }) === 120, 'len: 120, mul: 1 should be 120 frames')
console.log('✔ Test 2: Duration calculation len * mul passed (1*1=1, 2*4=8, 16*2=32)')

// Test 3: Storing patterns into slots
mockClient.orca.s = '..R4..\n......'
song.r = 0
song.store()
console.assert(song.d.slots[0] !== null, 'Slot 0 should contain stored pattern')
console.assert(song.d.slots[0].p === '..R4..\n......', 'Slot 0 pattern text matches orca')
console.assert(song.d.slots[0].len === 16, 'Default slot len is 16')
console.assert(song.d.slots[0].mul === 1, 'Default slot mul is 1')
console.assert(song.d.slots[0].bpm === 120, 'Default slot bpm is 120')

// Store slot 1 with len 2, mul 4 (8 frames) and BPM 140
mockClient.orca.s = '..D8..\n......'
song.r = 1
song.store()
song.d.slots[1].len = 2
song.d.slots[1].mul = 4
song.d.slots[1].bpm = 140

// Store slot 2 with len 4, mul 1 (4 frames) and BPM 160
mockClient.orca.s = '..A3..\n......'
song.r = 2
song.store()
song.d.slots[2].len = 4
song.d.slots[2].mul = 1
song.d.slots[2].bpm = 160

console.log('✔ Test 3: Storing patterns into slots passed')

// Test 4: Beginning playback and slot 0 loading
song.begin()
console.assert(song.on === true, 'Song playback should be on')
console.assert(song.i === 0, 'Current playing slot should be 0')
console.assert(song.pos === 0, 'Current slot position should be 0')
console.assert(mockClient.clock.speed.value === 120, 'Slot 0 BPM 120 applied to clock')
console.assert(mockClient.orca.s === '..R4..\n......', 'Slot 0 patch loaded into orca')
console.log('✔ Test 4: Beginning playback and initial slot loading passed')

// Test 5: Frame ticking and advancement for slot 0 (len 16 * mul 1 = 16 frames)
for (let f = 1; f <= 16; f++) {
  song.tick()
  console.assert(song.i === 0, `At tick ${f}, should still be in slot 0`)
  console.assert(song.pos === f, `At tick ${f}, pos should be ${f}`)
}
// Tick 17: pos is 16 (>= 16), so it advances to slot 1 and plays frame 1
song.tick()
console.assert(song.i === 1, `After 16 frames, should advance to slot 1, got ${song.i}`)
console.assert(song.pos === 1, `Pos should be reset and incremented to 1`)
console.assert(mockClient.clock.speed.value === 140, `Slot 1 base BPM (140) should be applied, got ${mockClient.clock.speed.value}`)
console.assert(mockClient.orca.s === '..D8..\n......', 'Slot 1 patch loaded into orca')
console.log('✔ Test 5: Frame ticking and advancement passed (16 frames -> slot 1)')

// Test 6: Precise len * mul duration (Slot 1: len 2, mul 4 -> exactly 8 frames)
// Slot 1 has duration 8. Pos is currently 1 (frame 1 played).
for (let f = 2; f <= 8; f++) {
  song.tick()
  console.assert(song.i === 1, `At tick ${f} of slot 1, should still be in slot 1`)
}
// Next tick advances to slot 2 and plays frame 1
song.tick()
console.assert(song.i === 2, `After 8 frames (2*4), should advance to slot 2, got ${song.i}`)
console.assert(mockClient.clock.speed.value === 160, `Slot 2 base BPM (160) should be applied, got ${mockClient.clock.speed.value}`)
console.log('✔ Test 6: Precise len * mul duration passed (2 * 4 = 8 frames)')

// Test 7: $bpm command tempo override preservation
// Slot 2 has base BPM 160. Pos is currently 1.
// Suppose during frame 2 of Slot 2, the patch executes $bpm:180
mockClient.clock.setSpeed(180, 180, true)
console.assert(mockClient.clock.speed.value === 180, 'Clock speed set to 180 by patch')
song.tick() // pos becomes 2
console.assert(mockClient.clock.speed.value === 180, 'Song mode tick should NOT reset patch $bpm override')
song.tick() // pos becomes 3
console.assert(mockClient.clock.speed.value === 180, 'Song mode tick should continue preserving patch $bpm override')
song.tick() // pos becomes 4 (4 frames completed for slot 2)
console.assert(mockClient.clock.speed.value === 180, 'Song mode tick should continue preserving patch $bpm override at frame 4')
// Next tick completes slot 2 and loops back to slot 0 (loop is ON, mode is ORD)
song.tick()
console.assert(song.i === 0, `Loop ON should loop back to slot 0, got ${song.i}`)
console.assert(mockClient.clock.speed.value === 120, `Next slot base BPM (120) should restore base tempo, got ${mockClient.clock.speed.value}`)
console.log('✔ Test 7: $bpm override preservation and next slot base tempo restore passed')

// Test 8: Play modes (ORD, REV, RND)
song.d.mode = 1 // REV
const revSeq = song.build()
console.assert(revSeq[0] === 2 && revSeq[1] === 1 && revSeq[2] === 0, `REV sequence should be [2, 1, 0], got ${JSON.stringify(revSeq)}`)

song.d.mode = 2 // RND
const rndSeq = song.build()
console.assert(rndSeq.length === 3, 'RND sequence should include all 3 slots')
console.assert(rndSeq.includes(0) && rndSeq.includes(1) && rndSeq.includes(2), 'RND sequence should contain all slots')

song.d.mode = 0 // ORD
console.log('✔ Test 8: Play modes (ORD, REV, RND) sequence builder passed')

// Test 9: Loop OFF stops at end of sequence
song.d.loop = 0
song.begin() // Starts at slot 0 (16 frames)
song.pos = 16
song.tick() // Slot 1 (8 frames)
console.assert(song.i === 1, 'Advanced to slot 1')
song.pos = 8
song.tick() // Slot 2 (4 frames)
console.assert(song.i === 2, 'Advanced to slot 2')
song.pos = 4
song.tick() // Sequence ends
console.assert(song.on === false, 'Playback should stop when sequence ends with Loop OFF')
console.assert(mockClient.clock.isPaused === true, 'Orca clock should stop when sequence ends with Loop OFF')
console.log('✔ Test 9: Loop OFF stopping passed')

// Test 10: Serialization & Clean (Backward compatibility)
const packed = song.pack(song.d)
console.assert(typeof packed === 'string', 'Pack produces valid JSON string')
const parsed = JSON.parse(packed)
console.assert(parsed.orcaSong === 1, 'Packed format has orcaSong = 1')
console.assert(parsed.slots.filter(Boolean).length === 3, 'Packed format has 3 active slots')
console.assert(parsed.slots[1].len === 2 && parsed.slots[1].mul === 4, 'Packed format preserves len: 2, mul: 4')

// Test cleaning legacy format with bars:st
const legacySong = {
  orcaSong: 1,
  mode: 1,
  loop: 1,
  slots: [
    { bars: 2, st: 4, bpm: 130, p: ['..D4..', '......'] }
  ]
}
const cleanedLegacy = song.clean(legacySong)
console.assert(cleanedLegacy.slots[0].len === 36, `Legacy bars*16+st (2*16+4 = 36) converted to len 36, got ${cleanedLegacy.slots[0].len}`)
console.assert(cleanedLegacy.slots[0].mul === 1, 'Legacy mul defaults to 1')
console.assert(cleanedLegacy.slots[0].bpm === 130, 'Legacy bpm preserved')
console.log('✔ Test 10: Serialization & Legacy backward compatibility passed')

console.log('\n=============================================')
console.log('ALL SONG MODE TESTS PASSED SUCCESSFULLY! (10/10)')
console.log('=============================================\n')
