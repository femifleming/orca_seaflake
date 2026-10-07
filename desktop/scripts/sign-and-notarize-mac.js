const { execFileSync } = require('node:child_process')
const path = require('node:path')
const { notarize } = require('@electron/notarize')
const { signAsync } = require('@electron/osx-sign')

const appPath = path.resolve(process.argv[2] || '')
const required = [
  'APPLE_DEVELOPER_IDENTITY',
  'APPLE_ID',
  'APPLE_APP_SPECIFIC_PASSWORD',
  'APPLE_TEAM_ID'
]

function requireEnvironment(names) {
  const missing = names.filter(name => !process.env[name])
  if (missing.length > 0) {
    throw new Error(`Missing required macOS release settings: ${missing.join(', ')}`)
  }
}

async function main() {
  if (process.platform !== 'darwin') {
    throw new Error('macOS signing and notarization must run on a macOS host.')
  }
  if (!appPath.endsWith('.app')) {
    throw new Error('Pass the path to the packaged Orca-Seaflake.app bundle.')
  }
  requireEnvironment(required)

  const entitlements = path.resolve(__dirname, '..', 'entitlements.mac.plist')
  await signAsync({
    app: appPath,
    identity: process.env.APPLE_DEVELOPER_IDENTITY,
    platform: 'darwin',
    version: require('electron/package.json').version,
    preAutoEntitlements: false,
    optionsForFile: () => ({
      entitlements,
      hardenedRuntime: true
    })
  })

  execFileSync('codesign', ['--verify', '--deep', '--strict', '--verbose=2', appPath], {
    stdio: 'inherit'
  })

  await notarize({
    appPath,
    appleId: process.env.APPLE_ID,
    appleIdPassword: process.env.APPLE_APP_SPECIFIC_PASSWORD,
    teamId: process.env.APPLE_TEAM_ID
  })

  execFileSync('xcrun', ['stapler', 'validate', appPath], { stdio: 'inherit' })
  execFileSync('spctl', ['--assess', '--type', 'execute', '--verbose=2', appPath], {
    stdio: 'inherit'
  })
}

main().catch(error => {
  console.error(error.message)
  process.exitCode = 1
})
