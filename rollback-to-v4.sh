#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
package="${project_dir}/xorg-xwayland-visible-warp-24.1.13-4-x86_64.pkg.tar.zst"

if [[ "$(id -u)" == 0 ]]; then
  printf 'Run this rollback as your normal user, not as root.\n' >&2
  exit 1
fi

if [[ ! -f "${package}" ]]; then
  printf 'The revision-4 package is missing: %s\n' "${package}" >&2
  exit 2
fi

sudo pacman -U "${package}"
printf '\nRolled back to revision 4. Log out and back in, or reboot.\n'
