#!/usr/bin/env bash
set -euo pipefail

expected_package='xorg-xwayland-visible-warp'
expected_version='24.1.13-19'
expected_binary_sha256='44f4121eb2e8c22c4338173153b3f376862b35257d54c2c95e8d6f8b05dfd0cb'

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
  printf '/usr/bin/Xwayland does not match the revision-19 package.\n' >&2
  exit 3
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
