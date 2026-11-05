; Inkwell Windows installer.
; Build the staged folder first (see packaging/windows/README.md), then from the repo root:
;   ISCC packaging\windows\Inkwell.iss
; Override the stage directory or version when they differ:
;   ISCC /DAppVersion=0.1.0 /DSourceDir=C:\path\to\windows-deploy packaging\windows\Inkwell.iss
; SourceDir is an absolute path, or a path relative to this script.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef VersionInfoVersion
  #define VersionInfoVersion AppVersion + ".0"
#endif
#ifndef SourceDir
  #define SourceDir SourcePath + "..\..\build\windows-deploy"
#endif

#ifnexist SourceDir + "\inkwell.exe"
  #error "Staged inkwell.exe was not found. Create build\windows-deploy first (packaging\windows\README.md)."
#endif

#define AppName "Inkwell"
#define AppPublisher "Inkwell"
#define AppURL "https://chaptertrail.com"
#define AppExeName "inkwell.exe"

[Setup]
AppId={{3F77A684-DABE-4856-8D11-E818950EBD9B}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
AppComments=A writing desk with spelling and grammar
VersionInfoVersion={#VersionInfoVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription=Inkwell Setup
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir={#SourcePath}..\..\dist
OutputBaseFilename=Inkwell-{#AppVersion}-windows-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName}
LicenseFile={#SourcePath}..\..\LICENSE
PrivilegesRequired=admin

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
