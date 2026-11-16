/*
 * vgrid_device.c: see vgrid_device.h
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "vgrid_device.h"

struct vgrid_device {
    int linked;
    int master;
    int slave; // kept open: without a slave the master reports hangup and never blocks
    char dir[64];
    char path[96];
    pthread_t thread;
    volatile int stop;
    pthread_mutex_t lock;
    vgrid_proto_t proto;
    void (*on_change)(void *ctx);
    void *ctx;
};

static void *run(void *arg) {
    vgrid_device_t *g = (vgrid_device_t *)arg;
    uint8_t in[256], reply[256];
    while (!g->stop) {
        struct pollfd pfd = {g->master, POLLIN, 0};
        if (poll(&pfd, 1, 100) <= 0) {
            continue;
        }
        ssize_t n = read(g->master, in, sizeof in);
        if (n <= 0) {
            // no one has the device open (EIO): wait for libmonome
            usleep(20000);
            continue;
        }
        int changed;
        pthread_mutex_lock(&g->lock);
        size_t r = vgrid_proto_feed(&g->proto, in, (size_t)n, reply, sizeof reply, &changed);
        pthread_mutex_unlock(&g->lock);
        if (r > 0 && write(g->master, reply, r) < 0) {
            perror("vgrid: write");
        }
        if (changed && g->on_change) {
            g->on_change(g->ctx);
        }
    }
    return NULL;
}

// rings > 0: an arc, otherwise a grid of cols x rows. serial: what libmonome reads from the
// link name, NULL for the virtual devices' fixed ones
static vgrid_device_t *open_device(const char *id, const char *serial, int cols, int rows, int rings,
                                   void (*on_change)(void *ctx), void *ctx) {
    vgrid_device_t *g = (vgrid_device_t *)calloc(1, sizeof *g);
    if (g == NULL) {
        return NULL;
    }
    g->slave = -1;
    g->master = posix_openpt(O_RDWR | O_NOCTTY);
    if (g->master < 0 || grantpt(g->master) < 0 || unlockpt(g->master) < 0) {
        goto fail;
    }
    const char *slave = ptsname(g->master);
    if (slave == NULL) {
        goto fail;
    }
    // raw, so nothing on the way is translated
    g->slave = open(slave, O_RDWR | O_NOCTTY);
    if (g->slave < 0) {
        goto fail;
    }
    struct termios t;
    if (tcgetattr(g->slave, &t) == 0) {
        cfmakeraw(&t);
        tcsetattr(g->slave, TCSANOW, &t);
    }
    // a private directory: only this process can put things in it
    snprintf(g->dir, sizeof g->dir, "/tmp/nornsgridXXXXXX");
    if (mkdtemp(g->dir) == NULL) {
        goto fail;
    }
    // distinct serials, so a grid and an arc can be plugged in at the same time
    if (serial == NULL) {
        serial = rings > 0 ? "m0000002" : "m0000001";
    }
    snprintf(g->path, sizeof g->path, "%s/%s-%s", g->dir, rings > 0 ? "arc" : "grid", serial);
    if (symlink(slave, g->path) < 0) {
        rmdir(g->dir);
        goto fail;
    }
    g->linked = 1;
    pthread_mutex_init(&g->lock, NULL);
    if (rings > 0) {
        vgrid_proto_init_arc(&g->proto, id, rings);
    } else {
        vgrid_proto_init(&g->proto, id, cols, rows);
    }
    g->on_change = on_change;
    g->ctx = ctx;
    if (pthread_create(&g->thread, NULL, run, g) != 0) {
        unlink(g->path);
        rmdir(g->dir);
        goto fail;
    }
    return g;

fail:
    if (g->slave >= 0) {
        close(g->slave);
    }
    if (g->master >= 0) {
        close(g->master);
    }
    free(g);
    return NULL;
}

vgrid_device_t *vgrid_open(const char *id, int cols, int rows, void (*on_change)(void *ctx), void *ctx) {
    return open_device(id, NULL, cols, rows, 0, on_change, ctx);
}

vgrid_device_t *vgrid_open_arc(const char *id, int rings, void (*on_change)(void *ctx), void *ctx) {
    return open_device(id, NULL, 0, 0, rings, on_change, ctx);
}

vgrid_device_t *vgrid_open_serial(const char *id, const char *serial, int cols, int rows, int rings,
                                  void (*on_change)(void *ctx), void *ctx) {
    return open_device(id, serial, cols, rows, rings, on_change, ctx);
}

void vgrid_close(vgrid_device_t *g) {
    if (g == NULL) {
        return;
    }
    g->stop = 1;
    pthread_join(g->thread, NULL);
    close(g->slave);
    close(g->master);
    vgrid_unlink(g);
    pthread_mutex_destroy(&g->lock);
    free(g);
}

const char *vgrid_path(const vgrid_device_t *g) {
    return g->path;
}

void vgrid_unlink(vgrid_device_t *g) {
    if (g->linked) {
        unlink(g->path);
        rmdir(g->dir);
        g->linked = 0;
    }
}

static void send_bytes(vgrid_device_t *g, const uint8_t *m, size_t n) {
    if (write(g->master, m, n) < 0) {
        perror("vgrid: write");
    }
}

void vgrid_key(vgrid_device_t *g, int x, int y, int down) {
    uint8_t m[3];
    vgrid_proto_key(x, y, down, m);
    send_bytes(g, m, 3);
}

void vgrid_enc(vgrid_device_t *g, int n, int delta) {
    // one message carries -128..127
    while (delta != 0) {
        int d = delta < -128 ? -128 : (delta > 127 ? 127 : delta);
        uint8_t m[3];
        vgrid_proto_enc(n, d, m);
        send_bytes(g, m, 3);
        delta -= d;
    }
}

void vgrid_enc_key(vgrid_device_t *g, int n, int down) {
    uint8_t m[2];
    vgrid_proto_enc_key(n, down, m);
    send_bytes(g, m, 2);
}

void vgrid_levels(vgrid_device_t *g, uint8_t level[VGRID_MAX][VGRID_MAX], int *intensity) {
    pthread_mutex_lock(&g->lock);
    memcpy(level, g->proto.level, sizeof g->proto.level);
    *intensity = g->proto.intensity;
    pthread_mutex_unlock(&g->lock);
}

void vgrid_rings(vgrid_device_t *g, uint8_t ring[VARC_RINGS][VARC_LEDS], int *intensity) {
    pthread_mutex_lock(&g->lock);
    memcpy(ring, g->proto.ring, sizeof g->proto.ring);
    *intensity = g->proto.intensity;
    pthread_mutex_unlock(&g->lock);
}
