#define AppName "NLSI Exclusive Logbook"
#define AppVersion "__APP_VERSION__"
#define AppPublisher "NLSI"
#define AppURL "https://github.com/Christian-0777/nlsi_telemetry"

[Setup]
AppId=NLSI Exclusive Logbook
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName=C:\Program Files\NLSI Exclusive Logbook
DefaultGroupName={#AppName}
OutputBaseFilename=NLSI-Telemetry-Setup-v{#AppVersion}
OutputDir=..\build\v{#AppVersion}
SourceDir=staging
SetupIconFile=__SETUP_ICON__
Compression=lzma
SolidCompression=yes
PrivilegesRequired=admin
UninstallDisplayName={#AppName}

[InstallDelete]
Type: files; Name: "{commonstartup}\NLSI Telemetry.lnk"
Type: files; Name: "{commonstartup}\NLSI Telemetry\NLSI Telemetry.bat"

[Files]
Source: "app\*"; DestDir: "{app}\app"; Flags: recursesubdirs createallsubdirs
Source: "bin\*"; DestDir: "{app}\bin"; Flags: recursesubdirs createallsubdirs
Source: "config\.env.example"; DestDir: "{app}\config"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "version.json"; DestDir: "{app}"
Source: "NLSI-Telemetry.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "tools\InstallGamePlugins.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion

[UninstallRun]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\tools\InstallGamePlugins.ps1"" -DllPath ""{app}\bin\nlsi_telemetry.dll"" -ManifestPath ""{app}\tools\installed-game-plugins.txt"" -StatusPath ""{app}\logs\game-plugin-status.txt"" -RemoveInstalledPlugins"; WorkingDir: "{app}"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveManagedGamePlugins"

[UninstallDelete]
Type: files; Name: "{app}\tools\installed-game-plugins.txt"

[Dirs]
Name: "{app}\config"; Flags: uninsneveruninstall
Name: "{app}\data"; Flags: uninsneveruninstall
Name: "{app}\logs"; Flags: uninsneveruninstall
Name: "{app}\runtime"; Flags: uninsneveruninstall
Name: "{app}\app\test\output"; Flags: uninsneveruninstall

[Icons]
Name: "{group}\NLSI Telemetry"; Filename: "{app}\NLSI-Telemetry.bat"; WorkingDir: "{app}\app"; IconFilename: "{app}\app\img\logo.ico"
Name: "{autodesktop}\NLSI Telemetry"; Filename: "{app}\NLSI-Telemetry.bat"; WorkingDir: "{app}\app"; IconFilename: "{app}\app\img\logo.ico"

[Code]
type
  TSocialLink = record
    URL: String;
    CheckBox: TNewCheckBox;
  end;

var
  FollowPage: TWizardPage;
  PluginStatusMemo: TNewMemo;
  SocialLinks: array of TSocialLink;

procedure AddSocialLink(const Caption, URL: String);
var
  LinkIndex: Integer;
  CheckBox: TNewCheckBox;
begin
  LinkIndex := GetArrayLength(SocialLinks);
  SetArrayLength(SocialLinks, LinkIndex + 1);
  SocialLinks[LinkIndex].URL := URL;

  CheckBox := TNewCheckBox.Create(FollowPage);
  CheckBox.Parent := FollowPage.Surface;
  CheckBox.Left := 0;
  CheckBox.Top := PluginStatusMemo.Top + PluginStatusMemo.Height + ScaleY(12) +
    LinkIndex * ScaleY(24);
  CheckBox.Width := FollowPage.SurfaceWidth;
  CheckBox.Caption := Caption;
  CheckBox.Checked := False;
  SocialLinks[LinkIndex].CheckBox := CheckBox;
end;

procedure InitializeWizard;
var
  DescriptionLabel: TNewStaticText;
begin
  FollowPage := CreateCustomPage(wpInfoAfter, 'Follow NLSI Socials', 'Stay connected with NLSI and review the latest release notes.');

  DescriptionLabel := TNewStaticText.Create(FollowPage);
  DescriptionLabel.Parent := FollowPage.Surface;
  DescriptionLabel.Left := 0;
  DescriptionLabel.Top := 0;
  DescriptionLabel.Width := FollowPage.SurfaceWidth;
  DescriptionLabel.Height := ScaleY(32);
  DescriptionLabel.AutoSize := False;
  DescriptionLabel.WordWrap := True;
  DescriptionLabel.Caption := 'The installation is complete. You can finish without opening any external links.';

  PluginStatusMemo := TNewMemo.Create(FollowPage);
  PluginStatusMemo.Parent := FollowPage.Surface;
  PluginStatusMemo.Left := 0;
  PluginStatusMemo.Top := DescriptionLabel.Top + DescriptionLabel.Height + ScaleY(8);
  PluginStatusMemo.Width := FollowPage.SurfaceWidth;
  PluginStatusMemo.Height := ScaleY(72);
  PluginStatusMemo.ReadOnly := True;
  PluginStatusMemo.ScrollBars := ssVertical;
  PluginStatusMemo.TabStop := False;
  PluginStatusMemo.Text := 'Plugin status is recorded in the installation logs.';

  AddSocialLink('Follow NLSI Socials', 'https://github.com/Christian-0777/nlsi_telemetry');
  AddSocialLink('View Release Notes', 'https://github.com/Christian-0777/nlsi_telemetry/releases');
end;

procedure CurPageChanged(CurPageID: Integer);
var
  StatusText: String;
  StatusPath: String;
  StatusLines: TArrayOfString;
  Index: Integer;
begin
  if (FollowPage <> nil) and (CurPageID = FollowPage.ID) then begin
    StatusPath := ExpandConstant('{app}\logs\game-plugin-status.txt');
    if LoadStringsFromFile(StatusPath, StatusLines) then begin
      StatusText := '';
      for Index := 0 to GetArrayLength(StatusLines) - 1 do
        StatusText := StatusText + StatusLines[Index] + #13#10;
      PluginStatusMemo.Text := StatusText
    end else
      PluginStatusMemo.Text := 'Game plugin status is unavailable. Check the installer log.';
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ErrorCode: Integer;
  ResultCode: Integer;
  PowerShellPath: String;
  PowerShellParams: String;
  StatusPath: String;
  StatusLines: TArrayOfString;
  StatusText: String;
  Index: Integer;
  LinkIndex: Integer;
begin
  if CurStep = ssPostInstall then begin
    PowerShellPath := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
    PowerShellParams := '-NoProfile -ExecutionPolicy Bypass -File "' +
      ExpandConstant('{app}\tools\InstallGamePlugins.ps1') +
      '" -DllPath "' + ExpandConstant('{app}\bin\nlsi_telemetry.dll') +
      '" -ManifestPath "' + ExpandConstant('{app}\tools\installed-game-plugins.txt') +
      '" -StatusPath "' + ExpandConstant('{app}\logs\game-plugin-status.txt') + '"';
    if not Exec(PowerShellPath, PowerShellParams, ExpandConstant('{app}'),
      SW_HIDE, ewWaitUntilTerminated, ResultCode) then
      RaiseException('Could not start the game plugin installer: ' + SysErrorMessage(ResultCode));

    if ResultCode <> 0 then begin
      StatusPath := ExpandConstant('{app}\logs\game-plugin-status.txt');
      StatusText := '';
      if LoadStringsFromFile(StatusPath, StatusLines) then
        for Index := 0 to GetArrayLength(StatusLines) - 1 do
          StatusText := StatusText + StatusLines[Index] + #13#10;
      RaiseException('Could not install the game plugin. Close ETS2 or ATS, then run setup again.' + #13#10 +
        StatusText + #13#10 + 'See the game-plugin-install.log file for details.');
    end;
  end;

  if CurStep = ssDone then begin
    for LinkIndex := 0 to GetArrayLength(SocialLinks) - 1 do begin
      if SocialLinks[LinkIndex].CheckBox.Checked then begin
        if not ShellExec('open', SocialLinks[LinkIndex].URL, '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode) then
          MsgBox('Could not open ' + SocialLinks[LinkIndex].URL + '.', mbError, MB_OK);
      end;
    end;
  end;
end;
