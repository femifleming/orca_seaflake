'use strict'

// The outputs the desktop app has besides MIDI notes, for the terminal:
// UDP (`;`), OSC (`=`), MIDI control change and pitch bend (`!`, `?`) and
// monophonic MIDI notes (`%`). Each keeps a stack that the operators push
// to during a frame; run() sends the stack at the start of the next frame
// (or at once when an operator is banged with `*`) and empties it, which is
// what the desktop client's per-frame clear() does.
//
// UDP and OSC use Node's own dgram, so there is nothing to compile. OSC
// messages are encoded here (address, type tags, big-endian int32 arguments)
// the same way node-osc encodes the desktop app's.

const dgram = require('node:dgram')

const DEFAULT_IP = '127.0.0.1'
const UDP_PORT = 49161 // Pilot (and Pilot Sealflake) listen here
const OSC_PORT = 49162

function validPort (port) {
  const n = Number(port)
  return Number.isInteger(n) && n >= 1000 && n <= 65535 ? n : null
}

class Socket {
  constructor () { this.socket = null }
  open () {
    if (this.socket) return this.socket
    this.socket = dgram.createSocket('udp4')
    this.socket.on('error', () => {}) // an unreachable target is not a crash
    this.socket.unref() // never keeps the process alive on its own
    return this.socket
  }
  sendBuffer (buffer, port, ip) {
    try { this.open().send(buffer, port, ip, () => {}) } catch {}
  }
  close () { try { if (this.socket) this.socket.close() } catch {}; this.socket = null }
}

class Udp extends Socket {
  constructor (target) {
    super()
    this.target = target
    this.port = UDP_PORT
    this.stack = []
  }
  push (msg) { this.stack.push(`${msg}`) }
  run () {
    if (!this.stack.length) return
    for (const msg of this.stack) this.sendBuffer(Buffer.from(msg, 'utf8'), this.port, this.target.ip)
    this.stack = []
  }
  select (port) {
    const n = validPort(port)
    if (n === null) return false
    this.port = n
    return true
  }
}

function oscString (text) {
  const bytes = Buffer.from(`${text}\0`, 'utf8')
  return Buffer.concat([bytes, Buffer.alloc((4 - (bytes.length % 4)) % 4)])
}

// One OSC message: an address, a type-tag string and int32 arguments.
function encodeOsc (address, ints) {
  const args = Buffer.alloc(4 * ints.length)
  ints.forEach((v, i) => args.writeInt32BE(v | 0, 4 * i))
  return Buffer.concat([oscString(address), oscString(',' + 'i'.repeat(ints.length)), args])
}

class Osc extends Socket {
  constructor (target, valueOf) {
    super()
    this.target = target
    this.valueOf = valueOf // base-36 value of a glyph, from the engine
    this.port = OSC_PORT
    this.stack = []
  }
  push (path, msg) { this.stack.push({ path, msg: `${msg}` }) }
  run () {
    if (!this.stack.length) return
    for (const { path, msg } of this.stack) {
      const ints = [...msg].map(g => this.valueOf(g))
      this.sendBuffer(encodeOsc(path, ints), this.port, this.target.ip)
    }
    this.stack = []
  }
  select (port) {
    const n = validPort(port)
    if (n === null) return false
    this.port = n
    return true
  }
}

// `!` control change and `?` pitch bend, through the MIDI output.
class MidiCC {
  constructor (midi) {
    this.midi = midi
    this.offset = 64 // the desktop app's default CC offset: `!` knob 0 is CC 64
    this.stack = []
  }
  run () {
    if (!this.stack.length) return
    for (const msg of this.stack) {
      const ch = Number(msg.channel) & 15
      if (msg.type === 'cc' && !isNaN(msg.knob) && !isNaN(msg.value)) this.midi.send([0xb0 + ch, (this.offset + Number(msg.knob)) & 127, Number(msg.value) & 127])
      else if (msg.type === 'pb' && !isNaN(msg.lsb) && !isNaN(msg.msb)) this.midi.send([0xe0 + ch, Number(msg.lsb) & 127, Number(msg.msb) & 127])
      else if (msg.type === 'pg') {
        if (!isNaN(msg.bank)) this.midi.send([0xb0 + ch, 0, Number(msg.bank) & 127])
        if (!isNaN(msg.sub)) this.midi.send([0xb0 + ch, 32, Number(msg.sub) & 127])
        if (!isNaN(msg.pgm)) this.midi.send([0xc0 + ch, Number(msg.pgm) & 127])
      }
    }
    this.stack = []
  }
}

// `%`: one note at a time per channel. A new note on a channel releases the
// note that was playing there.
class Mono {
  constructor (midi) {
    this.midi = midi
    this.stack = {}
  }
  push (channel, octave, note, velocity, length, isPlayed = false) {
    if (this.stack[channel]) this.release(this.stack[channel])
    this.stack[channel] = { channel, octave, note, velocity, length, isPlayed }
  }
  run () {
    for (const id of Object.keys(this.stack)) {
      const item = this.stack[id]
      if (!item) continue
      if (item.length < 1) { this.release(item); continue }
      if (!item.isPlayed) { this.midi.trigger(item, true); item.isPlayed = true }
      item.length--
    }
  }
  release (item) {
    if (!item) return
    if (item.isPlayed) this.midi.trigger(item, false)
    delete this.stack[item.channel]
  }
  silence () { for (const id of Object.keys(this.stack)) this.release(this.stack[id]) }
}

module.exports = { Udp, Osc, MidiCC, Mono, encodeOsc, oscString, validPort, DEFAULT_IP, UDP_PORT, OSC_PORT }
