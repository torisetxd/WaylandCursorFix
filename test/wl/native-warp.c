/*
 * native-warp: native Wayland client that asks the compositor to warp the
 * pointer with wp_pointer_warp_v1. Used to tell "compositor ignores warps"
 * apart from "compositor ignores warps for Xwayland windows".
 * Prints "NATIVE enter serial=.." and "NATIVE motion x y" so the harness can
 * see whether the warp request took effect.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "pointer-warp-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

static struct wl_compositor *comp;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct xdg_wm_base *wm;
static struct wp_pointer_warp_v1 *warp;
static struct wl_pointer *pointer;
static struct wl_surface *surface;
static uint32_t enter_serial;
static int configured, entered;

static long long rt_us(void) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); return t.tv_sec * 1000000LL + t.tv_nsec / 1000; }

static void p_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{ enter_serial = serial; entered = 1; printf("NATIVE enter serial=%u x=%.0f y=%.0f t_us=%lld\n", serial, wl_fixed_to_double(x), wl_fixed_to_double(y), rt_us()); fflush(stdout); }
static void p_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s) { entered = 0; }
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{ printf("NATIVE motion x=%.0f y=%.0f t_us=%lld\n", wl_fixed_to_double(x), wl_fixed_to_double(y), rt_us()); fflush(stdout); }
static void p_button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t, uint32_t b, uint32_t st) {}
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) {}
static void p_frame(void *d, struct wl_pointer *p) {}
static void p_axis_source(void *d, struct wl_pointer *p, uint32_t s) {}
static void p_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) {}
static void p_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) {}
static const struct wl_pointer_listener pl = { p_enter, p_leave, p_motion, p_button, p_axis,
    p_frame, p_axis_source, p_axis_stop, p_axis_discrete };

static void s_caps(void *d, struct wl_seat *s, uint32_t caps)
{ if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) { pointer = wl_seat_get_pointer(s); wl_pointer_add_listener(pointer, &pl, NULL); } }
static void s_name(void *d, struct wl_seat *s, const char *n) {}
static const struct wl_seat_listener sl = { s_caps, s_name };

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
    if (!strcmp(iface, "wl_compositor")) comp = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, "wl_shm")) shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, "wl_seat")) { seat = wl_registry_bind(r, name, &wl_seat_interface, 5); wl_seat_add_listener(seat, &sl, NULL); }
    else if (!strcmp(iface, "xdg_wm_base")) wm = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
    else if (!strcmp(iface, "wp_pointer_warp_v1")) warp = wl_registry_bind(r, name, &wp_pointer_warp_v1_interface, 1);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener rl = { reg_global, reg_remove };

static void wm_ping(void *d, struct xdg_wm_base *b, uint32_t s) { xdg_wm_base_pong(b, s); }
static const struct xdg_wm_base_listener wml = { wm_ping };
static void xs_conf(void *d, struct xdg_surface *x, uint32_t s) { xdg_surface_ack_configure(x, s); configured = 1; }
static const struct xdg_surface_listener xsl = { xs_conf };
static void tl_conf(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *s) {}
static void tl_close(void *d, struct xdg_toplevel *t) { exit(0); }
static const struct xdg_toplevel_listener tll = { tl_conf, tl_close };

int main(int argc, char **argv)
{
    int W = 400, H = 300;
    double tx = argc > 1 ? atof(argv[1]) : 100, ty = argc > 2 ? atof(argv[2]) : 100;
    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy) { fprintf(stderr, "no wayland display\n"); return 1; }
    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &rl, NULL);
    wl_display_roundtrip(dpy); wl_display_roundtrip(dpy);
    if (!warp) { printf("NATIVE no wp_pointer_warp_v1\n"); return 1; }
    xdg_wm_base_add_listener(wm, &wml, NULL);
    surface = wl_compositor_create_surface(comp);
    struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(wm, surface);
    xdg_surface_add_listener(xs, &xsl, NULL);
    struct xdg_toplevel *tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tll, NULL);
    xdg_toplevel_set_title(tl, "native-warp");
    wl_surface_commit(surface);
    while (!configured) wl_display_dispatch(dpy);
    int stride = W * 4, size = stride * H;
    int fd = memfd_create("buf", 0); ftruncate(fd, size);
    uint32_t *px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int i = 0; i < W * H; i++) px[i] = 0xff3060a0;
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_XRGB8888);
    wl_surface_attach(surface, buf, 0, 0); wl_surface_commit(surface);
    printf("NATIVE mapped\n"); fflush(stdout);
    /* wait to be told the pointer is over us */
    long long t0 = rt_us();
    while (!entered && rt_us() - t0 < 8000000) { wl_display_flush(dpy); if (wl_display_dispatch_pending(dpy) <= 0) { if (wl_display_prepare_read(dpy) == 0) { struct timespec ts = {0, 20000000}; nanosleep(&ts, NULL); wl_display_read_events(dpy); wl_display_dispatch_pending(dpy);} } }
    if (!entered) { printf("NATIVE never entered\n"); return 1; }
    printf("NATIVE warping to %.0f,%.0f serial=%u t_us=%lld\n", tx, ty, enter_serial, rt_us()); fflush(stdout);
    wp_pointer_warp_v1_warp_pointer(warp, surface, pointer, wl_fixed_from_double(tx), wl_fixed_from_double(ty), enter_serial);
    wl_display_flush(dpy);
    t0 = rt_us();
    while (rt_us() - t0 < 1000000) { wl_display_flush(dpy); if (wl_display_prepare_read(dpy) == 0) { struct timespec ts = {0, 10000000}; nanosleep(&ts, NULL); wl_display_read_events(dpy); } wl_display_dispatch_pending(dpy); }
    printf("NATIVE done\n");
    return 0;
}
