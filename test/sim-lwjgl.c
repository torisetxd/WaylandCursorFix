/*
 * sim-lwjgl: stand-in for a LWJGL 2 game (Minecraft 1.8.9 / Badlion) on X11.
 *
 * Reproduces the exact X11 request pattern recorded from the real game
 * (investigation/xrecord-badlion-session.log):
 *   - GrabPointer(window, confineTo=window, cursor=None) + XFixesHideCursor
 *   - while captured, every frame: for each MotionNotify, WarpPointer(center)
 *     (bursts of identical hidden warps in one millisecond)
 *   - release (inventory/menu opens): WarpPointer(center) immediately followed by
 *     UngrabPointer + XFixesShowCursor in the same flush
 * and renders real GLX frames at vsync so the Xwayland surface keeps
 * committing GPU buffers (this is what makes compositor-side state latching
 * behave like it does for a real game).
 *
 * It also plays the part of the user: it drives wcf-mouse through a FIFO
 * (camera motion while captured, optional motion at/after release).
 *
 * Output lines on stdout (machine readable, CLOCK_REALTIME microseconds):
 *   SIM win ox=.. oy=.. w=.. h=..
 *   SIM grab cycle=N t_us=..
 *   SIM release cycle=N t_us=.. tx=.. ty=..   (tx,ty: GLOBAL target of final warp)
 *   SIM after cycle=N t_us=.. qx=.. qy=..     (X11 XQueryPointer a while after)
 *   SIM done
 */
#define _GNU_SOURCE
#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <getopt.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

static Display *dpy;
static Window win, root;
static int W = 1280, H = 720, OX, OY;
static int fifo = -1;

/* options */
static int cycles = 12;
static int grab_ms = 700;
static int menu_ms = 700;
static int speed = 14;          /* px per 1 kHz mouse event while capturing */
static int hz = 1000;
static int jitter_us = 150;
static int still_release = 0;   /* stop the mouse before releasing */
static int post_ms = 0;         /* keep moving this long after release */
static int hide_fixes = 1;
static int confine_own = 1;
static int final_warp = 1;      /* send the explicit warp before ungrab */
static int recenter = 1;        /* per-event hidden warps while captured */
static int vsync = 1;
static int menu_visible_warp = 0;   /* warp (visible) inside the menu, like a game placing the cursor */
static int start_x = -1, start_y = -1; /* where the physical cursor is when capture starts (window-local) */
static int odd_center = 1;      /* alternate center y by +-1 like the real trace */
static int work_us = 0;         /* simulated per-frame CPU work */
static int gpu_quads = 0;       /* full-window blended quads per frame: GPU load so buffer fences are not ready at commit */

static long long rt_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static long long mono_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void mouse(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    int n;

    if (fifo < 0)
        return;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    buf[n++] = '\n';
    if (write(fifo, buf, n) < 0)
        perror("mouse fifo");
}

static void say(const char *fmt, ...)
{
    va_list ap;

    printf("SIM ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static GLXContext ctx;
static int frame_no;

static void render(void)
{
    float c = 0.5f + 0.5f * sinf(frame_no * 0.05f);
    glClearColor(0.1f, 0.2f + 0.3f * c, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0); glVertex2f(-0.5f + 0.2f * c, -0.5f);
    glColor3f(0, 1, 0); glVertex2f(0.5f, -0.5f);
    glColor3f(0, 0, 1); glVertex2f(0.0f, 0.5f);
    glEnd();
    if (gpu_quads) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(c, 0.5f, 1.0f - c, 0.03f);
        for (int q = 0; q < gpu_quads; q++) {
            glBegin(GL_QUADS);
            glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1);
            glEnd();
        }
        glDisable(GL_BLEND);
    }
    glXSwapBuffers(dpy, win);
    frame_no++;
    if (work_us) {
        long long t = mono_us() + work_us;
        while (mono_us() < t) { }
    }
}

static int cx(void) { return W / 2; }
static int cy(int cycle) { return H / 2 - (odd_center ? (cycle & 1) : 0); }

/* One simulated game frame: handle events (and recenter warps), render. */
static int capturing;
static int cur_cycle;
static long long motion_events;

static void game_frame(void)
{
    while (XPending(dpy)) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == MotionNotify && capturing) {
            motion_events++;
            if (recenter && (ev.xmotion.x != cx() || ev.xmotion.y != cy(cur_cycle)))
                XWarpPointer(dpy, None, win, 0, 0, 0, 0, cx(), cy(cur_cycle));
        }
    }
    XFlush(dpy);
    render();
}

