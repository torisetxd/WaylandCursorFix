"use strict";

const prefix = "WAYLANDCURSORFIX_KWIN";
let initial = workspace.cursorPos;
let previousX = initial.x;
let previousY = initial.y;
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

previousScreen = screenName(initial);
logPosition("start", initial, 0, 0, previousScreen);

workspace.cursorPosChanged.connect(function () {
    const current = workspace.cursorPos;
    const currentScreen = screenName(current);
    const dx = current.x - previousX;
    const dy = current.y - previousY;
    const screenChanged = currentScreen !== previousScreen;

    /*
     * Keep enough fast physical motion to identify amplification without
     * logging every ordinary single-pixel update.
     */
    if (screenChanged || Math.abs(dx) + Math.abs(dy) >= 12)
        logPosition(screenChanged ? "output-change" : "jump",
                    current, dx, dy, currentScreen);

    previousX = current.x;
    previousY = current.y;
    previousScreen = currentScreen;
});
