# X11 pointer-capture restore on KWin Wayland — full investigation

This document is the complete forensic record of the cursor-restore bugs
that this package fixes for X11 games (Badlion/Minecraft LWJGL-style
grab → hide → relative-motion capture → ungrab → show) running on Xwayland
under KWin Wayland. It covers every failing mechanism found, how each was
proven, and which patch carries the fix.

Fix lineage: revisions 1–10 apply `0001`–`0005`; revisions 11–13 add the
WCF_DEBUG diagnostic layer (`0006`, `0007`); revisions 14–16 complete the
fix chain with `0008`, `0009`, `0010`.

---

## 1. The subsystem and why it is fragile

Minecraft-style X11 capture:

1. `XGrabPointer` (often with `confineTo = own window`) + `XFixesHideCursor`
2. Play phase: game reads raw relative deltas every frame and repeatedly
   centers the virtual pointer with hidden `XWarpPointer` calls.
3. Menu phase: game sends a final warp (window center), `XUngrabPointer`,
   and `XFixesShowCursor`.

For this to work on Wayland, Xwayland must:

- install a `zwp_pointer_constraints_v1` **locked pointer** on the game's
  `wl_surface` so KWin feeds `zwp_relative_pointer_v1` events instead of
  moving the physical cursor;
- on release, place the *physical* cursor at the chosen position. Two
  compositor paths can do this, and must agree:
  - `zwp_locked_pointer_v1.set_cursor_position_hint` + destroy → KWin
    "unlock restore" applies the hint (`PointerInputRedirection::
    processWarp(globalHint)` in the lock's `aboutToBeDestroyed` /
    `destroyed` handler);
  - `wp_pointer_warp_v1` → KWin's warp handler
    (`serial == focusedSerial`, `boundingRect().contains(point)`,
    `findWindow(...)`), **rejected while `m_locked` is set**
    (`PointerInputRedirection::updatePosition()` early-returns).

Timeline hazards this creates:

- KWin gates hint state through the surface **Transaction** machinery:
  `zwp_locked_pointer_v1_set_cursor_position_hint` writes only *pending*
  state; `LockedPointerV1InterfacePrivate::apply()` moves it to the value
  the destroy reads (`cursorPositionHint()`), and `apply()` runs only when
  the surface's current Transaction passes its graphics fences — i.e.,
  **at least one frame after commit** (`wayland/transaction.cpp`,
  `surface.cpp`, `pointerconstraints_v1.cpp`).
- libwayland **defers resource destructor dispatch**, so
  `zwp_locked_pointer_v1`'s teardown (and thus `m_locked = false`, via
  `pointerConstraintsChanged`) can trail subsequent requests in the same
  client batch.
- Xwayland itself delays showing a newly visible cursor by ~5 ms, opening
  a window where queued relative motion can move the virtual sprite after
  the game's final warp.
- The lock can only engage while the *physical cursor is over the grab
  surface* (KWin evaluates constraints once, at install/activation/region
  change, `region().contains(m_pos)`); and `wp_pointer_warp_v1` requires
  the pointer to still be focused on that surface.

## 2. Bug history, one by one

### 2.1 Visible warps silently dropped (patch 0001)

Stock Xwayland ignores `XWarpPointer` whenever the cursor is visible:
the X11 sprite says "centered" while KWin's physical pointer is elsewhere.
Fix: honor visible warps through `wp_pointer_warp_v1` (focused, in-bounds
requests only), with the current pointer-enter serial.

### 2.2 Stale pre-warp absolute motions (patch 0002)

A warp can be overwritten ~1 ms later by an absolute motion event that was
already queued before the warp request (`x=3200 → 3933,1041` in the first
capture). Fix: after a warp, `wl_display.sync` barrier; drop absolute
positions ≠ target until the compositor reports the target or the sync
completes.

### 2.3 Atomic hidden warp → ungrab pair (patch 0003)

Relative motion queued before the game's final warp, or landing during the
5 ms show-delay window, moved the virtual sprite between warp and ungrab:
the restored cursor jumped far along the last camera movement.
Fix: remember hidden warps as a *release candidate*; invalidate on
genuinely newer motion while the grab is active; freeze at ungrab; commit
the candidate as final hint and send the same direct warp so both
compositor paths agree.

### 2.4 Ghost release while re-grabbing (patch 0004)

