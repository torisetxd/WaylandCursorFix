/*
 * sim-sdl: stand-in for an SDL3 game on the X11 backend (CS2 on Xwayland).
 *
 * Uses the real SDL3 X11 driver, so the X11 grab / cursor-hide / warp
 * requests are exactly what SDL emits, not an imitation:
 *   - capture:  SDL_SetWindowRelativeMouseMode(true) (+ optional grab)
 *   - release:  SDL_SetWindowRelativeMouseMode(false), then optionally the
 *               game's own SDL_WarpMouseInWindow(center) like a menu opening
 * Frames are real OpenGL swaps at vsync with optional GPU load.
 *
 * Output (stdout, CLOCK_REALTIME microseconds), same format as sim-lwjgl:
 *   SIM win ox=.. oy=.. w=.. h=..
 *   SIM grab cycle=N t_us=..
 *   SIM release cycle=N t_us=.. tx=.. ty=..   (GLOBAL expected cursor position)
 *   SIM after cycle=N ...
 *   SIM done
 */
#define _GNU_SOURCE
#include <GL/gl.h>
#include <SDL3/SDL.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int W = 1280, H = 720, OX, OY;
static int fifo = -1;
static int cycles = 8, grab_ms = 700, menu_ms = 700;
static int speed = 14, hz = 1000, jitter_us = 150;
static int still_release = 0, post_ms = 0;
static int warp_center = 1;     /* game warps to window center on release (CS2 menus) */
static int restore_prev = 0;    /* expected position is the pre-capture one (SDL's own restore) */
static int start_x = 250, start_y = 180;
static int gpu_quads = 0;
static int grab_too = 0;        /* also SDL_SetWindowMouseGrab */
static int outside = 0;         /* capture while the pointer is off the window (other monitor), then bring it in */

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

static SDL_Window *win;
static int frame_no;
static long ev_count;            /* relative motion events the game received */
static double ev_sx, ev_sy;      /* ...and their summed deltas */

static void frame(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        /* a real game reads motion deltas here */
        if (ev.type == SDL_EVENT_MOUSE_MOTION) {
            ev_count++;
            ev_sx += ev.motion.xrel;
            ev_sy += ev.motion.yrel;
        }
    }
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
    SDL_GL_SwapWindow(win);
    frame_no++;
}

static void run_until(long long until)
{
    while (mono_us() < until)
        frame();
}

static void camera_motion(long long until)
{
    long long next = 0;
    int k = 0;
    while (mono_us() < until) {
        if (mono_us() >= next) {
            double a = (k++ * 0.9);
            int dx = (int) lround(speed * cos(a)), dy = (int) lround(speed * sin(a) * 0.6);
            if (dx == 0 && dy == 0)
                dx = 1;
            mouse("stream %d %d %d %d", dx, dy, hz, jitter_us);
            next = mono_us() + 90000;
        }
        frame();
    }
}

