# Makes dist\trance-<version>-setup.exe, the installer, from dist\trance.
# Run package.ps1 first. It needs Inno Setup 6, as from
# `winget install JRSoftware.InnoSetup`.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\msvc.ps1"

$root = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path "$root\dist\trance\trance.exe")) { throw "There is no dist\trance; run package.ps1 first." }

# Installed for everyone, or for the one user winget installs it for.
$iscc = Get-Command iscc -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source
if (-not $iscc) {
    $iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" |
        Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $iscc) { throw "Inno Setup 6 was not found. Install it with: winget install JRSoftware.InnoSetup" }

& $iscc /Q "/DVersion=$(Get-TranceVersion)" "$root\pkgbuild\trance.iss"
if ($LASTEXITCODE) { throw "Inno Setup failed." }
Write-Output "Made $root\dist\trance-$(Get-TranceVersion)-setup.exe"
