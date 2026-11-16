/*
 * serialosc_test.cc: monome devices through serialosc (platform/macos/serialosc_bridge.cc),
 * headless. a fake serialosc (liblo, on a free port handed over in VERDANDI_SERIALOSC_PORT)
 * lists a grid one (16x8) and an arc; matron is stood in for by the real libmonome opened on
 * whatever path the bridge hands to ops.add, read by monome_event_loop as device_monome.cc
 * does. a pty speaking mext plays the grid's usb port for the direct route.
 *
 * run from the repo root:
 * P=../../../deps/prefix; B=$(brew --prefix); O=$(mktemp -d)
 * cc -std=c11 -c matron/src/platform/macos/vgrid_device.c -o $O/vd.o && \
 * cc -std=c11 -c matron/src/platform/macos/vgrid_proto.c -o $O/vp.o && \
 * c++ -std=c++17 -Wall -Wextra -I matron/src/platform/macos -I $P/include -I $B/include \
 *    macos/tests/serialosc_test.cc matron/src/platform/macos/serialosc_bridge.cc $O/vd.o $O/vp.o \
 *    -L $P/lib -lmonome -L $B/lib -llo -Wl,-rpath,$P/lib -o $O/sot && $O/sot
 */

#include <arpa/inet.h>
#include <fcntl.h>
#include <monome.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <util.h>

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <lo/lo.h>

#include "serialosc_bridge.h"

static int failures = 0;
#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

// the device runloop: the test's main thread ticks the bridge
template <typename F>
static bool tick_until(F cond, double timeout) {
    double end = now_s() + timeout;
    while (now_s() < end) {
        serialosc_tick();
        if (cond()) {
            return true;
        }
        usleep(10000);
    }
    return cond();
}
static void tick_for(double s) {
    tick_until([] { return false; }, s);
}

//--- matron's side: libmonome on the path ops.add gets

struct key_ev {
    int x, y, s;
};

struct mdev {
    std::string path, given_name, name, serial;
    monome_t *m = NULL;
    int rows = -1, cols = -1;
    pthread_t thread;
    std::mutex lock;
    std::vector<key_ev> keys;
    int enc_sum[4] = {0, 0, 0, 0};
    int enc_down = 0, enc_up = 0;
};

static std::mutex devs_lock;
static std::map<std::string, mdev *> devs;
static int adds = 0, removes = 0, failed_opens = 0;

static void on_key(const monome_event_t *e, void *d) {
    mdev *md = (mdev *)d;
    std::lock_guard<std::mutex> l(md->lock);
    md->keys.push_back({(int)e->grid.x, (int)e->grid.y, e->event_type == MONOME_BUTTON_DOWN});
}
static void on_delta(const monome_event_t *e, void *d) {
    mdev *md = (mdev *)d;
    std::lock_guard<std::mutex> l(md->lock);
    md->enc_sum[e->encoder.number & 3] += e->encoder.delta;
}
static void on_enc_down(const monome_event_t *e, void *d) {
    (void)e;
    mdev *md = (mdev *)d;
    std::lock_guard<std::mutex> l(md->lock);
    md->enc_down++;
}
static void on_enc_up(const monome_event_t *e, void *d) {
    (void)e;
    mdev *md = (mdev *)d;
    std::lock_guard<std::mutex> l(md->lock);
    md->enc_up++;
}
static void *event_loop(void *m) {
    monome_event_loop((monome_t *)m);
    return NULL;
}

static void op_add(const char *path, const char *name) {
    adds++;
    monome_t *m = monome_open(path); // dev_monome_init
    if (m == NULL) {
        failed_opens++;
        return;
    }
    mdev *md = new mdev();
    md->path = path;
    md->given_name = name;
    md->m = m;
    md->name = monome_get_friendly_name(m);
    md->serial = monome_get_serial(m);
    md->rows = monome_get_rows(m);
    md->cols = monome_get_cols(m);
    monome_register_handler(m, MONOME_BUTTON_DOWN, on_key, md);
    monome_register_handler(m, MONOME_BUTTON_UP, on_key, md);
    monome_register_handler(m, MONOME_ENCODER_DELTA, on_delta, md);
    monome_register_handler(m, MONOME_ENCODER_KEY_DOWN, on_enc_down, md);
    monome_register_handler(m, MONOME_ENCODER_KEY_UP, on_enc_up, md);
    pthread_create(&md->thread, NULL, event_loop, m);
    std::lock_guard<std::mutex> l(devs_lock);
    devs[path] = md;
}

