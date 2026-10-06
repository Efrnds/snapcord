# Builds Snapcord on Windows.
#   .\scripts\build.ps1                  -> Debug build
#   .\scripts\build.ps1 -Config release  -> Release build
#   .\scripts\build.ps1 -Run             -> build and launch the app
param(
    [ValidateSet('debug', 'release')]
    [string]$Config = 'debug',
    [switch]$Run
)

$ErrorActionPreference = 'Stop'

if (-not $env:QT_ROOT_DIR) { $env:QT_ROOT_DIR = Join-Path $HOME 'Qt\6.8.3\msvc2022_64' }
if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = Join-Path $HOME 'vcpkg' }

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

# CMake and Ninja installed with pip live in the user's Python scripts folder.
$pythonScripts = python -c "import sysconfig; print(sysconfig.get_path('scripts', 'nt_user'))"
if ($pythonScripts) { $env:PATH = "$pythonScripts;$env:PATH" }

$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    cmake --preset $Config
    if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
    cmake --build --preset $Config
    if ($LASTEXITCODE) { throw 'Build failed.' }
} finally {
    Pop-Location
}

$exe = Join-Path $root "build\$Config\Snapcord.exe"
Write-Host "Done: $exe"
if ($Run) { Start-Process $exe }
