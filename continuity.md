# WaylandCursorFix — continuity log (as of 2026-08-03, evening update)

Goal: make XWarpPointer honor real pointer movement in Xwayland under KWin Wayland,
and make grab→ungrab transitions (Minecraft/Badlion LWJGL2 style) restore the
cursor to the game's intended position without displacement or acceleration.

## Current package / patch state

| Rev | State | Contents |
|-----|-------|----------|
| v4  | kept (saved-revisions) | original direct warp + hidden-grab restore |
| v7  | kept, rollback baseline | 0001 + 0002 |
| v8  | superseded | + 0003 (frozen release candidate, atomic pair) |
| v9  | running on :0 NOW | + 0004 (skip release branch while an explicit pointer grab is active) |
| v10 | built, NOT installed | + 0005 (8ms grace on warp→ungrab candidate invalidation) |

- v10 package: `xorg-xwayland-visible-warp-24.1.13-10-x86_64.pkg.tar.zst`, sha256 `6de438c5609116fcf825e8e348e063bbfc4c7f3a3d062de61a5ff743b7294771`
- v10 packaged /usr/bin/Xwayland sha256 `62449f47dbd3bca6e00175915cdb657bfd6c900b4ba63cd08b67c460786b77cf`
- PKGBUILD pkgrel=10, all five patches + sha512sums updated; verify.sh/install.sh bumped to v10.
- install.sh needs pacman root; user installs: `sudo pacman -U xorg-xwayland-visible-warp-24.1.13-10-*.pkg.tar.zst` then log out/in (Xwayland :0 restart kills Badlion/Lunar — warn).
- Old table details in REVISIONS.md; SAVED_ARTIFACTS.sha256 now covers 0001-0005 + v4/v7/v8/v9/v10 packages.

- v9 package: `xorg-xwayland-visible-warp-24.1.13-9-x86_64.pkg.tar.zst`, sha256 `51f5530f5a983530cb6232fb2439f325718dd5a65be792c7a00c6342631d1afa`
- Patch 0004: `0004-xwayland-skip-release-while-grabbed.patch`, sha512 recorded in PKGBUILD (updated: pkgrel=9, source+checksums include 0004)
- Saved rollback artifacts for v4/v7 remain intact in the repo.

## Compositor / protocol facts established (KWin 6.6 source, .analysis/kwin)

- `PointerInputRedirection::updatePosition()` returns early when `m_locked` — ALL warps (including wp_pointer_warp_v1) are dropped while m_locked is set.
- wp_pointer_warp_v1 handler (pointer_input.cpp ~l.152): checks serial == `seat()->pointer()->focusedSerial()`, `surface->boundingRect().contains(point)`, `findWindow(...)`; no activeWindow check; calls `input()->pointer()->warp(...)`.
- Unlock-by-destroy order is correct in KWin: aboutToBeDestroyed → (surface cleanUp → pointerConstraintsChanged → m_locked=false) → destroyed → processWarp(globalHint). `delete q` order verified.
- `updatePointerConstraints()` engages a lock only if `m_enableConstraints && focus() == activeWindow() && lock->region().contains(pointer)` and evaluates it once (on constraintsChanged/activation/region change). If the pointer is out of the region at install, the lock never engages for that constraint's lifetime.
- ScaleOverride: on this machine xwaylandScale=1 (identity). On scaled systems KWin divides the unlock hint by scaleOverride (`pointerconstraints_v1.cpp` apply()), and wp warp coords are NOT divided — a scaled system behaves differently and is UNTESTED. Friend's machine should have scaling + versions checked first.
- Pointer warp coordinates: Xwayland sends surface-local logical coords (X coords divided by viewport_scale); KWin `mapFromLocal` then maps to global.

## What was reproduced live on this machine (v8 binary, KWin 6.6.4)

Rig in `.analysis/` (see "Rig" below). Verified:

1. **Normal flow works**: >80 grab/release cycles, pointer lock engages (physical
   cursor frozen during capture, incl. 3.5s holds), release lands ≤10px from target,
   continous motion after release is correct.
2. **Ghost/mid-capture release (root cause fixed by 0004)**: release (ungrab+show)
   followed by re-grab+re-hide within Xwayland's ~5ms delayed cursor-show window
   makes the v8 release branch fire *during the new capture*: it destroys the new
   grab's pointer lock and teleports the physical cursor to the stale release
   target. The capture then continues lock-free. Physical evidence: KWin trace
   showed a jump to the old target 4-45ms into the new grab, then free-drifting
   physical motion for the rest of the capture.
