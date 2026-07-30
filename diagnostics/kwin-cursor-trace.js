"use strict";

const prefix = "WAYLANDCURSORFIX_KWIN";
let previous = workspace.cursorPos;
let previousScreen = "";

function screenName(point) {
    const output = workspace.screenAt(point);
    return output ? output.name : "<none>";
}

function logPosition(reason, point, dx, dy, output) {
    print(prefix + " t_ms=" + Date.now()
        + " reason=" + reason
        + " x=" + point.x
        + " y=" + point.y
        + " dx=" + dx
        + " dy=" + dy
        + " output=" + output);
}

previousScreen = screenName(previous);
logPosition("start", previous, 0, 0, previousScreen);

workspace.cursorPosChanged.connect(function () {
    const current = workspace.cursorPos;
    const currentScreen = screenName(current);
    const dx = current.x - previous.x;
    const dy = current.y - previous.y;
    const screenChanged = currentScreen !== previousScreen;

    /*
     * Normal physical motion is intentionally omitted. Pointer warps and
     * cross-output transitions are the events relevant to this failure.
     */
    if (screenChanged || Math.abs(dx) >= 32 || Math.abs(dy) >= 32)
        logPosition(screenChanged ? "output-change" : "jump",
                    current, dx, dy, currentScreen);

    previous = current;
    previousScreen = currentScreen;
});
