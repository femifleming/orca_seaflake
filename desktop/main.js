'use strict'

/* global createWindow */

const { app, BrowserWindow, Menu, MenuItem, ipcMain, dialog } = require('electron')
const fs = require('fs')
const path = require('path')

let isShown = true

// On some Linux systems (e.g. Ubuntu 22.04) Electron 11's GPU-process sandbox
// crashes at startup with "GPU process isn't usable. Goodbye." Turning off only
// that sandbox fixes it; the rest of the app stays sandboxed. Tested in CI.
if (process.platform === 'linux') {
  app.commandLine.appendSwitch('disable-gpu-sandbox')
}

app.win = null

// Song Mode saves and opens .song files. Both the desktop app and the terminal
// version keep them in ~/Documents/Orca Seaflake/songs by default.
const SONG_FILTERS = [{ name: 'Orca Song', extensions: ['song', 'orca-song', 'json'] }, { name: 'All Files', extensions: ['*'] }]
function songsDir () {
  const dir = path.join(app.getPath('documents'), 'Orca Seaflake', 'songs')
  try { fs.mkdirSync(dir, { recursive: true }) } catch (err) {}
  return dir
}

ipcMain.handle('song:save-file', async (event, content, currentPath) => {
  const result = await dialog.showSaveDialog(app.win, {
    title: 'Save Song',
    defaultPath: currentPath || path.join(songsDir(), 'untitled.song'),
    buttonLabel: 'Save Song',
    filters: SONG_FILTERS
  })
  if (result.canceled || !result.filePath) return { canceled: true }
  let filePath = result.filePath
  if (!/\.(song|orca-song|json)$/i.test(filePath)) filePath += '.song'
  fs.writeFileSync(filePath, content, 'utf8')
  return { canceled: false, filePath }
})

ipcMain.handle('song:open-file', async (event, currentPath) => {
  const result = await dialog.showOpenDialog(app.win, {
    title: 'Open Song',
    defaultPath: currentPath ? path.dirname(currentPath) : songsDir(),
    buttonLabel: 'Open Song',
    properties: ['openFile'],
    filters: SONG_FILTERS
  })
  if (result.canceled || !result.filePaths.length) return { canceled: true }
  const filePath = result.filePaths[0]
  return { canceled: false, filePath, content: fs.readFileSync(filePath, 'utf8') }
})

// One-time move of songs from the old in-app banks into files. Never overwrites.
ipcMain.handle('song:save-banks', async (event, banks) => {
  const dir = songsDir()
  const written = []
  for (const { name, content } of banks) {
    let filePath = path.join(dir, `${name}.song`)
    for (let n = 2; fs.existsSync(filePath); n++) filePath = path.join(dir, `${name}-${n}.song`)
    fs.writeFileSync(filePath, content, 'utf8')
    written.push(path.basename(filePath))
  }
  return { dir, written }
})

app.on('ready', () => {
  app.win = new BrowserWindow({
    width: 780,
    height: 462,
    minWidth: 380,
    minHeight: 360,
    // Transparent on macOS so the Settings panel can make the background see-through.
    // The page paints its own background, so at 100% opacity it looks the same as before.
    transparent: process.platform === 'darwin',
    hasShadow: true,
    backgroundColor: process.platform === 'darwin' ? '#00000000' : '#000',
    icon: path.join(__dirname, { darwin: 'icon.icns', linux: 'icon.png', win32: 'icon.ico' }[process.platform] || 'icon.ico'),
    resizable: true,
    frame: process.platform !== 'darwin',
    skipTaskbar: process.platform === 'darwin',
    autoHideMenuBar: process.platform === 'darwin',
    webPreferences: { zoomFactor: 1.0, nodeIntegration: true, backgroundThrottling: false }
  })

  app.win.loadFile(path.join(__dirname, 'sources', 'index.html'))
  addSettingsMenuItem()
  // app.inspect()

  app.win.on('closed', () => {
    app.quit()
  })

  app.win.on('hide', function () {
    isShown = false
  })

  app.win.on('show', function () {
    isShown = true
  })

  app.on('window-all-closed', () => {
    app.quit()
  })

  app.on('activate', () => {
    if (app.win === null) {
      createWindow()
    } else {
      app.win.show()
    }
  })
})

// Puts "Settings…" (Cmd/Ctrl+Shift+P) at the top of the app menu; the page opens the panel.
function addSettingsMenuItem () {
  const menu = Menu.getApplicationMenu()
  const first = menu && menu.items[0] && menu.items[0].submenu
  if (!first) { return }
  const at = process.platform === 'darwin' ? 2 : 0
  first.insert(at, new MenuItem({ type: 'separator' }))
  first.insert(at, new MenuItem({
    label: 'Settings…',
    accelerator: 'CmdOrCtrl+Shift+P',
    click: () => { if (app.win) { app.win.webContents.send('settings:toggle') } }
  }))
  Menu.setApplicationMenu(menu)
}

app.inspect = function () {
  app.win.toggleDevTools()
}

app.toggleFullscreen = function () {
  app.win.setFullScreen(!app.win.isFullScreen())
}

app.toggleMenubar = function () {
  app.win.setMenuBarVisibility(!app.win.isMenuBarVisible())
}

app.toggleVisible = function () {
  if (process.platform !== 'darwin') {
    if (!app.win.isMinimized()) { app.win.minimize() } else { app.win.restore() }
  } else {
    if (isShown && !app.win.isFullScreen()) { app.win.hide() } else { app.win.show() }
  }
}

app.injectMenu = function (menu) {
  try {
    Menu.setApplicationMenu(Menu.buildFromTemplate(menu))
  } catch (err) {
    console.warn('Cannot inject menu.')
  }
}
