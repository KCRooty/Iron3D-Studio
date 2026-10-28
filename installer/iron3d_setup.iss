; Iron3D Studio -- instalador Windows (Fase 4, Documento Maestro + pedido de
; Core 2026-10-28: "quiero hacer un ejecutable que sea como el instalador...
; identico al de cinema 4d y el registro obviamente sin la parte de las
; licencias"). Flujo calcado del instalador real de Cinema 4D R19 (9 capturas
; de referencia de Core, de un tutorial de YouTube) en la MEDIDA en que tiene
; sentido para un producto de un solo ejecutable: bienvenida -> info/soporte
; -> EULA con checkbox -> SIN pagina de registro/serial (excluida a
; proposito) -> eleccion de backend de render (Vulkan/OpenGL, el "o en la
; instalacion" del pedido original) -> componentes -> carpeta de destino ->
; grupo del menu inicio -> instalando -> fin con opcion de lanzar la app.
;
; Clean-room: branding 100% propio (icono I3DS existente del repo, fondo
; oscuro igual que el clear color del viewport), nada de arte real de Maxon
; -- solo se replica la ESTRUCTURA del asistente, nunca sus imagenes.
;
; Requiere: Inno Setup 6 (ISCC.exe) + una build Release ya hecha en
; build\bin\Release\ (ver CMakeLists.txt). Compilar desde esta carpeta:
;   ISCC.exe iron3d_setup.iss

#define MyAppName "Iron3D Studio"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "Static Forge"
#define MyAppURL "https://staticforge.dpdns.org/i3ds.html"
#define MyAppExeName "Iron3DStudio.exe"

[Setup]
AppId={{DA8F4478-DF87-4033-8C76-13616F7E4B6E}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\Static Forge\Iron3D Studio
DefaultGroupName=Iron3D Studio
DisableProgramGroupPage=no
LicenseFile=..\LICENSE-MIT
InfoBeforeFile=installer_readme.txt
OutputDir=output
OutputBaseFilename=Iron3DStudio-Setup-{#MyAppVersion}
SetupIconFile=assets\setup_icon.ico
WizardImageFile=assets\wizard_image.bmp
WizardSmallImageFile=assets\wizard_small.bmp
WizardStyle=classic
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Types]
Name: "full"; Description: "Completa"
Name: "custom"; Description: "Personalizada"; Flags: iscustom

[Components]
Name: "main"; Description: "Iron3D Studio (nucleo)"; Types: full custom; Flags: fixed
Name: "samples"; Description: "Escenas de ejemplo"; Types: full
Name: "docs"; Description: "Documentacion (README, AGENTS.md, licencias)"; Types: full

[Files]
Source: "..\build\bin\Release\{#MyAppExeName}"; DestDir: "{app}"; Components: main; Flags: ignoreversion
Source: "..\build\bin\Release\shaders\*"; DestDir: "{app}\shaders"; Components: main; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "samples\*.iron"; DestDir: "{app}\scenes"; Components: samples; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Components: docs; Flags: ignoreversion
Source: "..\AGENTS.md"; DestDir: "{app}"; Components: docs; Flags: ignoreversion
Source: "..\LICENSE-MIT"; DestDir: "{app}"; Components: docs; Flags: ignoreversion
Source: "..\LICENSE-APACHE"; DestDir: "{app}"; Components: docs; Flags: ignoreversion

[Icons]
Name: "{group}\Iron3D Studio"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,Iron3D Studio}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Iron3D Studio"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,Iron3D Studio}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; preferences.cfg lo escribe el script [Code] (CurStepChanged), no esta en
; [Files], asi que Inno no lo rastrea solo -- sin esto quedaba huerfano tras
; desinstalar (visto al probar el ciclo completo instalar/desinstalar).
Type: files; Name: "{app}\preferences.cfg"

[Code]
var
  BackendPage: TWizardPage;
  RadioVulkan, RadioOpenGL: TNewRadioButton;
  BackendDesc: TNewStaticText;

procedure InitializeWizard;
begin
  // Pagina propia tras el EULA y SIN pagina de registro/serial (excluida a
  // proposito por pedido explicito de Core): el equivalente honesto del
  // "tipo de instalacion" de C4D para un producto de un solo ejecutable es
  // elegir aqui la API de render, exactamente el "o que puedas escogerlo en
  // la instalacion" del pedido original. Se escribe directo al mismo
  // preferences.cfg que lee Iron3D Studio en tiempo de ejecucion (ver
  // core/Preferences.cpp) -- formato identico, sin tocar su parser.
  BackendPage := CreateCustomPage(wpLicense, 'Backend de render',
    'Elige la API grafica que usara Iron3D Studio por defecto.');

  BackendDesc := TNewStaticText.Create(BackendPage);
  BackendDesc.Parent := BackendPage.Surface;
  BackendDesc.Caption := 'Se puede cambiar mas adelante desde Preferencias dentro de la '
    + 'aplicacion -- el cambio se aplica al reiniciar Iron3D Studio, no en caliente.';
  BackendDesc.AutoSize := False;
  BackendDesc.WordWrap := True;
  BackendDesc.Width := BackendPage.SurfaceWidth;
  BackendDesc.Height := ScaleY(32);
  BackendDesc.Top := 0;

  RadioVulkan := TNewRadioButton.Create(BackendPage);
  RadioVulkan.Parent := BackendPage.Surface;
  RadioVulkan.Caption := 'Vulkan (recomendado)';
  RadioVulkan.Top := BackendDesc.Top + BackendDesc.Height + ScaleY(12);
  RadioVulkan.Width := BackendPage.SurfaceWidth;
  RadioVulkan.Checked := True;

  RadioOpenGL := TNewRadioButton.Create(BackendPage);
  RadioOpenGL.Parent := BackendPage.Surface;
  RadioOpenGL.Caption := 'OpenGL';
  RadioOpenGL.Top := RadioVulkan.Top + RadioVulkan.Height + ScaleY(4);
  RadioOpenGL.Width := BackendPage.SurfaceWidth;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Backend: String;
  PrefsPath: String;
begin
  if CurStep = ssPostInstall then
  begin
    if RadioOpenGL.Checked then
      Backend := 'opengl'
    else
      Backend := 'vulkan';
    PrefsPath := ExpandConstant('{app}') + '\preferences.cfg';
    SaveStringToFile(PrefsPath, 'IRON3D_PREFS 1' + #13#10 + 'renderBackend=' + Backend + #13#10, False);
  end;
end;
