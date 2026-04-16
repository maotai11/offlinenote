# SPDX-License-Identifier: GPL-2.0-or-later
param(
    [string]$BuildDir = "C:\Users\LIN\OfflineNote\build",
    [string]$PortableDir = "C:\Users\LIN\OfflineNote\dist\portable"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (!(Test-Path "$BuildDir\offlinenote.exe")) {
    throw "Build output not found: $BuildDir\offlinenote.exe"
}

if (!(Test-Path "$PortableDir\offlinenote.exe")) {
    throw "Portable bundle not found: $PortableDir\offlinenote.exe"
}

Write-Host "=== Sync runtime to build ===" -ForegroundColor Cyan

Get-ChildItem -Path $PortableDir -Filter *.dll -File | ForEach-Object {
    Copy-Item $_.FullName "$BuildDir\$($_.Name)" -Force
    Write-Host "  [DLL] $($_.Name)"
}

@("lib", "share", "resources") | ForEach-Object {
    $src = Join-Path $PortableDir $_
    $dst = Join-Path $BuildDir $_
    if (Test-Path $src) {
        Copy-Item $src $dst -Recurse -Force
        Write-Host "  [DIR] $_"
    }
}

Write-Host "=== Build runtime sync complete: $BuildDir ===" -ForegroundColor Green
