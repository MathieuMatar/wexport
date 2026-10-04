; Inno Setup script: one setup .exe, per-user install, no admin rights needed.
; Build:  iscc /DSourceDir=<release output folder> installer\ChatKeeper.iss

#define AppName "ChatKeeper"
#define AppVersion "0.1.0"
#ifndef SourceDir
  #define SourceDir "..\src\App\x64\Release\ChatKeeper"
#endif

[Setup]
AppId={{60883967-8E23-48C2-AFAE-C7C21A4F29C8}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppName}
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir=..\out
OutputBaseFilename={#AppName}-Setup-{#AppVersion}
SetupIconFile=..\src\App\Assets\AppIcon.ico
UninstallDisplayIcon={app}\ChatKeeper.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\THIRD_PARTY_NOTICES.txt

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb,*.lib,*.exp,*.ilk"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\ChatKeeper.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\ChatKeeper.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\ChatKeeper.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
