#!/bin/bash
# Builds the installers of this plug-in from the signed builds in Сборки/:
#   Установщики/<Name>-<ver>-macOS.pkg                     AU, VST3 and the standalone app (each optional);
#                                                          "all users" (/Library, /Applications) or "only me" (~)
#   Установщики/<Name>-<ver>-Windows-x64-Setup.exe         VST3 to Common Files\VST3, standalone to Program Files,
#                                                          Start menu shortcut, uninstaller (NSIS: brew install makensis)
# The .pkg is not signed with a Developer ID (none available): macOS asks to confirm opening it.
#   bash Tools/make_installers.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PROJ="$(cd "$HERE/.." && pwd)"
NAME="$(basename "$PROJ")"
LOWER="$(echo "$NAME" | tr 'A-Z' 'a-z')"
VER="$(sed -n 's/^project([A-Za-z]* VERSION \([0-9.]*\).*/\1/p' "$PROJ/CMakeLists.txt")"
ID="com.mydarknessdays.$LOWER"
B="$PROJ/Сборки"; OUT="$PROJ/Установщики"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"

# ---------- macOS ----------
component() { # $1 = id suffix, $2 = bundle in Сборки/macOS, $3 = install location
  local stage="$WORK/stage-$1"; mkdir -p "$stage"
  # Without Finder/iCloud attributes (Сборки lives in iCloud): strict signature checks reject them.
  ditto --norsrc --noextattr --noqtn "$B/macOS/$2" "$stage/$2"
  xattr -cr "$stage/$2"
  codesign --verify --deep --strict "$stage/$2"
  pkgbuild --analyze --root "$stage" "$WORK/$1.plist" > /dev/null
  # Never relocate onto another copy the system happens to know about.
  plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/$1.plist"
  pkgbuild --quiet --root "$stage" --component-plist "$WORK/$1.plist" --identifier "$ID.$1" --version "$VER" \
           --install-location "$3" "$WORK/$NAME-$1.pkg"
}
component au "$NAME.component" "/Library/Audio/Plug-Ins/Components"
component vst3 "$NAME.vst3" "/Library/Audio/Plug-Ins/VST3"
component app "$NAME.app" "/Applications"

mkdir -p "$WORK/res"
cat > "$WORK/res/welcome.html" <<EOF
<html><body style="font-family:-apple-system,Helvetica;font-size:13px">
<h2>$NAME $VER</h2>
<p>MyDarknessDays $NAME for macOS 10.13 or later on Intel, macOS 11 or later on Apple silicon.</p>
<p>Choose <b>Customize</b> to pick the formats: Audio Unit (Logic Pro, GarageBand), VST3 (Ableton Live, Bitwig, Reaper...)
and the standalone application. After installing, restart your DAW.</p>
<p lang="ru">Выберите <b>Настроить</b>, чтобы отметить форматы: Audio Unit (Logic Pro, GarageBand), VST3 (Ableton Live,
Bitwig, Reaper…) и отдельное приложение. После установки перезапустите DAW.</p>
</body></html>
EOF
cat > "$WORK/dist.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>$NAME $VER</title>
  <organization>com.mydarknessdays</organization>
  <welcome file="welcome.html" mime-type="text/html"/>
  <domains enable_localSystem="true" enable_currentUserHome="true"/>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <os-version min="10.13"/>
  <choices-outline>
    <line choice="au"/>
    <line choice="vst3"/>
    <line choice="app"/>
  </choices-outline>
  <choice id="au" title="Audio Unit" description="$NAME.component for Logic Pro, GarageBand and other AU hosts">
    <pkg-ref id="$ID.au"/>
  </choice>
  <choice id="vst3" title="VST3" description="$NAME.vst3 for Ableton Live, Bitwig, Reaper, Cubase and other VST3 hosts">
    <pkg-ref id="$ID.vst3"/>
  </choice>
  <choice id="app" title="Standalone" description="$NAME as an application, without a DAW">
    <pkg-ref id="$ID.app"/>
  </choice>
  <pkg-ref id="$ID.au" version="$VER">$NAME-au.pkg</pkg-ref>
  <pkg-ref id="$ID.vst3" version="$VER">$NAME-vst3.pkg</pkg-ref>
  <pkg-ref id="$ID.app" version="$VER">$NAME-app.pkg</pkg-ref>
</installer-gui-script>
EOF
productbuild --quiet --distribution "$WORK/dist.xml" --package-path "$WORK" --resources "$WORK/res" "$OUT/$NAME-$VER-macOS.pkg"
echo "macOS:   $OUT/$NAME-$VER-macOS.pkg ($(du -h "$OUT/$NAME-$VER-macOS.pkg" | cut -f1))"

# ---------- Windows ----------
LC_ALL=en_US.UTF-8 makensis -V2 -INPUTCHARSET UTF8 -DNAME="$NAME" -DVER="$VER" -DSRC="$B/Windows" -DOUTFILE="$OUT/$NAME-$VER-Windows-x64-Setup.exe" \
         "$HERE/installer_windows.nsi"
echo "Windows: $OUT/$NAME-$VER-Windows-x64-Setup.exe ($(du -h "$OUT/$NAME-$VER-Windows-x64-Setup.exe" | cut -f1))"
