# Copyright Buckley Builds LLC 2026 All Rights Reserved.
#
# Vibe3D plugin packaging script for Fab marketplace submission.
#
# Produces a clean source-only plugin folder and .zip by default with everything Fab forbids
# stripped out: build output, VCS metadata, editor caches and binaries. Epic compiles the
# submitted source themselves, so no Binaries/ or Intermediate/ may ship.
#
#   .\MakePlugin.ps1                      # -> ../Vibe3D.zip (same location/layout as VibeUE.zip)
#   .\MakePlugin.ps1 -Zip:$false          # folder only
#   .\MakePlugin.ps1 -OutputDir D:\out
#
# Validate the result before submitting (see FAB-Checklist.md):
#   RunUAT.bat BuildPlugin -Plugin="<out>\Vibe3D\Vibe3D.uplugin" -Package="<tmp>" -TargetPlatforms=Win64+Linux+Mac

[CmdletBinding()]
param(
    # The archive sits beside the source plugin, matching VibeUE's packaging convention.
    [string]$OutputDir = (Split-Path -Parent $PSScriptRoot),
    [switch]$Zip = $true
)

$ErrorActionPreference = 'Stop'
$src = $PSScriptRoot
# Keep the unpacked staging copy outside Plugins to avoid duplicate module discovery.
$stageRoot = [System.IO.Path]::GetFullPath((Join-Path (Split-Path -Parent (Split-Path -Parent $src)) 'Saved\Vibe3D-Fab'))
$dest = [System.IO.Path]::GetFullPath((Join-Path $stageRoot 'Vibe3D'))
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
if ($dest -eq $src -or -not $dest.StartsWith($stageRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe staging destination: $dest"
}

# Directories that must never reach the package.
$excludeDirs = @(
    'Binaries',        # Epic builds from source; shipping binaries is rejected
    'Intermediate',
    'Saved',
    '.git',
    '.github',
    '.vs',
    '.vscode',
    '.cursor',
    'Packaged',
    '__pycache__',
    '.pytest_cache',
    '.venv',
    'venv',
    'build',
    'dist',
    '*.egg-info',
    'docs',
    'node_modules',
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
    '*.pyo',
    '*.log',
    '*.tmp',
    '*.exp',
    '*.ilk',
    '*~',
    '.DS_Store',
    'Thumbs.db',
    'Desktop.ini',
    '.gitignore',
    '.gitattributes',
    'MakePlugin.ps1',  # the packaging tool itself is not part of the product
    'LICENSE',        # match VibeUE: retain the repository license outside the submission ZIP
    'CLAUDE.md',
    'DEAD_HANDLERS_DELETED.md',
    'HANDLER_AUDIT.md',
    'HANDLER_AUDIT_COMPLETE.md',
    'ISSUE_SUMMARY.md',
    'BUILD_PLUGIN.md',
    'BuildPlugin.bat',
    'MCP-Inspector.bat',
    'BuildAndLaunchGame.ps1',
    'AddCopyrights.ps1',
    'FAB-DESCRIPTION.md',
    'FAB_Tech_Details.md',
    'FAB-Checklist.md'
)

Write-Host "Vibe3D -> $dest" -ForegroundColor Cyan
if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
New-Item -ItemType Directory -Force -Path $dest | Out-Null

$copied = 0
Get-ChildItem -Path $src -Recurse -File | ForEach-Object {
    $relative = $_.FullName.Substring($src.Length).TrimStart('\', '/')
    $segments = $relative -split '[\\/]'

    foreach ($dir in $excludeDirs) { if ($segments | Where-Object { $_ -like $dir }) { return } }
    foreach ($pattern in $excludeFiles) { if ($_.Name -like $pattern) { return } }

    $target = Join-Path $dest $relative
    $targetDir = Split-Path -Parent $target
    if (-not (Test-Path $targetDir)) { New-Item -ItemType Directory -Force -Path $targetDir | Out-Null }
    Copy-Item -LiteralPath $_.FullName -Destination $target -Force
    $script:copied++
}
Write-Host "  copied $copied files" -ForegroundColor DarkGray

# Required product files; listing collateral and the repository license are excluded above.
$required = @('Vibe3D.uplugin', 'README.md', 'Source', 'Content', 'Config', 'Resources',
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
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    $zipPath = Join-Path $OutputDir 'Vibe3D.zip'
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    # includeBaseDirectory=False puts the descriptor and Source directly at the ZIP root.
    [System.IO.Compression.ZipFile]::CreateFromDirectory($dest, $zipPath, [System.IO.Compression.CompressionLevel]::Optimal, $false)
    $archive = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $entries = @($archive.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        if ($entries -notcontains 'Vibe3D.uplugin' -or -not ($entries | Where-Object { $_.StartsWith('Source/') })) {
            throw 'Invalid plugin ZIP layout: descriptor and Source must be at the archive root.'
        }
        foreach ($entry in $entries) {
            $parts = $entry -split '/'
            foreach ($pattern in $excludeDirs) {
                if ($parts | Where-Object { $_ -like $pattern }) { throw "Excluded directory in ZIP: $entry" }
            }
            foreach ($pattern in $excludeFiles) {
                if ($parts[-1] -like $pattern) { throw "Excluded file in ZIP: $entry" }
            }
        }
    } finally { $archive.Dispose() }
    Write-Host "  zip: $zipPath" -ForegroundColor Green
}
