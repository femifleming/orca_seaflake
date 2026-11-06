'use strict'

// Everything that differs between macOS, Windows and Linux / Raspberry Pi OS
// lives here, so index.js stays platform-neutral.

const os = require('node:os')
const path = require('node:path')
const { spawnSync } = require('node:child_process')

const platform = process.platform

// ~/Documents/Orca Seaflake on every OS. On a Pi or headless Linux box with no
// Documents folder it is created on first save (mkdir -p).
function saveDir () {
  if (process.env.ORCA_SEAFLAKE_DIR) return path.resolve(process.env.ORCA_SEAFLAKE_DIR)
  return path.join(os.homedir(), 'Documents', 'Orca Seaflake')
}

// Ordered candidates: the first command that exists and exits 0 wins.
function clipboardCommands () {
  if (platform === 'darwin') return { copy: [['pbcopy', []]], paste: [['pbpaste', []]] }
  if (platform === 'win32') {
    return {
      copy: [['clip', []]],
      paste: [['powershell.exe', ['-NoProfile', '-Command', 'Get-Clipboard -Raw']]]
    }
  }
  // Linux / Raspberry Pi OS: Wayland first, then X11. Over SSH none of these
  // usually work, and the app falls back to its internal clipboard.
  return {
    copy: [['wl-copy', []], ['xclip', ['-selection', 'clipboard']], ['xsel', ['--clipboard', '--input']]],
    paste: [['wl-paste', ['--no-newline']], ['xclip', ['-selection', 'clipboard', '-o']], ['xsel', ['--clipboard', '--output']]]
  }
}

function run (candidates, input) {
  for (const [cmd, args] of candidates) {
    const r = spawnSync(cmd, args, { input, encoding: 'utf8', windowsHide: true, timeout: 2000 })
    if (!r.error && r.status === 0) return { ok: true, stdout: r.stdout }
  }
  return { ok: false }
}

function copyToClipboard (text) { return run(clipboardCommands().copy, text).ok }
function readClipboard () {
  const r = run(clipboardCommands().paste)
  return r.ok ? r.stdout : null
}

// The `midi` package is a native addon (RtMidi). If it failed to build, the
// app still runs as an editor and tells the user why there is no MIDI.
function loadMidi () {
  try {
    return { midi: require('midi'), error: null }
  } catch (e) {
    return { midi: null, error: e }
  }
}

const midiBackendName = platform === 'darwin' ? 'CoreMIDI' : platform === 'win32' ? 'Windows MIDI' : 'ALSA'

function midiSetupHint () {
  if (platform === 'darwin') return 'Enable the IAC Driver in Audio MIDI Setup for a virtual port.'
  if (platform === 'win32') return 'Install loopMIDI to create a virtual port for a DAW.'
  return 'Run `aconnect -l` to list ports; a USB MIDI device or `sudo modprobe snd-virmidi` gives you one.'
}

module.exports = { platform, saveDir, copyToClipboard, readClipboard, loadMidi, midiBackendName, midiSetupHint }
