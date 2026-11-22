; Rezalica - Windows installer (Inno Setup 6)
; Built by CI:  iscc /DAppVersion=0.1.0 /DSourceDir=<folder with Rezalica.exe> installer\Rezalica.iss

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\build\Rezalica_artefacts\Release\Standalone"
#endif

[Setup]
AppId={{6C1F3E2A-7B5D-4E8A-9C21-5A0D3B7E4F19}
AppName=Rezalica
AppVersion={#AppVersion}
AppVerName=Rezalica {#AppVersion}
AppPublisher=Rezalica
DefaultDirName={autopf}\Rezalica
DefaultGroupName=Rezalica
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\Rezalica.exe
UninstallDisplayName=Rezalica
SetupIconFile=..\Resources\Rezalica.ico
OutputDir=..\build\installer
OutputBaseFilename=Rezalica-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\Rezalica.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\Resources\PressStart2P-OFL.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "..\Resources\GreatVibes-OFL.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Rezalica"; Filename: "{app}\Rezalica.exe"
Name: "{autoprograms}\{cm:UninstallProgram,Rezalica}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Rezalica"; Filename: "{app}\Rezalica.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Rezalica.exe"; Description: "{cm:LaunchProgram,Rezalica}"; Flags: nowait postinstall skipifsilent
