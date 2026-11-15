# Puts the compiler and Qt within reach, for build.ps1 and test.ps1.
#
# The compiler is Visual Studio 2022's (the free Build Tools will do) and is
# found on its own. Qt is the MSVC 2022 64-bit kit: found through QTDIR (as in
# C:\Qt\6.8.3\msvc2022_64), through qmake on the PATH, or under C:\Qt.

function Enter-Msvc {
    if (Get-Command cl -ErrorAction SilentlyContinue) { return }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = if (Test-Path $vswhere) {
        & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (-not $vs) { throw "Visual Studio's C++ build tools were not found." }
    # vcvars is a batch file, so it is run in cmd and what it sets is taken over.
    cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
}

function Find-Qmake {
    if ($env:QTDIR) { return Join-Path $env:QTDIR 'bin\qmake.exe' }
    $onPath = Get-Command qmake6, qmake -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($onPath) { return $onPath.Source }
    $kit = Get-ChildItem 'C:\Qt\6.*\msvc2022_64\bin\qmake.exe' -ErrorAction SilentlyContinue |
        Sort-Object { [version]$_.Directory.Parent.Parent.Name } | Select-Object -Last 1
    if ($kit) { return $kit.FullName }
    throw "Qt's MSVC 2022 64-bit kit was not found. Set QTDIR to it, as in C:\Qt\6.8.3\msvc2022_64."
}

# qmake and then make, in the directory given.
function Invoke-Build([string]$project, [string]$directory) {
    Enter-Msvc
    $qmake = Find-Qmake
    New-Item -ItemType Directory -Force $directory | Out-Null
    Push-Location $directory
    try {
        & $qmake $project
        if ($LASTEXITCODE) { throw "qmake failed." }
        # jom is nmake on every core, and comes with Qt's installer.
        $make = if (Get-Command jom -ErrorAction SilentlyContinue) { 'jom' } else { 'nmake' }
        & $make /NOLOGO
        if ($LASTEXITCODE) { throw "The build failed." }
    } finally {
        Pop-Location
    }
    # Where Qt's own libraries are, for running what was built.
    $env:PATH = (Split-Path $qmake) + ';' + $env:PATH
}
