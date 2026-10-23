; AvatarCam telepítő (Inno Setup 6). Fordítás: tools\make_release.ps1, vagy
;   ISCC.exe /DAppVersion=0.2 installer\AvatarCam.iss
; Felhasználónkénti telepítés (nem kell rendszergazda), mert a program a beállításait az exe mellé menti.

#ifndef AppVersion
  #define AppVersion "0.2"
#endif
#define Root ".."

[Setup]
AppId={{6E0B7B52-3C1F-4C8E-9A57-1F7D2C9E4A31}
AppName=AvatarCam
AppVersion={#AppVersion}
AppVerName=AvatarCam {#AppVersion}
AppPublisher=FrankHUN88453
AppPublisherURL=https://github.com/FrankHUN88453/AvatarCam
AppSupportURL=https://github.com/FrankHUN88453/AvatarCam/issues
AppUpdatesURL=https://github.com/FrankHUN88453/AvatarCam/releases
VersionInfoVersion={#AppVersion}
VersionInfoProductName=AvatarCam
VersionInfoDescription=AvatarCam Setup
DefaultDirName={autopf}\AvatarCam
DefaultGroupName=AvatarCam
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
CloseApplications=force
CloseApplicationsFilter=AvatarCam.exe
RestartApplications=no
SetupIconFile={#Root}\src\app.ico
UninstallDisplayIcon={app}\AvatarCam.exe
UninstallDisplayName=AvatarCam
WizardStyle=modern
ShowLanguageDialog=yes
LicenseFile={#Root}\LICENSE
OutputDir={#Root}\dist
OutputBaseFilename=AvatarCam-{#AppVersion}-setup
Compression=lzma2/ultra64
SolidCompression=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "hungarian"; MessagesFile: "compiler:Languages\Hungarian.isl"

[CustomMessages]
english.DeleteUserData=Do you also want to delete your AvatarCam settings, presets and imported avatars?%n%nChoose No to keep them for a later installation.
hungarian.DeleteUserData=Az AvatarCam beállításait, előbeállításait és az importált avatárokat is törölni szeretnéd?%n%nA Nem gombbal megmaradnak egy későbbi telepítéshez.
english.ReadmeEntry=Read me
hungarian.ReadmeEntry=Leírás

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#Root}\bin\AvatarCam.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\bin\onnxruntime.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\bin\onnxruntime_providers_shared.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\bin\models\*"; DestDir: "{app}\models"; Flags: ignoreversion recursesubdirs
Source: "{#Root}\bin\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs
Source: "{#Root}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\README.hu.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\AvatarCam"; Filename: "{app}\AvatarCam.exe"
Name: "{group}\{cm:ReadmeEntry}"; Filename: "{app}\README.md"; Languages: english
Name: "{group}\{cm:ReadmeEntry}"; Filename: "{app}\README.hu.md"; Languages: hungarian
Name: "{group}\{cm:UninstallProgram,AvatarCam}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\AvatarCam"; Filename: "{app}\AvatarCam.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\AvatarCam.exe"; Description: "{cm:LaunchProgram,AvatarCam}"; Flags: nowait postinstall skipifsilent
; a programból indított frissítés (/update=1) után a program újraindul
Filename: "{app}\AvatarCam.exe"; Flags: nowait; Check: IsUpdate

[Code]
// a programból indított frissítés: AvatarCam-x-setup.exe /SILENT /update=1 /DIR=...
function IsUpdate: Boolean;
begin
  Result := ExpandConstant('{param:update|0}') = '1';
end;

// Magyar telepítésnél a program is magyarul induljon (csak ha még nincs beállításfájl)
procedure CurStepChanged(CurStep: TSetupStep);
var
  SettingsFile: String;
begin
  if (CurStep = ssPostInstall) and (ActiveLanguage = 'hungarian') then
  begin
    SettingsFile := ExpandConstant('{app}\settings.json');
    if not FileExists(SettingsFile) then
      SaveStringToFile(SettingsFile, '{"language": 1}', False);
  end;
end;

// Eltávolításkor megkérdezzük, hogy a felhasználó adatai (beállítások, avatárok) is törlődjenek-e
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  App: String;
begin
  if (CurUninstallStep = usUninstall) and not UninstallSilent then
  begin
    if MsgBox(CustomMessage('DeleteUserData'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
    begin
      App := ExpandConstant('{app}');
      DelTree(App + '\avatars', True, True, True);
      DelTree(App + '\presets', True, True, True);
      DeleteFile(App + '\settings.json');
      DeleteFile(App + '\imgui.ini');
      DeleteFile(App + '\avatarcam.log');
      DeleteFile(App + '\avatarcam_crash.dmp');
    end;
  end;
end;
