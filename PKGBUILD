# Maintainer: local system administrator

pkgname=xorg-xwayland-visible-warp
pkgver=24.1.13
pkgrel=16
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
  0004-xwayland-skip-release-while-grabbed.patch
  0005-xwayland-grace-warp-ungrab-race.patch
  0006-xwayland-debug-logging.patch
  0007-xwayland-extend-post-warp-logging.patch
  0008-xwayland-sync-ordered-release-warp.patch
  0009-xwayland-ack-driven-release-retry.patch
  0010-xwayland-frame-lagged-lock-destroy.patch
)
sha512sums=(
  e06e58025b441892fdd17ac55fd5c7e137bffc941b76ad784dc008047c778c6ee2895fcc47b9e8c74b1d8372491e69c39933c4186aec4df55571614f8ba98e3c
  8bb9c9aeb8ca4fc6858d10eb72cd23d1878d9c74fb0e33b9053c196d521b84349786046ebcef7d11f8eb1d8050d29c89aa15b99571179c6b3ab9458055905da4
  736d2b32c9d2c831614a4c84fbb45087f8e43b4f9a8f77e7860061143665df61dfdcf2b867887668bfff134af548bfcc4d25dad305ecd68ff2232eec209cd2fa
  c1fdb4a17c46819a56c9baeba548d3a75b67669aeafa9b3273187d474eb0b2f87f738938613fe851db4fa5ac88d1dc06a76f915dee56c3b91d59bac436efc3c6
  b36dec8680afe1f6d21e21bfc1609bdb3906f7080c484ff20b8b93c0283ff90958594184f343610e5b99ce1c10bac94fb78509077b0fa893bc21f76b9e390d3b
  914f853a12a8cd96b7517cb12a7a6c2c70336cafa9d0c127aa29986439b5ab83c212fd47e77d0b7d92eb58919da19c6f9bc8dd5cbd01dc9ba976a319967f8c2a
  b28b182cd99b3c792d6735943a96675b307e3e95a4e2ebd7edc06109ae25260ad5ee0b33e4c7aaca56bb58ae655ff2e7ce2dd8919817b58cad8f368f78559819
  f3d61e12e4ca1e31c5ffe15ed2dad8162d72c88dac8b50d20dceaa4b51b32b7884f3fe92297eef083a1d9489a57ecc5c1f70e05272341ab97ae2706e5153af26
  0375e76b329765ac0804084709adf558653f48d8a769cf5306eae98d1741a543a96d13609698f70c0ab87ba4e8567e115d0954a729dbfbed9b9ee5bfdaad03bd
  26a75e1128f2df06389779a977659fe085fbb8baaea62b40dfdf71988f389e795cb75c634f1e24d8f661b6372d1188827b9ea60575dd55c79986dfcf3dba2407
  838e2a1f97521a602a08f1c2b1a4de3bcbe8dc50864b8413629d1039678ddab321e2cad509d1612c1666249031ff957870a54771a803cdf9f135ec35e9194d2e
)

prepare() {
  patch -d "xwayland-${pkgver}" -Np1 \
    -i "${srcdir}/0001-xwayland-honor-visible-pointer-warps.patch"
  patch -d "xwayland-${pkgver}" -Np1 \
    -i "${srcdir}/0002-xwayland-drop-stale-pre-warp-motion.patch"
  patch -d "xwayland-${pkgver}" -Np1 \
    -i "${srcdir}/0003-xwayland-make-release-warp-atomic.patch"
  patch -d "xwayland-${pkgver}" -Np1 \
    -i "${srcdir}/0004-xwayland-skip-release-while-grabbed.patch"
  patch -d "xwayland-${pkgver}" -Np1 \
    -i "${srcdir}/0005-xwayland-grace-warp-ungrab-race.patch"
  patch -d "xwayland-${pkgver}" -Np1 \
    -i "${srcdir}/0006-xwayland-debug-logging.patch"
  patch -d "xwayland-${pkgver}" -Np1 \
    -i "${srcdir}/0007-xwayland-extend-post-warp-logging.patch"
  patch -d "xwayland-${pkgver}" -Np1 \
    -i "${srcdir}/0008-xwayland-sync-ordered-release-warp.patch"
  patch -d "xwayland-${pkgver}" -Np1 \
    -i "${srcdir}/0009-xwayland-ack-driven-release-retry.patch"
  patch -d "xwayland-${pkgver}" -Np1 \
    -i "${srcdir}/0010-xwayland-frame-lagged-lock-destroy.patch"
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
