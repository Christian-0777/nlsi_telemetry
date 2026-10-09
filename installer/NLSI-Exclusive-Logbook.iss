#define AppName "NLSI Exclusive Logbook"
#define AppVersion "1.3.8"
#define ReleaseTag "v" + AppVersion + "-alpha"
#define ReleasePayload "build\intermediate\installer-payload-" + ReleaseTag
#define AppPublisher "NLSI"
#define AppURL "https://github.com/Christian-0777/nlsi_telemetry"
#ifndef InstallPrivileges
#define InstallPrivileges "admin"
#endif
#ifndef LegacyDirectory
#define LegacyDirectory "C:\nlsi-tem"
#endif

[Setup]
AppId=NLSI Exclusive Logbook
AppName={#AppName}
AppVersion={#AppVersion} Alpha
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\NLSI Exclusive Logbook
ArchitecturesInstallIn64BitMode=x64compatible
DefaultGroupName={#AppName}
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
AllowNoIcons=yes
ArchitecturesAllowed=x64compatible
OutputDir=build\releases\{#ReleaseTag}
OutputBaseFilename=NLSI-Exclusive-Logbook-{#ReleaseTag}-Setup
SourceDir=..
SetupIconFile=img\logo.ico
LicenseFile=installer\licenses\TermsAndConditions.txt
WizardImageFile={#ReleasePayload}\wizard-background.bmp
WizardSmallImageFile={#ReleasePayload}\wizard-logo.bmp
WizardImageStretch=yes
WizardImageBackColor=$FFF7FA
WizardSmallImageBackColor=$FFF7FA
WizardStyle=modern
Compression=lzma2/ultra64
SolidCompression=yes
PrivilegesRequired={#InstallPrivileges}
UsedUserAreasWarning=no
Uninstallable=yes
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\NLSI-Exclusive-Logbook.exe
SetupLogging=yes
CloseApplications=yes
RestartApplications=no
VersionInfoVersion=1.3.8.0
VersionInfoProductVersion=1.3.8.0
VersionInfoCompany=NLSI
VersionInfoProductName={#AppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "startmenu"; Description: "Create Start Menu folder"; Flags: checkedonce
Name: "desktopicon"; Description: "Create Desktop shortcut"; Flags: checkedonce
Name: "startup"; Description: "Start NLSI Exclusive Logbook with Windows"; Flags: unchecked

[Files]
Source: "{#ReleasePayload}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "img\logo.ico"; DestDir: "{app}\img"; Flags: ignoreversion
Source: "img\logo.png"; DestDir: "{app}\img"; Flags: ignoreversion
Source: "version.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "installer\licenses\PrivacyPolicy.txt"; Flags: dontcopy

[Icons]
Name: "{group}\NLSI Exclusive Logbook"; Filename: "{app}\NLSI-Exclusive-Logbook.exe"; WorkingDir: "{app}"; IconFilename: "{app}\img\logo.ico"; Tasks: startmenu
Name: "{group}\Uninstall"; Filename: "{uninstallexe}"; Tasks: startmenu
Name: "{autodesktop}\NLSI Exclusive Logbook"; Filename: "{app}\NLSI-Exclusive-Logbook.exe"; WorkingDir: "{app}"; IconFilename: "{app}\img\logo.ico"; Tasks: desktopicon
Name: "{userstartup}\NLSI Exclusive Logbook"; Filename: "{app}\NLSI-Exclusive-Logbook.exe"; WorkingDir: "{app}"; IconFilename: "{app}\img\logo.ico"; Tasks: startup

[Run]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\tools\InstallTruckSimPlugin.ps1"" -DllPath64 ""{app}\plugins\trucksim\win_x64\plugins\trucksim-gps-telemetry.dll"" -DllPath32 ""{app}\plugins\trucksim\win_x86\plugins\trucksim-gps-telemetry.dll"" -ManifestPath ""{app}\tools\installed-trucksim-plugins.json"" -StatusPath ""{app}\logs\trucksim-plugin-status.txt"""; WorkingDir: "{app}"; Flags: runhidden waituntilterminated
Filename: "{app}\NLSI-Exclusive-Logbook.exe"; Description: "Launch NLSI Exclusive Logbook"; Flags: postinstall nowait skipifsilent unchecked
Filename: "https://www.tiktok.com/@kape_073"; Description: "Follow @kape_073 on TikTok"; Flags: postinstall shellexec nowait skipifsilent unchecked
Filename: "https://github.com/Christian-0777/nlsi_telemetry/releases/tag/{#ReleaseTag}"; Description: "View release notes"; Flags: postinstall shellexec nowait skipifsilent unchecked
[UninstallRun]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\tools\InstallTruckSimPlugin.ps1"" -DllPath64 ""{app}\plugins\trucksim\win_x64\plugins\trucksim-gps-telemetry.dll"" -DllPath32 ""{app}\plugins\trucksim\win_x86\plugins\trucksim-gps-telemetry.dll"" -ManifestPath ""{app}\tools\installed-trucksim-plugins.json"" -StatusPath ""{app}\logs\trucksim-plugin-status.txt"" -RemoveManagedPlugin"; WorkingDir: "{app}"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveManagedTruckSimPlugin"

[Code]
const
  UninstallRegistryKey =
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\NLSI Exclusive Logbook_is1';

var
  PrivacyPage: TWizardPage;
  PrivacyMemo: TNewMemo;
  PrivacyAccepted: TNewCheckBox;
  InstallInfoPage: TWizardPage;
  InstallInfoText: TNewStaticText;
  ExistingInstallDir: String;
  ExistingInstallVersion: String;
  ExistingInstallDetected: Boolean;

function DetectExistingInstallation: Boolean;
var
  RegistryInstallDir: String;
begin
  ExistingInstallDir := ExpandConstant('{autopf32}\NLSI Exclusive Logbook');
  ExistingInstallVersion := '';

  if not RegQueryStringValue(HKLM32, UninstallRegistryKey, 'InstallLocation',
     RegistryInstallDir) or not DirExists(RegistryInstallDir) then
    RegQueryStringValue(HKLM32, UninstallRegistryKey, 'Inno Setup: App Path',
      RegistryInstallDir);
  if DirExists(RegistryInstallDir) then
    ExistingInstallDir := RegistryInstallDir;

  Result := DirExists(ExistingInstallDir);
  if Result then
    RegQueryStringValue(HKLM32, UninstallRegistryKey, 'DisplayVersion',
      ExistingInstallVersion);
end;

function LegacyContainsUserData(const LegacyPath: String): Boolean;
begin
  Result :=
    DirExists(LegacyPath + '\config') or
    DirExists(LegacyPath + '\data') or
    DirExists(LegacyPath + '\logs') or
    DirExists(LegacyPath + '\app\test\output');
end;

function InitializeSetup: Boolean;
begin
  Result := not WizardSilent;
  if not Result then
    Log('Silent installation is disabled because Terms and Privacy acceptance are required.');
end;

procedure InitializeWizard;
var
  PolicyText: String;
  PolicyLines: TArrayOfString;
  Index: Integer;
  InstallationSummary: String;
begin
  ExistingInstallDetected := DetectExistingInstallation;

  InstallInfoPage := CreateCustomPage(wpWelcome, 'Installation type',
    'Review the installation or update details before continuing.');
  InstallInfoText := TNewStaticText.Create(InstallInfoPage);
  InstallInfoText.Parent := InstallInfoPage.Surface;
  InstallInfoText.SetBounds(0, 0, InstallInfoPage.SurfaceWidth,
    InstallInfoPage.SurfaceHeight);
  InstallInfoText.Anchors := [akLeft, akTop, akRight, akBottom];
  InstallInfoText.AutoSize := False;
  InstallInfoText.WordWrap := True;
  if ExistingInstallDetected then begin
    if ExistingInstallVersion = '' then
      ExistingInstallVersion := 'Not available';
    InstallationSummary :=
      'An existing NLSI Exclusive Logbook installation was detected.' + #13#10#13#10 +
      'This setup will UPDATE the existing installation.' + #13#10 +
      'Installed version: ' + ExistingInstallVersion + #13#10 +
      'New version: v{#AppVersion}-alpha' + #13#10 +
      'Installation directory: ' + ExistingInstallDir + #13#10#13#10 +
      'Existing configuration and log files will be preserved.' + #13#10#13#10 +
      'Setup automatically detects supported Steam ETS2/ATS installations and installs the official TruckSim GPS plugin into matching x64 and x86 plugin folders. Different existing plugin DLLs are preserved.';
  end else begin
    InstallationSummary :=
      'No existing NLSI Exclusive Logbook installation was detected.' + #13#10#13#10 +
      'This setup will perform a fresh installation of v{#AppVersion}-alpha under Program Files.' + #13#10#13#10 +
      'Setup automatically detects supported Steam ETS2/ATS installations and installs the official TruckSim GPS plugin into matching x64 and x86 plugin folders. Different existing plugin DLLs are preserved.';
  end;
  if DirExists(ExpandConstant('{#LegacyDirectory}')) and
     LegacyContainsUserData(ExpandConstant('{#LegacyDirectory}')) then
    InstallationSummary := InstallationSummary + #13#10#13#10 +
      'The legacy C:\nlsi-tem directory contains known user data and will be left untouched.';
  InstallInfoText.Caption := InstallationSummary;

  ExtractTemporaryFile('PrivacyPolicy.txt');
  if not LoadStringsFromFile(ExpandConstant('{tmp}\PrivacyPolicy.txt'), PolicyLines) then
    RaiseException('The Privacy Policy could not be loaded. Setup cannot continue.');
  PolicyText := '';
  for Index := 0 to GetArrayLength(PolicyLines) - 1 do
    PolicyText := PolicyText + PolicyLines[Index] + #13#10;

  PrivacyPage := CreateCustomPage(wpLicense, 'Privacy Policy',
    'Review and accept the Privacy Policy to continue.');
  PrivacyMemo := TNewMemo.Create(PrivacyPage);
  PrivacyMemo.Parent := PrivacyPage.Surface;
  PrivacyMemo.SetBounds(0, 0, PrivacyPage.SurfaceWidth, PrivacyPage.SurfaceHeight - ScaleY(42));
  PrivacyMemo.Anchors := [akLeft, akTop, akRight, akBottom];
  PrivacyMemo.ReadOnly := True;
  PrivacyMemo.ScrollBars := ssVertical;
  PrivacyMemo.TabStop := True;
  PrivacyMemo.Text := PolicyText;

  PrivacyAccepted := TNewCheckBox.Create(PrivacyPage);
  PrivacyAccepted.Parent := PrivacyPage.Surface;
  PrivacyAccepted.SetBounds(0, PrivacyPage.SurfaceHeight - ScaleY(30),
    PrivacyPage.SurfaceWidth, ScaleY(24));
  PrivacyAccepted.Anchors := [akLeft, akRight, akBottom];
  PrivacyAccepted.Caption := 'I have read and accept the Privacy Policy.';
  PrivacyAccepted.Checked := False;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (PrivacyPage <> nil) and (CurPageID = PrivacyPage.ID) and
     not PrivacyAccepted.Checked then begin
    MsgBox('You must accept the Privacy Policy before continuing.',
      mbInformation, MB_OK);
    Result := False;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  LegacyPath: String;
begin
  Result := '';
  LegacyPath := '{#LegacyDirectory}';
  if DirExists(LegacyPath) then begin
    if LegacyContainsUserData(LegacyPath) then begin
      Log('Preserving the legacy directory because it contains user data: ' + LegacyPath);
    end else begin
      Log('Removing the legacy application directory: ' + LegacyPath);
      if not DelTree(LegacyPath, True, True, True) then
        Result := 'The legacy directory could not be completely removed: ' + LegacyPath +
          '. Close programs using files in that directory and try again.';
      if (Result = '') and DirExists(LegacyPath) then
        Result := 'The legacy directory still exists and could not be completely removed: ' + LegacyPath;
    end;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then begin
    WizardForm.FinishedHeadingLabel.Caption := 'Installation complete';
    WizardForm.FinishedLabel.Caption :=
      'NLSI Exclusive Logbook v1.3.8-alpha has been installed.' + #13#10#13#10 +
      'What''s New' + #13#10 +
      '- Direct TruckSim GPS revision-13 telemetry; the separate server client is not required.' + #13#10 +
      '- TruckSim GPS is the only active game telemetry source; local events, jobs, sessions, and TXT logs remain supported.' + #13#10 +
      '- Setup installs the verified official TruckSim GPS plugin for detected supported game folders.' + #13#10#13#10 +
      'The About page includes NLSI community and creator links.';
  end;
end;