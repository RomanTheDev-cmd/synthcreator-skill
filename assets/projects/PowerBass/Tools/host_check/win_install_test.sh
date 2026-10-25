#!/bin/bash
# Installs <P>-Setup.exe silently into a fresh Wine prefix (a clean "Windows"), checks the files, the Start menu
# shortcut and the Apps & features entry, loads the installed VST3 with host_check (a VST3 host), starts the
# standalone app, uninstalls and checks that nothing is left.   bash win_install_test.sh PowerBass|PowerKick
# Needs: the installer in <plug-in>/Установщики, host_check built for Windows in ~/Library/Caches/HostCheckWin
# (see CMakeLists.txt here), Wine in ~/Library/Caches/PowerToolsWin. The previous test prefix goes to the Trash.
set -u
P="$1"; ROOT="/Users/roman/Library/Mobile Documents/com~apple~CloudDocs/Главная рабочая папка ИИ/Проекты"
W=~/Library/Caches/PowerToolsWin; BIN="$W/wine/Wine Stable.app/Contents/Resources/wine/bin"
export WINEPREFIX=$W/prefix-install-$P WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
SETUP="$ROOT/$P/Установщики/$P-1.0.0-Windows-x64-Setup.exe"; SRC="$ROOT/$P/Сборки/Windows"
HC=~/Library/Caches/HostCheckWin/host_check_artefacts/Release/host_check.exe
C="$WINEPREFIX/drive_c"; VST="$C/Program Files/Common Files/VST3/$P.vst3"; APPDIR="$C/Program Files/MyDarknessDays/$P"
LNK="$C/ProgramData/Microsoft/Windows/Start Menu/Programs/MyDarknessDays/$P.lnk"
KEY='HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\MyDarknessDays '"$P"
timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }  # no coreutils timeout on macOS
ok() { if eval "$2"; then echo "OK   $1"; else echo "FAIL $1"; fi; }
[ -d "$WINEPREFIX" ] && mv "$WINEPREFIX" ~/.Trash/"wine prefix-install-$P $(date +%H%M%S)"
"$BIN/wineboot" -i > /dev/null 2>&1; "$BIN/wineserver" -w
echo "== чистая Windows: $(ls "$C/Program Files/Common Files" | tr '\n' ' ')"
ok "до установки плагина нет" '[ ! -e "$VST" ]'
cp "$SETUP" "$C/setup.exe"
timeout 300 "$BIN/wine" 'C:\setup.exe' /S > /dev/null 2>&1; echo "установщик: код $?"; "$BIN/wineserver" -w
ok "VST3 в Common Files\\VST3 совпадает со Сборки/Windows" 'diff -rq "$VST" "$SRC/$P.vst3" > /dev/null'
ok "программа в Program Files\\MyDarknessDays\\$P совпадает" 'cmp -s "$APPDIR/$P.exe" "$SRC/$P.exe"'
ok "деинсталлятор на месте" '[ -f "$APPDIR/Uninstall $P.exe" ]'
ok "ярлык в меню Пуск (для всех пользователей)" '[ -f "$LNK" ]'
"$BIN/wine" reg query "$KEY" /reg:64 2>/dev/null | tr -d '\r' | grep -E "DisplayName|DisplayVersion|Publisher|UninstallString" | sed 's/^/     /'
ok "запись в «Приложения и возможности»" '"$BIN/wine" reg query "$KEY" /reg:64 > /dev/null 2>&1'
echo "== хост VST3 загружает установленный плагин"
cp "$HC" "$C/host_check.exe"
timeout 600 "$BIN/wine" 'C:\host_check.exe' "C:\\Program Files\\Common Files\\VST3\\$P.vst3" 2>/dev/null | tr -d '\r'; echo "host_check: код ${PIPESTATUS[0]}"
"$BIN/wineserver" -w
echo "== программа запускается"
"$BIN/wine" "C:\\Program Files\\MyDarknessDays\\$P\\$P.exe" > /dev/null 2>&1 & PID=$!
sleep 12; if kill -0 $PID 2>/dev/null; then echo "OK   программа работает через 12 с"; else wait $PID; echo "FAIL программа завершилась сама (код $?)"; fi
"$BIN/wineserver" -k; sleep 1
echo "== удаление"
"$BIN/wine" "C:\\Program Files\\MyDarknessDays\\$P\\Uninstall $P.exe" /S > /dev/null 2>&1; "$BIN/wineserver" -w; sleep 2; "$BIN/wineserver" -w
ok "VST3 удалён" '[ ! -e "$VST" ]'
ok "папка программы удалена" '[ ! -e "$APPDIR" ] && [ ! -e "$C/Program Files/MyDarknessDays" ]'
ok "ярлык удалён" '[ ! -e "$LNK" ] && [ ! -e "$(dirname "$LNK")" ]'
ok "запись в реестре удалена" '! "$BIN/wine" reg query "$KEY" /reg:64 > /dev/null 2>&1'
"$BIN/wineserver" -k 2>/dev/null
echo ГОТОВО
