#!/bin/sh
# Installe MAZZFLATOR (VST3, CLAP, LV2).
#   ./install.sh          -> pour l'utilisateur courant (~/.vst3, ~/.clap, ~/.lv2)
#   sudo ./install.sh -s  -> pour tout le système (/usr/lib/vst3, /usr/lib/clap, /usr/lib/lv2)
set -e
cd "$(dirname "$0")"
if [ "$1" = "-s" ]; then V=/usr/lib/vst3; C=/usr/lib/clap; L=/usr/lib/lv2
else V="$HOME/.vst3"; C="$HOME/.clap"; L="$HOME/.lv2"; fi
mkdir -p "$V" "$C" "$L"
rm -rf "$V/MAZZFLATOR.vst3" "$L/MAZZFLATOR.lv2"
cp -R MAZZFLATOR.vst3 "$V/"
cp MAZZFLATOR.clap "$C/"
cp -R MAZZFLATOR.lv2 "$L/"
echo "MAZZFLATOR installé dans : $V, $C, $L"
