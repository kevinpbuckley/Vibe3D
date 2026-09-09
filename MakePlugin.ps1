# Copyright Buckley Builds LLC 2026 All Rights Reserved.
#
# Vibe3D plugin packaging script for Fab marketplace submission.
#
# Produces a clean source-only plugin folder and .zip by default with everything Fab forbids
# stripped out: build output, VCS metadata, editor caches and binaries. Epic compiles the
# submitted source themselves, so no Binaries/ or Intermediate/ may ship.
#
#   .\MakePlugin.ps1                      # -> <Project>\Saved\Vibe3D-Fab\Vibe3D and Vibe3D-<version>.zip
#   .\MakePlugin.ps1 -Zip:$false          # folder only
#   .\MakePlugin.ps1 -OutputDir D:\out
#
# Validate the result before submitting (see FAB-Checklist.md):
#   RunUAT.bat BuildPlugin -Plugin="<out>\Vibe3D\Vibe3D.uplugin" -Package="<tmp>" -TargetPlatforms=Win64+Linux+Mac

[CmdletBinding()]
param(
    # Default output sits under the host project's Saved/ — never inside Plugins/, where the
    # engine would discover the packaged copy's .uplugin and try to load a duplicate module.
    [string]$OutputDir = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'Saved\Vibe3D-Fab'),
    [switch]$Zip = $true
)

$ErrorActionPreference = 'Stop'
$src = $PSScriptRoot
$dest = Join-Path $OutputDir 'Vibe3D'

# Directories that must never reach the package.
$excludeDirs = @(
    'Binaries',        # Epic builds from source; shipping binaries is rejected
    'Intermediate',
    'Saved',
    '.git',
    '.github',
    '.vs',
    '__pycache__',
    '.venv',
    'FabImages'        # listing art lives outside the plugin
)

# File patterns that must never reach the package.
$excludeFiles = @(
    '*.exe',           # executables are not allowed in Fab packages
    '*.dll',
    '*.pdb',
    '*.lib',
    '*.suo',
    '*.user',
    '*.pyc',
    '.gitignore',
    '.gitattributes',
    'MakePlugin.ps1'   # the packaging tool itself is not part of the product
)

Write-Host "Vibe3D -> $dest" -ForegroundColor Cyan
if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
New-Item -ItemType Directory -Force -Path $dest | Out-Null

$copied = 0
Get-ChildItem -Path $src -Recurse -File | ForEach-Object {
    $relative = $_.FullName.Substring($src.Length).TrimStart('\', '/')
    $segments = $relative -split '[\\/]'

    foreach ($dir in $excludeDirs) { if ($segments -contains $dir) { return } }
    foreach ($pattern in $excludeFiles) { if ($_.Name -like $pattern) { return } }

    $target = Join-Path $dest $relative
    $targetDir = Split-Path -Parent $target
    if (-not (Test-Path $targetDir)) { New-Item -ItemType Directory -Force -Path $targetDir | Out-Null }
    Copy-Item -LiteralPath $_.FullName -Destination $target -Force
    $script:copied++
}
Write-Host "  copied $copied files" -ForegroundColor DarkGray

# Fab requires a code plugin to carry these; fail loudly rather than submitting a broken zip.
$required = @('Vibe3D.uplugin', 'README.md', 'LICENSE', 'Source', 'Content', 'Config', 'Resources',
              'Resources\Icon128.png', 'Config\FilterPlugin.ini')
$missing = @()
foreach ($item in $required) { if (-not (Test-Path (Join-Path $dest $item))) { $missing += $item } }
if ($missing.Count -gt 0) {
    Write-Host "FAILED - missing required entries:" -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    exit 1
}

# Path-length rule: every path measured from the plugin folder must stay under 170 characters.
$longest = Get-ChildItem -Path $dest -Recurse -File |
    ForEach-Object { $_.FullName.Substring($dest.Length).TrimStart('\', '/') } |
    Sort-Object Length -Descending | Select-Object -First 1
if ($longest.Length -ge 170) {
    Write-Host "FAILED - path too long ($($longest.Length) chars): $longest" -ForegroundColor Red
    exit 1
}
Write-Host "  longest relative path: $($longest.Length) chars" -ForegroundColor DarkGray

$version = (Get-Content (Join-Path $dest 'Vibe3D.uplugin') -Raw | ConvertFrom-Json).VersionName
Write-Host "OK - Vibe3D $version packaged" -ForegroundColor Green

if ($Zip) {
    $zipPath = Join-Path $OutputDir "Vibe3D-$version.zip"
    if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
    Compress-Archive -Path $dest -DestinationPath $zipPath
    Write-Host "  zip: $zipPath" -ForegroundColor Green
}
