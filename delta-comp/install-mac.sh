#!/bin/bash
# Telecharge la derniere version de Delta Comp, la compile et l'installe.
# Usage : curl -fsSL https://raw.githubusercontent.com/Imagomusica/Delta-AB-Master/claude/project-thread-eg9cxp/delta-comp/install-mac.sh | bash
set -e
DIR="$HOME/dc-src"
rm -rf "$DIR"; mkdir -p "$DIR"; cd "$DIR"
echo "== Telechargement de la derniere version"
curl -fL -o src.zip "https://github.com/Imagomusica/Delta-AB-Master/archive/refs/heads/claude/project-thread-eg9cxp.zip"
unzip -q src.zip
bash "$DIR/Delta-AB-Master-claude-project-thread-eg9cxp/delta-comp/build-mac.sh"
