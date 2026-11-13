'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const { parse } = require('../src/input')

const keys = s => parse(s, true).events.filter(e => e.type === 'key')
const one = s => { const e = parse(s, true).events; assert.equal(e.length, 1, JSON.stringify(e)); return e[0] }

test('mouse reports never turn into typed characters', () => {
  // SGR (1006), X10 and urxvt (1015) reports, including motion and wheel
  const reports = '\x1b[<35;10;5M\x1b[<0;10;5M\x1b[<32;11;5M\x1b[<0;11;5m\x1b[<64;3;3M\x1b[M #$\x1b[32;10;5M'
  assert.deepEqual(keys(reports), [])
  const mice = parse(reports, true).events.filter(e => e.type === 'mouse')
  assert.equal(mice.length, 6)
  assert.deepEqual(mice[1], { ...mice[1], button: 0, x: 10, y: 5, release: false, drag: false })
  assert.equal(mice[2].drag, true)
  assert.equal(mice[3].release, true)
  assert.equal(mice[4].wheel, true)
})

test('a mouse report split across reads is held until complete', () => {
  const first = parse('\x1b[<35;1')
  assert.deepEqual(first.events, [])
  assert.equal(first.rest, '\x1b[<35;1')
  const second = parse(first.rest + '0;5M')
  assert.equal(second.events[0].type, 'mouse')
})

test('unknown sequences and terminal replies are swallowed whole', () => {
  assert.deepEqual(keys('\x1b[I\x1b[O\x1b[?1;2c\x1b[>0;95;0c\x1b[99;99X'), [])
})

test('arrows with every modifier encoding', () => {
  assert.deepEqual(one('\x1b[A'), { ...one('\x1b[A'), name: 'up', shift: false, ctrl: false, meta: false })
  assert.equal(one('\x1b[1;2C').shift, true)
  assert.equal(one('\x1b[1;5C').ctrl, true)
  assert.equal(one('\x1b[1;3C').meta, true)
  assert.equal(one('\x1b[1;9C').meta, true) // Cmd/Super in kitty-style terminals
  assert.equal(one('\x1b[5C').ctrl, true)
  assert.equal(one('\x1b[a').shift, true) // rxvt
  assert.equal(one('\x1bOB').name, 'down') // application cursor mode
  const opt = one('\x1b\x1b[A') // Terminal.app Option+Up
  assert.equal(opt.name, 'up'); assert.equal(opt.meta, true)
  const optLeft = one('\x1bb') // Option+Left in Terminal.app / iTerm2
  assert.equal(optLeft.name, 'b'); assert.equal(optLeft.meta, true)
})

test('navigation keys', () => {
  assert.equal(one('\x1b[5~').name, 'pageup')
  assert.equal(one('\x1b[6~').name, 'pagedown')
  assert.equal(one('\x1b[H').name, 'home')
  assert.equal(one('\x1b[4~').name, 'end')
  assert.equal(one('\x1b[3~').name, 'delete')
  assert.equal(one('\x1bOQ').name, 'f2')
})

test('CSI u keys (iTerm2 / kitty) decode to the right key, not stray text', () => {
  const ctrlC = one('\x1b[99;5u')
  assert.equal(ctrlC.name, 'c'); assert.equal(ctrlC.ctrl, true)
  assert.equal(one('\x1b[13u').name, 'return')
  assert.equal(one('\x1b[27u').name, 'escape')
  const shiftA = one('\x1b[97;2u')
  assert.equal(shiftA.str, 'A')
})

test('control keys and plain characters', () => {
  assert.equal(one('\x11').name, 'q'); assert.equal(one('\x11').ctrl, true)
  assert.equal(one('\x7f').name, 'backspace')
  assert.equal(one('\r').name, 'return')
  assert.equal(one('\t').name, 'tab')
  assert.equal(one(' ').name, 'space')
  assert.equal(one('\x1f').name, '/')
  const A = one('A'); assert.equal(A.str, 'A'); assert.equal(A.shift, true)
  assert.equal(one('+').str, '+')
})

test('a lone Escape waits for more input, then becomes the Escape key', () => {
  assert.deepEqual(parse('\x1b').events, [])
  assert.equal(parse('\x1b', true).events[0].name, 'escape')
})

test('bracketed paste arrives as one paste event, even across reads', () => {
  const r = parse('\x1b[200~ABC\nD')
  assert.deepEqual(r.events, [])
  const done = parse(r.rest + 'E\x1b[201~x')
  assert.deepEqual(done.events[0], { type: 'paste', text: 'ABC\nDE' })
  assert.equal(done.events[1].str, 'x')
})
