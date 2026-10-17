#!/usr/bin/env bash
# Debian/Ubuntu environment setup. Review before running; requires sudo/network.
set -euo pipefail
cd "$(dirname "$0")/.."
sudo apt-get update
sudo apt-get install -y ca-certificates curl wget git python3 python3-venv qemu-system-arm
# Official devkitPro apt repository, with signature verification enabled.
sudo mkdir -p /usr/share/keyrings
curl -fsSL -A "dkp apt" https://apt.devkitpro.org/devkitpro-pub.gpg | sudo tee /usr/share/keyrings/devkitpro-pub.gpg >/dev/null
printf '%s\n' 'deb [signed-by=/usr/share/keyrings/devkitpro-pub.gpg] https://apt.devkitpro.org stable main' | sudo tee /etc/apt/sources.list.d/devkitpro.list >/dev/null
sudo apt-get update
sudo apt-get install -y devkitpro-pacman
sudo /opt/devkitpro/pacman/bin/pacman -Syu --noconfirm devkitARM ndstool
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