If the game re-grabs+re-hides inside the 5 ms show-delay window
(inventory spam), the release branch fired during the new capture,
destroyed the new lock and teleported the cursor to the stale target —
the rest of the capture then ran lock-free. Fix: skip the release branch
while an explicit pointer grab (or, later, any hidden-cursor capture) is
active.

### 2.5 Warp→ungrab sub-millisecond race at high polling rates (patch 0005)

At ~1 kHz input, relative events landing between `ProcWarpPointer` and
`ProcUngrabPointer` (or delivered with newer timestamps while in flight)
invalidated the frozen release candidate; the restore then fell back to
the drifted sprite (~25 % of releases in the lab harness, errors of
exactly 1–2 event deltas, direction-preserving). XRecord showed real
pairs process within one server millisecond. Fix: 8 ms grace window on
invalidating timestamps.

### 2.6 Out-of-surface release (branch: open)

If the physical cursor has left the game surface (flick past the window
edge during a menu), the next `maybe_lock` finds `focus_window = nil` and
**no lock ever engages for the whole capture**, the unlock-hint commit is
skipped, and `wp_pointer_warp_v1` is dropped (`focus=(nil)` guard).
The restore is then structurally impossible. Captured live in
`investigation/xwl-debug-game1.log` (leave at 99567504, release at
99874063, `warp_v1 DROP ... focus=(nil)`). **Not fixed by this package** —
the true fix is compositor-side (KWin accepting warps for
constraint-holding surfaces, or re-evaluating constraints on surface
enter).

### 2.7 The residual: stale unlock-hint + destroy-ordering races (0008, 0009, 0010)

With 0001–0005 in place the *visible* failure remained: "inventory opens
with the cursor at the position it had when I closed it, not at center,
specifically while the mouse is moving fast." Six live instrumentation
rounds (`0006`/`0007` WCF_DEBUG logging) decomposed it:

**Round A (v11, game1):** releases logged as CANDIDATE + `warp_v1 SEND`
but the physical cursor ended elsewhere, or `DROP focus=(nil)` (2.6).

**Round B (v12, game2/game3):** first absolute motion after each accepted
warp logged. Good releases: absolute = target within ~30–70 µs.
Failing releases:

```
78349802 warp_v1 SEND target=(3200,718) serial=580
78349839 post-warp absolute sx=1985 sy=690 since_warp_us=32    STALE (~1 frame of drift)
```

The stuck position always equaled the last *mid-capture* fake_pos hint —
**one surface transaction older than the final warp**, exactly matching the
Transaction gating in §1.

**Round C (v13, game4):** multi-event probe (up to 8 absolutes after each
warp). Failing release: absolutes never included the target at all —
the direct warp did not land at all, and KWin's destroy had applied the
stale hint. Good releases: first absolute = target.

**Round D (v14, game5):** 0008's destroy→`wl_display.sync`→warp ordering
fixed the three good cases; the fourth still failed: sync can complete
**before** libwayland finishes the lock's destructor dispatch, so the
warp still occasionally landed while `m_locked` was set and got dropped.

**Round E (v15, game6):** 0009's ack-driven retry (re-send on
wrong-absolute or on barrier boundary, capped) prevented all *wrong*
settles — but the settle now arrived only through the destroy-hint at
**+32…+190 ms**, i.e., visibly late: the cursor opened ≈1 frame stale and
corrected itself over several frames. That's what the user felt as "where
it was when I closed it".

**Revision 16 (0010, final):** attack the root instead of the symptom —
hold the lock destroy ~25 ms (≈1 frame @60 Hz) after committing the final
hint, so the hint transaction has *already latched* when the destroy
runs. The unlock restore then lands on the target on the very first
compositor frame, with the entire 0008/0009 path kept as insurance and
the whole window hidden inside the pre-existing ~5 ms cursor-show delay.

### Result on v16 (user-verified)

- No reproducible failure. All releases land at the target.
- Residual cosmetic: the prior position can be visible for ~1 compositor
  frame before the warp ack; below perception thresholds for normal use.

## 3. Compositor facts established (KWin 6.6.4)

- `PointerInputRedirection::updatePosition()` early-returns while
  `m_locked` — all warps dropped while a lock is engaged.
