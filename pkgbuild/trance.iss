; Trance's installer for Windows, made from dist\trance by bin\installer.ps1.
;
; It installs for the user who runs it, into %LOCALAPPDATA%\Programs\Trance,
; so it asks for no administrator. Settings and presets are kept elsewhere,
; in %LOCALAPPDATA%\trance, and stay when Trance is uninstalled.

#ifndef Version
  #error Make the installer with bin\installer.ps1, which gives the version.
#endif

[Setup]
; The same for every version, so that a new one replaces the one installed.
AppId={{D9B07F1F-835A-4EFC-94B6-1D740832772F}
AppName=Trance
AppVersion={#Version}
AppPublisher=insomniarox
AppPublisherURL=https://github.com/insomniarox/trance
AppSupportURL=https://github.com/insomniarox/trance/issues
AppUpdatesURL=https://github.com/insomniarox/trance/releases
VersionInfoVersion={#Version}
PrivilegesRequired=lowest
DefaultDirName={autopf}\Trance
DisableProgramGroupPage=yes
; Windows 11 24H2, the first that opens no console beside the window (see
; trance.manifest), on x64 only: Arm has not been tried.
MinVersion=10.0.26100
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
OutputDir=..\dist
OutputBaseFilename=trance-{#Version}-setup
SetupIconFile=trance.ico
UninstallDisplayIcon={app}\trance.exe
UninstallDisplayName=Trance
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; A Trance that is running is closed before its files are replaced.
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\dist\trance\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Trance"; Filename: "{app}\trance.exe"
Name: "{autodesktop}\Trance"; Filename: "{app}\trance.exe"; Tasks: desktopicon

[Registry]
; So that `trance next` is found from Run (Win+R), `start trance next` and the
; tools that bind a key to a command, such as AutoHotkey, without a path.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\trance.exe"; ValueType: string; ValueName: ""; ValueData: "{app}\trance.exe"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\trance.exe"; ValueType: string; ValueName: "Path"; ValueData: "{app}"

[Run]
Filename: "{app}\trance.exe"; Description: "{cm:LaunchProgram,Trance}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; A Trance that is running holds its files, which would then stay behind.
Filename: "{sys}\taskkill.exe"; Parameters: "/f /im trance.exe"; Flags: runhidden; RunOnceId: "CloseTrance"
