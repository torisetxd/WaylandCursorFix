"use strict";
// KWin script: reports the compositor's real pointer position (where the
// cursor is actually drawn), independent of what any X11 client believes.
// print() output is not surfaced by a nested KWin, so each change is sent as
// a D-Bus call that `dbus-monitor member=cur` records with bus timestamps:
//   org.wcf.Probe.cur("<epoch_ms> <x> <y> <src>")   src: t = 1 ms poll, s = signal
let last = "";
function emit(src) {
    const p = workspace.cursorPos;
    const s = p.x.toFixed(1) + " " + p.y.toFixed(1);
    if (s !== last) {
        last = s;
        callDBus("org.wcf.Probe", "/wcf", "org.wcf.Probe", "cur", Date.now() + " " + s + " " + src);
    }
}
const t = new QTimer();
t.interval = 1;
t.timeout.connect(function () { emit("t"); });
t.start();
workspace.cursorPosChanged.connect(function () { emit("s"); });
callDBus("org.wcf.Probe", "/wcf", "org.wcf.Probe", "cur", Date.now() + " READY");