static void op_remove(const char *path) {
    removes++;
    mdev *md = NULL;
    {
        std::lock_guard<std::mutex> l(devs_lock);
        auto f = devs.find(path);
        if (f == devs.end()) {
            return;
        }
        md = f->second;
        devs.erase(f);
    }
    pthread_cancel(md->thread);
    pthread_join(md->thread, NULL);
    monome_close(md->m);
    delete md;
}

static mdev *dev_at(const std::string &path) {
    std::lock_guard<std::mutex> l(devs_lock);
    auto f = devs.find(path);
    return f == devs.end() ? NULL : f->second;
}

//--- a grid's usb port: a pty speaking mext (the direct route)

struct pty_grid {
    int master = -1, slave = -1;
    std::string link;
    pthread_t thread;
};

static void *pty_grid_run(void *arg) {
    pty_grid *p = (pty_grid *)arg;
    uint8_t b;
    for (;;) {
        ssize_t n = read(p->master, &b, 1);
        if (n <= 0) {
            usleep(5000);
            continue;
        }
        if (b == 0x00) {
            uint8_t r[] = {0x00, 0x01, 0x01};
            write(p->master, r, sizeof r);
        } else if (b == 0x01) {
            uint8_t r[33] = {0x01};
            strcpy((char *)r + 1, "monome one");
            write(p->master, r, sizeof r);
        } else if (b == 0x05) {
            uint8_t r[] = {0x03, 16, 8};
            write(p->master, r, sizeof r);
        }
    }
    return NULL;
}

static void pty_grid_open(pty_grid *p, const std::string &link) {
    char name[128];
    openpty(&p->master, &p->slave, name, NULL, NULL);
    struct termios t;
    tcgetattr(p->slave, &t);
    cfmakeraw(&t);
    tcsetattr(p->slave, TCSANOW, &t);
    p->link = link;
    unlink(link.c_str());
    symlink(name, link.c_str());
    pthread_create(&p->thread, NULL, pty_grid_run, p);
}

static void pty_grid_key(pty_grid *p, int x, int y, int s) {
    uint8_t m[3] = {(uint8_t)(s ? 0x21 : 0x20), (uint8_t)x, (uint8_t)y};
    write(p->master, m, 3);
}

//--- the fake serialosc

struct fake_dev {
    std::string id, type, prefix = "/monome";
    int cols, rows, rings;
    lo_server_thread st = NULL;
    int port = 0;
    std::atomic<int> dest_port{0};
    std::mutex lock;
    int level[16][16];
    int ring[4][64];
    int intensity = -1;
    int level_maps = 0, ring_maps = 0;
};

struct fake_serialosc {
    lo_server_thread st = NULL;
    std::mutex lock;
    std::vector<fake_dev *> listed;
    int notify_port = 0;
    int notify_regs = 0, lists = 0;
};

static fake_serialosc *so = NULL;

static int fake_dev_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg,
                            void *user) {
    (void)msg;
    fake_dev *d = (fake_dev *)user;
    std::lock_guard<std::mutex> l(d->lock);
    std::string p = path;
    if (p == "/sys/info" && strcmp(types, "si") == 0) {
        char port[16];
        snprintf(port, sizeof port, "%d", argv[1]->i);
        lo_address a = lo_address_new(&argv[0]->s, port);
        lo_send(a, "/sys/id", "s", d->id.c_str());
        lo_send(a, "/sys/size", "ii", d->cols, d->rows);
        lo_send(a, "/sys/host", "s", "127.0.0.1");
        lo_send(a, "/sys/port", "i", d->dest_port.load());
        lo_send(a, "/sys/prefix", "s", d->prefix.c_str());
        lo_send(a, "/sys/rotation", "i", 0);
        lo_address_free(a);
    } else if (p == "/sys/port" && strcmp(types, "i") == 0) {
        d->dest_port = argv[0]->i;
    } else if (p == d->prefix + "/grid/led/level/map" && argc == 66) {
        int qx = argv[0]->i, qy = argv[1]->i;
        for (int i = 0; i < 64; i++) {
            int x = qx + i % 8, y = qy + i / 8;
            if (x < 16 && y < 16) {
                d->level[y][x] = argv[2 + i]->i;
            }
        }
        d->level_maps++;
    } else if (p == d->prefix + "/grid/led/intensity" && argc == 1) {
        d->intensity = argv[0]->i;
    } else if (p == d->prefix + "/ring/map" && argc == 65) {
        int n = argv[0]->i & 3;
        for (int i = 0; i < 64; i++) {
            d->ring[n][i] = argv[1 + i]->i;
        }
        d->ring_maps++;
    }
    return 0;
}

