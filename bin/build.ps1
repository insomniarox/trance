# Builds Trance on Windows into build\trance.exe. See msvc.ps1 for what it needs.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\msvc.ps1"

$root = Split-Path -Parent $PSScriptRoot
Invoke-Build "$root\trance.pro" "$root\build"
Write-Output "Built $root\build\trance.exe"
