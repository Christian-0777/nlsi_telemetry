#define AppName "NLSI Exclusive Logbook"
#ifndef AppChannel
#define AppChannel "beta"
#endif
#ifndef AppFileVersion
#define AppFileVersion 1.5.2.0
#endif
#ifndef ReleaseLabel
#define ReleaseLabel "1.5.2-beta"
#endif
#define DefaultApplicationDir "C:\Program Files\NLSI Exclusive Logbook"
#ifndef ReleaseTag
#define ReleaseTag "v1.5.2-beta"
#endif
#ifndef ReleasePayload
#define ReleasePayload "build\intermediate\installer-payload-v1.5.2-beta"
#endif
#define AppPublisher "Nabski Logistics and Solutions Inc."
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
AppVersion={#ReleaseLabel}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={#DefaultApplicationDir}
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
VersionInfoVersion={#AppFileVersion}
VersionInfoProductVersion={#AppFileVersion}
VersionInfoCompany=Nabski Logistics and Solutions Inc.
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
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\tools\InstallScsPositionPlugin.ps1"" -DllPath64 ""{app}\plugins\scs-position\win_x64\nlsi.dll"" -DllPath32 ""{app}\plugins\scs-position\win_x86\nlsi.dll"" -ManifestPath ""{app}\tools\installed-scs-position-plugins.json"" -BackupDirectory ""{app}\plugin-backups\scs-position"" -StatusPath ""{app}\logs\scs-position-plugin-status.txt"""; WorkingDir: "{app}"; Flags: runhidden waituntilterminated
Filename: "{app}\NLSI-Exclusive-Logbook.exe"; Description: "Launch NLSI Exclusive Logbook"; Flags: postinstall nowait skipifsilent unchecked
Filename: "https://www.tiktok.com/@kape_073"; Description: "Follow @kape_073 on TikTok"; Flags: postinstall shellexec nowait skipifsilent unchecked
Filename: "https://github.com/Christian-0777/nlsi_telemetry/releases/tag/{#ReleaseTag}"; Description: "View release notes"; Flags: postinstall shellexec nowait skipifsilent unchecked
[UninstallRun]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\tools\InstallTruckSimPlugin.ps1"" -DllPath64 ""{app}\plugins\trucksim\win_x64\plugins\trucksim-gps-telemetry.dll"" -DllPath32 ""{app}\plugins\trucksim\win_x86\plugins\trucksim-gps-telemetry.dll"" -ManifestPath ""{app}\tools\installed-trucksim-plugins.json"" -StatusPath ""{app}\logs\trucksim-plugin-status.txt"" -RemoveManagedPlugin"; WorkingDir: "{app}"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveManagedTruckSimPlugin"
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\tools\InstallScsPositionPlugin.ps1"" -ManifestPath ""{app}\tools\installed-scs-position-plugins.json"" -BackupDirectory ""{app}\plugin-backups\scs-position"" -StatusPath ""{app}\logs\scs-position-plugin-status.txt"" -RestoreManagedPlugin"; WorkingDir: "{app}"; Flags: runhidden waituntilterminated; RunOnceId: "RestoreManagedScsPositionPlugin"

[Code]
const
  UninstallRegistryKey =
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\NLSI Exclusive Logbook_is1';
  DefaultInstallPath = 'C:\Program Files\NLSI Exclusive Logbook';

var
  PrivacyPage: TWizardPage;
  PrivacyMemo: TNewMemo;
  PrivacyAccepted: TNewCheckBox;
  InstallInfoPage: TWizardPage;
  InstallInfoText: TNewStaticText;
  ExistingInstallDir: String;
  ExistingInstallVersion: String;
  ExistingInstallDetected: Boolean;
  ExistingInstallRecovery: Boolean;
  ExistingInstallError: String;

function QueryUninstallValue(const ValueName: String; var Value: String): Boolean;
begin
  Result := RegQueryStringValue(HKLM, UninstallRegistryKey, ValueName, Value);
  if not Result and IsWin64 then
    Result := RegQueryStringValue(HKLM32, UninstallRegistryKey, ValueName, Value);
end;

function IsDigits(const Value: String): Boolean;
var
  Index: Integer;
begin
  Result := (Value <> '');
  for Index := 1 to Length(Value) do
    if (Value[Index] < '0') or (Value[Index] > '9') then begin
      Result := False;
      Exit;
    end;
end;

function ParseReleaseVersion(
  const Value: String;
  var Major: Integer;
  var Minor: Integer;
  var Patch: Integer;
  var ChannelRank: Integer): Boolean;
var
  Core: String;
  Channel: String;
  Part: String;
  FirstDot: Integer;
  SecondDot: Integer;
  Hyphen: Integer;
begin
  Result := False;
  Major := -1;
  Minor := -1;
  Patch := -1;
  ChannelRank := -1;

  Core := Trim(Value);
  Hyphen := Pos('-', Core);
  if Hyphen > 0 then begin
    Channel := Lowercase(Copy(Core, Hyphen + 1, Length(Core)));
    Core := Copy(Core, 1, Hyphen - 1);
  end else
    Channel := 'stable';

  if Channel = 'alpha' then
    ChannelRank := 1
  else if Channel = 'beta' then
    ChannelRank := 2
  else if Channel = 'stable' then
    ChannelRank := 3
  else
    Exit;

  FirstDot := Pos('.', Core);
  if FirstDot = 0 then Exit;
  Part := Copy(Core, 1, FirstDot - 1);
  if not IsDigits(Part) then Exit;
  Major := StrToIntDef(Part, -1);

  Core := Copy(Core, FirstDot + 1, Length(Core));
  SecondDot := Pos('.', Core);
  if SecondDot = 0 then Exit;
  Part := Copy(Core, 1, SecondDot - 1);
  if not IsDigits(Part) then Exit;
  Minor := StrToIntDef(Part, -1);

  Core := Copy(Core, SecondDot + 1, Length(Core));
  if Pos('.', Core) > 0 then Exit;
  if not IsDigits(Core) then Exit;
  Patch := StrToIntDef(Core, -1);
  Result := (Major >= 0) and (Minor >= 0) and (Patch >= 0);
end;

function CompareReleaseVersions(const LeftVersion: String;
  const RightVersion: String): Integer;
var
  LeftMajor: Integer;
  LeftMinor: Integer;
  LeftPatch: Integer;
  LeftChannel: Integer;
  RightMajor: Integer;
  RightMinor: Integer;
  RightPatch: Integer;
  RightChannel: Integer;
begin
  Result := 0;
  if not ParseReleaseVersion(LeftVersion, LeftMajor, LeftMinor, LeftPatch, LeftChannel) or
     not ParseReleaseVersion(RightVersion, RightMajor, RightMinor, RightPatch, RightChannel) then
    Exit;
  if LeftMajor <> RightMajor then
    Result := LeftMajor - RightMajor
  else if LeftMinor <> RightMinor then
    Result := LeftMinor - RightMinor
  else if LeftPatch <> RightPatch then
    Result := LeftPatch - RightPatch
  else
    Result := LeftChannel - RightChannel;
end;

function ReadManifestString(
  const Contents: String;
  const Key: String;
  var Value: String): Boolean;
var
  SearchStart: Integer;
  KeyStart: Integer;
  ColonPosition: Integer;
  ValueStart: Integer;
  ValueEnd: Integer;
  SearchText: String;
begin
  Result := False;
  Value := '';
  KeyStart := Pos('"' + Key + '"', Contents);
  if KeyStart = 0 then Exit;
  SearchStart := KeyStart + Length(Key) + 2;
  SearchText := Copy(Contents, SearchStart, Length(Contents));
  ColonPosition := Pos(':', SearchText);
  if ColonPosition = 0 then Exit;
  SearchStart := SearchStart + ColonPosition;
  while (SearchStart <= Length(Contents)) and
    ((Contents[SearchStart] = ' ') or (Contents[SearchStart] = #9) or
     (Contents[SearchStart] = #10) or (Contents[SearchStart] = #13)) do
    SearchStart := SearchStart + 1;
  if (SearchStart > Length(Contents)) or (Contents[SearchStart] <> '"') then Exit;
  ValueStart := SearchStart + 1;
  SearchText := Copy(Contents, ValueStart, Length(Contents));
  ValueEnd := Pos('"', SearchText);
  if ValueEnd = 0 then Exit;
  Value := Copy(Contents, ValueStart, ValueEnd - 1);
  Result := (Value <> '');
end;

function ReadManifestVersion(const InstallDir: String; var Version: String): Boolean;
var
  Contents: String;
  ManifestLines: TArrayOfString;
  Index: Integer;
  ManifestProduct: String;
  ManifestVersion: String;
  ManifestChannel: String;
  Major: Integer;
  Minor: Integer;
  Patch: Integer;
  ChannelRank: Integer;
begin
  Result := False;
  Version := '';
  if not LoadStringsFromFile(InstallDir + '\version.json', ManifestLines) then Exit;
  for Index := 0 to GetArrayLength(ManifestLines) - 1 do
    Contents := Contents + ManifestLines[Index] + #10;
  if not ReadManifestString(Contents, 'product', ManifestProduct) then Exit;
  if CompareText(ManifestProduct, 'NLSI Exclusive Logbook') <> 0 then Exit;
  if not ReadManifestString(Contents, 'version', ManifestVersion) then Exit;
  if not ReadManifestString(Contents, 'channel', ManifestChannel) then Exit;
  Version := ManifestVersion + '-' + Lowercase(ManifestChannel);
  if not ParseReleaseVersion(Version, Major, Minor, Patch, ChannelRank) then
    Exit;
  Result := True;
end;

function NormalizeInstallPath(const Value: String): String;
begin
  Result := ExpandFileName(Value);
  if (Length(Result) > 3) and (Result[Length(Result)] = '\') then
    SetLength(Result, Length(Result) - 1);
end;

function DirectoryHasEntries(const Directory: String): Boolean;
var
  FindRec: TFindRec;
begin
  Result := False;
  if FindFirst(AddBackslash(Directory) + '*', FindRec) then begin
    try
      repeat
        if (FindRec.Name <> '.') and (FindRec.Name <> '..') then begin
          Result := True;
          Exit;
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

function DetectExistingInstallation: Boolean;
var
  RegistryInstallDir: String;
  RegistryInstallVersion: String;
  ManifestInstallVersion: String;
  RegistryPathFound: Boolean;
  RegistryVersionFound: Boolean;
  ManifestVersionFound: Boolean;
  Major: Integer;
  Minor: Integer;
  Patch: Integer;
  ChannelRank: Integer;
begin
  ExistingInstallDir := DefaultInstallPath;
  ExistingInstallVersion := '';
  ExistingInstallDetected := False;
  ExistingInstallRecovery := False;
  ExistingInstallError := '';

  RegistryInstallDir := '';
  RegistryInstallVersion := '';
  RegistryPathFound := QueryUninstallValue('InstallLocation', RegistryInstallDir);
  if (not RegistryPathFound) or (Trim(RegistryInstallDir) = '') then
    RegistryPathFound := QueryUninstallValue('Inno Setup: App Path', RegistryInstallDir);

  if RegistryPathFound and (Trim(RegistryInstallDir) <> '') then
    ExistingInstallDir := RemoveBackslashUnlessRoot(Trim(RegistryInstallDir));
  Result := DirExists(ExistingInstallDir);

  if not Result then begin
    if FileExists(DefaultInstallPath + '\NLSI-Exclusive-Logbook.exe') or
      FileExists(DefaultInstallPath + '\version.json') then
      ExistingInstallError := 'FILES EXIST AT THE DEFAULT NLSI INSTALLATION PATH, BUT NO ' +
        'VALID INSTALLATION COULD BE IDENTIFIED. SETUP WILL NOT OVERWRITE AN UNKNOWN ' +
        'DIRECTORY: ' + DefaultInstallPath + '.'
    else begin
      ExistingInstallDir := ExpandConstant('{#DefaultApplicationDir}');
      Exit;
    end;
    Exit;
  end;

  if not FileExists(ExistingInstallDir + '\NLSI-Exclusive-Logbook.exe') then begin
    if ReadManifestVersion(ExistingInstallDir, ManifestInstallVersion) then begin
      ExistingInstallRecovery := True;
      ExistingInstallVersion := ManifestInstallVersion;
      Result := False;
      Log('A VALID NLSI INSTALLATION MANIFEST WAS FOUND WITHOUT THE APPLICATION EXECUTABLE; '
        + 'SETUP WILL REPAIR THE APPLICATION FILES AND PRESERVE EXISTING DATA.');
      Exit;
    end;
    if not DirectoryHasEntries(ExistingInstallDir) then begin
      Result := False;
      ExistingInstallDir := ExpandConstant('{#DefaultApplicationDir}');
      Exit;
    end;
    ExistingInstallError := 'THE DETECTED DIRECTORY HAS NO APPLICATION EXECUTABLE OR VALID ' +
      'NLSI VERSION MANIFEST. SETUP WILL NOT OVERWRITE UNKNOWN FILES: ' +
      ExistingInstallDir + '.';
    Exit;
  end;

  RegistryVersionFound := QueryUninstallValue('DisplayVersion', RegistryInstallVersion);
  RegistryVersionFound := RegistryVersionFound and ParseReleaseVersion(
    RegistryInstallVersion, Major, Minor, Patch, ChannelRank);
  ManifestVersionFound := ReadManifestVersion(ExistingInstallDir, ManifestInstallVersion);
  if not RegistryVersionFound and not ManifestVersionFound then begin
    ExistingInstallError := 'The installed NLSI version could not be validated at ' +
      ExistingInstallDir + '. Setup will not replace files without a known version.';
    Exit;
  end;
  if RegistryVersionFound and ManifestVersionFound and
    (CompareReleaseVersions(RegistryInstallVersion, ManifestInstallVersion) <> 0) then begin
    ExistingInstallError := 'The installed version metadata does not agree at ' +
      ExistingInstallDir + '. Repair the installation before continuing.';
    Exit;
  end;

  if RegistryVersionFound then
    ExistingInstallVersion := RegistryInstallVersion
  else
    ExistingInstallVersion := ManifestInstallVersion;
  ExistingInstallDetected := True;
end;

function InitializeSetup: Boolean;
begin
  Result := not WizardSilent;
  if not Result then begin
    Log('Silent installation is disabled because Terms and Privacy acceptance are required.');
    Exit;
  end;

  ExistingInstallDetected := DetectExistingInstallation;
  if ExistingInstallError <> '' then begin
    MsgBox(ExistingInstallError, mbCriticalError, MB_OK);
    Result := False;
    Exit;
  end;
  if (ExistingInstallDetected or ExistingInstallRecovery) and
    (CompareReleaseVersions('{#ReleaseLabel}', ExistingInstallVersion) < 0) then begin
    MsgBox('THE INSTALLED VERSION ' + ExistingInstallVersion + ' IS NEWER THAN THIS SETUP ' +
      '({#ReleaseLabel}). SETUP WILL NOT DOWNGRADE THE INSTALLATION.',
      mbCriticalError, MB_OK);
    Result := False;
  end;
end;

procedure InitializeWizard;
var
  PolicyText: String;
  PolicyLines: TArrayOfString;
  Index: Integer;
  InstallationSummary: String;
begin
  InstallInfoPage := CreateCustomPage(wpWelcome, 'INSTALLATION TYPE',
    'REVIEW THE INSTALLATION OR UPDATE DETAILS BEFORE CONTINUING.');
  InstallInfoText := TNewStaticText.Create(InstallInfoPage);
  InstallInfoText.Parent := InstallInfoPage.Surface;
  InstallInfoText.SetBounds(0, 0, InstallInfoPage.SurfaceWidth,
    InstallInfoPage.SurfaceHeight);
  InstallInfoText.Anchors := [akLeft, akTop, akRight, akBottom];
  InstallInfoText.AutoSize := False;
  InstallInfoText.WordWrap := True;
  if ExistingInstallRecovery then begin
    InstallationSummary :=
      'AN INCOMPLETE NLSI EXCLUSIVE LOGBOOK INSTALLATION WAS DETECTED.' + #13#10#13#10 +
      'THIS SETUP WILL REPAIR THE APPLICATION FILES; THE EXECUTABLE IS MISSING.' + #13#10 +
      'RECORDED VERSION: ' + ExistingInstallVersion + #13#10 +
      'NEW VERSION: V{#ReleaseLabel}' + #13#10 +
      'INSTALLATION DIRECTORY: ' + ExistingInstallDir + #13#10#13#10 +
      'USER CONFIGURATION, LOGS, AND OTHER EXISTING FILES WILL BE PRESERVED.';
  end else if ExistingInstallDetected then begin
    InstallationSummary :=
      'AN EXISTING NLSI EXCLUSIVE LOGBOOK INSTALLATION WAS DETECTED.' + #13#10#13#10 +
      'THIS SETUP WILL UPDATE THE EXISTING INSTALLATION.' + #13#10 +
      'INSTALLED VERSION: ' + ExistingInstallVersion + #13#10 +
      'NEW VERSION: V{#ReleaseLabel}' + #13#10 +
      'INSTALLATION DIRECTORY: ' + ExistingInstallDir + #13#10#13#10 +
      'EXISTING CONFIGURATION AND LOG FILES WILL BE PRESERVED.' + #13#10#13#10 +
      'SETUP DETECTS SUPPORTED STEAM ETS2/ATS INSTALLATIONS AND INSTALLS THE TRUCKSIM GPS PLUGIN AND THE SEPARATE SCS POSITION PLUGIN WHERE SUPPORTED. DIFFERENT EXISTING PLUGIN DLLS ARE PRESERVED, AND ANY EXISTING NLSI.DLL IS BACKED UP FOR RESTORATION.';
  end else begin
    InstallationSummary :=
      'NO VALID NLSI EXCLUSIVE LOGBOOK INSTALLATION WAS DETECTED.' + #13#10#13#10 +
      'THIS SETUP WILL PERFORM A FRESH INSTALLATION OF V{#ReleaseLabel} UNDER {#DefaultApplicationDir}.' + #13#10#13#10 +
      'SETUP DETECTS SUPPORTED STEAM ETS2/ATS INSTALLATIONS AND INSTALLS THE TRUCKSIM GPS PLUGIN AND THE SEPARATE SCS POSITION PLUGIN WHERE SUPPORTED. DIFFERENT EXISTING PLUGIN DLLS ARE PRESERVED, AND ANY EXISTING NLSI.DLL IS BACKED UP FOR RESTORATION.';
  end;
  if DirExists(ExpandConstant('{#LegacyDirectory}')) then
    InstallationSummary := InstallationSummary + #13#10#13#10 +
      'THE LEGACY C:\NLSI-TEM DIRECTORY WILL BE LEFT UNTOUCHED.';
  InstallInfoText.Caption := InstallationSummary;

  ExtractTemporaryFile('PrivacyPolicy.txt');
  if not LoadStringsFromFile(ExpandConstant('{tmp}\PrivacyPolicy.txt'), PolicyLines) then
    RaiseException('The Privacy Policy could not be loaded. Setup cannot continue.');
  PolicyText := '';
  for Index := 0 to GetArrayLength(PolicyLines) - 1 do
    PolicyText := PolicyText + PolicyLines[Index] + #13#10;

  PrivacyPage := CreateCustomPage(wpLicense, 'PRIVACY POLICY',
    'REVIEW AND ACCEPT THE PRIVACY POLICY TO CONTINUE.');
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
  PrivacyAccepted.Caption := 'I HAVE READ AND ACCEPT THE PRIVACY POLICY.';
  PrivacyAccepted.Checked := False;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectDir) and
     (ExistingInstallDetected or ExistingInstallRecovery) and
     (CompareText(NormalizeInstallPath(WizardDirValue),
       NormalizeInstallPath(ExistingInstallDir)) <> 0) then begin
    MsgBox('THIS SETUP MUST USE THE EXISTING INSTALLATION DIRECTORY: ' +
      ExistingInstallDir + '. CHOOSE THAT DIRECTORY TO AVOID CREATING A SECOND COPY.',
      mbInformation, MB_OK);
    Result := False;
    Exit;
  end;
  if (PrivacyPage <> nil) and (CurPageID = PrivacyPage.ID) and
     not PrivacyAccepted.Checked then begin
    MsgBox('YOU MUST ACCEPT THE PRIVACY POLICY BEFORE CONTINUING.',
      mbInformation, MB_OK);
    Result := False;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpSelectDir) and
     (ExistingInstallDetected or ExistingInstallRecovery) then
    WizardForm.DirEdit.Text := ExistingInstallDir
  else if CurPageID = wpFinished then begin
    WizardForm.FinishedHeadingLabel.Caption := 'Installation complete';
    WizardForm.FinishedLabel.Caption :=
      'NLSI Exclusive Logbook v{#ReleaseLabel} has been installed.' + #13#10#13#10 +
      'What''s New' + #13#10 +
      '- Direct TruckSim GPS revision-13 telemetry; the separate server client is not required.' + #13#10 +
      '- Capture complete revision-13 raw samples with local-only, pending synchronization records; no online service is configured.' + #13#10 +
      '- Local events, jobs, sessions, existing .nlsi logs, and TXT logs remain supported.' + #13#10 +
      '- Setup installs the verified TruckSim GPS plugin separately from the SCS SDK position-only nlsi.dll plugin.' + #13#10 +
      '- The SCS plugin publishes only ETS2/ATS world coordinates over its own versioned IPC mapping.' + #13#10#13#10 +
      'The About page includes Nabski Logistics and Solutions Inc. community and creator links.';
  end;
end;