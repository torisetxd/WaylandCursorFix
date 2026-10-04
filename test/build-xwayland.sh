#!/usr/bin/env bash
# Build Xwayland from the pinned tarball with a chosen patch set, without root
# and without installing anything. Prints the path of the resulting binary.
#
#   test/build-xwayland.sh OUTDIR [PATCH...]
#
# With no PATCH arguments every NNNN-*.patch in the repo root is applied in
# order. The binary is meant to be handed to test/nest.sh -x, or to
# test/run-case.sh -x.
#
# meson is not a hard system dependency here: if it is missing we fall back to
# the venv under .analysis/. xtrans (header-only, a makedepend) is picked up
# from .analysis/deps when the system does not have it.
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out=$(mkdir -p "$1" && cd "$1" && pwd); shift
version=24.1.13
tarball="$root/xwayland-$version.tar.xz"

if [[ ! -f $tarball ]]; then
  curl -fL -o "$tarball" "https://xorg.freedesktop.org/archive/individual/xserver/xwayland-$version.tar.xz"
fi

patches=("$@")
if [[ ${#patches[@]} -eq 0 ]]; then
  mapfile -t patches < <(ls "$root"/[0-9][0-9][0-9][0-9]-*.patch | sort)
fi

rm -rf "$out/src"
mkdir -p "$out/src"
tar -xf "$tarball" -C "$out/src"
srcdir="$out/src/xwayland-$version"
for p in "${patches[@]}"; do
  patch -d "$srcdir" -Np1 -s < "$p"
done

meson=meson
command -v meson >/dev/null || meson="$root/.analysis/venv/bin/meson"
export PATH="$(dirname "$meson"):$PATH"
if ! pkgconf --exists xtrans 2>/dev/null; then
  export PKG_CONFIG_PATH="$root/.analysis/deps/usr/share/pkgconfig:${PKG_CONFIG_PATH:-}"
fi

if [[ ! -d $out/build ]]; then
  "$meson" setup "$out/build" "$srcdir" \
    --prefix=/usr --libexecdir=lib --sbindir=bin \
    -D ipv6=true -D xvfb=false -D xdmcp=false -D xcsecurity=true \
    -D dri3=true -D glamor=true -D libdecor=true -D sha1=libnettle \
    -D xkb_dir=/usr/share/X11/xkb -D xkb_output_dir=/var/lib/xkb \
    -D buildtype=release -D b_ndebug=false >"$out/meson-setup.log" 2>&1 ||
    { tail -30 "$out/meson-setup.log" >&2; exit 1; }
fi
ninja -C "$out/build" >"$out/ninja.log" 2>&1 || { tail -40 "$out/ninja.log" >&2; exit 1; }
echo "$out/build/hw/xwayland/Xwayland"