static fake_dev *fake_dev_new(const char *id, const char *type, int cols, int rows, int rings) {
    fake_dev *d = new fake_dev();
    d->id = id;
    d->type = type;
    d->cols = cols;
    d->rows = rows;
    d->rings = rings;
    memset(d->level, 0xff, sizeof d->level);
    memset(d->ring, 0xff, sizeof d->ring);
    d->st = lo_server_thread_new(NULL, NULL);
    d->port = lo_server_thread_get_port(d->st);
    lo_server_thread_add_method(d->st, NULL, NULL, fake_dev_handler, d);
    lo_server_thread_start(d->st);
    return d;
}

static void fake_dev_send(fake_dev *d, const char *suffix, const char *types, int a, int b, int c) {
    char port[16];
    snprintf(port, sizeof port, "%d", d->dest_port.load());
    lo_address to = lo_address_new("127.0.0.1", port);
    std::string path = d->prefix + suffix;
    if (strlen(types) == 3) {
        lo_send(to, path.c_str(), types, a, b, c);
    } else {
        lo_send(to, path.c_str(), types, a, b);
    }
    lo_address_free(to);
}

static int fake_so_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg,
                           void *user) {
    (void)argc;
    (void)msg;
    (void)user;
    std::lock_guard<std::mutex> l(so->lock);
    if (strcmp(path, "/serialosc/list") == 0 && strcmp(types, "si") == 0) {
        so->lists++;
        char port[16];
        snprintf(port, sizeof port, "%d", argv[1]->i);
        lo_address a = lo_address_new(&argv[0]->s, port);
        for (fake_dev *d : so->listed) {
            lo_send(a, "/serialosc/device", "ssi", d->id.c_str(), d->type.c_str(), d->port);
        }
        lo_address_free(a);
    } else if (strcmp(path, "/serialosc/notify") == 0 && strcmp(types, "si") == 0) {
        so->notify_port = argv[1]->i;
        so->notify_regs++;
    }
    return 0;
}

static void fake_so_start(int port) {
    char ps[16];
    snprintf(ps, sizeof ps, "%d", port);
    so->st = lo_server_thread_new(ps, NULL);
    if (so->st == NULL) {
        printf("FATAL: fake serialosc could not bind %d\n", port);
        exit(1);
    }
    lo_server_thread_add_method(so->st, NULL, NULL, fake_so_handler, NULL);
    lo_server_thread_start(so->st);
}

static void fake_so_stop(void) {
    lo_server_thread_free(so->st);
    so->st = NULL;
}

// one-shot notification, like serialosc
static void fake_so_notify(const char *what, fake_dev *d) {
    std::lock_guard<std::mutex> l(so->lock);
    if (so->notify_port == 0) {
        return;
    }
    char port[16];
    snprintf(port, sizeof port, "%d", so->notify_port);
    lo_address a = lo_address_new("127.0.0.1", port);
    lo_send(a, what, "ssi", d->id.c_str(), d->type.c_str(), d->port);
    lo_address_free(a);
    so->notify_port = 0;
}

static int free_udp_port(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(fd, (struct sockaddr *)&a, sizeof a);
    socklen_t len = sizeof a;
    getsockname(fd, (struct sockaddr *)&a, &len);
    int port = ntohs(a.sin_port);
    close(fd);
    return port;
}

// serialosc applies /sys/port on its own thread: wait until it sends keys to the bridge
static bool claimed(fake_dev *d, const std::string &tty) {
    double end = now_s() + 1;
    while (now_s() < end) {
        if (d->dest_port == serialosc_bridge_port(tty.c_str())) {
            return true;
        }
        usleep(1000);
    }
    return false;
}

//--- checks

