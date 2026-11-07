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
  // Key help shown at the bottom of the panel. Short windows get two rows.
  const HELP = [
    [['ENT', 'store'], ['L', 'load'], ['DEL', 'clear']],
    [['SPC', 'play'], ['M', 'mode'], ['R', 'loop']],
    [['S', 'save song'], ['O', 'open song'], ['ESC', 'close']]
  ]
  const HELP_SHORT = [
    [['ENT', 'store'], ['L', 'load'], ['SPC', 'play']],
    [['S', 'save'], ['O', 'open'], ['M', 'mode'], ['R', 'loop']]
  ]
  const pad = (v, n) => `${v}`.padStart(n, '0')
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v))
  const read = (k) => { try { return JSON.parse(localStorage.getItem(k)) } catch (e) { return null } }
  // Duration in frames = len * mul (minimum 1 frame)
  const len = (s) => Math.max(1, Math.max(1, s.len | 0) * Math.max(1, s.mul | 0))
  const stop = (e) => { e.preventDefault(); e.stopPropagation() }
  const snip = (s) => (s.p.split('\n').find((l) => /[^.]/.test(l)) || '').slice(0, 20)
  const blank = () => ({ slots: Array(N).fill(null), mode: 0, loop: 1 })

  // Validates a song from localStorage or a file, returns null if it is not one
  // Early Song Mode builds saved { format: 'orca-seaflake-song', patterns: [...] }
  // with each grid as one long line. Convert that to the slot format.
  const fromPatterns = (j) => {
    const modes = { forward: 0, ordered: 0, order: 0, reverse: 1, backward: 1, random: 2, shuffle: 2 }
    const order = typeof j.order === 'string' ? modes[j.order.toLowerCase()] : undefined
    return {
      mode: order !== undefined ? order : j.mode,
      loop: j.loop,
      slots: j.patterns.map((pt) => {
        if (!pt || typeof pt.patch !== 'string') { return null }
        const w = pt.width | 0
        let p = pt.patch
        if (p.indexOf('\n') < 0 && w > 0 && p.length > w) { p = p.match(new RegExp(`.{1,${w}}`, 'g')).join('\n') }
        return { len: pt.steps | 0, mul: 1, bpm: (pt.tempo | 0) || (j.tempo | 0) || 120, p }
      })
    }
  }

  const clean = (j) => {
    if (!j) { return null }
    if (!Array.isArray(j.slots) && Array.isArray(j.patterns)) { j = fromPatterns(j) }
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
        bpm: clamp(s.bpm | 0 || 120, 60, 300),
        notes: String(s.notes || ''),
        p: L.map((l) => l.padEnd(w, '.')).join('\n')
      }
    })
    return { slots, mode: clamp(j.mode | 0, 0, 2), loop: j.loop ? 1 : 0 }
  }

  const pack = (d) => JSON.stringify({
    orcaSong: 2,
    mode: d.mode,
    loop: d.loop,
    slots: d.slots.map((s) => s && { len: s.len, mul: s.mul, bpm: s.bpm, notes: s.notes || '', p: s.p.split('\n') })
  }, null, 1)

  this.cols = 0 // slide progress in tiles
  this.open = false
  this.focus = false
  this.on = false
  this.d = clean(read('song_work')) || blank()
  this.r = 0 // cursor row
  this.f = 0 // cursor field (0: len, 1: mul, 2: bpm)
  this.file = localStorage.getItem('song_file') || '' // path of the open .song file, '' if never saved
  this.i = -1 // playing slot
  this.q = 0 // position in play order
  this.pos = 0 // frames played in slot
  this.seq = []
  this.buf = ''
  this.msg = ''

  this.save = () => { localStorage.setItem('song_work', JSON.stringify(this.d)) }
  this.setFile = (filePath) => {
    this.file = filePath || ''
    localStorage.setItem('song_file', this.file)
  }
  this.title = () => this.file ? this.file.split(/[\\/]/).pop().replace(/\.(song|orca-song|orcasong|json)$/i, '') : 'UNTITLED'
  this.ipc = () => { try { return window.require && window.require('electron').ipcRenderer } catch (err) { return null } }
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
      const ipc = this.ipc()
      if (ipc) {
        const result = await ipc.invoke('song:save-file', content, this.file)
        if (!result.canceled) {
          this.setFile(result.filePath)
          this.msg = `SAVED ${this.title().toUpperCase()}`
          client.update()
        }
        return
      }
    } catch (err) { console.warn('Song', 'Native save failed', err) }
    client.source.write('song', 'song', content, 'application/json')
  }

  this.importFile = async () => {
    try {
      const ipc = this.ipc()
      if (ipc) {
        const result = await ipc.invoke('song:open-file', this.file)
        if (!result.canceled) this.use(result.content, result.filePath)
        return
      }
    } catch (err) { console.warn('Song', 'Native open failed', err) }
    client.source.open('song', (file, text) => this.use(text))
  }

  // Used by the song:mode, song:save and song:open commands (Cmd+K)
  this.cycleMode = () => { this.d.mode = (this.d.mode + 1) % 3; this.save(); client.update() }
  this.saveSong = () => this.exportFile()
  this.openSong = () => this.importFile()

  this.use = (text, filePath) => {
    let j = null
    try { j = JSON.parse(text) } catch (err) { j = null }
    const d = clean(j)
    if (d) { this.d = d; this.on = false; this.r = 0; this.save(); this.setFile(filePath) }
    this.msg = d ? `OPENED ${this.title().toUpperCase()}` : 'NOT A SONG FILE'
    this.open = true
    this.focus = true
    this.slide()
  }

  this.key = (e) => {
    if (!this.focus || e.metaKey || e.ctrlKey || e.altKey) { return }
    if (client.settings && client.settings.isOpen) { return }
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
    else if (k === 'r') { this.d.loop ^= 1; this.save() }
    else if (k === 's') { this.exportFile() }
    else if (k === 'o') { this.importFile() }
    client.update()
  }

  // the panel stays inside the grid rows (0..o.h-1) so the footer below is never covered
  this.help = (h) => h >= 15 ? 3 : 2
  this.rows = (h) => Math.max(1, h - 5 - this.help(h))

  this.moveBanks = async () => {
    if (localStorage.getItem('song_banks_moved')) { return }
    const banks = []
    for (let n = 1; n <= 16; n++) {
      const d = clean(read(`song_${n}`))
      if (d && d.slots.some((sl) => sl)) { banks.push({ name: `bank-${pad(n, 2)}`, content: pack(d) }) }
    }
    const ipc = this.ipc()
    if (!banks.length || !ipc) { localStorage.setItem('song_banks_moved', '1'); return }
    try {
      const result = await ipc.invoke('song:save-banks', banks)
      localStorage.setItem('song_banks_moved', '1')
      if (result.written.length) {
        this.msg = `${result.written.length} OLD BANKS SAVED AS FILES`
        console.info('Song', `Saved old song banks to ${result.dir}:`, result.written.join(', '))
      }
    } catch (err) { console.warn('Song', 'Could not save old banks', err) }
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
    if (my >= o.h) { return }
    if (my >= 5 && my < 5 + this.rows(o.h)) {
      const vr = this.rows(o.h)
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
    client.context.clearRect(x0 * t.ws, 0, client.el.width, o.h * t.hs)
    // Draw cyan edge vertical dividing bar
    for (let y = 0; y < o.h; y++) { client.drawSprite(x0, y, ' ', 0) }
    const hd = this.focus ? 4 : 3
    w(' '.repeat(W - 1), 0, 0, hd)
    const state = this.on ? 'PLAY' : 'STOP'
    w(`SONG ${this.title().toUpperCase()}`.slice(0, W - 8), 1, 0, hd)
    w(state, W - 2 - state.length, 0, hd)
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
    const vr = this.rows(o.h)
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
    // Key help: the key in bright text, what it does in dim text, in grid-aligned columns
    const rows = this.help(o.h) === 3 ? HELP : HELP_SHORT
    const hy = o.h - rows.length
    rows.forEach((row, j) => {
      const cw = Math.floor((W - 1) / row.length)
      row.forEach(([key, label], c) => {
        w(key, c * cw, hy + j, 2)
        w(label, c * cw + key.length + 1, hy + j, 5)
      })
    })
  }

  window.addEventListener('keydown', this.key, true)
  this.moveBanks()
  window.addEventListener('mousedown', this.click)
}
