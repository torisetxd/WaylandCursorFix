# Immediate visible pointer warps for Xwayland

This is a system-wide Xwayland patch for games that call `XWarpPointer` (or
the equivalent XI2 request) and expect the real mouse pointer to move. Stock
Xwayland deliberately ignores the real warp whenever the cursor is visible,
so its internal X11 pointer can say “centered” while KWin's pointer remains
somewhere else.

The patch sends a focused, in-bounds visible warp through Wayland's dedicated
`wp_pointer_warp_v1` protocol:

1. Xwayland converts the X11 destination to surface-local coordinates.
2. It sends one direct warp request with the current pointer-enter serial.
3. KWin validates and applies it immediately.

There is no polling service, injected library, game-specific configuration,
sleep, timer, or display-frame wait. Hidden-cursor grabs keep tracking the
virtual X11 pointer; releasing the grab unlocks and directly restores the
latest tracked location even if the game sends no final explicit warp.
Tracking starts immediately for both confined grabs and focused explicit grabs
with `confineTo=None`, as used by LWJGL 2.

A direct warp is followed by a nonblocking Wayland sync marker. Until the
compositor reports the requested target or that marker completes, Xwayland
drops absolute positions that were already queued before the warp request.
This prevents an old physical-position event from overwriting the correct X11
warp a millisecond later, without delaying the warp or suppressing subsequent
motion. An event-driven pointer-lock fallback remains for compositors that do
not advertise the dedicated protocol.

Revision 8 also makes a hidden `WarpPointer` immediately followed by
`UngrabPointer` an atomic release operation. Xwayland normally delays showing
a newly visible cursor for up to 5 ms; relative camera motion could move its
virtual pointer during that interval and make the cursor reappear far away.
The patch freezes only the explicit final release target, commits it as the
locked-pointer unlock hint, and sends the same direct warp. Ordinary recenter
warps and games that do not send a final warp retain the revision-7 behavior.
This adds no sleep, polling, timer, or compositor round trip.

## Install on CachyOS or Arch Linux

Run as your normal user:

```sh
./install.sh
```

The repository includes a locally built package for this machine. The
installer uses it directly, so installation does not require a compiler or
build dependencies. If the package file is removed, the same script rebuilds
it from the pinned source and patch.

Then reboot, or log out of the Plasma Wayland session and back in. Existing
X11 applications must close when Xwayland restarts, which is why the installer
does not kill the live server. Rebooting is the simplest option if Plasma has
previously failed to complete a logout/login cycle cleanly.

Confirm the package and running executable:

```sh
./verify.sh
```

The package is named `xorg-xwayland-visible-warp` and provides
`xorg-xwayland`, so normal package dependency checks continue to work.

The revision-8 package SHA-256 is:

```text
13f0da165e5dc73e40b4ea3a632f4b141995197b58e90a6dd6febcd4d20924b0
```

The preserved revision-7 baseline is available for immediate rollback:

```sh
./rollback-to-v7.sh
```

The older revision-4 fallback is also retained:

```sh
./rollback-to-v4.sh
```

## Scope

The fix covers X11 and Xwayland games system-wide, including native Linux X11
games and Wine/Proton games using X11. It cannot alter a native Wayland game:
Wayland intentionally has no arbitrary pointer-warp request. Native Wayland
games must use relative-pointer and pointer-constraints correctly, or be run
through their X11 backend to pass through Xwayland.

Visible X11 applications regain the traditional X11 ability to move the
pointer, but only while focused and only to a point inside the focused
Xwayland surface. That is a deliberate compatibility/security boundary in
this patch.

## Uninstall

```sh
./uninstall.sh
```

Log out and back in after restoring the distribution package.

## Updating

The patch is pinned to Xwayland 24.1.13. When Xwayland is upgraded, update
`pkgver` and the source checksum, rebase the patch, rebuild, and reinstall.
Keeping a distinct provider package prevents an ordinary system upgrade from
silently replacing the patched binary with the stock package.
