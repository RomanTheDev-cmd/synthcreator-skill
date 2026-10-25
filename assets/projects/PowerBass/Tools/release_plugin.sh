#!/bin/bash
# Build, test, sign, copy into Сборки and install ONE plug-in of the series (PowerBass or PowerKick):
#   macOS universal + Windows cross build; DSP and wrapper tests on arm64, x86_64 (Rosetta) and Wine;
#   ad hoc signing; Сборки/macOS, Сборки/Windows and the Windows zip; ~/Library/Audio/Plug-Ins (old copies to
#   the Trash); auval (both architectures) and pluginval level 5. One summary line per step; stops on a failure.
#   bash Tools/release_plugin.sh PowerKick "<label for the Trash folder>"
set -u
P="$1"; STAMP="$2"; ROOT="/Users/roman/Library/Mobile Documents/com~apple~CloudDocs/Главная рабочая папка ИИ/Проекты"; D="$ROOT/$P"
C=~/Library/Caches/PowerKickTools/cmake/cmake/data/bin/cmake; W=~/Library/Caches/PowerToolsWin
export WIN_LLVM_ROOT=$W/llvm-mingw-20261020-ucrt-macos-universal WIN_SDK=$W/winsdk WIN_WINE=$W/wine-run.sh
BIN="$W/wine/Wine Stable.app/Contents/Resources/wine/bin"; U=~/Library/Caches/${P}BuildUniversal; BW=~/Library/Caches/${P}BuildWin
l=$(echo $P | tr 'A-Z' 'a-z'); O=~/Library/Caches/release-$P; mkdir -p "$O"
fail() { echo "ОСТАНОВЛЕНО: $1"; exit 1; }
$C --build $U -j10 > "$O/mac.log" 2>&1 || fail "сборка Mac"; echo "сборка Mac: ок"
$C --build $BW -j10 > "$O/win.log" 2>&1 || fail "сборка Windows"; echo "сборка Windows: ок"
for t in ${l}_tests ${l}_wrapper_tests; do for a in arm64 x86_64; do
  (cd "$D" && arch -$a $U/${t}_artefacts/Release/$t > "$O/$t-$a.txt" 2>&1); r=$?; echo "$t $a: $(tail -1 "$O/$t-$a.txt")"; [ $r = 0 ] || fail "$t $a"
done; done
export WINEPREFIX=$W/wineprefix WINEDEBUG=-all
for t in ${l}_tests ${l}_wrapper_tests; do
  (cd "$D" && "$BIN/wine" $BW/${t}_artefacts/Release/$t.exe > "$O/$t-wine.txt" 2>/dev/null); r=$?; echo "$t wine: $(tr -d '\r' < "$O/$t-wine.txt" | grep -E 'PASS|FAIL' | tail -1)"; [ $r = 0 ] || fail "$t wine"
done; "$BIN/wineserver" -k 2>/dev/null
S="$D/Сборки"; Uo=$U/${P}_artefacts/Release; Wo=$BW/${P}_artefacts/Release; I=~/Library/Audio/Plug-Ins
for b in "$Uo/AU/$P.component" "$Uo/VST3/$P.vst3" "$Uo/Standalone/$P.app"; do codesign --force --deep --sign - "$b" 2>/dev/null; codesign --verify --deep --strict "$b" || fail "подпись $b"; done
T=~/.Trash/"$P до $STAMP"; mkdir -p "$T/macOS" "$T/Windows" "$T/Установленные"
mv "$S/macOS/$P.app" "$S/macOS/$P.component" "$S/macOS/$P.vst3" "$T/macOS/"; mv "$S/Windows/$P.exe" "$S/Windows/$P.vst3" "$T/Windows/"; mv "$S/$P-1.0.0-Windows-x64.zip" "$T/"
ditto "$Uo/Standalone/$P.app" "$S/macOS/$P.app"; ditto "$Uo/AU/$P.component" "$S/macOS/$P.component"; ditto "$Uo/VST3/$P.vst3" "$S/macOS/$P.vst3"
ditto "$Wo/Standalone/$P.exe" "$S/Windows/$P.exe"; ditto "$Wo/VST3/$P.vst3" "$S/Windows/$P.vst3"
Z=$(mktemp -d); ditto "$S/Windows" "$Z/$P"; (cd "$Z" && zip -qr -X -D "$S/$P-1.0.0-Windows-x64.zip" "$P" -x "*.DS_Store")
mv "$I/Components/$P.component" "$I/VST3/$P.vst3" "$T/Установленные/"
ditto "$S/macOS/$P.component" "$I/Components/$P.component"; ditto "$S/macOS/$P.vst3" "$I/VST3/$P.vst3"
diff -rq "$I/Components/$P.component" "$S/macOS/$P.component" > /dev/null && diff -rq "$I/VST3/$P.vst3" "$S/macOS/$P.vst3" > /dev/null || fail "установка"
echo "Сборки и установка: ок (прежние — в Корзине «$P до $STAMP»)"
killall -9 AudioComponentRegistrar 2>/dev/null
code=$(grep -m1 "PLUGIN_CODE" "$D/CMakeLists.txt" | awk '{print $2}')
for a in arm64 x86_64; do arch -$a auval -v aumu $code Mddy > "$O/auval-$a.txt" 2>&1; echo "auval $a: $(grep -E 'AU VALIDATION SUCCEEDED|FAILED' "$O/auval-$a.txt" | tail -1)"; done
PV="$ROOT/PowerBass/Исследования/2026-10-22 — Проверка задержки/Host/tools/pluginval-v1.0.4/pluginval.app/Contents/MacOS/pluginval"
for f in "VST3/$P.vst3" "Components/$P.component"; do "$PV" --strictness-level 5 --validate-in-process --output-dir "$O" "$I/$f" > "$O/pv.txt" 2>&1; echo "pluginval $(basename $f): $(grep -E 'SUCCESS|FAILED' "$O/pv.txt" | tail -1)"; done
echo ГОТОВО
