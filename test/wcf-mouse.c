/*
 * wcf-mouse: a virtual "physical" mouse for the isolated test session.
 *
 * Connects to the nested KWin's EIS server (org.kde.KWin.EIS.RemoteDesktop on
 * the session bus named by DBUS_SESSION_BUS_ADDRESS) and emits libei pointer
 * events. Commands arrive on stdin, one per line:
 *
 *   rel DX DY            one relative motion event
 *   abs X Y              absolute motion (global compositor coordinates)
 *   button CODE 0|1      button release/press (272 = BTN_LEFT)
 *   click                left press + release
 *   stream DX DY HZ [JIT]  continuous relative motion at HZ events/s with
 *                        +-JIT microseconds of timing jitter (default 0)
 *   sweep DX DY HZ       like stream but direction reverses every 250 ms
 *   stop                 stop stream/sweep
 *   sleep MS             wait (events keep streaming)
 *   quit
 *
 * Prints "READY" once devices are usable and "OK <cmd>" after each command.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <libei.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <systemd/sd-bus.h>

static struct ei *ei;
static struct ei_device *dev_rel, *dev_abs, *dev_btn;
static int ready;
static FILE *evlog;    /* WCF_MOUSE_LOG: "<realtime_us> rel|abs dx dy" per injected event */

static long long realtime_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int connect_eis(void)
{
    sd_bus *bus = NULL;
    sd_bus_message *reply = NULL;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int fd = -1, cookie = 0, r;

    r = sd_bus_open_user(&bus);
    if (r < 0) {
        fprintf(stderr, "sd_bus_open_user: %s\n", strerror(-r));
        return -1;
    }
    /* capabilities bitmask as in the RemoteDesktop portal: 1 kbd, 2 pointer, 4 touch */
    r = sd_bus_call_method(bus, "org.kde.KWin", "/org/kde/KWin/EIS/RemoteDesktop",
                           "org.kde.KWin.EIS.RemoteDesktop", "connectToEIS",
                           &err, &reply, "i", 3);
    if (r < 0) {
        fprintf(stderr, "connectToEIS: %s\n", err.message ? err.message : strerror(-r));
        return -1;
    }
    r = sd_bus_message_read(reply, "hi", &fd, &cookie);
    if (r < 0) {
        fprintf(stderr, "read reply: %s\n", strerror(-r));
        return -1;
    }
    fd = dup(fd);
    sd_bus_message_unref(reply);
    /* keep the bus connection open: KWin drops the EIS client when it closes */
    return fd;
}

static void handle_events(void)
{
    struct ei_event *ev;

    ei_dispatch(ei);
    while ((ev = ei_get_event(ei))) {
        switch (ei_event_get_type(ev)) {
        case EI_EVENT_CONNECT:
            break;
        case EI_EVENT_SEAT_ADDED: {
            struct ei_seat *seat = ei_event_get_seat(ev);
            ei_seat_bind_capabilities(seat, EI_DEVICE_CAP_POINTER,
                                      EI_DEVICE_CAP_POINTER_ABSOLUTE,
                                      EI_DEVICE_CAP_BUTTON, NULL);
            break;
        }
        case EI_EVENT_DEVICE_ADDED: {
            struct ei_device *d = ei_event_get_device(ev);
            ei_device_ref(d);
            if (ei_device_has_capability(d, EI_DEVICE_CAP_POINTER) && !dev_rel)
                dev_rel = d;
            else if (ei_device_has_capability(d, EI_DEVICE_CAP_POINTER_ABSOLUTE) && !dev_abs)
                dev_abs = d;
            if (ei_device_has_capability(d, EI_DEVICE_CAP_BUTTON) && !dev_btn)
                dev_btn = d;
            break;
        }
        case EI_EVENT_DEVICE_RESUMED: {
            struct ei_device *d = ei_event_get_device(ev);
            ei_device_start_emulating(d, 1);
            if (dev_rel && dev_btn && !ready) {
                ready = 1;
                printf("READY\n");
                fflush(stdout);
            }
            break;
        }
        case EI_EVENT_DISCONNECT:
            fprintf(stderr, "EIS disconnected\n");
            exit(2);
        default:
            break;
        }
        ei_event_unref(ev);
    }
}

static void emit_rel(double dx, double dy)
{
    if (!dev_rel)
        return;
    ei_device_pointer_motion(dev_rel, dx, dy);
    ei_device_frame(dev_rel, ei_now(ei));
    if (evlog)
        fprintf(evlog, "%lld rel %g %g\n", realtime_us(), dx, dy);
}

static void emit_abs(double x, double y)
{
    if (!dev_abs)
        return;
    ei_device_pointer_motion_absolute(dev_abs, x, y);
    ei_device_frame(dev_abs, ei_now(ei));
    if (evlog)
        fprintf(evlog, "%lld abs %g %g\n", realtime_us(), x, y);
}

static void emit_button(uint32_t code, int down)
{
    if (!dev_btn)
        return;
    ei_device_button_button(dev_btn, code, down);
    ei_device_frame(dev_btn, ei_now(ei));
}

static long long now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