// n presses + n releases on varied pads; true if all arrived, in order, on the right pads
static bool keys_arrive(mdev *md, int n, void (*send)(int x, int y, int s), const char *label) {
    {
        std::lock_guard<std::mutex> l(md->lock);
        md->keys.clear();
    }
    for (int i = 0; i < n; i++) {
        int x = i % 16, y = (i / 16) % 8;
        send(x, y, 1);
        usleep(150);
        send(x, y, 0);
        usleep(150);
    }
    double end = now_s() + 3;
    size_t got = 0;
    while (now_s() < end) {
        std::lock_guard<std::mutex> l(md->lock);
        got = md->keys.size();
        if (got >= (size_t)(2 * n)) {
            break;
        }
    }
    usleep(100000);
    std::lock_guard<std::mutex> l(md->lock);
    int wrong = 0;
    for (size_t k = 0; k < md->keys.size() && k < (size_t)(2 * n); k++) {
        int i = (int)k / 2;
        key_ev e = md->keys[k];
        if (e.x != i % 16 || e.y != (i / 16) % 8 || e.s != (k % 2 == 0)) {
            wrong++;
        }
    }
    printf("%s: sent %d presses + %d releases -> matron got %zu events, %d on wrong pads/out of order\n", label, n,
           n, md->keys.size(), wrong);
    return md->keys.size() == (size_t)(2 * n) && wrong == 0;
}

