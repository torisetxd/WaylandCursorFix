# WaylandCursorFix

A patched Xwayland (`xorg-xwayland-visible-warp`, Xwayland 24.1.13) for KDE
Plasma / KWin Wayland on Arch and CachyOS. It fixes where the mouse pointer
reappears when an X11 game releases mouse capture: opening the inventory in
Minecraft 1.8.9 (LWJGL 2), or a menu in CS2 running on its X11 backend. Stock
Xwayland can put the pointer at a stale position, usually the window edge or
the spot it had before capture, particularly when the mouse is moving fast or
the game is GPU bound. Native Wayland clients are not affected and are not
touched.

**Revision 19 is the current release.** It replaces the revision 4–18 stack
(kept under [`legacy-patches/`](legacy-patches/)) with a single patch.

## What goes wrong in stock Xwayland

X11 games capture the mouse with a hidden cursor plus a grab. Xwayland
emulates that with a `zwp_locked_pointer_v1` whose *cursor position hint*
follows the virtual X11 pointer and the game's `XWarpPointer` calls. When the
game releases the capture, Xwayland destroys the lock and KWin moves the real
pointer to the hint.

The hint is double-buffered surface state. KWin only latches it when the
commit carrying it is applied, and that commit is applied *after every buffer
commit that preceded it*. When the game's previous frames are still waiting on
the GPU, the final hint is still queued when the lock is destroyed, and KWin
reads an older one. That is the whole bug; it reproduces on unpatched
Xwayland with no game involved (see [Testing](#testing)).

Two things that look like fixes do not work on KWin: Xwayland's
`wp_pointer_warp_v1` request is silently dropped for X11 windows (KWin looks
the window up in a list that only contains native Wayland windows; checked on
6.7.4, master and the 6.8 branch), so the position hint is the only way to
place the pointer.

## What the patch does

`0001-xwayland-hold-cursor-show-until-lock-hint-latched.patch`

When the game shows the cursor while the pointer lock is active, Xwayland no
longer destroys the lock at once. It keeps the cursor hidden, requests a
`wl_surface.frame` callback together with the latest hint commit, and destroys
the lock and shows the cursor when the callback fires. By then every earlier
commit, the hint included, has been applied, so the pointer appears at its
final position and is never visible anywhere else.

- Explicit warps during the wait re-arm the callback, so the last warp wins.
- A 100 ms timer bounds the wait if the compositor never sends a callback.
- A window with no outstanding frame callback (an idle window) has nothing
  queued and keeps the stock, immediate behavior.
- If the lock disappears during the wait, the pending cursor is shown at once.

The cost is that the cursor appears after at most about one compositor frame
(about 17 ms at 60 Hz, longer when the game itself needs several frames to
render) instead of instantly at the wrong place. The game's own menu frame
arrives through the same queue, so the cursor shows up together with it.

Visible-cursor warps (a game calling `XWarpPointer` while its cursor is
shown) are unchanged from stock, which ignores them. Revisions 1–18 tried to
honor them through `wp_pointer_warp_v1`, which has no effect on KWin for X11
windows.

## Install on CachyOS or Arch Linux

Run as your normal user:

```sh
./install.sh
```

If the matching local package is present the installer uses it directly,
otherwise it builds it from the pinned Xwayland source with `makepkg`.

Then reboot, or log out of the Plasma Wayland session and back in. The running
Xwayland cannot be replaced in place, and every X11 application has to be
closed when it restarts, which is why the installer does not do it for you.

```sh
./verify.sh
```

checks the installed package version, the binary hash and that the *running*
Xwayland is the installed one.

The package provides `xorg-xwayland`, so normal dependency checks keep
working. The revision-19 package SHA-256 is in
[SAVED_ARTIFACTS.sha256](SAVED_ARTIFACTS.sha256).

### Rollback

`./rollback-to-v7.sh` and `./rollback-to-v4.sh` reinstall the old saved
packages. Those were built against `libnettle.so.8`, which current Arch no
longer ships (it has `libnettle.so.9`), so they will not start until rebuilt.
To go back to the distribution's Xwayland use `./uninstall.sh`.

## Testing

`test/` is a self-contained rig, so a change can be checked without playing
anything. It runs a nested `kwin_wayland --virtual --xwayland` in a private
D-Bus session and never touches the live desktop:

- `nest.sh` starts the isolated session with a chosen Xwayland binary.
- `wcf-mouse` is a virtual mouse that injects input through KWin's EIS
  interface (1 kHz with jitter, absolute moves, clicks).
- `cursor-probe.js` is a KWin script reporting the compositor's real pointer
  position, which is the ground truth.
- `sim-lwjgl` reproduces the recorded X11 request pattern of Minecraft/LWJGL 2
  (hidden grab, per-frame recentering warps, warp + ungrab on release) and
  `sim-sdl` drives the real SDL3 X11 driver, both rendering real GL frames
  with adjustable GPU load.
- `analyze.py` scores each release: the pointer must reach the target, stay
  there and follow any motion injected afterwards.
- `run-case.sh` runs one scenario, `matrix.sh` runs them all,
  `build-xwayland.sh` builds a binary from the pinned tarball and patches
  without installing anything.

```sh
test/build-xwayland.sh /tmp/xw 0001-xwayland-hold-cursor-show-until-lock-hint-latched.patch
test/matrix.sh /tmp/xw/build/hw/xwayland/Xwayland mytest
```

`diagnostics/x11-cursor-trace` records every pointer grab / warp / cursor
request a client sends (including XInput2 and XFixes), and
`investigation/x11-cursor-trace-cs2-and-mc-rev19.log` is one such recording of
real CS2 and Minecraft sessions on revision 19.

`test/wl/native-warp` is a native Wayland client used to show that KWin
honors `wp_pointer_warp_v1` for native windows only.

Results (target tolerance 3 px; "settle" is the time from release until the
pointer is on target, worst case of the row):

| scenario (10 releases each) | stock Xwayland | revision 19 |
| --- | --- | --- |
| no GPU load, both simulators, moving or still release | 10/10, 6 ms | 10/10, 18 ms |
| moderate load (150 quads), both simulators | 10/10, 6-16 ms | 10/10, 18 ms |
| heavy load (about 40 ms frames), LWJGL-style, moving release | 5/10 | 10/10, 45 ms |
| heavy load, LWJGL-style, still release | 10/10 | 10/10, 43 ms |
| heavy load, SDL3, moving release | 1/10 | 10/10, 84 ms |
| heavy load, SDL3, still release | 3/10 | 10/10, 84 ms |
| pointer keeps moving after release; release then quick re-capture | not scored | 8/8 and 20/20 per simulator |

All revision-19 rows are from the packaged binary
(`xorg-xwayland-visible-warp-24.1.13-19`), stock rows from the unpatched 24.1.13
build, same rig. Stock only breaks once the game is GPU bound, which is also
when real games hit it.

The pointer is hidden during the settle time of the right-hand column, so the
settle time is how long the cursor takes to appear, not how long it is wrong.

## Known limitation: capture while the pointer is on another monitor

On real X11 a confining `XGrabPointer` pulls the pointer into the window. A
Wayland compositor has no equivalent for Xwayland: KWin only activates a
pointer lock or confine while the pointer is already over that surface
(`PointerInputRedirection::updatePointerConstraints`), `wp_pointer_warp_v1`
needs an existing pointer focus and is ignored for X11 windows anyway, and
KWin exposes no way to move the pointer from a script or D-Bus
(`workspace.cursorPos` is read-only). So if a game asks for capture while the
pointer is on another monitor, the capture only engages once the pointer
comes back over the game window (clicking the game window does it). Once it
is over the window the capture works, with stock Xwayland and with this patch
alike (`test/analyze-capture.py`, `sim-sdl --outside 1`). Fixing it needs
KWin-side support.

## Scope

System-wide for X11 and Xwayland clients, including native Linux X11 games
and Wine/Proton games using X11. Native Wayland games are not affected. The
patch is only meaningful on a compositor that applies the lock's position hint
on destroy, which KWin does.

## Build manually

```sh
makepkg --syncdeps --install
```

The build applies the single patch to the pinned Xwayland 24.1.13 tarball.
`pkgver` and the checksums must be updated and the patch rebased when Xwayland
is upgraded; keeping a distinct provider package prevents an ordinary system
upgrade from silently replacing the patched binary with the stock one.

## Other directories

- `legacy-patches/`: the revision 4–18 patch stack, for reference only.
- `diagnostics/`: read-only X11 request / KWin cursor tracing tools.
- `investigation/`, `REVISIONS.md`, `continuity.md`: the history and evidence
  behind the earlier revisions. Their conclusion that `wp_pointer_warp_v1`
  delivers the release warp is superseded by the findings above.
