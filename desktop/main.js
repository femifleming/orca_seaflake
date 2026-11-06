'use strict'

/* global createWindow */

const { app, BrowserWindow, Menu, ipcMain, dialog } = require('electron')
const fs = require('fs')
const path = require('path')

let isShown = true

app.win = null

// Native file dialogs for Song Mode tracker files (.song JSON).
ipcMain.handle('song:save-file', async (event, content) => {
  const result = await dialog.showSaveDialog(app.win, {
    title: 'Save Orca Seaflake Song',
    defaultPath: 'Untitled.song',
    buttonLabel: 'Save Song',
    filters: [{ name: 'Orca Song', extensions: ['song'] }, { name: 'JSON', extensions: ['json'] }]
  })
  if (result.canceled || !result.filePath) return { canceled: true }
  let filePath = result.filePath
  if (!/\.(song|json)$/i.test(filePath)) filePath += '.song'
  fs.writeFileSync(filePath, content, 'utf8')
  return { canceled: false, filePath }
})

ipcMain.handle('song:open-file', async () => {
  const result = await dialog.showOpenDialog(app.win, {
    title: 'Open Orca Seaflake Song',
    buttonLabel: 'Open Song',
    properties: ['openFile'],
    filters: [{ name: 'Orca Song', extensions: ['song', 'json'] }, { name: 'All Files', extensions: ['*'] }]
  })
  if (result.canceled || !result.filePaths.length) return { canceled: true }
  const filePath = result.filePaths[0]
  return { canceled: false, filePath, content: fs.readFileSync(filePath, 'utf8') }
})

app.on('ready', () => {
  app.win = new BrowserWindow({
    width: 780,
    height: 462,
    minWidth: 380,
    minHeight: 360,
    backgroundColor: '#000',
    icon: path.join(__dirname, { darwin: 'icon.icns', linux: 'icon.png', win32: 'icon.ico' }[process.platform] || 'icon.ico'),
    resizable: true,
    frame: process.platform !== 'darwin',
    skipTaskbar: process.platform === 'darwin',
    autoHideMenuBar: process.platform === 'darwin',
    webPreferences: { zoomFactor: 1.0, nodeIntegration: true, backgroundThrottling: false }
  })

  app.win.loadFile(path.join(__dirname, 'sources', 'index.html'))
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