static void run_frames_until(long long until_mono_us)
{
    while (mono_us() < until_mono_us)
        game_frame();
}

static void camera_motion(long long until_mono_us)
{
    /* the "user" sweeps the mouse around like a camera turn, changing direction */
    long long next = 0;
    int k = 0;
    while (mono_us() < until_mono_us) {
        if (mono_us() >= next) {
            double a = (k++ * 0.9) + (rand() % 100) * 0.01;
            int dx = (int) lround(speed * cos(a)), dy = (int) lround(speed * sin(a) * 0.6);
            if (dx == 0 && dy == 0)
                dx = 1;
            mouse("stream %d %d %d %d", dx, dy, hz, jitter_us);
            next = mono_us() + 90000;
        }
        game_frame();
    }
}

static void grab_capture(int cycle)
{
    Window confine = confine_own ? win : None;
    int r;

    cur_cycle = cycle;
    r = XGrabPointer(dpy, win, False,
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                     GrabModeAsync, GrabModeAsync, confine, None, CurrentTime);
    if (hide_fixes)
        XFixesHideCursor(dpy, win);
    else {
        static Cursor blank;
        if (!blank) {
            char data[1] = { 0 };
            XColor black = { 0 };
            Pixmap pm = XCreateBitmapFromData(dpy, win, data, 1, 1);
            blank = XCreatePixmapCursor(dpy, pm, pm, &black, &black, 0, 0);
            XFreePixmap(dpy, pm);
        }
        XDefineCursor(dpy, win, blank);
    }
    XWarpPointer(dpy, None, win, 0, 0, 0, 0, cx(), cy(cycle));
    XFlush(dpy);
    capturing = 1;
    say("grab cycle=%d t_us=%lld r=%d", cycle, rt_us(), r);
}

static void release_capture(int cycle)
{
    int tx = cx(), ty = cy(cycle);
    long long t;

    capturing = 0;
    t = rt_us();
    if (final_warp)
        XWarpPointer(dpy, None, win, 0, 0, 0, 0, tx, ty);
    XUngrabPointer(dpy, CurrentTime);
    if (hide_fixes)
        XFixesShowCursor(dpy, win);
    else
        XUndefineCursor(dpy, win);
    XFlush(dpy);
    say("release cycle=%d t_us=%lld tx=%d ty=%d final_warp=%d", cycle, t,
        OX + tx, OY + ty, final_warp);
}

