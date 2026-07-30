# Preserved revisions and bug split

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
   acceleration problem.

   This is separate from the visible-warp delivery/order bug. Revisions 5 and
   6 attempted to solve the resulting release offset with a persistent lock
   and stable anchor, but did not address the amplified motion and regressed
   behavior. They must not be treated as a valid baseline.

## Reproducible artifacts

- Package: `xorg-xwayland-visible-warp-24.1.13-7-x86_64.pkg.tar.zst`
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
