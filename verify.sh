#!/usr/bin/env bash
set -euo pipefail

expected_package='xorg-xwayland-visible-warp'
expected_version='24.1.13-7'
expected_binary_sha256='6e33f05e1a19d2e4dd9dda6580a65ef312866320c500a385b94bafd4ee02518e'

if ! pacman -Q "${expected_package}" >/dev/null 2>&1; then
  printf '%s is not installed.\n' "${expected_package}" >&2
  exit 1
fi

installed_version="$(pacman -Q "${expected_package}" | awk '{ print $2 }')"
if [[ "${installed_version}" != "${expected_version}" ]]; then
  printf 'Installed revision is %s; expected %s. Run ./install.sh again.\n' \
    "${installed_version}" "${expected_version}" >&2
  exit 2
fi

installed_binary_sha256="$(sha256sum /usr/bin/Xwayland | awk '{ print $1 }')"
if [[ "${installed_binary_sha256}" != "${expected_binary_sha256}" ]]; then
  printf '/usr/bin/Xwayland does not match the revision-7 package.\n' >&2
  exit 3
fi

if command -v wayland-info >/dev/null 2>&1; then
  if ! wayland-info 2>/dev/null |
      awk '/wp_pointer_warp_v1/ { found = 1 } END { exit !found }'; then
    printf 'The active Wayland compositor does not advertise wp_pointer_warp_v1.\n' >&2
    exit 4
  fi
fi

pacman -Q "${expected_package}"
Xwayland -version 2>&1 | sed -n '1,2p'

running_pid="$(pgrep -o -x Xwayland || true)"
if [[ -n "${running_pid}" ]]; then
  running_exe="$(readlink -f "/proc/${running_pid}/exe" 2>/dev/null || true)"
  installed_exe="$(readlink -f /usr/bin/Xwayland)"
  if [[ "${running_exe}" != "${installed_exe}" ]]; then
    printf 'The running Xwayland is not the installed binary; log out and back in.\n' >&2
    exit 5
  fi
  printf 'Running Xwayland executable: %s\n' "${running_exe}"
else
  printf 'Xwayland is demand-started; launch an X11 application after logging in.\n'
fi
