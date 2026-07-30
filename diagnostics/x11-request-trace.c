#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/extensions/record.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int64_t
realtime_microseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        return -1;

    return (int64_t) now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

static uint16_t
read_u16(const unsigned char *data, Bool swapped)
{
    if (swapped)
        return (uint16_t) data[0] << 8 | data[1];
    return (uint16_t) data[1] << 8 | data[0];
}

static int16_t
read_i16(const unsigned char *data, Bool swapped)
{
    return (int16_t) read_u16(data, swapped);
}

static uint32_t
read_u32(const unsigned char *data, Bool swapped)
{
    if (swapped) {
        return (uint32_t) data[0] << 24 |
               (uint32_t) data[1] << 16 |
               (uint32_t) data[2] << 8 |
               data[3];
    }

    return (uint32_t) data[3] << 24 |
           (uint32_t) data[2] << 16 |
           (uint32_t) data[1] << 8 |
           data[0];
}

static void
record_callback(XPointer closure, XRecordInterceptData *recorded)
{
    const unsigned char *request = recorded->data;
    unsigned int request_type;

    (void) closure;

    if (recorded->category != XRecordFromClient ||
        recorded->data_len == 0) {
        XRecordFreeData(recorded);
        return;
    }

    request_type = request[0];
    if (request_type == X_GrabPointer &&
        recorded->data_len * 4 >= sz_xGrabPointerReq) {
        printf("WAYLANDCURSORFIX_REQ t_us=%lld server_ms=%lu client=0x%lx seq=%lu"
               " request=GrabPointer window=0x%x owner=%u event_mask=0x%x"
               " pointer_mode=%u keyboard_mode=%u confine=0x%x cursor=0x%x\n",
               (long long) realtime_microseconds(),
               (unsigned long) recorded->server_time,
               recorded->id_base,
               recorded->client_seq,
               read_u32(request + 4, recorded->client_swapped),
               request[1],
               read_u16(request + 8, recorded->client_swapped),
               request[10],
               request[11],
               read_u32(request + 12, recorded->client_swapped),
               read_u32(request + 16, recorded->client_swapped));
    } else if (request_type == X_UngrabPointer &&
               recorded->data_len * 4 >= 8) {
        printf("WAYLANDCURSORFIX_REQ t_us=%lld server_ms=%lu client=0x%lx seq=%lu"
               " request=UngrabPointer time=%u\n",
               (long long) realtime_microseconds(),
               (unsigned long) recorded->server_time,
               recorded->id_base,
               recorded->client_seq,
               read_u32(request + 4, recorded->client_swapped));
    } else if (request_type == X_ChangeActivePointerGrab &&
               recorded->data_len * 4 >= 16) {
        printf("WAYLANDCURSORFIX_REQ t_us=%lld server_ms=%lu client=0x%lx seq=%lu"
               " request=ChangeActivePointerGrab cursor=0x%x time=%u event_mask=0x%x\n",
               (long long) realtime_microseconds(),
               (unsigned long) recorded->server_time,
               recorded->id_base,
               recorded->client_seq,
               read_u32(request + 4, recorded->client_swapped),
               read_u32(request + 8, recorded->client_swapped),
               read_u16(request + 12, recorded->client_swapped));
    } else if (request_type == X_WarpPointer &&
               recorded->data_len * 4 >= sz_xWarpPointerReq) {
        printf("WAYLANDCURSORFIX_REQ t_us=%lld server_ms=%lu client=0x%lx seq=%lu"
               " request=WarpPointer src=0x%x dst=0x%x"
               " src_x=%d src_y=%d src_w=%u src_h=%u dst_x=%d dst_y=%d\n",
               (long long) realtime_microseconds(),
               (unsigned long) recorded->server_time,
               recorded->id_base,
               recorded->client_seq,
               read_u32(request + 4, recorded->client_swapped),
               read_u32(request + 8, recorded->client_swapped),
               read_i16(request + 12, recorded->client_swapped),
               read_i16(request + 14, recorded->client_swapped),
               read_u16(request + 16, recorded->client_swapped),
               read_u16(request + 18, recorded->client_swapped),
               read_i16(request + 20, recorded->client_swapped),
               read_i16(request + 22, recorded->client_swapped));
    }

    XRecordFreeData(recorded);
}

int
main(void)
{
    Display *control_display;
    Display *data_display;
    XRecordClientSpec clients = XRecordAllClients;
    XRecordRange *ranges[3];
    XRecordContext context;
    int major;
    int minor;

    control_display = XOpenDisplay(NULL);
    data_display = XOpenDisplay(NULL);
    if (!control_display || !data_display) {
        fprintf(stderr, "cannot open DISPLAY\n");
        return EXIT_FAILURE;
    }

    if (!XRecordQueryVersion(control_display, &major, &minor)) {
        fprintf(stderr, "XRecord extension unavailable\n");
        return EXIT_FAILURE;
    }

    ranges[0] = XRecordAllocRange();
    ranges[1] = XRecordAllocRange();
    ranges[2] = XRecordAllocRange();
    if (!ranges[0] || !ranges[1] || !ranges[2])
        return EXIT_FAILURE;

    ranges[0]->core_requests.first = X_GrabPointer;
    ranges[0]->core_requests.last = X_UngrabPointer;
    ranges[1]->core_requests.first = X_ChangeActivePointerGrab;
    ranges[1]->core_requests.last = X_ChangeActivePointerGrab;
    ranges[2]->core_requests.first = X_WarpPointer;
    ranges[2]->core_requests.last = X_WarpPointer;

    context = XRecordCreateContext(control_display, 0, &clients, 1,
                                   ranges, 3);
    XFree(ranges[0]);
    XFree(ranges[1]);
    XFree(ranges[2]);
    if (!context) {
        fprintf(stderr, "cannot create XRecord context\n");
        return EXIT_FAILURE;
    }
    XSync(control_display, False);

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("WAYLANDCURSORFIX_REQ t_us=%lld reason=start xrecord=%d.%d\n",
           (long long) realtime_microseconds(), major, minor);

    if (!XRecordEnableContext(data_display, context,
                              record_callback, NULL)) {
        fprintf(stderr, "cannot enable XRecord context\n");
        XRecordFreeContext(control_display, context);
        return EXIT_FAILURE;
    }

    XRecordFreeContext(control_display, context);
    XCloseDisplay(data_display);
    XCloseDisplay(control_display);
    return EXIT_SUCCESS;
}
