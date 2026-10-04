#!/usr/bin/env bash
# Session driver, run by KWin inside the nested session (see nest.sh).
#   WCF_SIM        simulator binary (default: sim-lwjgl)
#   WCF_SIM_ARGS   its arguments
#   WCF_GLXVENDOR  set to "mesa" by default so Xwayland GLX uses the iGPU
set -u
T=$WCF_TEST
O=$WCF_OUT
SIM=${WCF_SIM:-$T/sim-lwjgl}
export __GLX_VENDOR_LIBRARY_NAME=${WCF_GLXVENDOR:-mesa}

sleep 1.5
dbus-monitor --session "member=cur" > "$O/probe.raw" 2>&1 &
MON=$!
sleep 0.4
ID=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$T/cursor-probe.js" wcf-probe)
qdbus6 org.kde.KWin "/Scripting/Script$ID" org.kde.kwin.Script.run
sleep 0.4

mkfifo "$O/mouse.in"
# keep a read-write descriptor open so the mouse never sees EOF between writers
exec 9<> "$O/mouse.in"
WCF_MOUSE_LOG="$O/mouse.log" "$T/wcf-mouse" < "$O/mouse.in" > "$O/mouse.out" 2> "$O/mouse.err" &
MOUSE=$!
for i in $(seq 1 60); do grep -q READY "$O/mouse.out" 2>/dev/null && break; sleep 0.1; done
grep -q READY "$O/mouse.out" || { echo "mouse never became ready" >&2; exit 3; }

# shellcheck disable=SC2086
"$SIM" --mouse-fifo "$O/mouse.in" ${WCF_SIM_ARGS:-} > "$O/sim.out" 2> "$O/sim.err"
echo "sim exit=$?" >> "$O/sim.err"

echo "stop" >&9; echo "quit" >&9
sleep 0.3
kill $MON 2>/dev/null
wait $MOUSE 2>/dev/null
exec 9>&-
exit 0
