# Preserved revisions and bug split

## Revision 16: destroy-ordering races closed (current)

Revisions 11–16 finish the residual "inventory opens at the last position
instead of center, only while moving fast" case. Six WCF_DEBUG-instrumented
live rounds proved two compositor-side races stacked on top of each other:

- KWin applies `set_cursor_position_hint` values only through surface
  Transactions (fence-gated, ≥1 frame late); destroying the locked pointer
  in the same batch as the final hint commit applies the *previous frame's*
  hint — the cursor visibly opens ~tens of pixels off and self-corrects
  32–190 ms later.
- libwayland destructor deferral means the direct `wp_pointer_warp_v1` can
  still land while `m_locked` is set and be silently dropped.

Fix chain: `0008` (destroy → `wl_display.sync` → warp ordering),
`0009` (wrong-absolute ack → immediate idempotent re-send, capped),
`0010` (hold the destroy ≈1 frame after the final hint commit so the hint
is latched when teardown reads it). User-verified on v16: bug no longer
reproducible; ~1-frame cosmetic shimmer remains.

The complete forensic record (mechanisms, log excerpts, tooling, known
risks incl. the unfixed out-of-surface branch) is in
[investigation/README.md](investigation/README.md).

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

## Reproducible artifacts

- Revision-16 package:
  `xorg-xwayland-visible-warp-24.1.13-16-x86_64.pkg.tar.zst`
- Revision-16 package SHA-256:
  `0446a3d7cfb3aa987130bbdb93b6707e2a170bfee096536044b36e0553adf1cb`
- Revision-16 packaged `/usr/bin/Xwayland` SHA-256:
  `f33c531e0f50620cc7c6a0ab2ebde8d37f5b41f078a1123080d915c32808b7a2`

Full per-revision artifact hashes (0001–0010 patches, packages v4–v16) are
tracked in `SAVED_ARTIFACTS.sha256`. Git tags preserve source: `v7-*`,
`v9-*`, `v10-*`, `v16-*`.
