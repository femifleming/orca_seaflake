'use strict'

/* global localStorage requestAnimationFrame */

// Song mode: Orca patches as tracker patterns. Slots play in order, reverse or
// random, each for len * mul frames at its own base tempo. The panel overlays the right
// edge of the window on the Orca canvas, following the active theme and grid alignment.

function Song (client) {
  const W = 37 // panel width in tiles: 1 edge bar + 36
  const N = 32 // slots
  const FW = [3, 3, 3] // field widths: len (frames), mul (multiplier), bpm (tempo)
  const KEYS = ['len', 'mul', 'bpm']
  const MODES = ['ORD', 'REV', 'RND']
  const pad = (v, n) => `${v}`.padStart(n, '0')
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v))
  const read = (k) => { try { return JSON.parse(localStorage.getItem(k)) } catch (e) { return null } }
  // Duration in frames = len * mul (minimum 1 frame)
  const len = (s) => Math.max(1, Math.max(1, s.len | 0) * Math.max(1, s.mul | 0))
  const stop = (e) => { e.preventDefault(); e.stopPropagation() }
  const snip = (s) => (s.p.split('\n').find((l) => /[^.]/.test(l)) || '').slice(0, 20)
  const blank = () => ({ slots: Array(N).fill(null), mode: 0, loop: 1 })

  // Validates a song from localStorage or a file, returns null if it is not one
  const clean = (j) => {
    if (!j) { return null }
    // Support { slots: [...] } as well as legacy or custom format
    const srcSlots = Array.isArray(j.slots) ? j.slots : Array.isArray(j.rows) ? j.rows : null
    if (!srcSlots) { return null }
    const slots = Array(N).fill(null)
    srcSlots.slice(0, N).forEach((s, i) => {
      if (!s) { return }
      // Get patch string
      let p = s.p
      if (Array.isArray(p)) { p = p.join('\n') }
      if (!p && j.patterns && s.patternId && j.patterns[s.patternId]) {
        p = j.patterns[s.patternId].s
      }
      if (typeof p !== 'string' || !p.length || p.length > 250000) { return }
      const L = p.split(/\r?\n/)
      const w = Math.max(...L.map((l) => l.length))
      if (!w) { return }

      // Duration fields: len in frames, mul multiplier
      let frameLen = 16
      let mul = 1
      if (s.len !== undefined) {
        frameLen = clamp(s.len | 0, 1, 999)
        mul = clamp(s.mul !== undefined ? s.mul | 0 : 1, 1, 999)
      } else if (s.bars !== undefined) {
        // Convert legacy bars:16th to frames
        frameLen = clamp(s.bars * 16 + (s.st || 0), 1, 999)
        mul = 1
      } else if (s.length !== undefined) {
        frameLen = clamp(s.length | 0, 1, 999)
        mul = clamp(s.repeat !== undefined ? s.repeat | 0 : 1, 1, 999)
      }

      slots[i] = {
        len: frameLen,
        mul: mul,
        bpm: clamp(s.bpm | 0, 60, 300),
        p: L.map((l) => l.padEnd(w, '.')).join('\n')
      }
    })
    return { slots, mode: clamp(j.mode | 0, 0, 2), loop: j.loop ? 1 : 0 }
  }

  const pack = (d) => JSON.stringify({
    orcaSong: 1,
    mode: d.mode,
    loop: d.loop,
    slots: d.slots.map((s) => s && { len: s.len, mul: s.mul, bpm: s.bpm, p: s.p.split('\n') })
  }, null, 1)

  this.cols = 0 // slide progress in tiles
  this.open = false
  this.focus = false
  this.on = false
  this.d = clean(read('song_work')) || blank()
  this.r = 0 // cursor row
  this.f = 0 // cursor field (0: len, 1: mul, 2: bpm)
  this.n = 1 // song bank
  this.i = -1 // playing slot
  this.q = 0 // position in play order
  this.pos = 0 // frames played in slot
  this.seq = []
  this.buf = ''
  this.msg = ''

  this.save = () => { localStorage.setItem('song_work', JSON.stringify(this.d)) }
  this.clean = clean
  this.pack = pack
  this.duration = len
  this.play = () => { if (!this.on) { this.begin() } }
  this.stop = () => { this.on = false }
  this.pause = () => { this.on = false }

  this.commit = () => {
    const s = this.d.slots[this.r]
    if (s) {
      s.len = clamp(s.len | 0, 1, 999)
      s.mul = clamp(s.mul | 0, 1, 999)
      s.bpm = clamp(s.bpm | 0, 60, 300)
    }
    this.buf = ''
    this.save()
  }

  this.toggle = () => {
    this.open = !(this.open && this.focus)
    this.focus = this.open
    this.slide()
  }

  this.slide = () => {
    const to = this.open ? W : 0
    if (this.cols !== to) {
      this.cols += Math.sign(to - this.cols) * Math.min(4, Math.abs(to - this.cols))
      requestAnimationFrame(this.slide)
    }
    client.update()
  }

  this.build = () => {
    const q = this.d.slots.map((s, i) => s ? i : -1).filter((i) => i >= 0)
    if (this.d.mode === 1) { q.reverse() }
    if (this.d.mode === 2) {
      for (let i = q.length - 1; i > 0; i--) {
        const j = Math.floor(Math.random() * (i + 1));
        [q[i], q[j]] = [q[j], q[i]]
      }
    }
    return q
  }

  this.load = (i) => {
    const s = this.d.slots[i]
    if (!s) { this.on = false; return false }
    client.whenOpen(null, s.p)
    client.clock.setSpeed(s.bpm, s.bpm, true)
    return true
  }

  this.begin = () => {
    this.seq = this.build()
    this.q = 0
    if (!this.seq.length) { this.msg = 'NO SLOTS'; return }
    this.on = true
    client.clock.play()
    this.i = this.seq[0]
    this.pos = 0
    this.load(this.i)
  }

  this.tick = () => { // once per orca frame, before the grid runs
    if (!this.on) { return }
    const s = this.d.slots[this.i]
    if (!s) { this.on = false; return }
    if (this.pos >= len(s)) {
      if (++this.q >= this.seq.length) {
        if (!this.d.loop) { this.on = false; client.clock.stop(); return }
        this.seq = this.build()
        this.q = 0
      }
      this.i = this.seq[this.q]
      this.pos = 0
      if (!this.load(this.i)) { return }
    }
    this.pos++
  }

  this.store = () => {
    const s = this.d.slots[this.r] || (this.d.slots[this.r] = { len: 16, mul: 1, bpm: client.clock.speed.target })
    s.p = `${client.orca}`
    this.msg = `STORED ${pad(this.r + 1, 2)}`
    this.save()
  }

  // Song files (.song, JSON). In desktop app these use native macOS dialogs via IPC
  this.exportFile = async () => {
    this.commit()
    const content = pack(this.d)
    try {
      const ipc = window.require && window.require('electron').ipcRenderer
      if (ipc) {
        const result = await ipc.invoke('song:save-file', content)
        if (!result.canceled) {
          const name = result.filePath.split('/').pop().replace(/\.(song|json)$/i, '').toUpperCase()
          this.msg = `SAVED ${name}`
          client.update()
        }
        return
      }
    } catch (err) { console.warn('Song', 'Native save failed', err) }
    client.source.write('song', 'song', content, 'application/json')
  }

  this.importFile = async () => {
    try {
      const ipc = window.require && window.require('electron').ipcRenderer
      if (ipc) {
        const result = await ipc.invoke('song:open-file')
        if (!result.canceled) this.use(result.content)
        return
      }
    } catch (err) { console.warn('Song', 'Native open failed', err) }
    client.source.open('song', (file, text) => this.use(text))
  }

  this.use = (text) => {
    let j = null
    try { j = JSON.parse(text) } catch (err) { j = null }
    const d = clean(j)
    this.msg = d ? 'SONG LOADED' : 'NOT A SONG FILE'
    if (d) { this.d = d; this.on = false; this.r = 0; this.save() }
    this.open = true
    this.focus = true
    this.slide()
  }

  this.key = (e) => {
    if (!this.focus || e.metaKey || e.ctrlKey || e.altKey) { return }
    stop(e)
    this.msg = ''
    const k = e.key.length === 1 ? e.key.toLowerCase() : e.key
    const s = this.d.slots[this.r]
    const nav = { ArrowUp: [-1, 0], ArrowDown: [1, 0], ArrowLeft: [0, -1], ArrowRight: [0, 1] }[k]
    if (nav) {
      this.commit()
      this.r = clamp(this.r + nav[0], 0, N - 1)
      this.f = clamp(this.f + nav[1], 0, 2)
    }
    else if (k === 'Escape') {
      this.commit()
      this.focus = false
      this.open = false
      this.slide()
    }
    else if (/^\d$/.test(k) && s) {
      this.buf = (this.buf + k).slice(-FW[this.f])
      s[KEYS[this.f]] = +this.buf
      this.save()
    }
    else if ((k === '+' || k === '=' || k === '-') && s) {
      this.commit()
      s[KEYS[this.f]] += k === '-' ? -1 : 1
      this.commit()
    }
    else if (k === 'Enter') { this.store() }
    else if (k === 'Backspace' || k === 'Delete') { this.d.slots[this.r] = null; this.save() }
    else if (k === ' ') { if (this.on) { this.on = false } else { this.begin() } }
    else if (k === 'l' && s) { this.load(this.r); this.msg = `LOADED ${pad(this.r + 1, 2)}` }
    else if (k === 'm') { this.d.mode = (this.d.mode + 1) % 3; this.save() }
    else if (k === 'o') { this.d.loop ^= 1; this.save() }
    else if (k === '[' || k === ']') { this.n = clamp(this.n + (k === ']' ? 1 : -1), 1, 16) }
    else if (k === 'w') { localStorage.setItem(`song_${this.n}`, JSON.stringify(this.d)); this.msg = `SAVED SONG ${pad(this.n, 2)}` }
    else if (k === 'r') {
      const d = clean(read(`song_${this.n}`))
      if (d) { this.d = d; this.on = false; this.save(); this.msg = `LOADED SONG ${pad(this.n, 2)}` } else { this.msg = 'EMPTY SONG' }
    }
    else if (k === 'e') { this.exportFile() }
    else if (k === 'i') { this.importFile() }
    client.update()
  }

  this.click = (e) => {
    if (!this.open) { return }
    const o = client.orca
    const t = client.tile
    const vis = Math.min(o.w, Math.ceil((window.innerWidth - 60) / t.w))
    const x0 = Math.max(0, vis - this.cols)
    const rect = client.el.getBoundingClientRect()
    const mx = Math.floor((e.clientX - rect.left) / t.w)
    const my = Math.floor((e.clientY - rect.top) / t.h)
    if (mx < x0) {
      if (this.focus) { this.commit(); this.focus = false; client.update() }
      return
    }
    this.focus = true
    const rx = mx - (x0 + 1)
    if (my >= 5 && my < o.h) {
      const vr = Math.max(1, o.h - 7)
      const top = clamp(this.r - (vr >> 1), 0, Math.max(0, N - vr))
      const row = top + (my - 5)
      if (row >= 0 && row < N) {
        this.commit()
        this.r = row
        if (rx >= 4 && rx < 8) { this.f = 0 }
        else if (rx >= 8 && rx < 12) { this.f = 1 }
        else if (rx >= 12 && rx < 16) { this.f = 2 }
      }
    } else if (my === 0) {
      if (this.on) { this.on = false } else { this.begin() }
    } else if (my === 1) {
      if (rx < 9) { this.d.mode = (this.d.mode + 1) % 3; this.save() }
      else { this.d.loop ^= 1; this.save() }
    }
    client.update()
  }

  this.draw = () => {
    if (this.cols < 1) { return }
    const o = client.orca
    const t = client.tile
    const d = this.d
    const cur = d.slots[this.i]
    // right edge of what fits on screen, so the panel sits flush even when the patch is wider than the window
    const vis = Math.min(o.w, Math.ceil((window.innerWidth - 60) / t.w))
    const x0 = Math.max(0, vis - this.cols)
    const w = (s, x, y, ty) => client.write(s, x0 + 1 + x, y, 99, ty)
    client.context.clearRect(x0 * t.ws, 0, client.el.width, client.el.height)
    // Draw cyan edge vertical dividing bar
    for (let y = 0; y < o.h + 2; y++) { client.drawSprite(x0, y, ' ', 0) }
    const hd = this.focus ? 4 : 3
    w(' '.repeat(W - 1), 0, 0, hd)
    w(`SONG ${pad(this.n, 2)} ${this.on ? 'PLAY' : 'STOP'}`, 1, 0, hd)
    w('MODE', 0, 1, 5); w(MODES[d.mode], 5, 1, 10)
    w('LOOP', 9, 1, 5); w(d.loop ? 'ON' : 'OFF', 14, 1, 10)
    const curLen = this.on && cur ? len(cur) : 0
    const curPos = this.on ? this.pos : 0
    w(`POS ${pad(curPos, 4)}/${pad(curLen, 4)} SLOT ${pad(this.on ? this.i + 1 : 0, 2)}`, 0, 2, 5)
    if (this.on && cur && client.clock.speed.value !== cur.bpm) {
      w(`${client.clock.speed.value}*`, 24, 2, 11)
    }
    w(this.msg, 0, 3, 11)
    w('  # LEN MUL BPM PATCH', 0, 4, 5)
    const vr = Math.max(1, o.h - 7)
    const top = clamp(this.r - (vr >> 1), 0, Math.max(0, N - vr))
    for (let k = 0; k < vr && top + k < N; k++) {
      const i = top + k
      const s = d.slots[i]
      const y = 5 + k
      const pl = this.on && i === this.i
      w(pl ? '>' : ' ', 0, y, 11)
      w(pad(i + 1, 2), 1, y, pl ? 3 : i === this.r ? 6 : 5)
      ;[[4, s ? pad(s.len, 3) : '...'], [8, s ? pad(s.mul, 3) : '...'], [12, s ? pad(s.bpm, 3) : '...']].forEach(([x, tx], f) => {
        w(tx, x, y, i === this.r && f === this.f ? (this.focus ? 4 : 6) : s ? 10 : 5)
      })
      if (s) { w(snip(s), 16, y, 5) }
    }
    w('ENT store L load SPC play DEL clr', 0, o.h, 5)
    w('M mode O loop [ ] song W save R load', 0, o.h + 1, 5)
    if (o.h >= 15) { w('E save song file  I open song file', 0, o.h + 2, 5) }
  }

  window.addEventListener('keydown', this.key, true)
  window.addEventListener('mousedown', this.click)
  window.addEventListener('drop', (e) => {
    for (const f of e.dataTransfer.files) {
      if (/\.(song|orcasong|json)$/i.test(f.name)) {
        client.source.read(f, (file, text) => this.use(text))
      }
    }
  })
}
