'use strict'

/* global localStorage */

// Settings panel: background color, background transparency and grid font.
// Saved in localStorage so they come back on the next launch.

function Settings (client) {
  const BUILT_IN = 'input_mono_medium'
  const FONTS = [
    [BUILT_IN, 'Input Mono (default)'],
    ['Menlo', 'Menlo'],
    ['Monaco', 'Monaco'],
    ['SF Mono', 'SF Mono'],
    ['Courier New', 'Courier New'],
    ['Courier', 'Courier'],
    ['Andale Mono', 'Andale Mono'],
    ['PT Mono', 'PT Mono'],
    ['JetBrains Mono', 'JetBrains Mono'],
    ['Fira Code', 'Fira Code'],
    ['Fira Mono', 'Fira Mono'],
    ['IBM Plex Mono', 'IBM Plex Mono'],
    ['Source Code Pro', 'Source Code Pro'],
    ['Roboto Mono', 'Roboto Mono'],
    ['Hack', 'Hack'],
    ['Consolas', 'Consolas'],
    ['Lucida Console', 'Lucida Console'],
    ['DejaVu Sans Mono', 'DejaVu Sans Mono'],
    ['Ubuntu Mono', 'Ubuntu Mono']
  ]
  // Transparent windows need a frameless window, which only the macOS build uses.
  this.canBeTransparent = typeof process !== 'undefined' && process.platform === 'darwin'

  const defaults = { opacity: 100, font: BUILT_IN }
  this.data = { ...defaults, ...read() }
  this.isOpen = false

  this.style = document.createElement('style')
  this.el = document.createElement('div')
  this.el.id = 'settings'

  this.install = (host) => {
    host.appendChild(this.style)
    host.appendChild(this.el)
    this.el.innerHTML = `
      <div class="head"><span>SETTINGS</span><button data-close title="Close (Esc)">×</button></div>
      <label>Background color <b data-bg-value></b><input type="color" data-bg></label>
      <label>Background opacity <b data-opacity-value></b><input type="range" min="0" max="100" step="1" data-opacity></label>
      <p class="note" data-opacity-note></p>
      <label>Font<select data-font></select></label>
      <label class="custom">Other installed font<input type="text" placeholder="Type a font name, press Enter" data-custom spellcheck="false"></label>
      <p class="note" data-font-note></p>
      <button data-reset>Reset to defaults</button>`
    this.q('[data-close]').onclick = () => this.close()
    this.q('[data-bg]').oninput = (e) => { this.setBackground(e.target.value); this.q('[data-bg-value]').textContent = e.target.value }
    this.q('[data-opacity]').oninput = (e) => this.setOpacity(+e.target.value)
    this.q('[data-font]').onchange = (e) => this.setFont(e.target.value)
    this.q('[data-custom]').onkeydown = (e) => {
      if (e.key === 'Enter') { this.setFont(e.target.value.trim()); e.target.blur() }
    }
    this.q('[data-reset]').onclick = () => this.reset()
    this.el.addEventListener('keydown', (e) => {
      if (e.key === 'Escape') { this.close(); e.preventDefault() }
      e.stopPropagation()
    })
    this.el.addEventListener('mousedown', (e) => e.stopPropagation())
  }

  this.start = () => {
    this.apply()
    // "Settings…" in the app menu (Cmd/Ctrl+Shift+P) is handled by the main process.
    try { require('electron').ipcRenderer.on('settings:toggle', () => this.toggle()) } catch (err) {}
  }

  this.q = (s) => this.el.querySelector(s)

  this.toggle = () => { this.isOpen ? this.close() : this.open() }

  this.open = () => {
    this.isOpen = true
    if (client.song) { client.song.focus = false }
    this.refresh()
    this.el.className = 'open'
  }

  this.close = () => {
    this.isOpen = false
    this.el.className = ''
    if (document.activeElement && this.el.contains(document.activeElement)) { document.activeElement.blur() }
  }

  this.setBackground = (hex) => {
    if (!/^#[0-9a-f]{6}$/i.test(hex)) { return }
    client.theme.load({ ...client.theme.active, background: hex })
  }

  this.setOpacity = (value) => {
    this.data.opacity = Math.max(0, Math.min(100, Math.round(value)))
    save(this.data)
    this.apply()
  }

  this.setFont = (name) => {
    if (!name) { return }
    this.data.font = name
    save(this.data)
    this.apply()
    this.refresh()
  }

  this.reset = () => {
    this.data = { ...defaults }
    save(this.data)
    client.theme.load({ ...client.theme.active, background: client.theme.default.background })
    this.apply()
    this.refresh()
  }

  // The background color with the chosen opacity, for the window and the canvas.
  this.background = () => {
    const hex = client.theme.active.background || '#000000'
    const alpha = this.canBeTransparent ? this.data.opacity / 100 : 1
    if (alpha >= 1 || !/^#[0-9a-f]{6}$/i.test(hex)) { return hex }
    const n = parseInt(hex.slice(1), 16)
    return `rgba(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255}, ${alpha})`
  }

  this.fontStack = () => {
    const f = this.data.font
    return f === BUILT_IN ? BUILT_IN : `"${f.replace(/"/g, '')}", ${BUILT_IN}, monospace`
  }

  // Called whenever the theme, opacity or font changes.
  this.apply = () => {
    this.style.innerHTML = `body { background-color: ${this.background()} !important; }`
    client.applyFont()
    client.update()
  }

  this.refresh = () => {
    this.q('[data-bg]').value = /^#[0-9a-f]{6}$/i.test(client.theme.active.background) ? client.theme.active.background : '#000000'
    this.q('[data-bg-value]').textContent = this.q('[data-bg]').value
    const range = this.q('[data-opacity]')
    range.value = this.data.opacity
    range.disabled = !this.canBeTransparent
    this.q('[data-opacity-value]').textContent = this.canBeTransparent ? `${this.data.opacity}%` : ''
    this.q('[data-opacity-note]').textContent = this.canBeTransparent
      ? 'Lower it to see the desktop behind the grid. Text stays solid.'
      : 'Transparency is only available on macOS.'

    const select = this.q('[data-font]')
    const options = FONTS.filter(([id]) => id === BUILT_IN || id === this.data.font || installed(id))
    if (!options.find(([id]) => id === this.data.font)) { options.push([this.data.font, this.data.font]) }
    select.innerHTML = options.map(([id, label]) => `<option value="${escape(id)}">${escape(label)}</option>`).join('')
    select.value = this.data.font
    const known = FONTS.find(([id]) => id === this.data.font)
    this.q('[data-custom]').value = known ? '' : this.data.font
    this.q('[data-font-note]').textContent = this.data.font !== BUILT_IN && !installed(this.data.font)
      ? `"${this.data.font}" isn't installed; using Input Mono instead.`
      : ''
  }

  // Helpers

  function read () {
    try { return JSON.parse(localStorage.getItem('settings')) || {} } catch (err) { return {} }
  }

  function save (data) {
    try { localStorage.setItem('settings', JSON.stringify(data)) } catch (err) {}
  }

  function escape (s) {
    return `${s}`.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]))
  }

  // A font is installed if text drawn with it measures differently from the fallbacks.
  const probe = document.createElement('canvas').getContext('2d')
  function installed (name) {
    if (name === BUILT_IN) { return true }
    const text = 'mmmmmmmmmmlli10OWX@#'
    return ['monospace', 'serif', 'sans-serif'].some((base) => {
      probe.font = `72px ${base}`
      const a = probe.measureText(text).width
      probe.font = `72px "${name.replace(/"/g, '')}", ${base}`
      return probe.measureText(text).width !== a
    })
  }
}
