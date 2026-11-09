; Launchme's Windows installer (Inno Setup 6.6 or newer), built by tools/build-installer.ps1:
;   ISCC /DAppVersion=<x.y.z> /DSourceDir=<cmake --install prefix> installer\launchme.iss
; Per user by default, no UAC: the program goes to %LOCALAPPDATA%\Programs\Launchme, a folder its own
; updater (src/platform/updater.hpp) can write to. "Install for all users" (or /ALLUSERS) asks for an
; administrator and uses Program Files; that copy updates by running the next Setup elevated.
; Settings and caches stay per user in %LOCALAPPDATA%\Launchme.
; Running it again over an installed copy upgrades it in place (same AppId).

#if Ver < EncodeVer(6, 6, 0)
  #error Inno Setup 6.6 or newer is needed (dark mode, WizardStyle=modern dynamic).
#endif
#ifndef AppVersion
  #error Pass /DAppVersion=<x.y.z> (the version in CMakeLists.txt's project()).
#endif
#ifndef SourceDir
  #error Pass /DSourceDir=<folder> (cmake --install ... --component Runtime --prefix <folder>).
#endif
#ifndef OutputDir
  #define OutputDir "..\build\installer"
#endif

#define AppName "Launchme"
#define AppExe "launchme.exe"
; Never change: it ties upgrades and the uninstall entry together (also in updater.hpp).
#define AppGuid "97E152F6-543A-4A06-B825-E709B1738F2A"

[Setup]
AppId={{{#AppGuid}}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=uxandai
AppPublisherURL=https://uxandai.github.io/launchme-website/
AppSupportURL=https://github.com/uxandai/launchme/issues
AppUpdatesURL=https://github.com/uxandai/launchme/releases
AppCopyright=GPL-3.0-or-later
VersionInfoVersion={#AppVersion}.0
VersionInfoProductVersion={#AppVersion}
VersionInfoDescription={#AppName} Setup
; Windows 10/11, 64-bit (also x64 emulation on Arm64).
MinVersion=10.0
#if Ver >= EncodeVer(7, 0, 0)
; Inno Setup 7 builds a 32-bit Setup unless told otherwise.
SetupArchitecture=x64
#endif
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Per user unless the start dialog (or /ALLUSERS) chooses all users.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
DefaultDirName={autopf}\{#AppName}
DisableDirPage=auto
DisableProgramGroupPage=yes
DefaultGroupName={#AppName}
UsePreviousAppDir=yes
UsePreviousTasks=yes
; The launcher still running is closed through the Restart Manager, not overwritten under it.
CloseApplications=yes
RestartApplications=no
SetupIconFile=..\res\launchme.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
WizardStyle=modern dynamic
ShowLanguageDialog=auto
Compression=lzma2/ultra64
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename=launchme-{#AppVersion}-win64-setup
#ifdef Sign
; tools/build-installer.ps1 -Sign defines the "launchme" sign tool for ISCC (/S).
SignTool=launchme
SignedUninstaller=yes
#endif

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "pl"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"

[CustomMessages]
en.RemoveData=Also remove your Launchme settings, accounts and caches?%n%n%1%n%nChoose No to keep them for a later install.
pl.RemoveData=Usunąć także ustawienia, konta i pamięć podręczną Launchme?%n%n%1%n%nWybierz Nie, aby zachować je na później.
ptbr.RemoveData=Remover também as configurações, contas e caches do Launchme?%n%n%1%n%nEscolha Não para mantê-los para uma instalação futura.
de.RemoveData=Auch die Einstellungen, Konten und Caches von Launchme entfernen?%n%n%1%n%nWähle Nein, um sie für eine spätere Installation zu behalten.
ru.RemoveData=Удалить также настройки, учётные записи и кэш Launchme?%n%n%1%n%nВыберите «Нет», чтобы сохранить их для следующей установки.
es.RemoveData=¿Eliminar también la configuración, las cuentas y la caché de Launchme?%n%n%1%n%nElige No para conservarlas para una instalación posterior.
en.CloseLaunchme=Launchme is running. Close it, then choose OK to continue.
pl.CloseLaunchme=Launchme jest uruchomiony. Zamknij go, a potem wybierz OK.
ptbr.CloseLaunchme=O Launchme está em execução. Feche-o e escolha OK para continuar.
de.CloseLaunchme=Launchme läuft gerade. Schließe es und wähle dann OK, um fortzufahren.
ru.CloseLaunchme=Launchme запущен. Закройте его, затем нажмите OK, чтобы продолжить.
es.CloseLaunchme=Launchme se está ejecutando. Ciérralo y elige Aceptar para continuar.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Exactly the release ZIP's contents (cmake --install, component Runtime).
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
; The launcher's own update (/RELAUNCH=1): the new version starts again, as the user, not elevated.
Filename: "{app}\{#AppExe}"; Flags: nowait runasoriginaluser; Check: Relaunch

[InstallDelete]
; An interrupted self-update's leftovers.
Type: filesandordirs; Name: "{app}\.update"
Type: files; Name: "{app}\*.update-old"

[UninstallDelete]
; Files a self-update brought in after this installer ran are not in its log.
Type: filesandordirs; Name: "{app}\assets"
Type: filesandordirs; Name: "{app}\plugin-packages"
Type: filesandordirs; Name: "{app}\.update"
Type: files; Name: "{app}\*.update-old"
Type: dirifempty; Name: "{app}"

[Code]
const
  WindowClass = 'LaunchmeNativeWindow'; // src/main.cpp's main window.

// Started by the launcher's update (src/app/updates.cpp), which closed itself for it.
function Relaunch: Boolean;
begin
  Result := ExpandConstant('{param:RELAUNCH|0}') = '1';
end;

function DataFolder: String;
begin
  Result := ExpandConstant('{localappdata}\Launchme');
end;

// The uninstaller has no Restart Manager: it waits until the launcher is closed (a silent one gives up).
function InitializeUninstall: Boolean;
begin
  Result := True;
  while FindWindowByClassName(WindowClass) <> 0 do
    if UninstallSilent or (MsgBox(CustomMessage('CloseLaunchme'), mbInformation, MB_OKCANCEL) <> IDOK) then
    begin
      Result := False;
      Exit;
    end;
end;

// Personal data stays unless the person asks for it to go (never in a silent uninstall).
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and not UninstallSilent and DirExists(DataFolder) then
    if SuppressibleMsgBox(FmtMessage(CustomMessage('RemoveData'), [DataFolder]), mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES then
      DelTree(DataFolder, True, True, True);
end;
