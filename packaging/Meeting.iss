#ifndef AppVersion
  #define AppVersion "1.0.10"
#endif

#ifndef AppVariant
  #define AppVariant ""
#endif

#define AppName "Audio-Video"
#define ArtifactName "Audio-Video" + AppVariant
#define AppExeName "Audio-Video.exe"
#define SourceDir "..\dist\" + ArtifactName + "-" + AppVersion

[Setup]
AppId={{A14BFCA1-CF8B-4FA0-9B9A-DA89DE804DB4}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Audio-Video
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename={#ArtifactName}-Setup-{#AppVersion}
SetupIconFile=..\images\message.ico
UninstallDisplayIcon={app}\{#AppExeName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#AppVersion}.0
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} installer

[Languages]
Name: "chinesesimp"; MessagesFile: "ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加快捷方式："; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\卸载 {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "启动 {#AppName}"; Flags: nowait postinstall skipifsilent
