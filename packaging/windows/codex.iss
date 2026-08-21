; Instalador de Windows (Inno Setup 6). Lo compila la CI, pero a mano es:
;
;   iscc /DMyAppVersion=1.2.0 /DStagingDir=..\..\dist packaging\windows\codex.iss
;
; StagingDir es la carpeta que deja `cmake --install` y rellena windeployqt:
; codex.exe con sus DLL de Qt y sus plugins al lado.

#define MyAppName "Codex"
#define MyAppPublisher "Stride"
#define MyAppURL "https://github.com/Larzt/codex"
#define MyAppExeName "codex.exe"

#ifndef MyAppVersion
  #define MyAppVersion "1.2.0"
#endif
#ifndef StagingDir
  #define StagingDir "..\..\dist"
#endif

[Setup]
; Este GUID identifica la aplicación para actualizarla y desinstalarla: no se
; cambia nunca, o cada versión se instalaría al lado de la anterior.
AppId={{D2A4220F-0955-49DA-B5AA-15D422189676}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
VersionInfoVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases

; Sin privilegios de administrador: se instala para el usuario que lo lanza
; (%LOCALAPPDATA%\Programs\Codex) y no sale ninguna ventana de UAC. Quien
; quiera ponerlo para todo el equipo puede elegirlo en el diálogo.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes

; Qt 6.9 no admite nada anterior a Windows 10 1809.
MinVersion=10.0.17763
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

OutputDir=..\..\dist-installer
OutputBaseFilename=Codex-{#MyAppVersion}-setup
SetupIconFile=codex.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[CustomMessages]
english.StartupTask=Open Codex when I sign in
english.DesktopIconTask=Create a shortcut on the desktop
spanish.StartupTask=Abrir Códice al iniciar sesión
spanish.DesktopIconTask=Crear un acceso directo en el escritorio

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIconTask}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startup"; Description: "{cm:StartupTask}"; Flags: unchecked

[Files]
; Todo lo que dejó windeployqt: el .exe, las DLL de Qt y las carpetas de
; plugins (platforms\, multimedia\, tls\...). Sin ellas la aplicación no
; arranca, así que van en bloque.
Source: "{#StagingDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
; En la carpeta de Inicio del usuario, no en el registro: se ve y se quita
; desde el Explorador, y el desinstalador se lo lleva.
Name: "{userstartup}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: startup

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

; Las notas viven en %APPDATA%\Stride\Codex y no se tocan al desinstalar:
; reinstalar tiene que devolverte tus notas, no una pizarra en blanco.
