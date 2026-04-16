param(
    [Parameter(Mandatory = $true)]
    [string[]]$FilePath,

    [switch]$RequireTrusted
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$acceptableStatuses = @("Valid", "NotTrusted", "UnknownError")

foreach ($file in $FilePath) {
    if (!(Test-Path $file)) {
        throw "File not found: $file"
    }

    $resolvedPath = (Resolve-Path $file).Path
    $signature = Get-AuthenticodeSignature -FilePath $resolvedPath

    if ($RequireTrusted) {
        if ($signature.Status -ne "Valid") {
            throw "Trusted signature verification failed for ${resolvedPath}: $($signature.Status)"
        }
    } elseif ($acceptableStatuses -notcontains $signature.Status) {
        throw "Signature verification failed for ${resolvedPath}: $($signature.Status)"
    }

    $subject = if ($signature.SignerCertificate) {
        $signature.SignerCertificate.Subject
    } else {
        "(no signer certificate)"
    }

    Write-Host "[SIGNATURE] $resolvedPath :: $($signature.Status) :: $subject" -ForegroundColor Green
}
