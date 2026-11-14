#!/bin/sh
# Construit l'installeur Mac (.pkg) : AU, VST3 et CLAP dans /Library/Audio/Plug-Ins/
# Usage : build_pkg.sh <dossier Release des artefacts> <version> <dossier de sortie>
set -e
ART="$1"; VER="$2"; OUT="$3"
W="$(mktemp -d)"
mkdir -p "$OUT"
fabrique() {   # $1 = nom court, $2 = bundle, $3 = dossier d'installation
    mkdir -p "$W/$1"
    cp -R "$ART/$2" "$W/$1/"
    codesign --force --deep --sign - "$W/$1/$(basename "$2")"
    # pas de « relocation » : sinon Installer remplace une copie trouvée ailleurs (ex. ~/Library) au lieu d'installer
    pkgbuild --analyze --root "$W/$1" "$W/$1.plist"
    /usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$W/$1.plist"
    pkgbuild --root "$W/$1" --component-plist "$W/$1.plist" --install-location "$3" \
             --identifier "com.mazztock.mazzflator.$1" --version "$VER" "$W/$1.pkg"
}
fabrique au   "AU/MAZZFLATOR.component" "/Library/Audio/Plug-Ins/Components"
fabrique vst3 "VST3/MAZZFLATOR.vst3"     "/Library/Audio/Plug-Ins/VST3"
fabrique clap "CLAP/MAZZFLATOR.clap"     "/Library/Audio/Plug-Ins/CLAP"
productbuild --package "$W/au.pkg" --package "$W/vst3.pkg" --package "$W/clap.pkg" \
             "$OUT/MAZZFLATOR-$VER-macOS.pkg"
echo "OK : $OUT/MAZZFLATOR-$VER-macOS.pkg"
