/*
 * x11-cursor-trace: read-only RECORD-extension tracer for everything an X11
 * client can use to capture, hide, show or move the pointer, including the
 * XInput2 and XFixes requests that x11-request-trace does not decode (SDL3
 * warps with XIWarpPointer and grabs with XIGrabDevice/XGrabPointer).
 *
 *   core:      GrabPointer, UngrabPointer, ChangeActivePointerGrab,
 *              WarpPointer, ChangeWindowAttributes (cursor attribute only)
 *   XInput2:   XIWarpPointer, XIChangeCursor, XIGrabDevice, XIUngrabDevice
 *   XFixes:    HideCursor, ShowCursor
 *
 * One line per request on stdout, CLOCK_REALTIME microseconds, so it can be
 * lined up with Xwayland / KWin traces. It never sends anything to clients.
 *
 *   gcc -O2 -Wall -o x11-cursor-trace x11-cursor-trace.c -lX11 -lXtst
 */
#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/extensions/record.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define XI_MINOR_WARP 41
#define XI_MINOR_CHANGE_CURSOR 42
#define XI_MINOR_GRAB 51
#define XI_MINOR_UNGRAB 52
#define XFIXES_MINOR_HIDE 29
#define XFIXES_MINOR_SHOW 30
#define CW_CURSOR_BIT 0x4000

static int xi_opcode, xfixes_opcode;

static long long
now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (long long) t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

static uint16_t
u16(const unsigned char *d, Bool sw)
{
    return sw ? (uint16_t) (d[0] << 8 | d[1]) : (uint16_t) (d[1] << 8 | d[0]);
}

static uint32_t
u32(const unsigned char *d, Bool sw)
{
    return sw ? (uint32_t) d[0] << 24 | d[1] << 16 | d[2] << 8 | d[3]
              : (uint32_t) d[3] << 24 | d[2] << 16 | d[1] << 8 | d[0];
}

static double
fp1616(const unsigned char *d, Bool sw)
{
    return (int32_t) u32(d, sw) / 65536.0;
}

#define HEAD(r) printf("CURSORTRACE t_us=%lld client=0x%lx seq=%lu request=" r, \
                       now_us(), rec->id_base, rec->client_seq)

static void
callback(XPointer closure, XRecordInterceptData *rec)
{
    const unsigned char *q = rec->data;
    Bool sw = rec->client_swapped;
    unsigned len = rec->data_len * 4;

    (void) closure;
    if (rec->category != XRecordFromClient || len < 4) {
        XRecordFreeData(rec);
        return;
    }

    if (q[0] == X_GrabPointer && len >= 24) {
        HEAD("GrabPointer");
        printf(" window=0x%x owner=%u confine=0x%x cursor=0x%x\n", u32(q + 4, sw),
               q[1], u32(q + 12, sw), u32(q + 16, sw));
    } else if (q[0] == X_UngrabPointer) {
        HEAD("UngrabPointer");
        printf("\n");
    } else if (q[0] == X_ChangeActivePointerGrab && len >= 16) {
        HEAD("ChangeActivePointerGrab");
        printf(" cursor=0x%x\n", u32(q + 4, sw));
    } else if (q[0] == X_WarpPointer && len >= 24) {
        HEAD("WarpPointer");
        printf(" src=0x%x dst=0x%x dst_x=%d dst_y=%d\n", u32(q + 4, sw),
               u32(q + 8, sw), (int16_t) u16(q + 20, sw), (int16_t) u16(q + 22, sw));
    } else if (q[0] == X_ChangeWindowAttributes && len >= 12) {
        uint32_t mask = u32(q + 8, sw);
        if (mask & CW_CURSOR_BIT) {
            unsigned idx = __builtin_popcount(mask & (CW_CURSOR_BIT - 1));
            if (len >= 12 + 4 * (idx + 1)) {
                HEAD("DefineCursor");
                printf(" window=0x%x cursor=0x%x\n", u32(q + 4, sw),
                       u32(q + 12 + 4 * idx, sw));
            }
        }
    } else if (q[0] == xi_opcode && q[1] == XI_MINOR_WARP && len >= 36) {
        HEAD("XIWarpPointer");
        printf(" src=0x%x dst=0x%x dst_x=%.1f dst_y=%.1f device=%u\n", u32(q + 4, sw),
               u32(q + 8, sw), fp1616(q + 24, sw), fp1616(q + 28, sw), u16(q + 32, sw));
    } else if (q[0] == xi_opcode && q[1] == XI_MINOR_CHANGE_CURSOR && len >= 16) {
        HEAD("XIChangeCursor");
        printf(" window=0x%x cursor=0x%x device=%u\n", u32(q + 4, sw),
               u32(q + 8, sw), u16(q + 12, sw));
    } else if (q[0] == xi_opcode && q[1] == XI_MINOR_GRAB && len >= 24) {
        HEAD("XIGrabDevice");
        printf(" window=0x%x cursor=0x%x device=%u owner=%u\n", u32(q + 4, sw),
               u32(q + 12, sw), u16(q + 16, sw), q[20]);
    } else if (q[0] == xi_opcode && q[1] == XI_MINOR_UNGRAB && len >= 12) {
        HEAD("XIUngrabDevice");
        printf(" device=%u\n", u16(q + 8, sw));
    } else if (q[0] == xfixes_opcode && q[1] == XFIXES_MINOR_HIDE && len >= 8) {
        HEAD("XFixesHideCursor");
        printf(" window=0x%x\n", u32(q + 4, sw));
    } else if (q[0] == xfixes_opcode && q[1] == XFIXES_MINOR_SHOW && len >= 8) {
        HEAD("XFixesShowCursor");
        printf(" window=0x%x\n", u32(q + 4, sw));
    }
    XRecordFreeData(rec);
}