3. **Out-of-surface release is structurally impossible**: when the physical cursor
   is not over the Xwayland surface, pointer_enter_serial is 0/stale →
   `xwl_seat_warp_pointer_v1()` silently returns (bounds/serial guards) and the
   unlock hint was never connected → no restore at all. Cycle self-sustains until
   the cursor re-enters.

## Branch (1) RESOLVED on 2026-08-03 evening (warp→ungrab race = 0005)

Live-reproduced on v9 with `.analysis/race-client` + `.analysis/portal-inject.py`
(RemoteDesktop portal relative injection, ~1kHz, deltas (6,4)px/event):

- Deterministic split (`RACE_SPLIT_MS=8`, forced 8ms warp→ungrab gap): 15/15
  releases landed at target + exactly the injected deltas accumulated during
  the gap ((42..66),(28..44) px, dot>0) — candidate invalidated, fallback =
  drifted sprite. race-run3.
- Natural tight pair (1kHz injection): 9/40 (~22%) releases missed by exactly
  1-2 event steps ((6,4)/(12,8), dot>0). race-run4. Matches the user's
  "sometimes, only when moving fast".
- Baseline quiet (no injection): 8/8 exact.

Fix: 0005 treats motion within `RELEASE_WARP_GRACE_US = 8000` µs of the
candidate warp as not-newer. XRecord traces (req-run*.log) show real
warp→ungrab pairs process within one server ms; mid-capture recenter warps
are 16+ ms apart so their candidates are still invalidated normally.

- race-run1/2 invalidate: those runs had the physical cursor OUTSIDE the
  window (no lock ever engaged → sprite-only path, "exact" was meaningless).
  Lesson: verify lock engagement via journal WCFTRACE freeze before trusting
  measurement runs.

## v9 early result (user)

- Acceleration/large-displacement symptom: appears fixed ("maybe").
- Residual: **mouse sometimes does not reset to center**, specifically when the
  mouse is actively moving fast at release time; mouse is 1kHz polling, worse at
  speeds where the polling rate is actually approached. → explained by branch (1), fixed in v10.

## Rig changes 2026-08-03 evening

