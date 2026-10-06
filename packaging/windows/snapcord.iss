; Inno Setup script for the Windows installer. Built by packaging/windows/package.ps1, which passes
; AppVersion, SourceDir (the deployed app folder) and OutputDir.

[Setup]
AppId={{8C5D1A5E-3F2B-4C7E-9A61-5B7D2E4F9C10}
AppName=Snapcord
AppVersion={#AppVersion}
AppVerName=Snapcord {#AppVersion}
AppPublisher=Snapcord
AppPublisherURL=https://github.com/pedrordgsr/snapcord
AppSupportURL=https://github.com/pedrordgsr/snapcord/issues
DefaultDirName={autopf}\Snapcord
DisableProgramGroupPage=yes
; Installs for the current user without administrator rights; the user can still choose "all users".
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir={#OutputDir}
OutputBaseFilename=Snapcord-{#AppVersion}-windows-x64-setup
SetupIconFile=..\icons\snapcord.ico
UninstallDisplayIcon={app}\Snapcord.exe
LicenseFile=..\..\LICENSE
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Snapcord"; Filename: "{app}\Snapcord.exe"
Name: "{autodesktop}\Snapcord"; Filename: "{app}\Snapcord.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Snapcord.exe"; Description: "{cm:LaunchProgram,Snapcord}"; Flags: nowait postinstall skipifsilent
