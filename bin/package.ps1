# Puts Trance and everything it needs to run into dist\trance, to zip and
# carry to a PC that has neither Qt nor Visual Studio. Run build.ps1 first.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\msvc.ps1"

$root = Split-Path -Parent $PSScriptRoot
Enter-Msvc
$qt = Split-Path (Find-Qmake)
$out = "$root\dist\trance"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item "$root\build\trance.exe" $out

# Qt's libraries and the QML the window is made of. Not the software renderer
# Qt would add: it stops at OpenGL 3.0, and Trance needs 3.3.
& "$qt\windeployqt.exe" --release --qmldir "$root\src" --no-translations --no-compiler-runtime --no-opengl-sw "$out\trance.exe"
if ($LASTEXITCODE) { throw "windeployqt failed." }

# Microsoft's C++ runtime beside the program, so none has to be installed.
Copy-Item "$env:VCToolsRedistDir\x64\Microsoft.VC*.CRT\*.dll" $out
Copy-Item "$root\LICENSE" "$out\LICENSE.txt"
Write-Output "Packaged $out"