static XRecordRange *
ext_range(int major, int minor_first, int minor_last)
{
    XRecordRange *r = XRecordAllocRange();
    if (!r)
        exit(EXIT_FAILURE);
    r->ext_requests.ext_major.first = r->ext_requests.ext_major.last = major;
    r->ext_requests.ext_minor.first = minor_first;
    r->ext_requests.ext_minor.last = minor_last;
    return r;
}

static XRecordRange *
core_range(int first, int last)
{
    XRecordRange *r = XRecordAllocRange();
    if (!r)
        exit(EXIT_FAILURE);
    r->core_requests.first = first;
    r->core_requests.last = last;
    return r;
}

int
main(void)
{
    Display *ctl = XOpenDisplay(NULL), *data = XOpenDisplay(NULL);
    XRecordClientSpec clients = XRecordAllClients;
    XRecordRange *ranges[8];
    int n = 0, major, minor, ev, err;
    XRecordContext ctx;

    if (!ctl || !data || !XRecordQueryVersion(ctl, &major, &minor)) {
        fprintf(stderr, "cannot open DISPLAY or no RECORD extension\n");
        return EXIT_FAILURE;
    }
    if (!XQueryExtension(ctl, "XInputExtension", &xi_opcode, &ev, &err))
        xi_opcode = -1;
    if (!XQueryExtension(ctl, "XFIXES", &xfixes_opcode, &ev, &err))
        xfixes_opcode = -1;

    ranges[n++] = core_range(X_GrabPointer, X_UngrabPointer);
    ranges[n++] = core_range(X_ChangeActivePointerGrab, X_ChangeActivePointerGrab);
    ranges[n++] = core_range(X_WarpPointer, X_WarpPointer);
    ranges[n++] = core_range(X_ChangeWindowAttributes, X_ChangeWindowAttributes);
    if (xi_opcode > 0) {
        ranges[n++] = ext_range(xi_opcode, XI_MINOR_WARP, XI_MINOR_CHANGE_CURSOR);
        ranges[n++] = ext_range(xi_opcode, XI_MINOR_GRAB, XI_MINOR_UNGRAB);
    }
    if (xfixes_opcode > 0)
        ranges[n++] = ext_range(xfixes_opcode, XFIXES_MINOR_HIDE, XFIXES_MINOR_SHOW);

    ctx = XRecordCreateContext(ctl, 0, &clients, 1, ranges, n);
    if (!ctx) {
        fprintf(stderr, "cannot create RECORD context\n");
        return EXIT_FAILURE;
    }
    XSync(ctl, False);
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("CURSORTRACE t_us=%lld reason=start xi=%d xfixes=%d\n", now_us(), xi_opcode,
           xfixes_opcode);
    if (!XRecordEnableContext(data, ctx, callback, NULL))
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
