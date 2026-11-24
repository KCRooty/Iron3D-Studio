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
#define MyAppVersion "0.3.0-alpha"
#define MyAppPublisher "Static Forge"
#define MyAppURL "https://staticforge.me/i3ds.html"
#define MyAppExeName "Iron3DStudio.exe"

[Setup]
AppId={{DA8F4478-DF87-4033-8C76-13616F7E4B6E}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
; Propiedades > Detalles del propio instalador (version, copyright, descripcion). MyAppVersion se iguala
; con IRON3D_VERSION_* de CMakeLists.txt.
VersionInfoVersion=0.3.0.0
VersionInfoProductVersion=0.3.0.0
VersionInfoProductTextVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoProductName={#MyAppName}
VersionInfoDescription={#MyAppName} - instalador / installer
VersionInfoCopyright=Copyright (c) 2026 Static Forge
AppCopyright=Copyright (c) 2026 Static Forge
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
; Asociaciones de archivo (tareas assoc*): avisa al Explorador al terminar.
ChangesAssociations=yes

; Idioma (2026-11-20): como Cinema 4D, el instalador deja ELEGIR el idioma al
; arrancar (dialogo de Inno, activo porque hay varios) y esa eleccion se guarda
; en {app}\language.cfg para que Iron3D Studio arranque en ese idioma. Despues
; se cambia en Preferencias > Interfaz. El ingles va primero (idioma por
; defecto si el del sistema no esta en la lista). El chino no esta aqui porque
; Inno Setup no trae su .isl: la app si lo tiene (se elige dentro).
[Languages]
Name: "english";  MessagesFile: "compiler:Default.isl"
Name: "spanish";  MessagesFile: "compiler:Languages\Spanish.isl"
Name: "german";   MessagesFile: "compiler:Languages\German.isl"
Name: "french";   MessagesFile: "compiler:Languages\French.isl"
Name: "italian";  MessagesFile: "compiler:Languages\Italian.isl"
Name: "polish";   MessagesFile: "compiler:Languages\Polish.isl"
Name: "czech";    MessagesFile: "compiler:Languages\Czech.isl"
Name: "russian";  MessagesFile: "compiler:Languages\Russian.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "korean";   MessagesFile: "compiler:Languages\Korean.isl"

[CustomMessages]
english.AssocGroup=Associate files:
english.AssocIron=Open .iron scenes with Iron3D Studio
english.AssocLib=Open .lib4d libraries in the Content Browser
english.AssocC4d=Open .c4d files with Iron3D Studio (removes the Cinema 4D association)
english.TypeFull=Full
english.TypeCustom=Custom
english.CompMain=Iron3D Studio (core)
english.CompSamples=Sample scenes
english.CompDocs=Documentation (README, AGENTS.md, licenses)
english.BackendTitle=Render backend
english.BackendSub=Choose the graphics API Iron3D Studio will use by default.
english.BackendDesc=You can change it later in Preferences inside the application; the change applies when Iron3D Studio restarts, not live.
english.BackendVulkan=Vulkan (recommended)
spanish.AssocGroup=Asociar archivos:
spanish.AssocIron=Abrir las escenas .iron con Iron3D Studio
spanish.AssocLib=Abrir las librerías .lib4d en el Navegador de Contenido
spanish.AssocC4d=Abrir los .c4d con Iron3D Studio (quita la asociación de Cinema 4D)
spanish.TypeFull=Completa
spanish.TypeCustom=Personalizada
spanish.CompMain=Iron3D Studio (núcleo)
spanish.CompSamples=Escenas de ejemplo
spanish.CompDocs=Documentación (README, AGENTS.md, licencias)
spanish.BackendTitle=Backend de render
spanish.BackendSub=Elige la API gráfica que usará Iron3D Studio por defecto.
spanish.BackendDesc=Se puede cambiar más adelante desde Preferencias dentro de la aplicación; el cambio se aplica al reiniciar Iron3D Studio, no en caliente.
spanish.BackendVulkan=Vulkan (recomendado)
german.AssocGroup=Dateien zuordnen:
german.AssocIron=.iron-Szenen mit Iron3D Studio öffnen
german.AssocLib=.lib4d-Bibliotheken im Content Browser öffnen
german.AssocC4d=.c4d-Dateien mit Iron3D Studio öffnen (entfernt die Zuordnung zu Cinema 4D)
german.TypeFull=Vollständig
german.TypeCustom=Benutzerdefiniert
german.CompMain=Iron3D Studio (Kern)
german.CompSamples=Beispielszenen
german.CompDocs=Dokumentation (README, AGENTS.md, Lizenzen)
german.BackendTitle=Render-Backend
german.BackendSub=Wählen Sie die Grafik-API, die Iron3D Studio standardmäßig verwendet.
german.BackendDesc=Sie können sie später in den Einstellungen der Anwendung ändern; die Änderung gilt nach einem Neustart von Iron3D Studio.
german.BackendVulkan=Vulkan (empfohlen)
french.AssocGroup=Associer les fichiers :
french.AssocIron=Ouvrir les scènes .iron avec Iron3D Studio
french.AssocLib=Ouvrir les bibliothèques .lib4d dans le navigateur de contenu
french.AssocC4d=Ouvrir les fichiers .c4d avec Iron3D Studio (supprime l'association de Cinema 4D)
french.TypeFull=Complète
french.TypeCustom=Personnalisée
french.CompMain=Iron3D Studio (noyau)
french.CompSamples=Scènes d'exemple
french.CompDocs=Documentation (README, AGENTS.md, licences)
french.BackendTitle=Moteur de rendu
french.BackendSub=Choisissez l'API graphique qu'Iron3D Studio utilisera par défaut.
french.BackendDesc=Vous pourrez la modifier plus tard dans les Préférences de l'application ; le changement s'applique au redémarrage d'Iron3D Studio.
french.BackendVulkan=Vulkan (recommandé)
italian.AssocGroup=Associa i file:
italian.AssocIron=Apri le scene .iron con Iron3D Studio
italian.AssocLib=Apri le librerie .lib4d nel Content Browser
italian.AssocC4d=Apri i file .c4d con Iron3D Studio (rimuove l'associazione di Cinema 4D)
italian.TypeFull=Completa
italian.TypeCustom=Personalizzata
italian.CompMain=Iron3D Studio (nucleo)
italian.CompSamples=Scene di esempio
italian.CompDocs=Documentazione (README, AGENTS.md, licenze)
italian.BackendTitle=Backend di rendering
italian.BackendSub=Scegli l'API grafica che Iron3D Studio userà per impostazione predefinita.
italian.BackendDesc=Potrai cambiarla in seguito dalle Preferenze dell'applicazione; la modifica ha effetto al riavvio di Iron3D Studio.
italian.BackendVulkan=Vulkan (consigliato)
polish.AssocGroup=Skojarz pliki:
polish.AssocIron=Otwieraj sceny .iron w Iron3D Studio
polish.AssocLib=Otwieraj biblioteki .lib4d w Przeglądarce zawartości
polish.AssocC4d=Otwieraj pliki .c4d w Iron3D Studio (usuwa skojarzenie z Cinema 4D)
polish.TypeFull=Pełna
polish.TypeCustom=Niestandardowa
polish.CompMain=Iron3D Studio (rdzeń)
polish.CompSamples=Przykładowe sceny
polish.CompDocs=Dokumentacja (README, AGENTS.md, licencje)
polish.BackendTitle=Silnik renderowania
polish.BackendSub=Wybierz interfejs graficzny używany domyślnie przez Iron3D Studio.
polish.BackendDesc=Można to później zmienić w Preferencjach aplikacji; zmiana zadziała po ponownym uruchomieniu Iron3D Studio.
polish.BackendVulkan=Vulkan (zalecany)
czech.AssocGroup=Přidružit soubory:
czech.AssocIron=Otevírat scény .iron v Iron3D Studio
czech.AssocLib=Otevírat knihovny .lib4d v Prohlížeči obsahu
czech.AssocC4d=Otevírat soubory .c4d v Iron3D Studio (odstraní přidružení Cinema 4D)
czech.TypeFull=Úplná
czech.TypeCustom=Vlastní
czech.CompMain=Iron3D Studio (jádro)
czech.CompSamples=Ukázkové scény
czech.CompDocs=Dokumentace (README, AGENTS.md, licence)
czech.BackendTitle=Vykreslovací backend
czech.BackendSub=Vyberte grafické API, které bude Iron3D Studio používat ve výchozím nastavení.
czech.BackendDesc=Později ji můžete změnit v Předvolbách aplikace; změna se projeví po restartu Iron3D Studio.
czech.BackendVulkan=Vulkan (doporučeno)
russian.AssocGroup=Ассоциировать файлы:
russian.AssocIron=Открывать сцены .iron в Iron3D Studio
russian.AssocLib=Открывать библиотеки .lib4d в Обозревателе контента
russian.AssocC4d=Открывать файлы .c4d в Iron3D Studio (удаляет ассоциацию Cinema 4D)
russian.TypeFull=Полная
russian.TypeCustom=Выборочная
russian.CompMain=Iron3D Studio (ядро)
russian.CompSamples=Примеры сцен
russian.CompDocs=Документация (README, AGENTS.md, лицензии)
russian.BackendTitle=Графический бэкенд
russian.BackendSub=Выберите графический API, который Iron3D Studio будет использовать по умолчанию.
russian.BackendDesc=Позже его можно изменить в настройках приложения; изменение вступит в силу после перезапуска Iron3D Studio.
russian.BackendVulkan=Vulkan (рекомендуется)
japanese.AssocGroup=ファイルの関連付け:
japanese.AssocIron=.iron シーンを Iron3D Studio で開く
japanese.AssocLib=.lib4d ライブラリをコンテンツブラウザで開く
japanese.AssocC4d=.c4d ファイルを Iron3D Studio で開く (Cinema 4D の関連付けを解除します)
japanese.TypeFull=フル
japanese.TypeCustom=カスタム
japanese.CompMain=Iron3D Studio (コア)
japanese.CompSamples=サンプルシーン
japanese.CompDocs=ドキュメント (README、AGENTS.md、ライセンス)
japanese.BackendTitle=レンダーバックエンド
japanese.BackendSub=Iron3D Studio が既定で使用するグラフィックス API を選択してください。
japanese.BackendDesc=後でアプリ内の環境設定から変更できます。変更は Iron3D Studio の再起動後に反映されます。
japanese.BackendVulkan=Vulkan (推奨)
korean.AssocGroup=파일 연결:
korean.AssocIron=.iron 장면을 Iron3D Studio로 열기
korean.AssocLib=.lib4d 라이브러리를 콘텐츠 브라우저에서 열기
korean.AssocC4d=.c4d 파일을 Iron3D Studio로 열기 (Cinema 4D 연결을 제거합니다)
korean.TypeFull=전체
korean.TypeCustom=사용자 지정
korean.CompMain=Iron3D Studio (코어)
korean.CompSamples=샘플 장면
korean.CompDocs=문서 (README, AGENTS.md, 라이선스)
korean.BackendTitle=렌더 백엔드
korean.BackendSub=Iron3D Studio가 기본으로 사용할 그래픽 API를 선택하세요.
korean.BackendDesc=나중에 앱의 환경설정에서 변경할 수 있으며, 변경 사항은 Iron3D Studio를 다시 시작하면 적용됩니다.
korean.BackendVulkan=Vulkan (권장)

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
; Doble clic / "Abrir con": .iron abre la escena, .lib4d abre el Navegador de
; Contenido con esa libreria (main.cpp). .c4d va desmarcado: si Cinema 4D esta
; instalado, quitarle su asociacion sin preguntar seria una faena.
Name: "associron"; Description: "{cm:AssocIron}"; GroupDescription: "{cm:AssocGroup}"
Name: "assoclib4d"; Description: "{cm:AssocLib}"; GroupDescription: "{cm:AssocGroup}"
Name: "assocc4d"; Description: "{cm:AssocC4d}"; GroupDescription: "{cm:AssocGroup}"; Flags: unchecked

[Types]
Name: "full"; Description: "{cm:TypeFull}"
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
Name: "main"; Description: "{cm:CompMain}"; Types: full custom; Flags: fixed
Name: "samples"; Description: "{cm:CompSamples}"; Types: full
Name: "docs"; Description: "{cm:CompDocs}"; Types: full

[Files]
Source: "..\build\bin\Release\{#MyAppExeName}"; DestDir: "{app}"; Components: main; Flags: ignoreversion
; Runtime de Visual C++ (msvcp140, vcruntime140, vcruntime140_1), copiado por CMake en Release.
Source: "..\build\bin\Release\msvcp140.dll"; DestDir: "{app}"; Components: main; Flags: ignoreversion
Source: "..\build\bin\Release\vcruntime140.dll"; DestDir: "{app}"; Components: main; Flags: ignoreversion
Source: "..\build\bin\Release\vcruntime140_1.dll"; DestDir: "{app}"; Components: main; Flags: ignoreversion
Source: "..\build\bin\Release\shaders\*"; DestDir: "{app}\shaders"; Components: main; Flags: ignoreversion recursesubdirs createallsubdirs
; Idiomas de la interfaz (lang\<codigo>.txt, ver src/core/I18n.h).
Source: "..\build\bin\Release\lang\*"; DestDir: "{app}\lang"; Components: main; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "samples\*.iron"; DestDir: "{app}\scenes"; Components: samples; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Components: docs; Flags: ignoreversion
Source: "..\AGENTS.md"; DestDir: "{app}"; Components: docs; Flags: ignoreversion
Source: "..\LICENSE-MIT"; DestDir: "{app}"; Components: docs; Flags: ignoreversion
Source: "..\LICENSE-APACHE"; DestDir: "{app}"; Components: docs; Flags: ignoreversion

[Icons]
Name: "{group}\Iron3D Studio"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,Iron3D Studio}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Iron3D Studio"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; ProgIDs propios + "Abrir con" (OpenWithProgids) para las tres extensiones;
; el valor por defecto de la extension solo se cambia si se marco la tarea.
Root: HKA; Subkey: "Software\Classes\Iron3D.Scene"; ValueType: string; ValueName: ""; ValueData: "Iron3D Studio Scene"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Iron3D.Scene\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKA; Subkey: "Software\Classes\Iron3D.Scene\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\Iron3D.Library"; ValueType: string; ValueName: ""; ValueData: "Preset Library (.lib4d)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Iron3D.Library\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKA; Subkey: "Software\Classes\Iron3D.Library\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\Iron3D.C4D"; ValueType: string; ValueName: ""; ValueData: "Cinema 4D Scene (Iron3D Studio)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Iron3D.C4D\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKA; Subkey: "Software\Classes\Iron3D.C4D\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\.iron\OpenWithProgids"; ValueType: string; ValueName: "Iron3D.Scene"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.lib4d\OpenWithProgids"; ValueType: string; ValueName: "Iron3D.Library"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.c4d\OpenWithProgids"; ValueType: string; ValueName: "Iron3D.C4D"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.iron"; ValueType: string; ValueName: ""; ValueData: "Iron3D.Scene"; Flags: uninsdeletevalue; Tasks: associron
Root: HKA; Subkey: "Software\Classes\.lib4d"; ValueType: string; ValueName: ""; ValueData: "Iron3D.Library"; Flags: uninsdeletevalue; Tasks: assoclib4d
Root: HKA; Subkey: "Software\Classes\.c4d"; ValueType: string; ValueName: ""; ValueData: "Iron3D.C4D"; Flags: uninsdeletevalue; Tasks: assocc4d

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,Iron3D Studio}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; preferences.cfg lo escribe el script [Code] (CurStepChanged), no esta en
; [Files], asi que Inno no lo rastrea solo -- sin esto quedaba huerfano tras
; desinstalar (visto al probar el ciclo completo instalar/desinstalar).
Type: files; Name: "{app}\preferences.cfg"
Type: files; Name: "{app}\language.cfg"

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
  BackendPage := CreateCustomPage(wpLicense, CustomMessage('BackendTitle'),
    CustomMessage('BackendSub'));

  BackendDesc := TNewStaticText.Create(BackendPage);
  BackendDesc.Parent := BackendPage.Surface;
  BackendDesc.Caption := CustomMessage('BackendDesc');
  BackendDesc.AutoSize := False;
  BackendDesc.WordWrap := True;
  BackendDesc.Width := BackendPage.SurfaceWidth;
  BackendDesc.Height := ScaleY(32);
  BackendDesc.Top := 0;

  RadioVulkan := TNewRadioButton.Create(BackendPage);
  RadioVulkan.Parent := BackendPage.Surface;
  RadioVulkan.Caption := CustomMessage('BackendVulkan');
  RadioVulkan.Top := BackendDesc.Top + BackendDesc.Height + ScaleY(12);
  RadioVulkan.Width := BackendPage.SurfaceWidth;
  RadioVulkan.Checked := True;

  RadioOpenGL := TNewRadioButton.Create(BackendPage);
  RadioOpenGL.Parent := BackendPage.Surface;
  RadioOpenGL.Caption := 'OpenGL';
  RadioOpenGL.Top := RadioVulkan.Top + RadioVulkan.Height + ScaleY(4);
  RadioOpenGL.Width := BackendPage.SurfaceWidth;
end;

// Codigo de idioma de Iron3D Studio (lang\<codigo>.txt) para el idioma elegido
// en el instalador.
function AppLanguageCode: String;
var
  L: String;
begin
  L := ActiveLanguage;
  if L = 'spanish' then Result := 'es'
  else if L = 'german' then Result := 'de'
  else if L = 'french' then Result := 'fr'
  else if L = 'italian' then Result := 'it'
  else if L = 'polish' then Result := 'pl'
  else if L = 'czech' then Result := 'cs'
  else if L = 'russian' then Result := 'ru'
  else if L = 'japanese' then Result := 'ja'
  else if L = 'korean' then Result := 'ko'
  else Result := 'en';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Backend: String;
  PrefsPath: String;
begin
  if CurStep = ssPostInstall then
  begin
    // Idioma elegido en el instalador: la app lo lee de language.cfg si el
    // usuario aun no ha fijado uno en Preferencias (core/I18n.cpp).
    SaveStringToFile(ExpandConstant('{app}') + '\language.cfg', AppLanguageCode + #13#10, False);
    if RadioOpenGL.Checked then
      Backend := 'opengl'
    else
      Backend := 'vulkan';
    PrefsPath := ExpandConstant('{app}') + '\preferences.cfg';
    SaveStringToFile(PrefsPath, 'IRON3D_PREFS 1' + #13#10 + 'renderBackend=' + Backend + #13#10, False);
  end;
end;
