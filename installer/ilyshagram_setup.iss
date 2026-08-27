#define MyAppName "ilyshaGram"
#define MyAppVersion RemoveFileExt(GetFileVersion("..\out\Release\ilyshaGram.exe"))
#define MyAppPublisher "ilyshaGram"
#define MyAppExeName "ilyshaGram.exe"
#define MyAppSource "..\out\Release\ilyshaGram.exe"

[Setup]
AppId={{8F6E8B2C-1A3D-4C7E-9B5A-2D8C1F4E6A90}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir=out
OutputBaseFilename=ilyshagram-setup
Compression=lzma2/ultra
SolidCompression=yes
WizardStyle=modern dark
DisableProgramGroupPage=yes
SetupIconFile=..\Telegram\Resources\OwpenGram\art\icon256.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=lowest

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "Создать ярлык на Рабочем столе"; GroupDescription: "Дополнительно:"

[Files]
Source: "{#MyAppSource}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Запустить {#MyAppName}"; Flags: nowait postinstall skipifsilent
