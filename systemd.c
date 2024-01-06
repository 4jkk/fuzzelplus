#include "systemd.h"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/random.h>
#include <systemd/sd-bus.h>

#include <string.h>

#define LOG_MODULE "systemd"
#define LOG_ENABLE_DBG 0
#include "log.h"

#ifndef HAVE_SYSTEMD

bool
put_pid_in_scope(pid_t pid, const char *name)
{
    (void)pid;
    (void)name;
    return true;
}

#else

struct userdata {
    char *path;
    int done;
};

static int
match_job_removed(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    const char *path, *unit, *result;
    int r;
    struct userdata *data = userdata;

    r = sd_bus_message_read(m, "uoss", NULL, &path, &unit, &result);
    if (r < 0) {
        LOG_ERR("Failed to read message: %s", error->message);
        return 0;
    }

    if (data->path && !strcmp(path, data->path)) {
        data->done = 1;
        if (strcmp(result, "done")) {
            LOG_ERR("Job %s not done: %s", path, result);
        }
    }

    return 0;
}


bool
put_pid_in_scope(pid_t pid, const char *name)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *m = NULL;
    sd_bus *bus = NULL;

    /* Connect to the user bus */
    int r = sd_bus_open_user(&bus);
    if (r < 0) {
        LOG_ERR("Failed to connect to user bus: %s", strerror(-r));
        goto finish;
    }

    /* Generate application scope postfix */
    unsigned long long rnd = 0;
    r = getrandom(&rnd, sizeof(rnd), 0);
    if (r == -1) {
        LOG_ERRNO("failed to get random bytes");
        /* Take pid of forked process as postfix instead */
        rnd = pid;
    }
    char *scope_name = NULL;
    r = asprintf(&scope_name, "%s-%llx.scope", basename(name), rnd);
    if (r == -1) {
        LOG_ERRNO("failed to allocate scope name buffer");
        goto finish;
    }

    /* Enable signals */
    r = sd_bus_call_method(bus,
            "org.freedesktop.systemd1",         /* service to contact */
            "/org/freedesktop/systemd1",        /* object path */
            "org.freedesktop.systemd1.Manager", /* interface name */
            "Subscribe",                        /* method name */
            &error,                             /* object to return error in */
            NULL,                               /* return message on success */
            NULL);                              /* input signature */
    if (r < 0) {
        LOG_ERR("Failed to issue method call: %s", error.message);
        goto finish;
    }

    struct userdata data = {0};
    sd_bus_slot *slot = NULL;
    /* Listen for JobRemoved signal */
    r = sd_bus_match_signal(bus,
            &slot,
            "org.freedesktop.systemd1",         /* sender */
            "/org/freedesktop/systemd1",        /* object path */
            "org.freedesktop.systemd1.Manager", /* interface name */
            "JobRemoved",
            match_job_removed,
            &data);
    if (r < 0) {
        LOG_ERR("Failed to install match: %s", error.message);
        goto finish;
    }

    /* Queue the StartTransientUnit job */
    r = sd_bus_call_method(bus,
            "org.freedesktop.systemd1",         /* service to contact */
            "/org/freedesktop/systemd1",        /* object path */
            "org.freedesktop.systemd1.Manager", /* interface name */
            "StartTransientUnit",               /* method name */
            &error,                             /* object to return error in */
            &m,                                 /* return message on success */
            "ssa(sv)a(sa(sv))",                 /* input signature */
            scope_name,
            "fail",
            1,
            "PIDs",
            "au",
            1,
            pid,
            0);
    if (r < 0) {
        LOG_ERR("Failed to issue method call: %s", error.message);
        goto finish;
    }

    /* Get job path */
    r = sd_bus_message_read(m, "o", &data.path);
    if (r < 0) {
        LOG_ERR("Failed to parse response message: %s", strerror(-r));
        goto finish;
    }

    LOG_INFO("Queued service job as %s.", data.path);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double start_ns = ts.tv_sec * 1e9 + ts.tv_nsec;
    double elapsed_ns = 0.0;

    while (!data.done && elapsed_ns < 1e8) {
        sd_bus_wait(bus, 10000);
        sd_bus_process(bus, &m);
        clock_gettime(CLOCK_MONOTONIC, &ts);
        elapsed_ns = (ts.tv_sec * 1e9 + ts.tv_nsec) - start_ns;
    }

    if (data.done) {
        LOG_INFO("job %s done: %g ns.", data.path, elapsed_ns);
    } else {
        LOG_ERR("job %s took too long: %g ns.", data.path, elapsed_ns);
    }

finish:
    free(scope_name);
    sd_bus_error_free(&error);
    sd_bus_message_unref(m);
    sd_bus_slot_unref(slot);
    sd_bus_unref(bus);

    return r >= 0;
}

#endif
