; Installeur Windows de MAZZFLATOR (Inno Setup 6)
; ISCC /DAppVer=2.0.0 /DArt=<dossier Release des artefacts> /DOutDir=<sortie> mazzflator.iss
#ifndef AppVer
  #define AppVer "2.0.0"
#endif

[Setup]
AppId={{6E3B1F2A-8C4D-4E7A-9B1C-4D2F7A9E0B13}
AppName=MAZZFLATOR
AppVersion={#AppVer}
AppPublisher=Mazztock
AppPublisherURL=https://github.com/mazztock/MAZZFLATOR
DefaultDirName={commonpf64}\Mazztock\MAZZFLATOR
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
LicenseFile={#SourcePath}\..\..\..\LICENSE
OutputDir={#OutDir}
OutputBaseFilename=MAZZFLATOR-{#AppVer}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "VST3 + CLAP"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3  (C:\Program Files\Common Files\VST3)"; Types: full custom
Name: "clap"; Description: "CLAP  (C:\Program Files\Common Files\CLAP)"; Types: full custom

[Files]
Source: "{#Art}\VST3\MAZZFLATOR.vst3\*"; DestDir: "{commoncf64}\VST3\MAZZFLATOR.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Art}\CLAP\MAZZFLATOR.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "{#SourcePath}\..\..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
