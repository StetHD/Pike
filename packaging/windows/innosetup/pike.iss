; Script generated by the Inno Setup Script Wizard.
; SEE THE DOCUMENTATION FOR DETAILS ON CREATING INNO SETUP SCRIPT FILES!

[Setup]
AppName=Pike 7.4 (Beta)
AppVerName=(Beta) Pike 7.4.11, SDL, OpenGL, MySQL, Freetype, Gz and GTK+ support
AppPublisherURL=http://pike.ida.liu.se/
AppSupportURL=http://pike.ida.liu.se/
AppUpdatesURL=http://pike.ida.liu.se/
DefaultDirName={pf}\Pike
DefaultGroupName=Pike
AllowNoIcons=yes
LicenseFile=Y:\win32-pike\pikeinstaller\Copying.txt
; uncomment the following line if you want your installation to run on NT 3.51 too.
; MinVersion=4,3.51

;DistSource=Y:\win32-pike\dists\Pike-v7.4.10-Win32-5.1.2600.exe

;FIXME: pike.exe fallerar p� win98 om pike redan �r installerat. Ordna
;automatisk radering. Late note: Seems to fail on XP too. G�ller det 7.4 ocks� tro?

[Files]
Source: "Y:\win32-pike\dists\Pike-v7.4.11-Win32-tsubasa-beta.exe"; DestDir: "{tmp}"; CopyMode: alwaysoverwrite; Flags: deleteafterinstall
Source: "Y:\win32-pike\extras\pike.ico"; DestDir: "{app}"; CopyMode: alwaysoverwrite; Attribs: hidden

; Begin GTK+ files
;Source: "Y:\win32-pike\dlls\gdk-1.3.dll"; DestDir: "{sys}"; CopyMode: alwaysoverwrite; Flags: restartreplace  sharedfile; Components: gtk
;Source: "Y:\win32-pike\dlls\gnu-intl.dll"; DestDir: "{sys}"; CopyMode: alwaysoverwrite; Flags: restartreplace  sharedfile; Components: gtk
;Source: "Y:\win32-pike\dlls\gtk-1.3.dll"; DestDir: "{sys}"; CopyMode: alwaysoverwrite; Flags: restartreplace  sharedfile; Components: gtk
;Source: "Y:\win32-pike\dlls\gdk-1.3.dll"; DestDir: "{sys}"; CopyMode: alwaysoverwrite; Flags: restartreplace  sharedfile; Components: gtk 
;Source: "Y:\win32-pike\dlls\gmodule-1.3.dll"; DestDir: "{sys}"; CopyMode: alwaysoverwrite; Flags: restartreplace  sharedfile; Components: gtk 
;Source: "Y:\win32-pike\dlls\gobject-1.3.dll"; DestDir: "{sys}"; CopyMode: alwaysoverwrite; Flags: restartreplace  sharedfile; Components: gtk 
;Source: "Y:\win32-pike\dlls\gthread-1.3.dll"; DestDir: "{sys}"; CopyMode: alwaysoverwrite; Flags: restartreplace  sharedfile; Components: gtk 
;Source: "Y:\win32-pike\dlls\glib-1.3.dll"; DestDir: "{sys}"; CopyMode: alwaysoverwrite; Flags: restartreplace  sharedfile; Components: gtk 
;Source: "Y:\win32-pike\dlls\gdk_imlib.dll"; DestDir: "{sys}"; CopyMode: alwaysoverwrite; Flags: restartreplace  sharedfile; Components: gtk 
;Source: "Y:\win32-pike\dlls\gnu-intl.dll"; DestDir: "{sys}"; CopyMode: alwaysoverwrite; Flags: restartreplace  sharedfile; Components: gtk 
;Source: "Y:\win32-pike\dlls\iconv-1.3.dll"; DestDir: "{sys}"; CopyMode: alwaysoverwrite; Flags: restartreplace  sharedfile; Components: gtk 
;Source: "Y:\win32-pike\dlls\imlib-jpeg.dll"; DestDir: "{sys}"; CopyMode: alwaysoverwrite; Flags: restartreplace  sharedfile; Components: gtk 
;Source: "Y:\win32-pike\dlls\imlib-png.dll"; DestDir: "{sys}"; CopyMode: alwaysoverwrite; Flags: restartreplace  sharedfile; Components: gtk 
;Source: "Y:\win32-pike\dlls\imlib-tiff.dll"; DestDir: "{sys}"; CopyMode: alwaysoverwrite; Flags: restartreplace  sharedfile; Components: gtk 
; End GTK+ files

;;;Begin MySQL files
;Source: "Y:\win32-pike\dlls\libmySQL.dll"; DestDir: "{sys}"; CopyMode: alwaysoverwrite; Flags: restartreplace sharedfile; Components: mysql
;;;End MySQL files


[Run]
Filename: "{tmp}\Pike-v7.4.11-Win32-tsubasa-beta.exe"; Parameters: "--no-gui --traditional ""prefix={app}"""

[Types]
Name: "full"; Description: "Full installation"
;Name: "custom"; Description: "Pike only"; Flags: iscustom

[Components]
;Name: "gtk"; Description: "GTK+"; Types: full custom; Flags: fixed
Name: "pike"; Description: "Pike"; Types: full; Flags: fixed; ExtraDiskSpaceRequired: 40000000

[Tasks]
;;Name: gtk; Description: "Install GTK+ support"
;Name: mysql; Description: "Install MySQL client support"
Name: associate; Description: "Associate .pike and .pmod extensions with Pike"

[Dirs]
;Name: "{app}\apps"; Flags: uninsneveruninstall

[Registry]
Root: HKCR; Subkey: ".pike"; ValueType: string; ValueData: "pike_file"; Tasks: associate
Root: HKCR; Subkey: ".pike"; ValueType: string; ValueName: "ContentType"; ValueData: "text/x-pike-code"; Tasks: associate
Root: HKCR; Subkey: ".pmod"; ValueType: string; ValueData: "pike_module"; Tasks: associate
Root: HKCR; Subkey: ".pmod"; ValueType: string; ValueName: "ContentType"; ValueData: "text/x-pike-code"; Tasks: associate
Root: HKCR; Subkey: "pike_file"; ValueType: string; ValueData: "Pike program file"; Tasks: associate
Root: HKCR; Subkey: "pike_file\DefaultIcon"; ValueType: string; ValueData: "{app}\pike.ico,0"; Tasks: associate
Root: HKCR; Subkey: "pike_file\Shell\Open\Command"; ValueType: string; ValueData: """{app}\bin\pike.exe"" ""%1"""; Flags: uninsdeletevalue; Tasks: associate
Root: HKCR; Subkey: "pike_file\Shell\Edit\Command"; ValueType: string; ValueData: """notepad.exe"" ""%1"""; Flags: createvalueifdoesntexist; Tasks: associate
Root: HKCR; Subkey: "pike_module"; ValueType: string; ValueData: "Pike module file"; Tasks: associate
Root: HKCR; Subkey: "pike_module\DefaultIcon"; ValueType: string; ValueData: "{app}\pike.ico,0"; Tasks: associate
Root: HKCR; Subkey: "pike_module\Shell\Edit\Command"; ValueType: string; ValueData: """notepad.exe"" ""%1"""; Flags: createvalueifdoesntexist; Tasks: associate

[Icons]
Name: "{group}\Pike"; Filename: "{app}\bin\pike.exe"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\include"
Type: filesandordirs; Name: "{app}\lib"
Type: filesandordirs; Name: "{app}\man"



