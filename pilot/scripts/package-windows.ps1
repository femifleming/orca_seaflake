# Packages a Release build of Pilot Sealflake for Windows:
#   Pilot-Sealflake-<version>-windows-x64.zip   Pilot Sealflake.exe + Pilot Sealflake.vst3 + docs
# usage: pilot/scripts/package-windows.ps1 <build dir> <output dir>
param([string]$Build, [string]$Out)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $PSScriptRoot
$version = (Select-String -Path "$here/CMakeLists.txt" -Pattern 'project\(PilotSealflake VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$art = Join-Path $Build 'PilotSealflake_artefacts/Release'
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$name = "Pilot-Sealflake-$version-windows-x64"
$stage = Join-Path $env:RUNNER_TEMP $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item (Join-Path $art 'Standalone/Pilot Sealflake.exe') $stage
Copy-Item -Recurse (Join-Path $art 'VST3/Pilot Sealflake.vst3') $stage
Copy-Item "$here/README.md", "$here/COMMANDS.md" $stage
@"
Pilot Sealflake $version for Windows 10 / 11 (64-bit)

App:   run "Pilot Sealflake.exe" (keep it anywhere you like).
VST3:  copy the "Pilot Sealflake.vst3" folder to C:\Program Files\Common Files\VST3

Windows may say "Windows protected your PC" the first time: click More info, then Run anyway.
"@ | Set-Content -Path (Join-Path $stage 'INSTALL.txt')
Compress-Archive -Path $stage -DestinationPath (Join-Path $Out "$name.zip") -CompressionLevel Optimal -Force
Get-ChildItem $Out