int main(int argc, char **argv)
{
    static const struct option opts[] = {
        { "cycles", 1, 0, 'c' }, { "grab-ms", 1, 0, 'g' }, { "menu-ms", 1, 0, 'm' },
        { "speed", 1, 0, 's' }, { "hz", 1, 0, 'z' }, { "jitter-us", 1, 0, 'j' },
        { "still-release", 1, 0, 'r' }, { "post-ms", 1, 0, 'p' },
        { "hide", 1, 0, 'H' }, { "confine", 1, 0, 'C' }, { "final-warp", 1, 0, 'f' },
        { "recenter", 1, 0, 'R' }, { "vsync", 1, 0, 'v' }, { "mouse-fifo", 1, 0, 'F' },
        { "size", 1, 0, 'S' }, { "menu-warp", 1, 0, 'w' }, { "start", 1, 0, 'a' },
        { "work-us", 1, 0, 'W' }, { "gpu-quads", 1, 0, 'Q' }, { "seed", 1, 0, 'x' }, { 0 }
    };
    int o, seed = 1;

    while ((o = getopt_long(argc, argv, "", opts, NULL)) != -1) {
        switch (o) {
        case 'c': cycles = atoi(optarg); break;
        case 'g': grab_ms = atoi(optarg); break;
        case 'm': menu_ms = atoi(optarg); break;
        case 's': speed = atoi(optarg); break;
        case 'z': hz = atoi(optarg); break;
        case 'j': jitter_us = atoi(optarg); break;
        case 'r': still_release = atoi(optarg); break;
        case 'p': post_ms = atoi(optarg); break;
        case 'H': hide_fixes = !strcmp(optarg, "fixes"); break;
        case 'C': confine_own = !strcmp(optarg, "own"); break;
        case 'f': final_warp = atoi(optarg); break;
        case 'R': recenter = atoi(optarg); break;
        case 'v': vsync = atoi(optarg); break;
        case 'F': fifo = open(optarg, O_WRONLY); break;
        case 'S': sscanf(optarg, "%dx%d", &W, &H); break;
        case 'w': menu_visible_warp = atoi(optarg); break;
        case 'a': sscanf(optarg, "%d,%d", &start_x, &start_y); break;
        case 'W': work_us = atoi(optarg); break;
        case 'Q': gpu_quads = atoi(optarg); break;
        case 'x': seed = atoi(optarg); break;
        default: return 2;
        }
    }
    srand(seed);

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "cannot open display\n");
        return 1;
    }
    root = DefaultRootWindow(dpy);

    int attrs[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
                    GLX_BLUE_SIZE, 8, GLX_DEPTH_SIZE, 16, None };
    XVisualInfo *vi = glXChooseVisual(dpy, DefaultScreen(dpy), attrs);
    if (!vi) {
        fprintf(stderr, "no GLX visual\n");
        return 1;
    }
    XSetWindowAttributes swa = { 0 };
    swa.colormap = XCreateColormap(dpy, root, vi->visual, AllocNone);
    swa.event_mask = StructureNotifyMask | PointerMotionMask | ButtonPressMask |
                     ButtonReleaseMask | EnterWindowMask | LeaveWindowMask | ExposureMask;
    win = XCreateWindow(dpy, root, 300, 150, W, H, 0, vi->depth, InputOutput, vi->visual,
                        CWColormap | CWEventMask, &swa);
    XStoreName(dpy, win, "sim-lwjgl");
    XSizeHints hints = { .flags = USPosition | PPosition | USSize | PSize,
                         .x = 300, .y = 150, .width = W, .height = H };
    XSetWMNormalHints(dpy, win, &hints);
    ctx = glXCreateContext(dpy, vi, NULL, True);
    glXMakeCurrent(dpy, win, ctx);
    if (vsync) {
        typedef void (*swapint_t)(Display *, GLXDrawable, int);
        swapint_t f = (swapint_t) glXGetProcAddressARB((const GLubyte *) "glXSwapIntervalEXT");
        if (f)
            f(dpy, win, 1);
    }
    XMapWindow(dpy, win);
    XFlush(dpy);

    /* wait for the window to be mapped, keep rendering */
    long long t0 = mono_us();
    while (mono_us() - t0 < 1200000)
        game_frame();

    XClientMessageEvent cm = { .type = ClientMessage, .window = win, .format = 32,
                               .message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False) };
    cm.data.l[0] = 2;
    XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, (XEvent *) &cm);
    XFlush(dpy);
    run_frames_until(mono_us() + 300000);

    {
        Window child;
        XTranslateCoordinates(dpy, win, root, 0, 0, &OX, &OY, &child);
        say("win ox=%d oy=%d w=%d h=%d", OX, OY, W, H);
    }

    for (int i = 0; i < cycles; i++) {
        int sx = start_x >= 0 ? start_x : cx();
        int sy = start_y >= 0 ? start_y : cy(i);

        /* user rests the mouse inside the window; compositor gives pointer focus */
        mouse("stop");
        mouse("abs %d %d", OX + sx, OY + sy);
        run_frames_until(mono_us() + 250000);

        grab_capture(i);
        camera_motion(mono_us() + (long long) grab_ms * 1000);

        if (still_release) {
            mouse("stop");
            run_frames_until(mono_us() + 40000);
        }
        release_capture(i);

        if (post_ms) {
            run_frames_until(mono_us() + (long long) post_ms * 1000);
            mouse("stop");
        } else if (!still_release) {
            mouse("stop");
        }

        if (menu_visible_warp) {
            run_frames_until(mono_us() + 200000);
            XWarpPointer(dpy, None, win, 0, 0, 0, 0, W / 4, H / 4);
            XFlush(dpy);
            say("menuwarp cycle=%d t_us=%lld tx=%d ty=%d", i, rt_us(), OX + W / 4, OY + H / 4);
        }
        run_frames_until(mono_us() + (long long) menu_ms * 1000);

        {
            Window rr, cc;
            int rx, ry, wx, wy;
            unsigned int mask;
            XQueryPointer(dpy, root, &rr, &cc, &rx, &ry, &wx, &wy, &mask);
            say("after cycle=%d t_us=%lld qx=%d qy=%d", i, rt_us(), rx, ry);
        }
    }
    fprintf(stderr, "frames=%d avg_ms=%.2f\n", frame_no,
            (mono_us() - t0) / 1000.0 / (frame_no ? frame_no : 1));
    say("done");
    glXMakeCurrent(dpy, None, NULL);
    XCloseDisplay(dpy);
    return 0;
}
