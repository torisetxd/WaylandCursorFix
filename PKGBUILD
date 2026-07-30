# Maintainer: local system administrator

pkgname=xorg-xwayland-visible-warp
pkgver=24.1.13
pkgrel=8
pkgdesc='Xwayland with immediate, focused visible-cursor pointer warps'
arch=('x86_64')
url='https://xorg.freedesktop.org'
license=(
  LicenseRef-Adobe-Display-PostScript
  BSD-3-Clause
  LicenseRef-DEC-3-Clause
  HPND
  LicenseRef-HPND-sell-MIT-disclaimer-xserver
  HPND-sell-variant
  ICU
  ISC
  MIT
  MIT-open-group
  NTP
  SGI-B-2.0
  SMLNJ
  X11
  X11-distribute-modifications-variant
)
groups=('xorg')
depends=(
  glibc
  libdecor
  libdrm
  libei
  libepoxy
  libglvnd
  libtirpc
  libxau
  libxcvt
  libxfont2
  libxshmfence
  mesa
  nettle
  pixman
  wayland
  xorg-server-common
)
makedepends=(
  dbus
  libxkbfile
  mesa-libgl
  meson
  systemd
  wayland-protocols
  xorg-font-util
  xorgproto
  xtrans
)
provides=(
  "xorg-xwayland=${pkgver}"
  xorg-server-xwayland
)
conflicts=(
  xorg-xwayland
  xorg-server-xwayland
)
replaces=(
  xorg-xwayland
  xorg-server-xwayland
)
install=xorg-xwayland-visible-warp.install
source=(
  "https://xorg.freedesktop.org/archive/individual/xserver/xwayland-${pkgver}.tar.xz"
  0001-xwayland-honor-visible-pointer-warps.patch
  0002-xwayland-drop-stale-pre-warp-motion.patch
  0003-xwayland-make-release-warp-atomic.patch
)
sha512sums=(
  e06e58025b441892fdd17ac55fd5c7e137bffc941b76ad784dc008047c778c6ee2895fcc47b9e8c74b1d8372491e69c39933c4186aec4df55571614f8ba98e3c
  8bb9c9aeb8ca4fc6858d10eb72cd23d1878d9c74fb0e33b9053c196d521b84349786046ebcef7d11f8eb1d8050d29c89aa15b99571179c6b3ab9458055905da4
  736d2b32c9d2c831614a4c84fbb45087f8e43b4f9a8f77e7860061143665df61dfdcf2b867887668bfff134af548bfcc4d25dad305ecd68ff2232eec209cd2fa
  c1fdb4a17c46819a56c9baeba548d3a75b67669aeafa9b3273187d474eb0b2f87f738938613fe851db4fa5ac88d1dc06a76f915dee56c3b91d59bac436efc3c6
)

prepare() {
  patch -d "xwayland-${pkgver}" -Np1 \
    -i "${srcdir}/0001-xwayland-honor-visible-pointer-warps.patch"
  patch -d "xwayland-${pkgver}" -Np1 \
    -i "${srcdir}/0002-xwayland-drop-stale-pre-warp-motion.patch"
  patch -d "xwayland-${pkgver}" -Np1 \
    -i "${srcdir}/0003-xwayland-make-release-warp-atomic.patch"
}

build() {
  arch-meson "xwayland-${pkgver}" build \
    -D ipv6=true \
    -D xvfb=false \
    -D xdmcp=false \
    -D xcsecurity=true \
    -D dri3=true \
    -D glamor=true \
    -D libdecor=true \
    -D sha1=libnettle \
    -D xkb_dir=/usr/share/X11/xkb \
    -D xkb_output_dir=/var/lib/xkb

  meson configure build
  ninja -C build
}

package() {
  DESTDIR="${pkgdir}" ninja -C build install

  rm "${pkgdir}/usr/lib/xorg/protocol.txt"
  rmdir "${pkgdir}/usr/lib/xorg"
  rm "${pkgdir}/usr/share/man/man1/Xserver.1"

  install -Dm644 "xwayland-${pkgver}/COPYING" \
    "${pkgdir}/usr/share/licenses/${pkgname}/COPYING"
}
