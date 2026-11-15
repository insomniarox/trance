# Builds and runs the tests on Windows. See msvc.ps1 for what it needs.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\msvc.ps1"

$root = Split-Path -Parent $PSScriptRoot
Invoke-Build "$root\tests\trance_tests.pro" "$root\build\tests"
& "$root\build\tests\trance_tests.exe"
exit $LASTEXITCODE
