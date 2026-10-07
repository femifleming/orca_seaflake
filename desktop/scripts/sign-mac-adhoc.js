const { execFileSync } = require('node:child_process')
const path = require('node:path')
const { signAsync } = require('@electron/osx-sign')

const appPath = path.resolve(process.argv[2] || '')

async function main() {
  if (process.platform !== 'darwin') {
    throw new Error('macOS signing must run on a macOS host.')
  }
  if (!appPath.endsWith('.app')) {
    throw new Error('Pass the path to the packaged Orca-Seaflake.app bundle.')
  }

  const entitlements = path.resolve(__dirname, '..', 'entitlements.mac.plist')
  await signAsync({
    app: appPath,
    identity: '-',
    identityValidation: false,
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
}

main().catch(error => {
  console.error(error.message)
  process.exitCode = 1
})
