param(
    [Parameter(Mandatory = $true)]
    [string[]]$FilePath,

    [string]$CertPath = $env:OFFLINENOTE_SIGN_CERT_FILE,
    [string]$CertPassword = $env:OFFLINENOTE_SIGN_CERT_PASSWORD,
    [string]$TimestampUrl = $(if ($env:OFFLINENOTE_TIMESTAMP_URL) { $env:OFFLINENOTE_TIMESTAMP_URL } else { "http://timestamp.digicert.com" }),
    [string]$SignToolPath = $env:OFFLINENOTE_SIGNTOOL_PATH
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-SignTool {
    param([string]$RequestedPath)

    if ($RequestedPath) {
        if (!(Test-Path $RequestedPath)) {
            throw "signtool.exe not found at explicit path: $RequestedPath"
        }
        return (Resolve-Path $RequestedPath).Path
    }

    $kitsRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    if (Test-Path $kitsRoot) {
        $candidates = Get-ChildItem $kitsRoot -Directory |
            Sort-Object Name -Descending |
            ForEach-Object {
                Join-Path $_.FullName "x64\signtool.exe"
                Join-Path $_.FullName "x86\signtool.exe"
            }

        foreach ($candidate in $candidates) {
            if (Test-Path $candidate) {
                return (Resolve-Path $candidate).Path
            }
        }
    }

    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "signtool.exe not found. Install the Windows SDK or set OFFLINENOTE_SIGNTOOL_PATH."
}

$resolvedFiles = @()
foreach ($file in $FilePath) {
    if (!(Test-Path $file)) {
        throw "File to sign not found: $file"
    }
    $resolvedFiles += (Resolve-Path $file).Path
}

if (-not $CertPath) {
    throw "No certificate configured. Set OFFLINENOTE_SIGN_CERT_FILE or pass -CertPath."
}

if (!(Test-Path $CertPath)) {
    throw "Certificate file not found: $CertPath"
}

$resolvedCertPath = (Resolve-Path $CertPath).Path
$signTool = Resolve-SignTool -RequestedPath $SignToolPath

$arguments = @(
    "sign",
    "/fd", "SHA256",
    "/td", "SHA256",
    "/tr", $TimestampUrl,
    "/f", $resolvedCertPath
)

if ($CertPassword) {
    $arguments += @("/p", $CertPassword)
}

$arguments += $resolvedFiles

Write-Host "Using signtool: $signTool" -ForegroundColor Cyan
& $signTool @arguments
if ($LASTEXITCODE -ne 0) {
    throw "signtool failed with exit code $LASTEXITCODE"
}

foreach ($file in $resolvedFiles) {
    $signature = Get-AuthenticodeSignature -FilePath $file
    if ($signature.Status -eq "NotSigned" -or $signature.Status -eq "HashMismatch") {
        throw "Signature verification failed for ${file}: $($signature.Status)"
    }
    Write-Host "[SIGNED] $file :: $($signature.Status)" -ForegroundColor Green
}