- **Boot gotcha (this boot 20:42)**: `plasma-xdg-desktop-portal-kde.service`
  had NOT started; xdg-desktop-portal fell back to gtk portals → NO
  RemoteDesktop interface, and XTEST silently degraded to Xwayland-internal
  fake input (XQueryPointer moves, KWin's real cursor does NOT). Fixed by
  `systemctl --user start plasma-xdg-desktop-portal-kde` + `restart
  xdg-desktop-portal`. If injection "works" but WCFTRACE is silent, check
  this first. Why it didn't autostart this boot: UNKNOWN, follow up.
- KWin scripts were also unloaded at boot; reloaded fresh (wcf-trace-all,
  wcf-dump as Script0/Script1, running; unload probe scripts kwin-probe.js /
  kwin-probe2.js were temporary).
- **portal-inject.py** (new): RemoteDesktop portal injector (`rel`/`abs`
  modes; abs needs streams=unsupported for pointer-only sessions, use rel
  chains). Uses gi/Gio (python-dbus broken on py3.14). Auto-approved via the
  already-stored kde-authorized permission.
- **race-client** (new, `.analysis/race-client.c`): env RACE_CYCLES /
  RACE_SPLIT_MS / RACE_QUIET / RACE_INJ_{DX,DY,PERIOD,STEPS,LEAD}. Per cycle:
  spawn injector with lead-in, grab+hide, recenter warps, final warp,
  [split], SIGSTOP injector, ungrab+show, measure vs target, classify
  exact/drifted(dot>0)/other. Pre-cycle it visible-warps the sprite to
  window center; REQUIRE the physical cursor inside the window first
  (portal-inject rel-chains are the way to move it).

## Rig (everything under `.analysis/`)

- `repro-client.c` + built binary: modes:
  - normal: cycles grab+hide+recenter warps+release (variants: edge-warp-then-release / no-warp / direct-warp)
  - `REPRO_WARPTEST=1`: visible warp probes
  - `REPRO_BREAK=1`: single capture+inject 3s hold+release
  - `REPRO_GHOST=1`: release→immediate re-grab+hide (the 0004 trigger)
  - `REPRO_HOLD=1`: 3s lock hold
  - `REPRO_SWEEP=1`: large-step injections
  - `REPRO_GEOM=WxH+X+Y`, `REPRO_ANCHOR_X/Y` (window placement; KWin resizes/repositions windows — verify with "inside window rect" line), `REPRO_REDIRECT=1` (for bare nested Xwayland only)
- Motion injection: `xdotool mousemove_relative` (XTEST via Xwayland's `-enable-ei-portal`). REQUIRED the portal MegaAuth pre-approval once: PermissionStore table `kde-authorized`, id `remote-desktop`, app `""` → `["yes"]` (see `setperm.py`). This permission is STILL SET on this machine.
- KWin-side traces via KWin scripts (STILL LOADED in session):
  - `kwin-trace-all.js` (WCFTRACE prefix) — full cursor position stream
  - `kwin-dump.js` (WCFDUMP) — activation/window info
  - Load: `qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript <file> <name>` then `.../Script{N} org.kde.kwin.Script.run`
- X11 request trace: `diagnostics/x11-request-trace` (XRecord; grab/ungrab/warp with server_time + wall us)
- Journal is ground truth: `journalctl -b --since "@<epoch>" -o cat | grep WCFTRACE`
  (live pipes `journalctl -f | grep` silently truncate — caused false failure flags twice).
- Time sync: XRecord pairs wall_us ↔ server_ms (monotonic); KWin t_ms is wall.
- Nested Xwayland (:33/:34) works for protocol logs (WAYLAND_DEBUG) but windows can never be associated to KWin (needs the X11-side WM handshake) → use real :0 for behavior, nested for wire logs only.
- Dev build tree: `.analysis/xwayland-24.1.13/` (0001-0003 + guard + debug envs, build `meson build`, binary `build/hw/xwayland/Xwayland`). It has extra `WCF_*` logging lines not in the package.

## System notes

- No display scaling on this machine (xwaylandScale=1).
- User idle/Afk during most runs; runs 17-19 had user activity (Discord activations) — disregard oddities there.
- KWin trace scripts + portal remote-desktop permission remain enabled. Clean up later at convenience.
- uinput/uaccess: no `input` group, no sudo in agent shell; thus all real-motion came from XTEST/portal or the user.

## Open branches / next suspects

1. ~~Inter-request race in warp→ungrab~~ **CONFIRMED + FIXED by 0005 (v10).**
2. **run5 anomaly**: pointer focus present + motion events flowing to the client +
   no lock engagement, no warp effects. Unexplained. Possible KWin one-shot
   region-check race in `updatePointerConstraints()`, or activeWindow/focus
   mismatch for the game window at that grab.
3. **KWin hint vs wp-warp ordering residual**: even with the candidate correct,
   `set_cursor_position_hint` semantics (committed hint at destroy) vs
   `wp_pointer_warp_v1` at the same position — need WAYLAND_DEBUG wire capture
   on a nested instance at high motion to confirm ordering matches 0003's intent in all sub-cases.
4. **Friend's machine (patch "does nothing")**: not investigated; gather
   KWin version, `verify.sh` output, display scaling/xwaylandScale, whether
   wp_pointer_warp_v1 is advertised, and whether the game runs X11.
5. **Scaled-displays branch**: unlock hint is divided by `surface->scaleOverride`
   in KWin while wp warp coords are not — coordinate systems disagree when
   xwaylandScale != 1. Untested; likely broken releases on scaled setups.

## Next steps (priority)

1. User installs v10 (needs pacman root; Xwayland :0 restart kills the
   running Badlion/Lunar session — coordinate), log out/in or restart
   Xwayland, then verify: `RACE_CYCLES=40 .analysis/race-client` on :0
   (expect ~0 drifted), `RACE_SPLIT_MS=8` (grace now covers the 8ms gap →
   exact too; use `RACE_SPLIT_MS=32` to confirm beyond-grace gaps still
   drift = mechanism still armed), plus the original repro-client normal
   mode (~80 cycles) and the real Badlion feel test.
2. If the residual persists on real 1kHz hardware after v10, dump the dev
   build's WCF_DEBUG invalidation gaps (`xwl33` nested gives protocol logs
   only; the dev binary has `getenv("WCF_DEBUG")` invalidation logging with
   gap_us).
3. Ask friend for: `pacman -Q kwin`, `verify.sh` output, scaling mode
   ([Xwayland] Scale in kwinrc, output scales), game backend (x11 vs wayland).
   ALSO: whether plasma-xdg-desktop-portal-kde was actually running.
4. Commit 0004+0005 + PKGBUILD + docs — DONE in commit `pending` (v9/v10 tags).
