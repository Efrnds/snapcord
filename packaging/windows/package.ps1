# Packages a Release build for Windows: a portable .zip and an installer (Inno Setup).
#   .\packaging\windows\package.ps1 [-BuildDir build\release] [-OutDir dist] [-NoInstaller]
# Needs windeployqt (Qt's bin folder on PATH or QT_ROOT_DIR set) and, for the installer, Inno Setup 6.
param(
    [string]$BuildDir = 'build\release',
    [string]$OutDir = 'dist',
    [switch]$NoInstaller
)

$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$version = (Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'VERSION (\d+\.\d+\.\d+)').Matches[0].Groups[1].Value

New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$stage = Join-Path $OutDir 'Snapcord'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

# The executable plus the Qt libraries, plugins and the Visual C++ runtime it needs.
function Resolve-BuiltExe([string]$name) {
    foreach ($path in @(
            (Join-Path $BuildDir $name),
            (Join-Path $BuildDir "src\app\$name"),
            (Join-Path $BuildDir "src\app\Release\$name"),
            (Join-Path $BuildDir "Release\$name"))) {
        if (Test-Path $path) { return $path }
    }
    throw "Could not find $name under $BuildDir"
}
Copy-Item (Resolve-BuiltExe 'Snapcord.exe') $stage
Copy-Item (Resolve-BuiltExe 'snapcord-video.exe') $stage
$windeployqt = Get-Command windeployqt -ErrorAction SilentlyContinue
$windeployqt = if ($windeployqt) { $windeployqt.Source } else { Join-Path $env:QT_ROOT_DIR 'bin\windeployqt.exe' }
# Windows PowerShell treats anything a tool prints on stderr (windeployqt warnings) as an error; rely on exit codes.
$ErrorActionPreference = 'Continue'
& $windeployqt --release --compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler (Join-Path $stage 'Snapcord.exe') 2>&1 | Out-Host
if ($LASTEXITCODE) { throw 'windeployqt failed.' }
# Multimedia plugins come from the video player, not from Snapcord.exe.
& $windeployqt --release --compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler (Join-Path $stage 'snapcord-video.exe') 2>&1 | Out-Host
if ($LASTEXITCODE) { throw 'windeployqt failed for snapcord-video.' }
$ErrorActionPreference = 'Stop'
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $stage 'LICENSE.txt')

$zip = Join-Path $OutDir "Snapcord-$version-windows-x64.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Write-Host "Created $zip"

if ($NoInstaller) { return }

# Installer.
$iscc = Get-Command iscc -ErrorAction SilentlyContinue
$iscc = if ($iscc) { $iscc.Source } else { Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe' }
if (-not (Test-Path $iscc)) {
    Write-Host 'Inno Setup not found, installing it with Chocolatey...'
    choco install innosetup -y --no-progress | Out-Null
}
$ErrorActionPreference = 'Continue'
& $iscc "/DAppVersion=$version" "/DSourceDir=$stage" "/DOutputDir=$OutDir" (Join-Path $PSScriptRoot 'snapcord.iss') 2>&1 | Out-Host
if ($LASTEXITCODE) { throw 'Inno Setup failed.' }
Write-Host "Created $(Join-Path $OutDir "Snapcord-$version-windows-x64-setup.exe")"
