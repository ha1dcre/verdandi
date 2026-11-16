/*
 * vcrow_device.c: see vcrow_device.h
 */

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "vcrow_device.h"

// text for norns waiting to be written; more than this (norns not reading) is dropped
#define OUT_CAP 65536
// redraws while something moves, and how long the scope keeps moving after a change
#define NOTIFY_EVERY (1.0 / 30)
#define SCOPE_SPAN (VCROW_SCOPE * VCROW_SCOPE_BIN)

struct vcrow_device {
    int master;
    int slave; // kept open: without a slave the master reports hangup
    int wake[2];
    char path[128];
    pthread_t thread;
    volatile int stop;
    vcrow_sim_t *sim;
    char out[OUT_CAP];
    size_t out_len;
    pthread_mutex_t lock; // the fields below
    double in[VCROW_INPUTS];
    double vout[VCROW_OUTPUTS];
    char mode[VCROW_INPUTS][VCROW_LABEL];
    float smin[VCROW_CHANNELS][VCROW_SCOPE], smax[VCROW_CHANNELS][VCROW_SCOPE];
    int64_t bin;                // the bin being filled, counted from the start
    float prev[VCROW_CHANNELS]; // the values at the last tick
    void (*on_change)(void *ctx);
    void *ctx;
};

static double mono(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// device thread only
static void emit(void *ctx, const char *t, size_t n) {
    vcrow_device_t *d = (vcrow_device_t *)ctx;
    if (d->out_len + n > OUT_CAP) {
        return;
    }
    memcpy(d->out + d->out_len, t, n);
    d->out_len += n;
}

static void flush_out(vcrow_device_t *d) {
    size_t done = 0;
    while (done < d->out_len) {
        ssize_t n = write(d->master, d->out + done, d->out_len - done);
        if (n <= 0) {
            break; // full (EAGAIN): the rest next time
        }
        done += (size_t)n;
    }
    memmove(d->out, d->out + done, d->out_len - done);
    d->out_len -= done;
}

static int idx(int64_t bin) {
    return (int)(bin % VCROW_SCOPE);
}

// lock held: the scope up to time t with the present values
static void scope_record(vcrow_device_t *d, double t) {
    int64_t b = (int64_t)floor(t / VCROW_SCOPE_BIN);
    float v[VCROW_CHANNELS];
    for (int c = 0; c < VCROW_INPUTS; c++) {
        v[c] = (float)d->in[c];
    }
    for (int c = 0; c < VCROW_OUTPUTS; c++) {
        v[VCROW_INPUTS + c] = (float)d->vout[c];
    }
    if (b - d->bin > VCROW_SCOPE) {
        d->bin = b - VCROW_SCOPE;
    }
    // bins passed without a tick held the value from before; this one starts with the new
    while (d->bin < b) {
        d->bin++;
        int i = idx(d->bin);
        for (int c = 0; c < VCROW_CHANNELS; c++) {
            float x = d->bin < b ? d->prev[c] : v[c];
            d->smin[c][i] = d->smax[c][i] = x;
        }
    }
    int i = idx(b);
    for (int c = 0; c < VCROW_CHANNELS; c++) {
        if (v[c] < d->smin[c][i]) {
            d->smin[c][i] = v[c];
        }
        if (v[c] > d->smax[c][i]) {
            d->smax[c][i] = v[c];
        }
        d->prev[c] = v[c];
    }
}

static void *run(void *arg) {
    vcrow_device_t *d = (vcrow_device_t *)arg;
    pthread_setname_np("virtual crow");
    double t0 = mono(), wait = 0, last_change = -10, last_notify = -10;
    double last_in[VCROW_INPUTS] = {0};
    char buf[1024];
    while (!d->stop) {
        int ms = (int)ceil(wait * 1000);
        if (ms < 1) {
            ms = 1;
        } else if (ms > 1000) {
            ms = 1000;
        }
        struct pollfd pfd[2] = {{d->master, POLLIN, 0}, {d->wake[0], POLLIN, 0}};
        int r = poll(pfd, 2, ms);
        if (r > 0 && (pfd[1].revents & POLLIN)) {
            while (read(d->wake[0], buf, sizeof buf) > 0) {
            }
        }
        if (r > 0 && (pfd[0].revents & POLLIN)) {
            ssize_t n = read(d->master, buf, sizeof buf);
            if (n > 0) {
                vcrow_sim_feed(d->sim, buf, (size_t)n, mono() - t0);
            } else if (n < 0 && errno != EAGAIN) {
                usleep(20000);
            }
        }
        double in[VCROW_INPUTS];
        pthread_mutex_lock(&d->lock);
        memcpy(in, d->in, sizeof in);
        pthread_mutex_unlock(&d->lock);
        double t = mono() - t0;
        wait = vcrow_sim_tick(d->sim, t, in);
        flush_out(d);

        double out[VCROW_OUTPUTS];
        vcrow_sim_outputs(d->sim, out);
        pthread_mutex_lock(&d->lock);
        int changed = memcmp(out, d->vout, sizeof out) != 0;
        memcpy(d->vout, out, sizeof out);
        for (int n = 0; n < VCROW_INPUTS; n++) {
            const char *m = vcrow_sim_mode(d->sim, n);
            if (strcmp(m, d->mode[n]) != 0) {
                snprintf(d->mode[n], VCROW_LABEL, "%s", m);
                changed = 1;
            }
        }
        if (memcmp(in, last_in, sizeof in) != 0) {
            memcpy(last_in, in, sizeof in);
            changed = 1;
        }
        scope_record(d, t);
        pthread_mutex_unlock(&d->lock);
        if (changed) {
            last_change = t;
        }
        // while the scope shows a change, the window keeps redrawing
        if (t - last_change < SCOPE_SPAN + 0.1) {
            if (t - last_notify >= NOTIFY_EVERY) {
                last_notify = t;
                if (d->on_change != NULL) {
                    d->on_change(d->ctx);
                }
            }
            double until = last_notify + NOTIFY_EVERY - t;
            if (until < wait) {
                wait = until;
            }
        }
        if (d->out_len > 0 && wait > 0.002) {
            wait = 0.002; // norns has not taken it all yet
        }
    }
    return NULL;
}

vcrow_device_t *vcrow_open(const char *lua_dir, void (*on_change)(void *ctx), void *ctx) {
    vcrow_device_t *d = (vcrow_device_t *)calloc(1, sizeof *d);
    if (d == NULL) {
        return NULL;
    }
    d->slave = -1;
    d->wake[0] = d->wake[1] = -1;
    char fw[1024], lib[1024];
    snprintf(fw, sizeof fw, "%s/core/crow/virtual.lua", lua_dir);
    snprintf(lib, sizeof lib, "%s/lib", lua_dir);
    d->sim = vcrow_sim_new(fw, lib, emit, d);
    if (d->sim == NULL) {
        free(d);
        return NULL;
    }
    d->master = posix_openpt(O_RDWR | O_NOCTTY);
    if (d->master < 0 || grantpt(d->master) < 0 || unlockpt(d->master) < 0) {
        goto fail;
    }
    const char *slave = ptsname(d->master);
    if (slave == NULL) {
        goto fail;
    }
    snprintf(d->path, sizeof d->path, "%s", slave);
    d->slave = open(slave, O_RDWR | O_NOCTTY);
    if (d->slave < 0) {
        goto fail;
    }
    struct termios t;
    if (tcgetattr(d->slave, &t) == 0) {
        cfmakeraw(&t);
        tcsetattr(d->slave, TCSANOW, &t);
    }
    fcntl(d->master, F_SETFL, fcntl(d->master, F_GETFL) | O_NONBLOCK);
    if (pipe(d->wake) < 0) {
        goto fail;
    }
    fcntl(d->wake[0], F_SETFL, O_NONBLOCK);
    fcntl(d->wake[1], F_SETFL, O_NONBLOCK);
    for (int n = 0; n < VCROW_INPUTS; n++) {
        snprintf(d->mode[n], VCROW_LABEL, "none");
    }
    pthread_mutex_init(&d->lock, NULL);
    d->on_change = on_change;
    d->ctx = ctx;
    if (pthread_create(&d->thread, NULL, run, d) != 0) {
        pthread_mutex_destroy(&d->lock);
        goto fail;
    }
    return d;

fail:
    for (int i = 0; i < 2; i++) {
        if (d->wake[i] >= 0) {
            close(d->wake[i]);
        }
    }
    if (d->slave >= 0) {
        close(d->slave);
    }
    if (d->master >= 0) {
        close(d->master);
    }
    vcrow_sim_free(d->sim);
    free(d);
    return NULL;
}

void vcrow_close(vcrow_device_t *d) {
    if (d == NULL) {
        return;
    }
    d->stop = 1;
    if (write(d->wake[1], "x", 1) < 0) {
        // the thread wakes within a second anyway
    }
    pthread_join(d->thread, NULL);
    close(d->wake[0]);
    close(d->wake[1]);
    close(d->slave);
    close(d->master);
    vcrow_sim_free(d->sim);
    pthread_mutex_destroy(&d->lock);
    free(d);
}

const char *vcrow_path(const vcrow_device_t *d) {
    return d->path;
}

void vcrow_set_input(vcrow_device_t *d, int n, double volts) {
    if (n < 0 || n >= VCROW_INPUTS) {
        return;
    }
    volts = volts < -5 ? -5 : (volts > 10 ? 10 : volts);
    pthread_mutex_lock(&d->lock);
    d->in[n] = volts;
    pthread_mutex_unlock(&d->lock);
    if (write(d->wake[1], "x", 1) < 0) {
        // pipe full: a wake is pending already
    }
}

double vcrow_input(vcrow_device_t *d, int n) {
    if (n < 0 || n >= VCROW_INPUTS) {
        return 0;
    }
    pthread_mutex_lock(&d->lock);
    double v = d->in[n];
    pthread_mutex_unlock(&d->lock);
    return v;
}

void vcrow_view(vcrow_device_t *d, vcrow_view_t *v) {
    pthread_mutex_lock(&d->lock);
    memcpy(v->in, d->in, sizeof v->in);
    memcpy(v->out, d->vout, sizeof v->out);
    memcpy(v->mode, d->mode, sizeof v->mode);
    for (int c = 0; c < VCROW_CHANNELS; c++) {
        for (int k = 0; k < VCROW_SCOPE; k++) {
            int i = idx(d->bin + 1 + k); // oldest first, the current bin last
            v->scope_min[c][k] = d->smin[c][i];
            v->scope_max[c][k] = d->smax[c][i];
        }
    }
    pthread_mutex_unlock(&d->lock);
}
