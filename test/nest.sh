#!/usr/bin/env bash
# Run a command inside an isolated nested KWin (virtual output) + Xwayland.
#
#   test/nest.sh [-x XWAYLAND_BINARY] [-o OUTDIR] [-s WxH] [-t SECONDS] -- DRIVER
#
# Nothing here touches the running desktop session: private D-Bus, private
# XDG config/data dirs, its own Wayland socket. The Xwayland binary under
# test is found through PATH, which is how KWin launches it, so a freshly
# built (uninstalled) binary can be exercised directly.
#
# DRIVER runs as KWin's session application; KWin exits when it does. The
# driver sees: DISPLAY, WAYLAND_DISPLAY, DBUS_SESSION_BUS_ADDRESS, WCF_OUT
# (output dir), WCF_TEST (this test/ dir). KWin's stderr (which carries the
# QML cursor-probe log) goes to OUTDIR/kwin.log; Xwayland's to OUTDIR/xwayland.log
# when WAYLAND_DEBUG-style variables are set by the caller.
set -euo pipefail

test_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
xwayland=/usr/bin/Xwayland
outdir=""
size=1920x1080
limit=180

while getopts 'x:o:s:t:' opt; do
  case $opt in
    x) xwayland=$(readlink -f "$OPTARG") ;;
    o) outdir=$OPTARG ;;
    s) size=$OPTARG ;;
    t) limit=$OPTARG ;;
    *) exit 2 ;;
  esac
done
shift $((OPTIND - 1))
[[ ${1:-} == -- ]] && shift
[[ $# -eq 1 ]] || { echo "usage: nest.sh [opts] -- DRIVER   (pass parameters via environment)" >&2; exit 2; }

[[ -x $xwayland ]] || { echo "no such Xwayland: $xwayland" >&2; exit 2; }
outdir=${outdir:-$(mktemp -d "${TMPDIR:-/tmp}/wcf-nest.XXXXXX")}
mkdir -p "$outdir"/{cfg,data,cache,state,bin}
outdir=$(readlink -f "$outdir")
ln -sf "$xwayland" "$outdir/bin/Xwayland"

cat > "$outdir/cfg/kwinrc" <<EOF
[Compositing]
Backend=OpenGL
[Plugins]
blurEnabled=false
contrastEnabled=false
slidingpopupsEnabled=false
[Xwayland]
Scale=1
[Windows]
Placement=Centered
EOF

socket="wcf-$$"
export WCF_OUT=$outdir WCF_TEST=$test_dir WCF_XWAYLAND=$xwayland

# Everything below runs in a clean environment so it cannot latch onto the
# real session (WAYLAND_DISPLAY / DISPLAY of the desktop).
env -u WAYLAND_DISPLAY -u DISPLAY -u WAYLAND_SOCKET -u DBUS_SESSION_BUS_ADDRESS \
  -u KDE_FULL_SESSION -u XDG_SESSION_ID \
  PATH="$outdir/bin:$PATH" \
  XDG_CONFIG_HOME="$outdir/cfg" XDG_DATA_HOME="$outdir/data" \
  XDG_CACHE_HOME="$outdir/cache" XDG_STATE_HOME="$outdir/state" \
  XDG_CURRENT_DESKTOP=KDE \
  QT_LOGGING_RULES="kwin_scripting.debug=true;js.debug=true;qml.debug=true;kwin_scripting.info=true" \
  KWIN_WAYLAND_NO_PERMISSION_CHECKS=1 \
  WCF_DEBUG="$outdir/xwl.log" \
  timeout --kill-after=5 "$limit" \
  dbus-run-session -- \
  kwin_wayland --virtual --xwayland --socket "$socket" --no-lockscreen --no-global-shortcuts \
    --width "${size%x*}" --height "${size#*x}" \
    --exit-with-session "$1" \
  > "$outdir/kwin.log" 2>&1 < /dev/null
rc=$?
echo "nest: session exited rc=$rc outdir=$outdir" >&2
exit $rc
