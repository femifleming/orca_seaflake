'use strict'
// The outputs besides MIDI notes, checked without any real port: what each
// operator's push turns into on the wire.
const test = require('node:test')
const assert = require('node:assert/strict')
const { Udp, Osc, MidiCC, Mono, encodeOsc, oscString, validPort, UDP_PORT, OSC_PORT } = require('../src/io')

const keys = '0123456789abcdefghijklmnopqrstuvwxyz'
const valueOf = g => (!g || g === '.' || g === '*') ? 0 : keys.indexOf(String(g).toLowerCase())

function capture (client) {
  const sent = []
  client.sendBuffer = (buffer, port, ip) => sent.push({ buffer, port, ip })
  return sent
}

test('OSC strings are NUL-terminated and padded to four bytes', () => {
  assert.equal(oscString('/a').length, 4)
  assert.equal(oscString('/abc').length, 8)
  assert.deepEqual([...oscString('/a')], [47, 97, 0, 0])
})

test('an OSC message is address, type tags and big-endian int32 arguments', () => {
  const b = encodeOsc('/a', [1, 35, 0])
  assert.equal(b.toString('latin1', 0, 4), '/a\0\0')
  assert.equal(b.toString('latin1', 4, 12), ',iii\0\0\0\0')
  assert.deepEqual([b.readInt32BE(12), b.readInt32BE(16), b.readInt32BE(20)], [1, 35, 0])
})

test('`;` sends its message as text to the UDP port, then empties its stack', () => {
  const udp = new Udp({ ip: '127.0.0.1' })
  const sent = capture(udp)
  assert.equal(udp.port, UDP_PORT)
  udp.push('04C'); udp.push('bpm140')
  udp.run()
  assert.deepEqual(sent.map(s => [s.buffer.toString(), s.port, s.ip]), [['04C', 49161, '127.0.0.1'], ['bpm140', 49161, '127.0.0.1']])
  udp.run()
  assert.equal(sent.length, 2, 'nothing is sent twice')
})

test('`=` sends an OSC message whose arguments are the base-36 values of the glyphs', () => {
  const osc = new Osc({ ip: '10.0.0.2' }, valueOf)
  const sent = capture(osc)
  assert.equal(osc.port, OSC_PORT)
  osc.push('/a', '1z.')
  osc.run()
  assert.equal(sent.length, 1)
  assert.equal(sent[0].ip, '10.0.0.2')
  assert.equal(sent[0].port, 49162)
  assert.deepEqual(sent[0].buffer, encodeOsc('/a', [1, 35, 0]))
})

test('udp: and osc: accept ports from 1000 to 65535 only', () => {
  const udp = new Udp({ ip: '127.0.0.1' })
  assert.equal(validPort(999), null)
  assert.equal(validPort('49162'), 49162)
  assert.equal(udp.select(80), false)
  assert.equal(udp.port, UDP_PORT)
  assert.equal(udp.select(57120), true)
  assert.equal(udp.port, 57120)
})

test('`!` and `?` become control change and pitch bend bytes', () => {
  const bytes = []
  const cc = new MidiCC({ send: b => bytes.push(b) })
  cc.stack.push({ type: 'cc', channel: 1, knob: 2, value: 100 })
  cc.stack.push({ type: 'pb', channel: 0, lsb: 0, msb: 64 })
  cc.run()
  assert.deepEqual(bytes, [[0xb1, 66, 100], [0xe0, 0, 64]])
  assert.equal(cc.stack.length, 0)
})

test('`%` plays one note per channel: a new note releases the old one', () => {
  const events = []
  const mono = new Mono({ trigger: (item, down) => events.push([item.note, down]) })
  mono.push(0, 4, 'C', 15, 2)
  mono.run()
  assert.deepEqual(events, [['C', true]])
  mono.push(0, 4, 'E', 15, 2)
  assert.deepEqual(events, [['C', true], ['C', false]], 'the old note is released when the new one is pushed')
  mono.run()
  assert.deepEqual(events.at(-1), ['E', true])
  mono.run(); mono.run()
  assert.deepEqual(events.at(-1), ['E', false], 'released when its length runs out')
  mono.push(1, 4, 'G', 15, 9); mono.run()
  mono.silence()
  assert.deepEqual(events.at(-1), ['G', false])
})
