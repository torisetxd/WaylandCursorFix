# Preserved revisions and bug split

## Revision 10: warp→ungrab invalidation grace

At ~1 kHz input rates, relative motion which physically lands inside the
sub-millisecond window between `WarpPointer` and `UngrabPointer` request
processing, or which the compositor had already delivered while the warp was
being processed, invalidated the revision-8 release candidate. The release
branch then fell back to the moved sprite position: direction-preserving,
speed-correlated reset misses of one to several event deltas
(`0005-xwayland-grace-warp-ungrab-race.patch`).

Reproduced on KWin 6.6.4 with a RemoteDesktop-portal injector and the
`race-client` harness in `.analysis/`: 9/40 tight-window releases restored
1-2 event deltas past target; 15/15 releases with an 8 ms warp→ungrab split
showed exactly the accumulated window deltas. XRecord traces show the real
pair is processed within one server millisecond, so motion inside an 8 ms
grace window cannot be meaningfully newer; capture-mode recenter warps
(16+ ms apart) are still invalidated by the continuous motion that follows.

## Revision 9: skip release while re-grabbed

Xwayland's delayed cursor show can fire up to 5 ms after a release, while the
game has already re-grabbed and re-hidden the pointer (Badlion/Minecraft menu
spam). The revision-8 release branch fired during the new capture, destroyed
its pointer lock and teleported the cursor to the stale target; the capture
continued lock-free and the following release restored nothing.
`0004-xwayland-skip-release-while-grabbed.patch` skips the release branch
whenever an explicit pointer grab is active.

## Revision 8: atomic hidden-warp release

The second live trace captured 36 grab/ungrab cycles from Badlion Minecraft.
For every affected transition, the client sent the correct local center warp
to `(1280, 690)` and immediately followed it with `UngrabPointer`. It never
sent an off-screen or amplified warp. In failing cycles, KWin retained or
resumed from the locked physical position, sometimes on DP-2.

Xwayland delays showing a newly visible cursor for up to 5 ms. During that
interval, queued relative camera motion can move its virtual pointer away from
the game's final center warp. KWin also ignores a direct pointer-warp request
while its pointer constraint is still active, leaving the later unlock hint as
the authoritative fallback.

`0003-xwayland-make-release-warp-atomic.patch` addresses that exact handoff:

- A hidden warp becomes a release candidate.
- Newer relative motion invalidates ordinary recenter candidates while the
  grab remains active.
- An immediately following explicit ungrab freezes the final candidate across
  the delayed cursor-show interval.
- Xwayland commits the selected target as the final unlock hint and issues the
  direct warp to the same target.

There is no persistent lock, stable anchor across ordinary movement,
forced-immediate cursor show, sleep, timer, or added Wayland round trip.
Revision 7 remains tagged and packaged as the rollback baseline until revision
8 has been validated in the original Minecraft reproduction.

## Revision 7: stale pre-warp motion barrier

Revision 7 is the preserved baseline for further work. It consists of:

- `0001-xwayland-honor-visible-pointer-warps.patch`: the direct visible
  `wp_pointer_warp_v1` implementation and v4-era hidden-grab behavior.
- `0002-xwayland-drop-stale-pre-warp-motion.patch`: the nonblocking Wayland
  ordering barrier added after the live revision-6 trace.

The trace captured Minecraft moving the X11 pointer to the exact center of
DP-1, `(3200, 718)`, followed 1.06 ms later by an old absolute motion event
which restored `(3933, 1041)`. Revision 7 drops only such pre-request absolute
positions until the target acknowledgement or the immediately following
`wl_display.sync` boundary.

Revision 7 intentionally removes every revision-5/revision-6 stable-anchor,
persistent-lock, and forced-immediate-cursor-show change.

## Keep the two bugs separate

1. **Visible warp delivery/order bug:** an X11 game requests the correct warp,
   but stock Xwayland ignores it or a stale Wayland absolute motion event
   overwrites it. The v4 direct warp and v7 ordering barrier address this bug.

2. **Relative-motion amplification during ungrab:** if a menu or inventory
   opens while the mouse is actively moving in grabbed camera mode, the latest
   motion vector can be preserved in the correct direction but amplified by
   roughly two orders of magnitude. The released cursor can consequently
   travel thousands of pixels or cross onto another monitor. The live trace
   observed about 2,000 pixels of travel in roughly 130 ms. Native Wayland
   applications do not show this behavior, so this remains an X11/Xwayland
   grab-transition bug rather than a physical-device or compositor-wide
   acceleration problem. Revision 8 targets the traced atomic-release failure;
   it remains test-pending in the original reproduction.

   This is separate from the visible-warp delivery/order bug. Revisions 5 and
   6 attempted to solve the resulting release offset with a persistent lock
   and stable anchor, but did not address the amplified motion and regressed
   behavior. They must not be treated as a valid baseline.

## Reproducible artifacts

- Revision-10 package:
  `xorg-xwayland-visible-warp-24.1.13-10-x86_64.pkg.tar.zst`
- Revision-10 package SHA-256:
  `6de438c5609116fcf825e8e348e063bbfc4c7f3a3d062de61a5ff743b7294771`
- Revision-10 packaged `/usr/bin/Xwayland` SHA-256:
  `62449f47dbd3bca6e00175915cdb657bfd6c900b4ba63cd08b67c460786b77cf`

- Revision-9 package:
  `xorg-xwayland-visible-warp-24.1.13-9-x86_64.pkg.tar.zst`
- Revision-9 package SHA-256:
  `51f5530f5a983530cb6232fb2439f325718dd5a65be792c7a00c6342631d1afa`
- Revision-9 packaged `/usr/bin/Xwayland` SHA-256:
  `9cc886474a0eea9b97e37dbe3eda89d1276fc14d9051f315316ced5ed02941fa`

- Revision-8 package:
  `xorg-xwayland-visible-warp-24.1.13-8-x86_64.pkg.tar.zst`
- Revision-8 package SHA-256:
  `13f0da165e5dc73e40b4ea3a632f4b141995197b58e90a6dd6febcd4d20924b0`
- Revision-8 packaged `/usr/bin/Xwayland` SHA-256:
  `3b78541759005f058711d65bf942f1f1598e91bc2cec310ca4a93cdcbdf8c03b`

- Revision-7 package: `xorg-xwayland-visible-warp-24.1.13-7-x86_64.pkg.tar.zst`
- Package SHA-256:
  `eb756466d7ff7c8f974a8ef2e5297e104a6dbe51aaa463c2c0a29e901eebb95d`
- Packaged `/usr/bin/Xwayland` SHA-256:
  `6e33f05e1a19d2e4dd9dda6580a65ef312866320c500a385b94bafd4ee02518e`
- Last known-better fallback package:
  `xorg-xwayland-visible-warp-24.1.13-4-x86_64.pkg.tar.zst`
- Revision-4 package SHA-256:
  `926900e8015420111166145ae68a5b466de91569e12f32b911292505381a2a0e`

The Git tag `v7-stale-pre-warp-barrier` and the standalone bundle under
`saved-revisions/` preserve the source independently of the build trees.
