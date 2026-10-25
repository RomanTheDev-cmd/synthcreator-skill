; Windows installer of a MyDarknessDays plug-in (NSIS 3, built on macOS by Tools/make_installers.sh).
; makensis -DNAME=PowerBass -DVER=1.0.0 -DSRC=<Сборки/Windows> -DOUTFILE=<...-Setup.exe> installer_windows.nsi
; VST3 -> C:\Program Files\Common Files\VST3, Standalone -> C:\Program Files\MyDarknessDays\<NAME>,
; Start menu shortcut, uninstaller in Apps & features.

Unicode true
!include "MUI2.nsh"
!include "x64.nsh"

Name "${NAME} ${VER}"
OutFile "${OUTFILE}"
RequestExecutionLevel admin
InstallDir "$PROGRAMFILES64\MyDarknessDays\${NAME}"
SetCompressor /SOLID lzma
BrandingText "MyDarknessDays"
VIProductVersion "${VER}.0"
VIAddVersionKey /LANG=1033 "ProductName" "${NAME}"
VIAddVersionKey /LANG=1033 "CompanyName" "MyDarknessDays"
VIAddVersionKey /LANG=1033 "FileDescription" "${NAME} installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${VER}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${VER}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "MyDarknessDays"

!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\MyDarknessDays ${NAME}"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_SMALLDESC

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Russian"

LangString DESC_VST3 ${LANG_ENGLISH} "The plug-in for your DAW (VST3), in Common Files\VST3."
LangString DESC_VST3 ${LANG_RUSSIAN} "Плагин для DAW (VST3) в Common Files\VST3."
LangString DESC_APP ${LANG_ENGLISH} "${NAME} as a standalone application, with a Start menu shortcut."
LangString DESC_APP ${LANG_RUSSIAN} "${NAME} как отдельная программа, с ярлыком в меню «Пуск»."
LangString NEED64 ${LANG_ENGLISH} "${NAME} needs 64-bit Windows."
LangString NEED64 ${LANG_RUSSIAN} "${NAME} работает только в 64-битной Windows."

Function .onInit
  !insertmacro MUI_LANGDLL_DISPLAY
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "$(NEED64)"
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all ; Start menu shortcut for every user (the installer runs as admin)
FunctionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext all
FunctionEnd

Section "VST3" SecVST3
  SetOutPath "$COMMONFILES64\VST3"
  File /r "${SRC}/${NAME}.vst3"
SectionEnd

Section "Standalone" SecApp
  SetOutPath "$INSTDIR"
  File "${SRC}/${NAME}.exe"
  CreateDirectory "$SMPROGRAMS\MyDarknessDays"
  CreateShortcut "$SMPROGRAMS\MyDarknessDays\${NAME}.lnk" "$INSTDIR\${NAME}.exe"
SectionEnd

Section "-Uninstaller"
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall ${NAME}.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${NAME} ${VER}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VER}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "MyDarknessDays"
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall ${NAME}.exe"'
  WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall ${NAME}.exe" /S'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecVST3} "$(DESC_VST3)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} "$(DESC_APP)"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  RMDir /r "$COMMONFILES64\VST3\${NAME}.vst3"
  Delete "$INSTDIR\${NAME}.exe"
  Delete "$INSTDIR\Uninstall ${NAME}.exe"
  RMDir "$INSTDIR"
  RMDir "$PROGRAMFILES64\MyDarknessDays"
  Delete "$SMPROGRAMS\MyDarknessDays\${NAME}.lnk"
  RMDir "$SMPROGRAMS\MyDarknessDays"
  DeleteRegKey HKLM "${UNINST_KEY}"
SectionEnd
