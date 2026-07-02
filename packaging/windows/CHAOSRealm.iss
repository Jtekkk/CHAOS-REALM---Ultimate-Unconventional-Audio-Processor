; CHAOS REALM — Windows installer (Inno Setup 6)
; Built by .github/workflows/windows-installer.yml, but also runnable locally:
;   ISCC.exe /DMyAppVersion=1.0.0 /DStagingDir=<abs path to staging> ^
;            /DHasStandalone=1 /O<output dir> packaging\windows\CHAOSRealm.iss
;
; The staging directory must contain:
;   staging\CHAOS REALM.vst3\   (the VST3 bundle folder)
;   staging\CHAOS REALM.exe     (the Standalone app, if HasStandalone is set)

#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif
#ifndef StagingDir
  #define StagingDir "staging"
#endif

#define MyAppName "CHAOS REALM"
#define MyAppPublisher "Chaos Realm Audio"
#define MyAppURL "https://github.com/Jtekkk/CHAOS-REALM---Ultimate-Unconventional-Audio-Processor"

[Setup]
AppId={{9F2C7E10-CA05-4E7A-9C11-A1B2C3D4E5F6}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputBaseFilename=CHAOS-REALM-{#MyAppVersion}-Windows
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
UninstallDisplayName={#MyAppName} {#MyAppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full";   Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (for your DAW)"; Types: full custom; Flags: fixed
#ifdef HasStandalone
Name: "standalone"; Description: "Standalone application"; Types: full custom
#endif

[Files]
; VST3 bundle -> the standard shared VST3 location.
Source: "{#StagingDir}\{#MyAppName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#MyAppName}.vst3"; \
  Flags: recursesubdirs createallsubdirs ignoreversion; Components: vst3
#ifdef HasStandalone
Source: "{#StagingDir}\{#MyAppName}.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone
#endif

[Icons]
#ifdef HasStandalone
Name: "{group}\{#MyAppName}";           Filename: "{app}\{#MyAppName}.exe"; Components: standalone
#endif
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

#ifdef HasStandalone
[Run]
Filename: "{app}\{#MyAppName}.exe"; Description: "Launch {#MyAppName}"; \
  Flags: nowait postinstall skipifsilent; Components: standalone
#endif
