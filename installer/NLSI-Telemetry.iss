#define AppName "NLSI Telemetry"
#define AppVersion "__APP_VERSION__"
#define AppPublisher "NLSI"
#define AppURL "https://github.com/Christian-0777/nlsi_telemetry.git"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName=C:\nlsi-tem
DefaultGroupName={#AppName}
OutputBaseFilename=NLSI-Telemetry-Setup-v{#AppVersion}
OutputDir=..\build\v{#AppVersion}
SourceDir=staging
Compression=lzma
SolidCompression=yes
PrivilegesRequired=admin
UninstallDisplayName={#AppName}

[Files]
Source: "app\*"; DestDir: "{app}\app"; Flags: recursesubdirs createallsubdirs
Source: "bin\*"; DestDir: "{app}\bin"; Flags: recursesubdirs createallsubdirs
Source: "config\.env.example"; DestDir: "{app}\config"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "version.json"; DestDir: "{app}"
Source: "Desktop\NLSI Telemetry.bat"; DestDir: "{commonstartup}\NLSI Telemetry"

[Dirs]
Name: "{app}\config"; Flags: uninsneveruninstall
Name: "{app}\data"; Flags: uninsneveruninstall
Name: "{app}\logs"; Flags: uninsneveruninstall
Name: "{app}\runtime"; Flags: uninsneveruninstall

[Icons]
Name: "{group}\NLSI Telemetry"; Filename: "{commonstartup}\NLSI Telemetry\NLSI Telemetry.bat"; WorkingDir: "{app}\app"
Name: "{commonstartup}\NLSI Telemetry"; Filename: "{commonstartup}\NLSI Telemetry\NLSI Telemetry.bat"; WorkingDir: "{app}\app"