int main(void)
{
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    int eisfd = connect_eis();
    char line[256];
    size_t llen = 0;
    int streaming = 0, sweep = 0;
    double sdx = 0, sdy = 0;
    long period_us = 1000, jitter_us = 0;
    long long sweep_t0 = 0;
    long long sleep_until = 0;

    if (eisfd < 0)
        return 1;
    ei = ei_new_sender(NULL);
    if (ei) ei_configure_name(ei, "wcf-mouse");
    if (!ei || ei_setup_backend_fd(ei, eisfd) != 0) {
        fprintf(stderr, "libei setup failed\n");
        return 1;
    }
    
    srand(1234);
    if (getenv("WCF_MOUSE_LOG"))
        evlog = fopen(getenv("WCF_MOUSE_LOG"), "w");
    if (evlog)
        setvbuf(evlog, NULL, _IOFBF, 1 << 16);

    for (;;) {
        struct pollfd pfd[3] = {
            { .fd = ei_get_fd(ei), .events = POLLIN },
            { .fd = STDIN_FILENO, .events = sleep_until ? 0 : POLLIN },
            { .fd = tfd, .events = POLLIN },
        };
        int timeout = -1;

        if (sleep_until) {
            long long left = sleep_until - now_us();
            if (left <= 0) {
                sleep_until = 0;
                printf("OK sleep\n");
                fflush(stdout);
                continue;
            }
            timeout = (int) (left / 1000) + 1;
        }
        if (poll(pfd, 3, timeout) < 0 && errno != EINTR)
            break;

        if (pfd[0].revents & POLLIN)
            handle_events();

        if (pfd[2].revents & POLLIN) {
            uint64_t n;
            if (read(tfd, &n, sizeof n) > 0 && streaming) {
                double dx = sdx, dy = sdy;
                if (sweep && ((now_us() - sweep_t0) / 250000) % 2 == 1) {
                    dx = -dx;
                    dy = -dy;
                }
                emit_rel(dx, dy);
                if (jitter_us) {
                    long j = (rand() % (2 * jitter_us + 1)) - jitter_us;
                    struct itimerspec its = { .it_value = {
                        .tv_sec = 0, .tv_nsec = (period_us + j > 100 ? period_us + j : 100) * 1000 } };
                    timerfd_settime(tfd, 0, &its, NULL);
                }
            }
        }

        if (pfd[1].revents & (POLLIN | POLLHUP)) {
            char c;
            ssize_t n = read(STDIN_FILENO, &c, 1);
            if (n == 0)
                break;
            if (n < 0)
                continue;
            if (c != '\n') {
                if (llen < sizeof line - 1)
                    line[llen++] = c;
                continue;
            }
            line[llen] = 0;
            llen = 0;

            double a, b, hz;
            long jit = 0;
            int code, down;
            if (!ready && strncmp(line, "quit", 4) != 0) {
                /* commands before READY are dropped with a notice */
                fprintf(stderr, "not ready, dropped: %s\n", line);
            } else if (sscanf(line, "rel %lf %lf", &a, &b) == 2) {
                emit_rel(a, b);
                printf("OK rel\n");
            } else if (sscanf(line, "abs %lf %lf", &a, &b) == 2) {
                emit_abs(a, b);
                printf("OK abs\n");
            } else if (sscanf(line, "button %d %d", &code, &down) == 2) {
                emit_button(code, down);
                printf("OK button\n");
            } else if (!strncmp(line, "click", 5)) {
                emit_button(272, 1);
                emit_button(272, 0);
                printf("OK click\n");
            } else if (sscanf(line, "stream %lf %lf %lf %ld", &a, &b, &hz, &jit) >= 3 ||
                       sscanf(line, "sweep %lf %lf %lf", &a, &b, &hz) == 3) {
                sweep = !strncmp(line, "sweep", 5);
                sdx = a;
                sdy = b;
                period_us = (long) (1e6 / hz);
                jitter_us = sweep ? 0 : jit;
                sweep_t0 = now_us();
                streaming = 1;
                struct itimerspec its = {
                    .it_value = { .tv_sec = 0, .tv_nsec = period_us * 1000 },
                    .it_interval = { .tv_sec = 0, .tv_nsec = jitter_us ? 0 : period_us * 1000 },
                };
                if (its.it_value.tv_nsec >= 1000000000L) {
                    its.it_value.tv_sec = its.it_value.tv_nsec / 1000000000L;
                    its.it_value.tv_nsec %= 1000000000L;
                }
                timerfd_settime(tfd, 0, &its, NULL);
                printf("OK stream\n");
            } else if (!strncmp(line, "stop", 4)) {
                streaming = 0;
                struct itimerspec its = { 0 };
                timerfd_settime(tfd, 0, &its, NULL);
                printf("OK stop\n");
            } else if (sscanf(line, "sleep %lf", &a) == 1) {
                sleep_until = now_us() + (long long) (a * 1000);
                continue;
            } else if (!strncmp(line, "quit", 4)) {
                break;
            } else {
                fprintf(stderr, "unknown command: %s\n", line);
            }
            fflush(stdout);
            if (evlog)
                fflush(evlog);
        }
    }
    if (evlog)
        fflush(evlog);
    return 0;
}
