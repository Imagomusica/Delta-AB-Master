#!/bin/bash
# Build Delta Comp (VST3 + AU) on this Mac and install it in ~/Library/Audio/Plug-Ins.
# Needs only the Xcode Command Line Tools; CMake and Ninja are installed in ~/dc-tools the first time.
set -e
SRC="$(cd "$(dirname "$0")" && pwd)"
BUILD="$HOME/dc-build"

if ! xcode-select -p >/dev/null 2>&1; then
    echo "Il faut d'abord les outils Xcode : une fenetre va s'ouvrir, clique Installer, puis relance cette commande."
    xcode-select --install || true
    exit 1
fi

if [ ! -x "$HOME/dc-tools/bin/cmake" ]; then
    echo "== Installation de CMake et Ninja (une seule fois)"
    python3 -m venv "$HOME/dc-tools"
    "$HOME/dc-tools/bin/pip" install -q --upgrade pip cmake ninja
fi
export PATH="$HOME/dc-tools/bin:$PATH"

echo "== Compilation (la premiere fois environ 10 minutes, ensuite beaucoup moins)"
rm -f "$BUILD/CMakeCache.txt"
cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --target DeltaComp_VST3 DeltaComp_AU

A="$BUILD/DeltaComp_artefacts/Release"
V="$HOME/Library/Audio/Plug-Ins/VST3"; C="$HOME/Library/Audio/Plug-Ins/Components"
[ -d "$A/VST3/Delta Comp.vst3" ] && [ -d "$A/AU/Delta Comp.component" ] || { echo "Plugin non trouve apres la compilation"; exit 1; }
mkdir -p "$V" "$C"
rm -rf "$V/Delta Comp.vst3" "$C/Delta Comp.component"
cp -R "$A/VST3/Delta Comp.vst3" "$V/"
cp -R "$A/AU/Delta Comp.component" "$C/"
xattr -dr com.apple.quarantine "$V/Delta Comp.vst3" "$C/Delta Comp.component" 2>/dev/null || true
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo
echo "INSTALLE. Redemarre Reaper."
