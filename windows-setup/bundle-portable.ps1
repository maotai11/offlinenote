# windows-setup/bundle-portable.ps1
# 建立 OfflineNote Portable 版本
# SPDX-License-Identifier: GPL-2.0-or-later
param(
    [string]$BuildDir = "C:\Users\LIN\OfflineNote\build",
    [string]$OutputDir = "C:\Users\LIN\OfflineNote\dist\portable"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$MSYS64 = "C:\msys64\mingw64"
$BIN = "$MSYS64\bin"

Write-Host "=== OfflineNote Portable Bundle ===" -ForegroundColor Cyan

# ── 建立目錄結構
New-Item -ItemType Directory -Force -Path "$OutputDir" | Out-Null
New-Item -ItemType Directory -Force -Path "$OutputDir\resources\fonts" | Out-Null
New-Item -ItemType Directory -Force -Path "$OutputDir\resources\icons" | Out-Null
New-Item -ItemType Directory -Force -Path "$OutputDir\resources\themes\offlinenote-fallback" | Out-Null
New-Item -ItemType Directory -Force -Path "$OutputDir\resources\translations" | Out-Null
New-Item -ItemType Directory -Force -Path "$OutputDir\data\logs" | Out-Null
New-Item -ItemType Directory -Force -Path "$OutputDir\share\glib-2.0\schemas" | Out-Null
New-Item -ItemType Directory -Force -Path "$OutputDir\share\icons\hicolor" | Out-Null
New-Item -ItemType Directory -Force -Path "$OutputDir\lib\gdk-pixbuf-2.0\2.10.0" | Out-Null

# ── 複製可執行檔
Copy-Item "$BuildDir\offlinenote.exe" "$OutputDir\" -Force
Write-Host "[OK] offlinenote.exe"

# ── portable.flag
New-Item -ItemType File -Path "$OutputDir\portable.flag" -Force | Out-Null
Write-Host "[OK] portable.flag"

# ── DLL 收集函式
$CopiedDlls = @{}

function Copy-Dll {
    param([string]$Name)
    $src = "$BIN\$Name"
    $alreadyCopied = $CopiedDlls.ContainsKey($Name)
    if ((Test-Path $src) -and (!$alreadyCopied)) {
        Copy-Item $src "$OutputDir\$Name" -Force
        $CopiedDlls[$Name] = $true
        Write-Host "  [DLL] $Name"
    }
}

# ── 必要 DLL 清單
$RequiredDlls = @(
    "libgtk-3-0.dll",
    "libgdk-3-0.dll",
    "libglib-2.0-0.dll",
    "libgobject-2.0-0.dll",
    "libgio-2.0-0.dll",
    "libcairo-2.dll",
    "libcairo-gobject-2.dll",
    "libpango-1.0-0.dll",
    "libpangocairo-1.0-0.dll",
    "libpangoft2-1.0-0.dll",
    "libpangowin32-1.0-0.dll",
    "libgdk_pixbuf-2.0-0.dll",
    "libatk-1.0-0.dll",
    "libfontconfig-1.dll",
    "libfreetype-6.dll",
    "libharfbuzz-0.dll",
    "libpixman-1-0.dll",
    "libpng16-16.dll",
    "libxml2-2.dll",
    "libz.dll",
    "libiconv-2.dll",
    "libintl-8.dll",
    "libffi-8.dll",
    "libpcre2-8-0.dll",
    "libepoxy-0.dll",
    "libbrotlidec.dll",
    "libbrotlicommon.dll",
    "libfribidi-0.dll",
    "libgraphite2.dll",
    "libthai-0.dll",
    "libdatrie-1.dll",
    "libbz2-1.dll",
    "libexpat-1.dll",
    "libgcc_s_seh-1.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll",
    "zlib1.dll",
    "liblzma-5.dll",
    "libpsl-5.dll",
    "libidn2-0.dll",
    "libtasn1-6.dll",
    "libunistring-5.dll",
    "libp11-kit-0.dll",
    "libgmp-10.dll"
)

Write-Host "`nCollecting DLLs..."
foreach ($dll in $RequiredDlls) {
    Copy-Dll $dll
}

# ── 遞迴收集額外依賴
function Get-Deps {
    param([string]$Binary)
    if (!(Test-Path $Binary)) { return }
    try {
        $deps = & "$BIN\objdump.exe" -p $Binary 2>$null | Select-String "DLL Name:" | ForEach-Object { $_.Line -replace ".*DLL Name: ", "" }
    } catch { return }
    foreach ($dep in $deps) {
        $dep = $dep.Trim()
        if (!$dep) { continue }
        # 跳過系統 DLL
        if ($dep -match "^(KERNEL32|USER32|GDI32|ADVAPI32|SHELL32|OLE32|OLEAUT32|COMCTL32|NTDLL|MSVCRT|WS2_32|WINMM|IMM32|DNSAPI|IPHLPAPI|WINHTTP|CRYPT32|RPCRT4|SETUPAPI|DEVOBJ|CFGMRG|PROPSYS|DXGI|D3D11|bcrypt|ncrypt|CRYPTBASE|SSPICLI|secur32|api-ms|ucrtbase|VCRUNTIME)") {
            continue
        }
        $alreadyCopied = $CopiedDlls.ContainsKey($dep)
        if (!$alreadyCopied) {
            Copy-Dll $dep
            Get-Deps "$BIN\$dep"
        }
    }
}

Write-Host "`nResolving additional dependencies..."
Get-Deps "$OutputDir\offlinenote.exe"
# 固定副本避免修改集合
$dllList = @($CopiedDlls.Keys)
foreach ($dll in $dllList) {
    Get-Deps "$OutputDir\$dll"
}

# ── GDK-Pixbuf loaders
$LoaderSrc = "$MSYS64\lib\gdk-pixbuf-2.0\2.10.0"
if (Test-Path "$LoaderSrc\loaders") {
    Copy-Item "$LoaderSrc\loaders" "$OutputDir\lib\gdk-pixbuf-2.0\2.10.0\" -Recurse -Force
    Write-Host "[OK] gdk-pixbuf loaders"
}

# loaders.cache（生成相對路徑）
$LoadersDir = (Get-Item "$OutputDir\lib\gdk-pixbuf-2.0\2.10.0\loaders").FullName
$CacheEntries = @()
foreach ($dll in Get-ChildItem "$LoadersDir\*.dll" -File) {
    $relPath = ".\lib\gdk-pixbuf-2.0\2.10.0\loaders\" + $dll.Name
    $CacheEntries += "`"$relPath`" 1"
}
$CacheContent = "# GDK-Pixbuf loader cache (portable)`n" + ($CacheEntries -join "`n") + "`n"
Set-Content -Path "$OutputDir\lib\gdk-pixbuf-2.0\2.10.0\loaders.cache" -Value $CacheContent -Encoding UTF8
Write-Host "[OK] loaders.cache"

# ── 圖示主題
if (Test-Path "$MSYS64\share\icons\hicolor\16x16") {
    Copy-Item "$MSYS64\share\icons\hicolor" "$OutputDir\share\icons\" -Recurse -Force
    Write-Host "[OK] hicolor icon theme"
}

# ── GLib schemas
if (Test-Path "$MSYS64\share\glib-2.0\schemas") {
    Copy-Item "$MSYS64\share\glib-2.0\schemas\*" "$OutputDir\share\glib-2.0\schemas\" -Force
    Write-Host "[OK] glib schemas"
}

# ── 啟動腳本
$startupScript = @'
@echo off
setlocal

set "APP_DIR=%~dp0"
set "PATH=%APP_DIR%;%PATH%"

set "GDK_PIXBUF_MODULEDIR=%APP_DIR%lib\gdk-pixbuf-2.0\2.10.0"
set "GDK_PIXBUF_MODULEFILE=%APP_DIR%lib\gdk-pixbuf-2.0\2.10.0\loaders.cache"
set "XDG_DATA_DIRS=%APP_DIR%share"
set "FONTCONFIG_FILE=%APP_DIR%resources\fonts\fonts.conf"

start "" "%APP_DIR%offlinenote.exe"
'@
Set-Content -Path "$OutputDir\run.bat" -Value $startupScript -Encoding ASCII
Write-Host "[OK] run.bat"

# ── fonts.conf
$fontsConf = @'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
  <dir>./resources/fonts</dir>
  <cachedir>./data/cache</cachedir>
</fontconfig>
'@
Set-Content -Path "$OutputDir\resources\fonts\fonts.conf" -Value $fontsConf -Encoding UTF8
Write-Host "[OK] fonts.conf"

# ── 最小 fallback CSS
$css = @'
/* OfflineNote fallback theme */
window { background-color: #f5f5f5; }
button { padding: 4px 8px; }
'@
Set-Content -Path "$OutputDir\resources\themes\offlinenote-fallback\style.css" -Value $css -Encoding UTF8
Write-Host "[OK] style.css"

# ── 驗證
Write-Host "`n=== Verification ===" -ForegroundColor Cyan
$RequiredDlls | ForEach-Object {
    if (Test-Path "$OutputDir\$_") { Write-Host "  [OK] $_" -ForegroundColor Green }
    else { Write-Host "  [MISSING] $_" -ForegroundColor Red }
}

$TotalSize = (Get-ChildItem $OutputDir -Recurse | Measure-Object -Property Length -Sum).Sum
Write-Host "`nTotal bundle size: $([math]::Round($TotalSize / 1MB, 1)) MB"
Write-Host "`n=== Bundle complete: $OutputDir ===" -ForegroundColor Green
