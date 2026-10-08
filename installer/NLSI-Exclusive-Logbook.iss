#define AppName "NLSI Exclusive Logbook"
#define AppVersion "1.3.2"
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
DefaultDirName={autopf32}\NLSI Exclusive Logbook
DefaultGroupName={#AppName}
UsePreviousAppDir=no
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
VersionInfoVersion=1.3.2.0
VersionInfoProductVersion=1.3.2.0
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
Filename: "{app}\NLSI-Exclusive-Logbook.exe"; Description: "Launch NLSI Exclusive Logbook"; Flags: postinstall nowait skipifsilent unchecked
Filename: "https://www.tiktok.com/@kape_073"; Description: "Follow @kape_073 on TikTok"; Flags: postinstall shellexec nowait skipifsilent unchecked
Filename: "https://github.com/Christian-0777/nlsi_telemetry/releases/tag/{#ReleaseTag}"; Description: "View release notes"; Flags: postinstall shellexec nowait skipifsilent unchecked

[Code]
var
  PrivacyPage: TWizardPage;
  PrivacyMemo: TNewMemo;
  PrivacyAccepted: TNewCheckBox;

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
begin
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
    Log('Removing the requested legacy installation directory: ' + LegacyPath);
    if not DelTree(LegacyPath, True, True, True) then
      Result := 'The legacy directory could not be completely removed: ' + LegacyPath +
        '. Close programs using files in that directory and try again.';
    if (Result = '') and DirExists(LegacyPath) then
      Result := 'The legacy directory still exists and could not be completely removed: ' + LegacyPath;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then begin
    WizardForm.FinishedHeadingLabel.Caption := 'Installation complete';
    WizardForm.FinishedLabel.Caption :=
      'NLSI Exclusive Logbook v1.3.2-alpha has been installed.' + #13#10#13#10 +
      'What''s New' + #13#10 +
      '- Moved detailed NLSI and RenCloud status to Settings > Providers.' + #13#10 +
      '- Improved responsive layouts and opened at the existing minimum size.' + #13#10 +
      '- Retained lightweight, change-only telemetry updates.' + #13#10#13#10 +
      'Select any optional action below, then click Finish.';
  end;
end;