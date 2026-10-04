# Maintainer: local system administrator

pkgname=xorg-xwayland-visible-warp
pkgver=24.1.13
pkgrel=19
pkgdesc='Xwayland that restores the pointer at the right spot when a game releases mouse capture'
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
  0001-xwayland-hold-cursor-show-until-lock-hint-latched.patch
)
sha512sums=('e06e58025b441892fdd17ac55fd5c7e137bffc941b76ad784dc008047c778c6ee2895fcc47b9e8c74b1d8372491e69c39933c4186aec4df55571614f8ba98e3c'
            '0a29e939224c231cdd04998399dc256c268fd64fdd89ed1ca120622468cbf411d0f8c46378f16862f8ab5cdf6ebd48746fb4d84c290fff6137e571229e5ac1ca')

prepare() {
  patch -d "xwayland-${pkgver}" -Np1 \
    -i "${srcdir}/0001-xwayland-hold-cursor-show-until-lock-hint-latched.patch"
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
