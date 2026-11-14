; enrollment.iss — Inno Setup 6 script for a single-file installer.
;
; WHY: some offices prefer one Setup.exe ("Next, Next, Finish") over a
; portable zip. Build steps:
;
;   1. Run  bash deploy/deploy-portable.sh  first (creates dist/EnrollmentApp).
;   2. Install Inno Setup 6 (free): https://jrsoftware.org/isinfo.php
;   3. Open this file in Inno Setup's Compiler, press "Compile".
;   4. Output: dist/EnrollmentApp-Setup.exe — send that to staff PCs.
;
; The installer copies the whole portable bundle (exe + DLLs + runtime)
; into Program Files and creates Start-menu / desktop shortcuts that
; launch run.bat, so the bundled runtime paths are set correctly.

#define MyAppName "Enrollment App"
#define MyAppVersion "2.1"
#define MyAppPublisher "Registrar / Enrollment Committee"
#define MyAppExeName "run.bat"

[Setup]
; Fixed AppId so upgrades replace the previous installation.
AppId={{8F3A2C61-9D4E-4B7B-A1C2-3D5E6F708192}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\EnrollmentApp
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=EnrollmentApp-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
; Writing into Program Files needs admin rights.
PrivilegesRequired=admin
UninstallDisplayIcon={app}\enrollment-app.exe

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The whole portable bundle produced by deploy-portable.sh.
Source: "..\dist\EnrollmentApp\*"; DestDir: "{app}"; \
    Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; \
    WorkingDir: "{app}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; \
    WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; \
    Description: "{cm:LaunchProgram,{#MyAppName}}"; \
    Flags: nowait postinstall skipifsilent