- `wp_pointer_warp_v1` handler checks
  `serial == seat()->pointer()->focusedSerial()`,
  `surface->boundingRect().contains(point)`, `findWindow(...)`; no
  activeWindow check.
- Unlock-by-destroy ordering: `aboutToBeDestroyed` → surface cleanUp →
  `pointerConstraintsChanged` (`m_locked=false`) → `destroyed` →
  `processWarp(globalHint)` with `delete q`.
- `set_cursor_position_hint` is double-buffered through surface
  Transactions; `apply()` on fence completion — ≥1 frame latency.
- Constraints are evaluated once (`constraintsChanged`, activation,
  region change): a lock installed while the pointer is outside the
  surface region never engages until re-activation.
- `updatePosition` also ignores input during `PositionUpdateBlocker`
  re-entrancy (schedules positions internally; not implicated).
- libwayland defers `wl_resource` destructor dispatch independently of
  request FIFO order of other interfaces in the same batch.

## 4. Evidence (investigation/)

| file | content |
|------|---------|
| `xwl-debug-v16-final.log` | WCF_DEBUG log of the **passing** v16 session (74 frame-lagged destroy+restore events) |
| `xwl-debug-game4.log` | v13 probe: failing release settles at stale hint, target never acked |
| `xwl-debug-game5.log` | v14 (sync ordering): 3/4 good, 4th shows destructor-deferral warp drop |
| `xwl-debug-game6.log` | v15 (ack retry): no wrong settles; +32–190 ms settle latency proven |
| `xwl-debug-game2.log` | v12: first post-warp absolute = stale position (32 µs after good warp) |
| `xwl-debug-game3.log` | v12: bigger session with `post-warp absolute sx=2344` stale settlement |
| `xwl-debug-game1.log` | v11: out-of-surface `warp_v1 DROP focus=(nil)` capture |
| `xrecord-badlion-session.log` | live Badlion grabs/warps/ungrabs with server ms (pattern reference) |
| `kwin-poll-positions.log` | KWin-side physical cursor samples (200 ms poll) across sessions |

## 5. Diagnostic tooling (for future regressions)

- **WCF_DEBUG logging layer (0006/0007, built into the package, inert
  when unset):** `WCF_DEBUG=/tmp/xwl-debug.log` in
  `~/.config/environment.d/wcf.conf` (settings apply at login).
  Logs cover pointer enter/leave, lock decisions/engagement, candidate
  set/invalidate (with µs gaps), release-branch decisions,
  hint commits, warp sends/drops, and the first 8 absolute motions after
  every accepted warp with µs offsets.
- **Physical-cursor truth**: `~/.analysis/watch-cursor.sh` loads a KWin
  script polling `workspace.cursorPos` (200 ms, prints `WCFPOLL`).
  Faster journal traces (`cursorPosChanged`) die reproducibly after
  warp events in KWin 6.6 — use the poll.
- **Request timeline**: `diagnostics/x11-request-trace` (XRecord,
  grab/warp/ungrab with server ms).
- **Motion injection**: `.analysis/portal-inject.py` (RemoteDesktop
  portal, pre-authorized via PermissionStore `kde-authorized`,
  `remote-desktop` → `yes`). Note: XTEST silently degrades to
  Xwayland-internal fake input when `plasma-xdg-desktop-portal-kde` is
  not running — verify KWin-side movement before believing any harness.
- **Repro clients**: `.analysis/repro-client.c` (cycle patterns),
  `.analysis/race-client.c` (tight/split warp-ungrab races, SEQ mode,
  lock-discriminating motion windows).

## 6. Known remaining risks

- **Out-of-surface capture/restore (§2.6)** is unfixed and needs a
  compositor-side change; workaround: avoid flicks that carry the cursor
  past the game's surface during menus.
- **Scaled displays** (`xwaylandScale != 1`): the unlock hint is divided
  by `surface->scaleOverride` in KWin while wp warp coords are not;
  untested, likely wrong restores on scaled setups.
- **Frame-lagged destroy**: hard-coded 25 ms assumes ~60 Hz-class frame
  pacing; on a 240 Hz display the hint latches sooner (harmless, just the
  fixed hold); on sub-40 Hz panels it is the intended lower bound.
- The WCF_DEBUG layer adds string/string work only when enabled; consider
  dropping 0006/0007 from a "stable" build once the area is quiet.
