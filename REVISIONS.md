# Revisions

## Revision 19: one patch, hidden cursor until the hint is latched

Rebuilt from the pristine 24.1.13 source with a single patch,
`0001-xwayland-hold-cursor-show-until-lock-hint-latched.patch`. The old stack
is in `legacy-patches/`.

Findings that drove it, all reproduced in the nested-KWin rig under `test/`:

- The restore bug is the double-buffered lock position hint. KWin latches it
  when the surface transaction that carried it is applied, which happens after
  every earlier unready buffer commit of that surface. Under GPU load the
  final hint is still queued when the lock is destroyed, so KWin reads an
  older one. Unpatched Xwayland: 3 of 10 correct releases at heavy load.
- KWin (6.7.4, master, 6.8) drops `wp_pointer_warp_v1` for Xwayland windows:
  the handler uses `WaylandServer::findWindow()`, which only searches native
  Wayland windows. The release warps of patches 0001, 0003, 0008 and 0009
  therefore never had an effect on KWin; every earlier "success" came from
  the hint path. `xwl_seat_has_explicit_pointer_grab()` always returned FALSE,
  so patch 0004 was dead code as well.
- The earlier diagnosis that libwayland defers the lock destructor is not
  supported by the KWin source (destroy is synchronous).
- Revisions 16-18 waited for the hint to latch while the cursor was already
  visible at the stale position, then jumped it (a visible teleport, 33-125 ms
  late under load). Revision 19 waits the same way but keeps the cursor
  hidden, so it only ever appears at the final position.

Verification: `test/matrix.sh` (both simulators x 0/150/400 GPU quads x moving
/ still release, 10 cycles each) passes 12 of 12 on the packaged binary, plus
post-release motion tracking and quick re-capture. See the README table.

The first revision-19 analysis run of the test rig mis-scored some cycles
because the simulator repositioned the pointer for the next cycle inside the
analysis window; `analyze.py` now ends the window at that reposition. The
rev-18 "fails the SDL pattern at light load" reading from that run was this
artifact, not a bug in revision 18.

# Preserved revisions and bug split

## Revision 17: frame-paced lock destroy (current)

Revision 16's fix for the stale-hint unlock race held the lock destroy for a
fixed 25 ms — a blind guess tuned to ~60 Hz pacing. Revision 17 replaces the
guess with the exact signal: a `wl_surface.frame` callback requested
immediately *before* the final hint commit, so it binds to that commit's
transaction. KWin merges hint state and frame callbacks through the same
`pending` → `current` boundary and fires the callback only at
`frameRendered`, i.e. strictly after the hint latched. The destroy therefore
happens the instant the frame carrying the hint is presented — the same
frame if the commit makes the upcoming repaint, the next painted frame
otherwise — at minimal and refresh-agnostic latency (~2–16 ms instead of a
constant 25 ms, and never more than one painted frame). A 100 ms backstop
timer (25 ms when no callback could be armed) covers compositors throttling
frame callbacks on occluded surfaces, and window teardown cancels a pending
callback so no proxy outlives its `wl_surface`. Same re-grab / cursor-hidden
guards as revision 16.

## Revision 16: destroy-ordering races closed

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

- Revision-17 package:
  `xorg-xwayland-visible-warp-24.1.13-17-x86_64.pkg.tar.zst`
- Revision-17 package SHA-256:
  `49bc95dd5e3a0788d0766b45b06f6fe8224cd35f6bcb035ffea96fbfd5885fb9`
- Revision-17 packaged `/usr/bin/Xwayland` SHA-256:
  `0e7662b2a2e38e087cbfd519ce8678890f405e24dc395b0e0dece63b0d501877`

Full per-revision artifact hashes (0001–0010 patches, packages v4–v17) are
tracked in `SAVED_ARTIFACTS.sha256`. Git tags preserve source: `v7-*`,
`v9-*`, `v10-*`, `v16-*`.