static fake_dev *g_grid = NULL, *g_arc = NULL;
static pty_grid g_pty;
static void send_via_serialosc(int x, int y, int s) {
    fake_dev_send(g_grid, "/grid/key", "iii", x, y, s);
}
static void send_via_usb(int x, int y, int s) {
    pty_grid_key(&g_pty, x, y, s);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    char dir[] = "/tmp/sotestXXXXXX"; // no '-' before the serial: libmonome reads it from the path
    mkdtemp(dir);
    std::string grid_tty = std::string(dir) + "/cu.usbmodemm07548291"; // the tester's grid one
    std::string arc_tty = std::string(dir) + "/cu.usbserial-m0000123"; // never opened while bridged
    pty_grid_open(&g_pty, grid_tty);

    int port = free_udp_port();
    char ps[16];
    snprintf(ps, sizeof ps, "%d", port);
    setenv("VERDANDI_SERIALOSC_PORT", ps, 1);
    serialosc_ops_t ops = {op_add, op_remove};
    serialosc_init(&ops);

    // 1. no serialosc: the usb port, at once, as before
    printf("-- 1. no serialosc\n");
    serialosc_tty_added(grid_tty.c_str(), "monome one", "m0754829");
    CHECK(serialosc_route(grid_tty.c_str()) == 1, "route %d, want direct", serialosc_route(grid_tty.c_str()));
    mdev *md = dev_at(grid_tty);
    CHECK(md != NULL, "grid not opened on its usb port");
    if (md != NULL) {
        printf("direct: name=\"%s\" serial=%s %dx%d\n", md->name.c_str(), md->serial.c_str(), md->cols, md->rows);
        CHECK(md->name == "monome one" && md->serial == "m07548291" && md->cols == 16 && md->rows == 8,
              "direct device wrong");
        CHECK(keys_arrive(md, 1000, send_via_usb, "direct"), "direct keys lost");
    }
    serialosc_tty_removed(grid_tty.c_str());
    CHECK(dev_at(grid_tty) == NULL, "grid still open after unplug");

    // 2. serialosc runs and lists both (grid id = the usb serial number, arc id = the path's)
    printf("-- 2. serialosc lists a grid one and an arc\n");
    so = new fake_serialosc();
    g_grid = fake_dev_new("m0754829", "monome one", 16, 8, 0);
    g_arc = fake_dev_new("m0000123", "monome arc 4", 0, 0, 4);
    g_grid->dest_port = 9; // another app had it
    so->listed = {g_grid, g_arc};
    fake_so_start(port);
    double t0 = now_s();
    serialosc_tty_added(grid_tty.c_str(), "monome one", "m0754829");
    serialosc_tty_added(arc_tty.c_str(), "monome arc", "m0000123");
    bool both = tick_until(
        [&] { return serialosc_route(grid_tty.c_str()) == 2 && serialosc_route(arc_tty.c_str()) == 2; }, 3);
    printf("routed through serialosc after %.2f s: %s\n", now_s() - t0, both ? "yes" : "no");
    CHECK(both, "not routed through serialosc");
    CHECK(dev_at(grid_tty) == NULL, "the grid's usb port was opened while serialosc has it");
    mdev *g = dev_at(serialosc_bridge_path(grid_tty.c_str()));
    mdev *a = dev_at(serialosc_bridge_path(arc_tty.c_str()));
    CHECK(g != NULL && a != NULL, "bridges not opened by libmonome");
    if (g == NULL || a == NULL) {
        printf("serialosc_test: %d failures\n", failures);
        return 1;
    }
    printf("grid: name=\"%s\" serial=%s %dx%d (ops.add name \"%s\")\n", g->name.c_str(), g->serial.c_str(), g->cols,
           g->rows, g->given_name.c_str());
    printf("arc:  name=\"%s\" serial=%s %dx%d\n", a->name.c_str(), a->serial.c_str(), a->cols, a->rows);
    CHECK(g->name == "monome one" && g->serial == "m07548291" && g->cols == 16 && g->rows == 8,
          "bridged grid differs from the usb one");
    CHECK(a->name == "monome arc 4" && a->serial == "m0000123" && a->cols == 0 && a->rows == 0, "bridged arc wrong");
    CHECK(claimed(g_grid, grid_tty), "grid keys not claimed (port %d)",
          g_grid->dest_port.load());
    CHECK(claimed(g_arc, arc_tty), "arc keys not claimed");
    CHECK(so->notify_regs >= 1, "no /serialosc/notify registration");

    // 3. keys and encoders
    printf("-- 3. keys and encoders through serialosc\n");
    CHECK(keys_arrive(g, 1000, send_via_serialosc, "serialosc"), "keys lost through serialosc");
    int want[4] = {0, 0, 0, 0};
    for (int i = 0; i < 400; i++) {
        int n = i % 4, d = (i % 3) - 1 + (i % 7 == 0 ? 40 : 0);
        want[n] += d;
        fake_dev_send(g_arc, "/enc/delta", "ii", n, d, 0);
        usleep(150);
    }
    for (int i = 0; i < 50; i++) {
        fake_dev_send(g_arc, "/enc/key", "ii", i % 4, 1, 0);
        usleep(150);
        fake_dev_send(g_arc, "/enc/key", "ii", i % 4, 0, 0);
        usleep(150);
    }
    usleep(300000);
    {
        std::lock_guard<std::mutex> l(a->lock);
        printf("arc: deltas %d %d %d %d (want %d %d %d %d), key down %d up %d (want 50 50)\n", a->enc_sum[0],
               a->enc_sum[1], a->enc_sum[2], a->enc_sum[3], want[0], want[1], want[2], want[3], a->enc_down,
               a->enc_up);
        CHECK(memcmp(a->enc_sum, want, sizeof want) == 0 && a->enc_down == 50 && a->enc_up == 50, "arc input lost");
    }

    // 4. leds
    printf("-- 4. leds through serialosc\n");
    uint8_t quad[64];
    for (int i = 0; i < 64; i++) {
        quad[i] = (uint8_t)(i % 16);
    }
    monome_led_level_map(g->m, 8, 0, quad);
    monome_led_level_set(g->m, 1, 2, 11);
    monome_led_intensity(g->m, 9);
    uint8_t levels[64];
    for (int i = 0; i < 64; i++) {
        levels[i] = (uint8_t)((i * 3) % 16);
    }
    monome_led_ring_map(a->m, 2, levels);
    tick_for(0.3);
    {
        std::lock_guard<std::mutex> l(g_grid->lock);
        int bad = 0;
        for (int i = 0; i < 64; i++) {
            bad += g_grid->level[i / 8][8 + i % 8] != i % 16;
        }
        bad += g_grid->level[2][1] != 11;
        printf("grid leds: %d wrong, intensity %d, %d level maps sent\n", bad, g_grid->intensity,
               g_grid->level_maps);
        CHECK(bad == 0 && g_grid->intensity == 9, "grid leds wrong");
    }
    {
        std::lock_guard<std::mutex> l(g_arc->lock);
        int bad = 0;
        for (int i = 0; i < 64; i++) {
            bad += g_arc->ring[2][i] != (i * 3) % 16;
        }
        printf("arc ring 2: %d wrong, %d ring maps sent\n", bad, g_arc->ring_maps);
        CHECK(bad == 0, "arc leds wrong");
    }

    // 5. unplug and plug the grid again
    printf("-- 5. hotplug\n");
    {
        std::lock_guard<std::mutex> l(so->lock);
        so->listed = {g_arc};
    }
    fake_so_notify("/serialosc/remove", g_grid);
    serialosc_tty_removed(grid_tty.c_str());
    int r0 = removes;
    CHECK(serialosc_route(grid_tty.c_str()) == 0 && dev_at(grid_tty) == NULL, "grid still there after unplug");
    printf("unplug: removes=%d, devices open=%zu\n", r0, devs.size());
    tick_for(0.3); // a notification registration again
    {
        std::lock_guard<std::mutex> l(so->lock);
        so->listed = {g_grid, g_arc};
    }
    t0 = now_s();
    serialosc_tty_added(grid_tty.c_str(), "monome one", "m0754829");
    fake_so_notify("/serialosc/add", g_grid);
    bool back = tick_until([&] { return serialosc_route(grid_tty.c_str()) == 2; }, 3);
    printf("replug: through serialosc again after %.2f s\n", now_s() - t0);
    CHECK(back, "replugged grid not routed through serialosc");
    g = dev_at(serialosc_bridge_path(grid_tty.c_str()));
    CHECK(g != NULL && claimed(g_grid, grid_tty) && keys_arrive(g, 100, send_via_serialosc, "replug"),
          "keys after replug");

    // 6. serialosc quits: back to the usb ports
    printf("-- 6. serialosc quits\n");
    fake_so_stop();
    t0 = now_s();
    bool direct = tick_until([&] { return serialosc_route(grid_tty.c_str()) == 1; }, 7);
    printf("back on the usb port after %.2f s\n", now_s() - t0);
    CHECK(direct && serialosc_route(arc_tty.c_str()) == 1, "not back on the usb ports");
    md = dev_at(grid_tty);
    CHECK(md != NULL && md->name == "monome one", "grid not reopened on its usb port");
    if (md != NULL) {
        CHECK(keys_arrive(md, 1000, send_via_usb, "usb after serialosc quit"), "keys lost after serialosc quit");
    }

    // 7. serialosc starts again: moved over
    printf("-- 7. serialosc starts again\n");
    fake_so_start(port);
    t0 = now_s();
    bool moved = tick_until([&] { return serialosc_route(grid_tty.c_str()) == 2; }, 5);
    printf("moved to serialosc after %.2f s\n", now_s() - t0);
    CHECK(moved && dev_at(grid_tty) == NULL, "not moved to serialosc (usb port still open: %d)",
          dev_at(grid_tty) != NULL);
    g = dev_at(serialosc_bridge_path(grid_tty.c_str()));
    CHECK(g != NULL && claimed(g_grid, grid_tty) && keys_arrive(g, 200, send_via_serialosc, "after serialosc restart"),
          "keys after restart");

    // 8. serialosc runs but does not list the grid: the usb port after 2 s
    printf("-- 8. serialosc runs without the grid\n");
    serialosc_tty_removed(grid_tty.c_str());
    {
        std::lock_guard<std::mutex> l(so->lock);
        so->listed = {g_arc};
    }
    t0 = now_s();
    serialosc_tty_added(grid_tty.c_str(), "monome one", "m0754829");
    CHECK(serialosc_route(grid_tty.c_str()) == 0, "decided before serialosc could answer");
    direct = tick_until([&] { return serialosc_route(grid_tty.c_str()) == 1; }, 4);
    printf("usb port after %.2f s\n", now_s() - t0);
    CHECK(direct && dev_at(grid_tty) != NULL, "unlisted grid not opened on its usb port");

    serialosc_tty_removed(grid_tty.c_str());
    serialosc_tty_removed(arc_tty.c_str());
    printf("adds=%d removes=%d failed opens=%d (the arc's usb port is not real here), open now=%zu\n", adds,
           removes, failed_opens, devs.size());
    CHECK(devs.empty(), "devices left open");
    unlink(grid_tty.c_str());
    rmdir(dir);
    printf("serialosc_test: %d failures\n", failures);
    _exit(failures == 0 ? 0 : 1);
}
