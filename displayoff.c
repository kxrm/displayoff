// displayoff turns the built-in display off while the external monitors stay
// on, waits, and then turns the built-in display on again.
//
//   displayoff            off until you press Enter or Ctrl-C
//   displayoff 30         off for 30 seconds (Enter or Ctrl-C ends it sooner)
//   displayoff on [id]    turn a display on again after a killed run (id defaults to 1)
//   displayoff list       show the online displays
//   displayoff --force 5  the same as above, but also with no external monitor
//
// The change uses kCGConfigureForSession, so a logout also brings the display back.

#include <ApplicationServices/ApplicationServices.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Private CoreGraphics call. It adds a display to the desktop or removes it.
extern CGError CGSConfigureDisplayEnabled(CGDisplayConfigRef config,
                                          CGDirectDisplayID display,
                                          bool enabled);

#define MAX_DISPLAYS 16

static volatile sig_atomic_t stop_requested = 0;

static void request_stop(int sig) {
    (void)sig;
    stop_requested = 1;
}

static bool set_enabled(CGDirectDisplayID id, bool enabled) {
    CGDisplayConfigRef config;
    CGError err = CGBeginDisplayConfiguration(&config);
    if (err != kCGErrorSuccess) {
        fprintf(stderr, "displayoff: cannot start a display change (error %d)\n", err);
        return false;
    }
    err = CGSConfigureDisplayEnabled(config, id, enabled);
    if (err != kCGErrorSuccess) {
        CGCancelDisplayConfiguration(config);
        fprintf(stderr, "displayoff: cannot turn display %u %s (error %d)\n",
                id, enabled ? "on" : "off", err);
        return false;
    }
    err = CGCompleteDisplayConfiguration(config, kCGConfigureForSession);
    if (err != kCGErrorSuccess) {
        fprintf(stderr, "displayoff: the display change failed (error %d)\n", err);
        return false;
    }
    return true;
}

static uint32_t online_displays(CGDirectDisplayID *ids) {
    uint32_t count = 0;
    if (CGGetOnlineDisplayList(MAX_DISPLAYS, ids, &count) != kCGErrorSuccess) {
        return 0;
    }
    return count;
}

static int list_displays(void) {
    CGDirectDisplayID ids[MAX_DISPLAYS];
    uint32_t count = online_displays(ids);
    for (uint32_t i = 0; i < count; i++) {
        printf("%u  %zux%zu  %s  %s\n", ids[i],
               CGDisplayPixelsWide(ids[i]), CGDisplayPixelsHigh(ids[i]),
               CGDisplayIsBuiltin(ids[i]) ? "built-in" : "external",
               CGDisplayIsActive(ids[i]) ? "active" : "inactive");
    }
    return 0;
}

// Wait until the time is up, the user presses Enter, or a signal arrives.
// A seconds value of 0 means no time limit.
static void wait_for_stop(long seconds) {
    time_t deadline = seconds > 0 ? time(NULL) + seconds : 0;
    bool watch_stdin = true;
    while (!stop_requested) {
        if (deadline && time(NULL) >= deadline) {
            return;
        }
        struct pollfd fd = {.fd = STDIN_FILENO, .events = POLLIN};
        if (poll(&fd, watch_stdin ? 1 : 0, 250) > 0) {
            char buf[64];
            if (read(STDIN_FILENO, buf, sizeof buf) > 0) {
                return;
            }
            watch_stdin = false; // stdin is closed, so wait for the time or a signal
            if (!deadline) {
                fprintf(stderr, "displayoff: no terminal input, send a signal to finish\n");
            }
        }
    }
}

static int off_then_on(long seconds, bool force) {
    CGDirectDisplayID ids[MAX_DISPLAYS];
    uint32_t count = online_displays(ids);
    CGDirectDisplayID builtin = kCGNullDirectDisplay;
    uint32_t other_active = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (CGDisplayIsBuiltin(ids[i])) {
            builtin = ids[i];
        } else if (CGDisplayIsActive(ids[i])) {
            other_active++;
        }
    }
    if (builtin == kCGNullDirectDisplay) {
        fprintf(stderr, "displayoff: found no built-in display\n");
        return 1;
    }
    if (other_active == 0 && !force) {
        fprintf(stderr, "displayoff: no external monitor is active, so the built-in display stays on\n");
        return 1;
    }

    struct sigaction sa = {.sa_handler = request_stop};
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    if (!set_enabled(builtin, false)) {
        return 1;
    }
    printf("Built-in display %u is off. Press Enter to turn it on again.\n", builtin);
    printf("If this run is killed, use: displayoff on %u\n", builtin);
    fflush(stdout);

    wait_for_stop(seconds);

    if (!set_enabled(builtin, true)) {
        return 1;
    }
    printf("Built-in display %u is on.\n", builtin);
    return 0;
}

int main(int argc, char **argv) {
    bool force = false;
    if (argc >= 2 && strcmp(argv[1], "--force") == 0) {
        force = true;
        argv++;
        argc--;
    }
    if (argc >= 2 && strcmp(argv[1], "list") == 0) {
        return list_displays();
    }
    if (argc >= 2 && strcmp(argv[1], "on") == 0) {
        CGDirectDisplayID id = argc >= 3 ? (CGDirectDisplayID)strtoul(argv[2], NULL, 10) : 1;
        return set_enabled(id, true) ? 0 : 1;
    }
    long seconds = 0;
    if (argc >= 2) {
        char *end;
        seconds = strtol(argv[1], &end, 10);
        if (*end != '\0' || seconds <= 0) {
            fprintf(stderr, "usage: displayoff [--force] [seconds] | on [id] | list\n");
            return 2;
        }
    }
    return off_then_on(seconds, force);
}
