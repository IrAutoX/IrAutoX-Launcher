#ifndef SourceDir
  #define SourceDir "..\\dist"
#endif
#ifndef OutputDir
  #define OutputDir "..\\release"
#endif
#ifndef Version
  #define Version "2.0.2"
#endif

[Setup]
AppId={{7F8E2D5A-3B76-4AE5-90EA-B57EAFB3AF32}
AppName=IrAutoX Launcher
AppVersion={#Version}
AppVerName=IrAutoX Launcher {#Version}
AppPublisher=IrAutoX
AppPublisherURL=https://irautox.ir
AppSupportURL=https://irautox.ir
AppUpdatesURL=https://irautox.ir/version/version.json
AppComments=IrAutoX game launcher, updater, command broker, game library, download manager and irautox:// protocol handler.
DefaultDirName={autopf}\IrAutoX Launcher
DefaultGroupName=IrAutoX
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=IrAutoX-Launcher-v{#Version}-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName=IrAutoX Launcher
UninstallDisplayIcon={app}\IrAutoXLauncher.exe
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
VersionInfoVersion={#Version}.0
VersionInfoCompany=IrAutoX
VersionInfoDescription=IrAutoX Launcher - Game Library, Downloader, Updater, Presence, SDK Bridge and Protocol Handler
VersionInfoProductName=IrAutoX Launcher
VersionInfoProductVersion={#Version}
SetupIconFile=..\resources\app.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create an IrAutoX Launcher desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked
Name: "startup"; Description: "Start IrAutoX Launcher with Windows"; GroupDescription: "Startup:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\IrAutoX Launcher"; Filename: "{app}\IrAutoXLauncher.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\IrAutoX Launcher"; Filename: "{app}\IrAutoXLauncher.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "IrAutoXLauncher"; ValueData: """{app}\IrAutoXLauncher.exe"" --background"; Flags: uninsdeletevalue; Tasks: startup
Root: HKCU; Subkey: "Software\IrAutoX\Launcher"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\irautox"; ValueType: string; ValueName: ""; ValueData: "URL:IrAutoX Protocol"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\irautox"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\irautox\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\IrAutoXLauncher.exe,0"
Root: HKCU; Subkey: "Software\Classes\irautox\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\irxcmd.exe"" ""%1"""

[Run]
Filename: "{app}\IrAutoXLauncher.exe"; Description: "Launch IrAutoX Launcher"; Flags: nowait runasoriginaluser skipifdoesntexist