int main(int argc, char **argv)
{
    static const struct option opts[] = {
        { "cycles", 1, 0, 'c' }, { "grab-ms", 1, 0, 'g' }, { "menu-ms", 1, 0, 'm' },
        { "speed", 1, 0, 's' }, { "hz", 1, 0, 'z' }, { "jitter-us", 1, 0, 'j' },
        { "still-release", 1, 0, 'r' }, { "post-ms", 1, 0, 'p' },
        { "mouse-fifo", 1, 0, 'F' }, { "size", 1, 0, 'S' }, { "start", 1, 0, 'a' },
        { "warp-center", 1, 0, 'w' }, { "restore-prev", 1, 0, 'P' },
        { "gpu-quads", 1, 0, 'Q' }, { "grab", 1, 0, 'G' }, { "outside", 1, 0, 'O' }, { 0 }
    };
    int o;

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
        case 'F': fifo = open(optarg, O_WRONLY); break;
        case 'S': sscanf(optarg, "%dx%d", &W, &H); break;
        case 'a': sscanf(optarg, "%d,%d", &start_x, &start_y); break;
        case 'w': warp_center = atoi(optarg); break;
        case 'P': restore_prev = atoi(optarg); break;
        case 'Q': gpu_quads = atoi(optarg); break;
        case 'G': grab_too = atoi(optarg); break;
        case 'O': outside = atoi(optarg); break;
        default: return 2;
        }
    }

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    win = SDL_CreateWindow("sim-sdl", W, H, SDL_WINDOW_OPENGL);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowPosition(win, 300, 150);
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    SDL_GL_SetSwapInterval(1);
    SDL_ShowWindow(win);
    SDL_RaiseWindow(win);
    fprintf(stderr, "video driver: %s\n", SDL_GetCurrentVideoDriver());

    run_until(mono_us() + 1500000);
    {
        int x, y;
        SDL_GetWindowPosition(win, &x, &y);
        /* SDL reports the frame origin; the client area origin equals it for X11
           clients without borders handled by the compositor, so rely on the
           first absolute injection + pointer query below instead. */
        OX = x;
        OY = y;
    }
    /* Find the client-area origin precisely: put the pointer at a known global
       position and read SDL's window-relative coordinates. */
    mouse("abs 700 400");
    run_until(mono_us() + 300000);
    {
        float mx, my;
        SDL_GetMouseState(&mx, &my);
        OX = 700 - (int) lroundf(mx);
        OY = 400 - (int) lroundf(my);
    }
    say("win ox=%d oy=%d w=%d h=%d", OX, OY, W, H);

    for (int i = 0; i < cycles; i++) {
        mouse("stop");
        if (outside)
            mouse("abs 60 60");     /* nowhere near the game window */
        else
            mouse("abs %d %d", OX + start_x, OY + start_y);
        run_until(mono_us() + 300000);

        if (grab_too)
            SDL_SetWindowMouseGrab(win, true);
        SDL_SetWindowRelativeMouseMode(win, true);
        say("grab cycle=%d t_us=%lld", i, rt_us());
        if (outside) {
            /* the game asked for capture, the pointer is still elsewhere; the
               user then brings it into the window */
            run_until(mono_us() + 300000);
            mouse("abs %d %d", OX + W / 2, OY + H / 2);
            run_until(mono_us() + 200000);
            say("entered cycle=%d t_us=%lld", i, rt_us());
            ev_count = 0;
            ev_sx = ev_sy = 0;
        }
        camera_motion(mono_us() + (long long) grab_ms * 1000);
        if (outside)
            say("capture cycle=%d events=%ld sumx=%.0f sumy=%.0f", i, ev_count, ev_sx, ev_sy);

        if (still_release) {
            mouse("stop");
            run_until(mono_us() + 40000);
        }
        long long t = rt_us();
        SDL_SetWindowRelativeMouseMode(win, false);
        if (grab_too)
            SDL_SetWindowMouseGrab(win, false);
        int tx, ty;
        if (warp_center) {
            tx = W / 2;
            ty = H / 2;
            SDL_WarpMouseInWindow(win, tx, ty);
        } else {
            /* nothing explicit: SDL's own behaviour decides (restores the pre-capture position) */
            tx = start_x;
            ty = start_y;
        }
        (void) restore_prev;
        say("release cycle=%d t_us=%lld tx=%d ty=%d warp=%d", i, t, OX + tx, OY + ty, warp_center);

        if (post_ms) {
            run_until(mono_us() + (long long) post_ms * 1000);
            mouse("stop");
        } else if (!still_release) {
            mouse("stop");
        }
        run_until(mono_us() + (long long) menu_ms * 1000);
        {
            float mx, my;
            SDL_GetMouseState(&mx, &my);
            say("after cycle=%d t_us=%lld qx=%d qy=%d", i, rt_us(), OX + (int) mx, OY + (int) my);
        }
    }
    say("done");
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
