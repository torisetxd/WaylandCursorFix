#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
built_package="${project_dir}/xorg-xwayland-visible-warp-24.1.13-16-x86_64.pkg.tar.zst"

if [[ "$(id -u)" == 0 ]]; then
  printf 'Run this installer as your normal user, not as root.\n' >&2
  exit 1
fi

if [[ ! -f /etc/arch-release ]]; then
  printf 'This package targets Arch Linux and CachyOS.\n' >&2
  exit 1
fi

cd "${project_dir}"
if [[ -f "${built_package}" ]]; then
  sudo pacman -U --needed "${built_package}"
else
  makepkg --cleanbuild --clean --syncdeps --install
fi

printf '\nInstalled. Reboot (or log out and back in) to restart Xwayland.\n'
