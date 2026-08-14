#ifndef SourceDir
  #define SourceDir "..\\dist"
#endif
#ifndef OutputDir
  #define OutputDir "..\\release"
#endif
#ifndef Version
  #define Version "2.0.0"
#endif

[Setup]
AppId={{7F8E2D5A-3B76-4AE5-90EA-B57EAFB3AF32}
AppName=IrAutoX Launcher
AppVersion={#Version}
AppPublisher=IrAutoX
AppPublisherURL=https://irautox.ir
AppSupportURL=https://github.com/IrAutoX/IrAutoX-Launcher/issues
DefaultDirName={autopf}\IrAutoX Launcher
DefaultGroupName=IrAutoX
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=IrAutoX-Launcher-v{#Version}-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName=IrAutoX Launcher
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
VersionInfoVersion={#Version}.0
VersionInfoCompany=IrAutoX
VersionInfoDescription=IrAutoX Launcher Installer
VersionInfoProductName=IrAutoX Launcher

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked
Name: "startup"; Description: "Start IrAutoX Launcher with Windows"; GroupDescription: "Startup:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "setup.install"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\IrAutoX Launcher"; Filename: "{app}\IrAutoXLauncher.exe"
Name: "{autodesktop}\IrAutoX Launcher"; Filename: "{app}\IrAutoXLauncher.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "IrAutoXLauncher"; ValueData: """{app}\IrAutoXLauncher.exe"" --background"; Flags: uninsdeletevalue; Tasks: startup
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "IrAutoXUpdater"; ValueData: """{app}\IrAutoXUpdater.exe"" --background"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\IrAutoXUpdater.exe"; Parameters: "--background"; Flags: nowait runhidden
Filename: "{app}\IrAutoXLauncher.exe"; Description: "Launch IrAutoX"; Flags: nowait postinstall skipifsilent
