'use strict'

/* global transposeTable */

function Midi (client) {
  this.mode = 0
  this.isClock = false

  this.outputIndex = -1
  this.inputIndex = -1

  this.outputs = []
  this.inputs = []
  this.stack = []
  this.sequences = new Map()

  this.start = function () {
    console.info('Midi Starting..')
    this.refresh()
  }

  this.clear = function () {
    this.stack = this.stack.filter((item) => { return item })
  }

  this.run = function () {
    for (const id in this.stack) {
      this.runItem(this.stack[id], id)
    }
  }

  this.runItem = function (item, id = this.stack.indexOf(item)) {
    if (!item) { return }
    if (item.isPlayed === false) { this.press(item) }
    if (item.length < 1) {
      this.release(item, id)
    } else {
      item.length--
    }
  }

  this.trigger = function (item, down) {
    if (!this.outputDevice()) { console.warn('MIDI', 'No midi output!'); return }

    const transposed = item.midiNote === undefined
      ? this.transpose(item.note, item.octave)
      : { id: item.midiNote }
    const channel = !isNaN(item.channel) ? parseInt(item.channel) : client.orca.valueOf(item.channel)

    if (!transposed) { return }

    const c = down === true ? 0x90 + channel : 0x80 + channel
    const n = transposed.id
    const v = parseInt((item.velocity / 16) * 127)

    if (!n || c === 127) { return }

    if (down === true && item.pitchBend !== undefined) {
      this.sendPitchBend(channel, item.pitchBend)
    }
    this.outputDevice().send([c, n, v])
    if (down !== true && item.pitchBend !== undefined) {
      this.sendPitchBend(channel, 0)
    }
  }

  // MIDI pitch bend is channel-wide and is sent in semitones around the
  // standard +/- 2 semitone range. Reset it after a tuned note is released.
  this.sendPitchBend = function (channel, semitones) {
    if (!this.outputDevice()) { return }
    const bend = clamp(Math.round(8192 + (semitones / 2) * 8192), 0, 16383)
    this.outputDevice().send([0xE0 + channel, bend & 0x7F, (bend >> 7) & 0x7F])
  }

  this.press = function (item) {
    if (!item) { return }
    this.trigger(item, true)
    item.isPlayed = true
  }

  this.release = function (item, id) {
    if (!item) { return }
    this.trigger(item, false)
    delete this.stack[id]
  }

  this.silence = function () {
    this.cancelSequences()
    for (const item of this.stack) {
      this.release(item)
    }
  }

  // High-resolution MIDI sequences use their own timers. They never advance or
  // modify Orca's frame counter, so the shared clock and D operator stay intact.
  this.scheduleSequence = function (id, sequence) {
    this.cancelSequence(id)
    const state = { timers: [], active: false, sequence, startedAt: performance.now(), emitted: 0 }
    this.sequences.set(id, state)
    this.rescheduleSequence(id, state)
  }

  this.updateSequence = function (id, sequence) {
    const state = this.sequences.get(id)
    if (!state) { return }
    Object.assign(state.sequence, sequence)
    this.rescheduleSequence(id, state)
  }

  this.rescheduleSequence = function (id, state) {
    state.timers.forEach(clearTimeout)
    state.timers = []

    const sequence = state.sequence
    const frameDuration = 60000 / client.clock.speed.value / 4
    const totalDuration = Math.max(1, sequence.duration) * frameDuration
    const elapsed = performance.now() - state.startedAt
    const eventCount = Math.max(0, Math.floor(sequence.repeats))

    for (let index = state.emitted; index < eventCount; index++) {
      const progress = eventCount < 2 ? 0 : index / eventCount
      let position = progress
      if (sequence.shape === 1) { position = 1 - Math.sqrt(1 - progress) }
      if (sequence.shape === 2) { position = Math.sqrt(progress) }
      if (sequence.shape === 3) {
        position = progress < 0.5
          ? Math.sqrt(progress / 2)
          : 1 - Math.sqrt((1 - progress) / 2)
      }
      if (sequence.shape === 4) { position = progress * sequence.pulsewidth }

      const at = Math.min(totalDuration - 1, Math.max(0, position * totalDuration))
      state.timers.push(setTimeout(() => {
        if (this.sequences.get(id) !== state || state.emitted !== index) { return }
        if (state.active) { this.trigger(sequence, false) }
        this.trigger(sequence, true)
        state.active = true
        state.emitted++
      }, Math.max(0, at - elapsed)))
    }

    state.timers.push(setTimeout(() => {
      if (this.sequences.get(id) !== state) { return }
      if (state.active) { this.trigger(sequence, false) }
      state.active = false
      this.sequences.delete(id)
    }, Math.max(1, totalDuration - elapsed)))
  }

  this.cancelSequence = function (id) {
    const state = this.sequences.get(id)
    if (!state) { return }
    state.timers.forEach(clearTimeout)
    if (state.active) { this.trigger(state.sequence, false) }
    this.sequences.delete(id)
  }

  this.cancelSequences = function () {
    for (const id of this.sequences.keys()) { this.cancelSequence(id) }
  }

  this.push = function (channel, octave, note, velocity, length, isPlayed = false) {
    const item = { channel, octave, note, velocity, length, isPlayed }
    return this.addItem(item)
  }

  this.pushAbsolute = function (channel, midiNote, velocity, length, pitchBend) {
    const item = { channel, midiNote, velocity, length, pitchBend, isPlayed: false }
    return this.addItem(item)
  }

  this.addItem = function (item) {
    const itemPitch = item.midiNote === undefined
      ? this.transpose(item.note, item.octave)
      : { id: item.midiNote }

    // Retrigger duplicates
    for (const id in this.stack) {
      const dup = this.stack[id]
      const duplicatePitch = dup.midiNote === undefined
        ? this.transpose(dup.note, dup.octave)
        : { id: dup.midiNote }
      if (itemPitch && duplicatePitch && dup.channel === item.channel && itemPitch.id === duplicatePitch.id) {
        this.release(dup, id)
      }
    }
    this.stack.push(item)
    return item
  }

  this.allNotesOff = function () {
    if (!this.outputDevice()) { return }
    console.log('MIDI', 'All Notes Off')
    for (let chan = 0; chan < 16; chan++) {
      this.outputDevice().send([0xB0 + chan, 123, 0])
    }
  }

  // Clock

  this.ticks = []

  this.sendClockStart = function () {
    if (!this.outputDevice()) { return }
    this.isClock = true
    this.outputDevice().send([0xFA], 0)
    console.log('MIDI', 'MIDI Start Sent')
  }

  this.sendClockStop = function () {
    if (!this.outputDevice()) { return }
    this.isClock = false
    this.outputDevice().send([0xFC], 0)
    console.log('MIDI', 'MIDI Stop Sent')
  }

  this.sendClock = function () {
    if (!this.outputDevice()) { return }
    if (this.isClock !== true) { return }

    const bpm = client.clock.speed.value
    const frameTime = (60000 / bpm) / 4
    const frameFrag = frameTime / 6

    for (let id = 0; id < 6; id++) {
      if (this.ticks[id]) { clearTimeout(this.ticks[id]) }
      this.ticks[id] = setTimeout(() => { this.outputDevice().send([0xF8], 0) }, parseInt(id) * frameFrag)
    }
  }

  this.receive = function (msg) {
    switch (msg.data[0]) {
      // Clock
      case 0xF8:
        client.clock.tap()
        break
      case 0xFA:
        console.log('MIDI', 'Start Received')
        client.clock.play(false, true)
        break
      case 0xFB:
        console.log('MIDI', 'Continue Received')
        client.clock.play()
        break
      case 0xFC:
        console.log('MIDI', 'Stop Received')
        client.clock.stop()
        break
    }
  }

  // Tools

  this.selectOutput = function (id) {
    if (id === -1) { this.outputIndex = -1; console.log('MIDI', 'Select Output Device: None'); return }
    if (!this.outputs[id]) { console.warn('MIDI', `Unknown device with id ${id}`); return }

    this.outputIndex = parseInt(id)
    console.log('MIDI', `Select Output Device: ${this.outputDevice().name}`)
  }

  this.selectInput = function (id) {
    if (this.inputDevice()) { this.inputDevice().onmidimessage = null }
    if (id === -1) { this.inputIndex = -1; console.log('MIDI', 'Select Input Device: None'); return }
    if (!this.inputs[id]) { console.warn('MIDI', `Unknown device with id ${id}`); return }

    this.inputIndex = parseInt(id)
    this.inputDevice().onmidimessage = (msg) => { this.receive(msg) }
    console.log('MIDI', `Select Input Device: ${this.inputDevice().name}`)
  }

  this.outputDevice = function () {
    return this.outputs[this.outputIndex]
  }

  this.inputDevice = function () {
    return this.inputs[this.inputIndex]
  }

  this.selectNextOutput = () => {
    this.outputIndex = this.outputIndex < this.outputs.length ? this.outputIndex + 1 : 0
    client.update()
  }

  this.selectNextInput = () => {
    const id = this.inputIndex < this.inputs.length - 1 ? this.inputIndex + 1 : -1
    this.selectInput(id)
    client.update()
  }

  // Setup

  this.refresh = function () {
    if (!navigator.requestMIDIAccess) { return }
    navigator.requestMIDIAccess().then(this.access, (err) => {
      console.warn('No Midi', err)
    })
  }

  this.access = (midiAccess) => {
    const outputs = midiAccess.outputs.values()
    this.outputs = []
    for (let i = outputs.next(); i && !i.done; i = outputs.next()) {
      this.outputs.push(i.value)
    }
    this.selectOutput(0)

    const inputs = midiAccess.inputs.values()
    this.inputs = []
    for (let i = inputs.next(); i && !i.done; i = inputs.next()) {
      this.inputs.push(i.value)
    }
    this.selectInput(-1)
  }

  // UI

  this.transpose = function (n, o = 3) {
    if (!transposeTable[n]) { return null }
    const octave = clamp(parseInt(o) + parseInt(transposeTable[n].charAt(1)), 0, 8)
    const note = transposeTable[n].charAt(0)
    const value = ['C', 'c', 'D', 'd', 'E', 'F', 'f', 'G', 'g', 'A', 'a', 'B'].indexOf(note)
    const id = clamp((octave * 12) + value + 24, 0, 127)
    return { id, value, note, octave }
  }

  this.convert = function (id) {
    const note = ['C', 'c', 'D', 'd', 'E', 'F', 'f', 'G', 'g', 'A', 'a', 'B'][id % 12]
    const octave = Math.floor(id / 12) - 5
    const name = `${note}${octave}`
    const key = Object.values(transposeTable).indexOf(name)
    return Object.keys(transposeTable)[key]
  }

  this.toString = function () {
    return !navigator.requestMIDIAccess ? 'No Midi Support' : this.outputDevice() ? `${this.outputDevice().name}` : 'No Midi Device'
  }

  this.toInputString = () => {
    return !navigator.requestMIDIAccess ? 'No Midi Support' : this.inputDevice() ? `${this.inputDevice().name}` : 'No Input Device'
  }

  this.toOutputString = () => {
    return !navigator.requestMIDIAccess ? 'No Midi Support' : this.outputDevice() ? `${this.outputDevice().name}` : 'No Output Device'
  }

  this.length = function () {
    return this.stack.length
  }

  function clamp (v, min, max) { return v < min ? min : v > max ? max : v }
}
