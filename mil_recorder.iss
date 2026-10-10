[Setup]
AppName=Mil Recorder
AppVersion=1.0
AppPublisher=easy-phi
DefaultDirName={autopf}\MilRecorder
DefaultGroupName=Mil Recorder
UninstallDisplayIcon={app}\mil-recorder.exe
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir=dist
OutputBaseFilename=MilRecorder_Setup
DisableProgramGroupPage=yes
PrivilegesRequired=lowest

[Files]
Source: "mil-recorder.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Mil Recorder"; Filename: "{app}\mil-recorder.exe"
Name: "{autodesktop}\Mil Recorder"; Filename: "{app}\mil-recorder.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加图标:"

[Run]
Filename: "{app}\mil-recorder.exe"; Description: "立即启动 Mil Recorder"; Flags: nowait postinstall skipifsilent
