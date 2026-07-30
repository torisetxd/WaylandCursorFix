#!/usr/bin/env bash
set -euo pipefail

if [[ "$(id -u)" == 0 ]]; then
  printf 'Run this uninstaller as your normal user, not as root.\n' >&2
  exit 1
fi

sudo pacman -S --needed xorg-xwayland

printf '\nRestored the distribution Xwayland package. Log out and back in.\n'
