#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static volatile sig_atomic_t running = 1;

static void
stop_trace(int signal_number)
{
    (void) signal_number;
    running = 0;
}

static int64_t
realtime_microseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        return -1;

    return (int64_t) now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

int
main(void)
{
    const struct timespec sample_interval = {
        .tv_sec = 0,
        .tv_nsec = 1000000,
    };
    Display *display;
    Window root;
    Window returned_root;
    Window returned_child;
    int root_x;
    int root_y;
    int window_x;
    int window_y;
    int previous_x = 0;
    int previous_y = 0;
    Bool have_previous = False;
    unsigned int mask;

    signal(SIGINT, stop_trace);
    signal(SIGTERM, stop_trace);

    display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "cannot open DISPLAY\n");
        return EXIT_FAILURE;
    }

    root = DefaultRootWindow(display);
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("WAYLANDCURSORFIX_X11 t_us=%lld reason=start\n",
           (long long) realtime_microseconds());

    while (running) {
        if (XQueryPointer(display, root,
                          &returned_root, &returned_child,
                          &root_x, &root_y, &window_x, &window_y,
                          &mask)) {
            if (!have_previous ||
                root_x != previous_x ||
                root_y != previous_y) {
                printf("WAYLANDCURSORFIX_X11 t_us=%lld x=%d y=%d dx=%d dy=%d child=0x%lx\n",
                       (long long) realtime_microseconds(),
                       root_x, root_y,
                       have_previous ? root_x - previous_x : 0,
                       have_previous ? root_y - previous_y : 0,
                       returned_child);
                previous_x = root_x;
                previous_y = root_y;
                have_previous = True;
            }
        }

        while (nanosleep(&sample_interval, NULL) != 0 && errno == EINTR) {
            if (!running)
                break;
        }
    }

    printf("WAYLANDCURSORFIX_X11 t_us=%lld reason=stop\n",
           (long long) realtime_microseconds());
    XCloseDisplay(display);
    return EXIT_SUCCESS;
}
