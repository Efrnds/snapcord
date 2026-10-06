# Builds Snapcord on Windows.
#   .\scripts\build.ps1                  -> Debug build
#   .\scripts\build.ps1 -Config release  -> Release build
#   .\scripts\build.ps1 -Run             -> build and launch the app
#   .\scripts\build.ps1 -Target snapcord_tests -> build a single target (e.g. while the app is open)
param(
    [ValidateSet('debug', 'release')]
    [string]$Config = 'debug',
    [string]$Target = '',
    [switch]$Run
)

$ErrorActionPreference = 'Stop'

if (-not $env:QT_ROOT_DIR) { $env:QT_ROOT_DIR = Join-Path $HOME 'Qt\6.8.3\msvc2022_64' }
# The Visual Studio developer shell points VCPKG_ROOT at its own bundled (older) vcpkg, so remember ours first.
$vcpkgRoot = if ($env:VCPKG_ROOT -and $env:VCPKG_ROOT -notmatch 'Microsoft Visual Studio') { $env:VCPKG_ROOT } else { Join-Path $HOME 'vcpkg' }

# Load the Visual Studio (MSVC) compiler into this session if it isn't loaded yet.
if (-not $env:VSCMD_VER) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw 'Visual Studio Build Tools with C++ not found.' }
    # VsDevCmd calls vswhere itself and expects it on PATH.
    $env:PATH = "$(Split-Path $vswhere);$env:PATH"
    Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

$env:VCPKG_ROOT = $vcpkgRoot

# CMake and Ninja installed with pip live in the user's Python scripts folder.
$pythonScripts = python -c "import sysconfig; print(sysconfig.get_path('scripts', 'nt_user'))"
if ($pythonScripts) { $env:PATH = "$pythonScripts;$env:PATH" }

$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    cmake --preset $Config
    if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
    if ($Target) { cmake --build --preset $Config --target $Target } else { cmake --build --preset $Config }
    if ($LASTEXITCODE) { throw 'Build failed.' }
} finally {
    Pop-Location
}

$exe = Join-Path $root "build\$Config\Snapcord.exe"
Write-Host "Done: $exe"
if ($Run) { Start-Process $exe }
