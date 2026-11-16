/*
 * serialosc_bridge.cc: see serialosc_bridge.h
 *
 * serialosc protocol (monome.org/docs/serialosc/osc): /serialosc/list si <host> <port> answers
 * /serialosc/device ssi <id> <type> <port> per device; /serialosc/notify si registers once
 * for the next /serialosc/add or /serialosc/remove. each device listens on its own port:
 * /sys/info si <host> <port> answers /sys/size, /sys/prefix, ...; /sys/host + /sys/port pick
 * where its keys go; leds are /<prefix>/grid/led/level/map, /<prefix>/ring/map, ...
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iterator>
#include <map>
#include <mutex>
#include <string>

#include <lo/lo.h>

#include "serialosc_bridge.h"
#include "vgrid_device.h"

namespace {

const double PROBE_FAST = 0.25; // while a port waits for its route
const double PROBE_SLOW = 2.0;
const double PENDING_MAX = 2.0; // serialosc runs but does not list the device: open the port
const double STALE = 5.0;       // serialosc stopped listing a device it had

enum { ROUTE_PENDING = 0,
       ROUTE_DIRECT = 1,
       ROUTE_SERIALOSC = 2 };

double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

void lo_err(int num, const char *msg, const char *path) {
    fprintf(stderr, "serialosc: liblo error %d in %s: %s\n", num, path ? path : "-", msg ? msg : "");
}

// libmonome (platform/darwin.c) takes a usb device's serial from its path
std::string tty_serial(const char *path) {
    const char *s = strstr(path, "usbmodem");
    if (s != NULL) {
        return s + 8;
    }
    s = strchr(path, '-');
    return s == NULL ? "" : s + 1;
}

// the serial the bridge's pty shows libmonome: the device id when it reads as a mext one
// (m<digits>, what every current grid and arc reports), else a stable stand-in
std::string link_serial(const std::string &id) {
    bool ok = id.size() >= 2 && id.size() <= 24 && id[0] == 'm';
    for (size_t i = 1; ok && i < id.size(); i++) {
        ok = id[i] >= '0' && id[i] <= '9';
    }
    if (ok) {
        return id;
    }
    uint32_t h = 2166136261u;
    for (char c : id) {
        h = (h ^ (uint8_t)c) * 16777619u;
    }
    char buf[16];
    snprintf(buf, sizeof buf, "m9%06u", h % 1000000u);
    return buf;
}

bool bind_fails(int family, const void *addr, socklen_t len) {
    int fd = socket(family, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }
    if (family == AF_INET6) {
        int one = 1;
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof one);
    }
    bool used = bind(fd, (const struct sockaddr *)addr, len) < 0 && errno == EADDRINUSE;
    close(fd);
    return used;
}

// is anything bound to udp <port> (serialosc's 12002)? a free port means no serialosc
bool udp_port_in_use(int port) {
    struct sockaddr_in a4;
    memset(&a4, 0, sizeof a4);
    a4.sin_len = sizeof a4;
    a4.sin_family = AF_INET;
    a4.sin_port = htons((uint16_t)port);
    a4.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind_fails(AF_INET, &a4, sizeof a4)) {
        return true;
    }
    a4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind_fails(AF_INET, &a4, sizeof a4)) {
        return true;
    }
    struct sockaddr_in6 a6;
    memset(&a6, 0, sizeof a6);
    a6.sin6_len = sizeof a6;
    a6.sin6_family = AF_INET6;
    a6.sin6_port = htons((uint16_t)port);
    a6.sin6_addr = in6addr_any;
    return bind_fails(AF_INET6, &a6, sizeof a6);
}

//--- one device through serialosc

struct bridge {
    std::string serial, type, path;
    int dev_port = 0;
    lo_server_thread st = NULL;
    int port = 0;
    lo_address dev = NULL; // used under m only
    std::atomic<vgrid_device_t *> g{NULL};
    std::atomic<bool> live{false};
    int cols = 0, rows = 0, rings = 0;

    std::mutex m;
    std::condition_variable cv;
    bool have_size = false, have_prefix = false, arc_type = false;
    std::string prefix;
    uint8_t sent[VGRID_MAX][VGRID_MAX];
    uint8_t sent_ring[VARC_RINGS][VARC_LEDS];
    int sent_intensity = -1;
};

bool path_ends(const char *path, const char *suffix) {
    size_t n = strlen(path), k = strlen(suffix);
    return n >= k && strcmp(path + n - k, suffix) == 0;
}

int bridge_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user) {
    (void)argc;
    (void)msg;
    bridge *b = (bridge *)user;
    if (strncmp(path, "/sys/", 5) == 0) {
        std::lock_guard<std::mutex> l(b->m);
        if (strcmp(path, "/sys/size") == 0 && strcmp(types, "ii") == 0) {
            b->cols = argv[0]->i;
            b->rows = argv[1]->i;
            b->have_size = true;
        } else if (strcmp(path, "/sys/prefix") == 0 && types[0] == 's') {
            b->prefix = &argv[0]->s;
            b->have_prefix = true;
        }
        b->cv.notify_all();
        return 0;
    }
    vgrid_device_t *g = b->g.load();
    if (g == NULL || !b->live) {
        return 0;
    }
    if (path_ends(path, "/grid/key") && strcmp(types, "iii") == 0) {
        int x = argv[0]->i, y = argv[1]->i;
        if (x >= 0 && y >= 0 && x < b->cols && y < b->rows) {
            vgrid_key(g, x, y, argv[2]->i != 0);
        }
    } else if (path_ends(path, "/enc/delta") && strcmp(types, "ii") == 0) {
        if (argv[0]->i >= 0 && argv[0]->i < b->rings) {
            vgrid_enc(g, argv[0]->i, argv[1]->i);
        }
    } else if (path_ends(path, "/enc/key") && strcmp(types, "ii") == 0) {
        if (argv[0]->i >= 0 && argv[0]->i < b->rings) {
            vgrid_enc_key(g, argv[0]->i, argv[1]->i != 0);
        }
    }
    return 0;
}

// matron's leds (as the pty received them) -> serialosc; only what changed unless force
void push_leds(bridge *b, bool force) {
    vgrid_device_t *g = b->g.load();
    if (g == NULL) {
        return;
    }
    std::lock_guard<std::mutex> l(b->m);
    if (!b->live) {
        return;
    }
    int intensity;
    if (b->rings > 0) {
        uint8_t ring[VARC_RINGS][VARC_LEDS];
        vgrid_rings(g, ring, &intensity);
        std::string p = b->prefix + "/ring/map";
        for (int r = 0; r < b->rings; r++) {
            if (!force && memcmp(ring[r], b->sent_ring[r], VARC_LEDS) == 0) {
                continue;
            }
            lo_message m = lo_message_new();
            lo_message_add_int32(m, r);
            for (int i = 0; i < VARC_LEDS; i++) {
                lo_message_add_int32(m, ring[r][i]);
            }
            lo_send_message(b->dev, p.c_str(), m);
            lo_message_free(m);
            memcpy(b->sent_ring[r], ring[r], VARC_LEDS);
        }
        return;
    }
    uint8_t level[VGRID_MAX][VGRID_MAX];
    vgrid_levels(g, level, &intensity);
    std::string p = b->prefix + "/grid/led/level/map";
    for (int qy = 0; qy < b->rows; qy += 8) {
        for (int qx = 0; qx < b->cols; qx += 8) {
            bool same = !force;
            for (int y = qy; same && y < qy + 8 && y < b->rows; y++) {
                same = memcmp(&level[y][qx], &b->sent[y][qx], 8) == 0;
            }
            if (same) {
                continue;
            }
            lo_message m = lo_message_new();
            lo_message_add_int32(m, qx);
            lo_message_add_int32(m, qy);
            for (int y = qy; y < qy + 8; y++) {
                for (int x = qx; x < qx + 8; x++) {
                    lo_message_add_int32(m, y < b->rows && x < b->cols ? level[y][x] : 0);
                }
            }
            lo_send_message(b->dev, p.c_str(), m);
            lo_message_free(m);
            for (int y = qy; y < qy + 8 && y < b->rows; y++) {
                memcpy(&b->sent[y][qx], &level[y][qx], 8);
            }
        }
    }
    if (force || intensity != b->sent_intensity) {
        lo_send(b->dev, (b->prefix + "/grid/led/intensity").c_str(), "i", intensity);
        b->sent_intensity = intensity;
    }
}

void leds_changed(void *ctx) {
    push_leds((bridge *)ctx, false);
}

void bridge_close(bridge *b) {
    if (b == NULL) {
        return;
    }
    b->live = false;
    if (b->st != NULL) {
        lo_server_thread_free(b->st); // stops its thread: no more keys
    }
    vgrid_device_t *g = b->g.exchange(NULL);
    if (g != NULL) {
        vgrid_close(g);
    }
    if (b->dev != NULL) {
        lo_address_free(b->dev);
    }
    delete b;
}

// asks the device for its size and prefix, then makes the pty libmonome will open
bridge *bridge_open(const std::string &serial, const std::string &type, int dev_port) {
    bridge *b = new bridge();
    b->serial = serial;
    b->type = type;
    b->dev_port = dev_port;
    b->arc_type = type.find("arc") != std::string::npos;
    b->st = lo_server_thread_new(NULL, lo_err);
    if (b->st == NULL) {
        delete b;
        return NULL;
    }
    b->port = lo_server_thread_get_port(b->st);
    lo_server_thread_add_method(b->st, NULL, NULL, bridge_handler, b);
    char ps[16];
    snprintf(ps, sizeof ps, "%d", dev_port);
    b->dev = lo_address_new("127.0.0.1", ps);
    lo_server_thread_start(b->st);
    {
        std::unique_lock<std::mutex> l(b->m);
        lo_send(b->dev, "/sys/info", "si", "127.0.0.1", b->port);
        b->cv.wait_for(l, std::chrono::milliseconds(1000),
                       [b] { return b->have_prefix && (b->have_size || b->arc_type); });
    }
    bool arc = b->arc_type || (b->have_size && b->cols == 0 && b->rows == 0);
    if (!b->have_prefix || (!arc && (!b->have_size || b->cols <= 0 || b->rows <= 0))) {
        fprintf(stderr, "serialosc: %s (%s) did not answer /sys/info\n", serial.c_str(), type.c_str());
        bridge_close(b);
        return NULL;
    }
    if (arc) {
        // "monome arc 4" / "monome arc 2": the number of rings
        size_t d = type.find_last_of("0123456789");
        int n = d == std::string::npos ? VARC_RINGS : type[d] - '0';
        b->rings = n >= 1 && n <= VARC_RINGS ? n : VARC_RINGS;
        b->cols = b->rows = 0;
    } else {
        b->cols = b->cols > VGRID_MAX ? VGRID_MAX : b->cols;
        b->rows = b->rows > VGRID_MAX ? VGRID_MAX : b->rows;
    }
    vgrid_device_t *g = vgrid_open_serial(b->type.c_str(), link_serial(serial).c_str(), b->cols, b->rows, b->rings,
                                          leds_changed, b);
    if (g == NULL) {
        bridge_close(b);
        return NULL;
    }
    b->path = vgrid_path(g);
    b->g = g;
    return b;
}

// keys to this bridge from now on, and the device shows matron's leds
void bridge_claim(bridge *b) {
    {
        std::lock_guard<std::mutex> l(b->m);
        lo_send(b->dev, "/sys/host", "s", "127.0.0.1");
        lo_send(b->dev, "/sys/port", "i", b->port);
        b->live = true;
    }
    push_leds(b, true);
}

// keeps the prefix current (an app that takes the device over may change it)
void bridge_refresh(bridge *b) {
    std::lock_guard<std::mutex> l(b->m);
    lo_send(b->dev, "/sys/info", "si", "127.0.0.1", b->port);
}

//--- discovery

struct report {
    std::string type;
    int port = 0;
    double seen = 0;
};

std::mutex reports_lock;
std::map<std::string, report> reports; // serialosc id -> latest listing
std::atomic<bool> notify_registered{false};
std::atomic<bool> probe_now{false};

int disc_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user) {
    (void)argc;
    (void)msg;
    (void)user;
    bool listing = strcmp(path, "/serialosc/device") == 0;
    bool add = strcmp(path, "/serialosc/add") == 0;
    bool rm = strcmp(path, "/serialosc/remove") == 0;
    if (add || rm) {
        notify_registered = false; // one-shot: register again on the next probe
        probe_now = true;
    }
    if ((listing || add) && strcmp(types, "ssi") == 0) {
        std::lock_guard<std::mutex> l(reports_lock);
        report &r = reports[&argv[0]->s];
        r.type = &argv[1]->s;
        r.port = argv[2]->i;
        r.seen = now_s();
    }
    return 0;
}

//--- routing

struct tty {
    std::string path, name, serial; // serial: libmonome's, from the path
    std::string usb_serial;         // the usb serial number property
    int route = ROUTE_PENDING;
    double since = 0;
    bridge *b = NULL;
};

serialosc_ops_t ops = {NULL, NULL};
std::map<std::string, tty> ttys;
int so_port = 12002;
lo_server_thread disc = NULL;
int disc_port = 0;
lo_address so_addr = NULL;
bool serialosc_up = false;
double next_probe = 0;

void probe(void) {
    serialosc_up = disc != NULL && udp_port_in_use(so_port);
    if (!serialosc_up) {
        notify_registered = false;
        return;
    }
    lo_send(so_addr, "/serialosc/list", "si", "127.0.0.1", disc_port);
    if (!notify_registered.exchange(true)) {
        lo_send(so_addr, "/serialosc/notify", "si", "127.0.0.1", disc_port);
    }
    for (auto &kv : ttys) {
        if (kv.second.b != NULL) {
            bridge_refresh(kv.second.b);
        }
    }
}

void to_direct(tty &t) {
    if (t.b != NULL) {
        t.b->live = false; // no more keys into the pty once matron lets go of it
        ops.remove(t.b->path.c_str());
        bridge_close(t.b);
        t.b = NULL;
        fprintf(stderr, "serialosc: %s back on its usb port\n", t.serial.c_str());
    }
    ops.add(t.path.c_str(), t.name.c_str());
    t.route = ROUTE_DIRECT;
    t.since = now_s();
}

void to_serialosc(tty &t, const report &r) {
    bridge *b = bridge_open(t.serial, r.type, r.port);
    if (b == NULL) {
        if (t.route == ROUTE_PENDING) {
            to_direct(t);
        } else {
            t.since = now_s(); // try again when serialosc lists it next
        }
        return;
    }
    if (t.route == ROUTE_DIRECT) {
        ops.remove(t.path.c_str()); // let go of the port: serialosc has it
    }
    ops.add(b->path.c_str(), b->type.c_str());
    vgrid_unlink(b->g.load()); // libmonome has the pty open
    bridge_claim(b);
    t.b = b;
    t.route = ROUTE_SERIALOSC;
    t.since = now_s();
    fprintf(stderr, "serialosc: %s (%s) through serialosc, port %d\n", t.serial.c_str(), r.type.c_str(), r.port);
}

// serialosc's id for a port: libmonome's serial from the path (what serialosc itself gets
// through libmonome), or the usb serial number; macOS may append an interface digit to
// usbmodem paths, so a path serial that starts with the id counts too
bool same_device(const tty &t, const std::string &id) {
    if (id.empty()) {
        return false;
    }
    return id == t.serial || id == t.usb_serial || (id.size() >= 4 && t.serial.compare(0, id.size(), id) == 0);
}

const tty *find_tty(const char *path) {
    auto f = ttys.find(path == NULL ? "" : path);
    return f == ttys.end() ? NULL : &f->second;
}

} // namespace

void serialosc_init(const serialosc_ops_t *o) {
    ops = *o;
    const char *env = getenv("VERDANDI_SERIALOSC_PORT");
    if (env != NULL && atoi(env) > 0) {
        so_port = atoi(env);
    }
    char ps[16];
    snprintf(ps, sizeof ps, "%d", so_port);
    so_addr = lo_address_new("127.0.0.1", ps);
    disc = lo_server_thread_new(NULL, lo_err);
    if (disc != NULL) {
        disc_port = lo_server_thread_get_port(disc);
        lo_server_thread_add_method(disc, NULL, NULL, disc_handler, NULL);
        lo_server_thread_start(disc);
    }
}

void serialosc_deinit(void) {
    // bridges stay: matron may still have them open while it shuts down
    if (disc != NULL) {
        lo_server_thread_free(disc);
        disc = NULL;
    }
}

void serialosc_tty_added(const char *path, const char *name, const char *usb_serial) {
    if (ttys.count(path) != 0) {
        return;
    }
    tty &t = ttys[path];
    t.path = path;
    t.name = name == NULL ? "" : name;
    t.serial = tty_serial(path);
    t.usb_serial = usb_serial == NULL ? "" : usb_serial;
    t.since = now_s();
    {
        std::lock_guard<std::mutex> l(reports_lock);
        for (auto r = reports.begin(); r != reports.end();) {
            r = same_device(t, r->first) ? reports.erase(r) : std::next(r); // a fresh plug: only a new listing counts
        }
    }
    probe();
    if (t.serial.empty() || !serialosc_up) {
        to_direct(t); // no serialosc: the port, at once, as always
        return;
    }
    next_probe = t.since + PROBE_FAST;
}

void serialosc_tty_removed(const char *path) {
    auto f = ttys.find(path);
    if (f == ttys.end()) {
        return;
    }
    tty &t = f->second;
    if (t.b != NULL) {
        t.b->live = false;
        ops.remove(t.b->path.c_str());
        bridge_close(t.b);
    } else if (t.route == ROUTE_DIRECT) {
        ops.remove(t.path.c_str());
    }
    ttys.erase(f);
}

void serialosc_tick(void) {
    if (ttys.empty()) {
        return;
    }
    double now = now_s();
    bool pending = false;
    for (auto &kv : ttys) {
        pending = pending || kv.second.route == ROUTE_PENDING;
    }
    if (probe_now.exchange(false) || now >= next_probe) {
        probe();
        next_probe = now + (pending ? PROBE_FAST : PROBE_SLOW);
    }
    std::map<std::string, report> snap;
    {
        std::lock_guard<std::mutex> l(reports_lock);
        snap = reports;
    }
    for (auto &kv : ttys) {
        tty &t = kv.second;
        auto r = snap.begin();
        while (r != snap.end() && !same_device(t, r->first)) {
            ++r;
        }
        bool listed = serialosc_up && r != snap.end() && now - r->second.seen < STALE;
        switch (t.route) {
        case ROUTE_PENDING:
            if (listed) {
                to_serialosc(t, r->second);
            } else if (!serialosc_up || now - t.since > PENDING_MAX) {
                to_direct(t);
            }
            break;
        case ROUTE_DIRECT:
            // serialosc started (or was slow): it lists the device since we opened the port
            if (listed && r->second.seen > t.since) {
                to_serialosc(t, r->second);
            }
            break;
        case ROUTE_SERIALOSC:
            if (!listed) {
                to_direct(t); // serialosc quit
            }
            break;
        }
    }
}

int serialosc_route(const char *tty_path) {
    const tty *t = find_tty(tty_path);
    return t == NULL ? ROUTE_PENDING : t->route;
}

const char *serialosc_bridge_path(const char *tty_path) {
    const tty *t = find_tty(tty_path);
    return t == NULL || t->b == NULL ? "" : t->b->path.c_str();
}

int serialosc_bridge_port(const char *tty_path) {
    const tty *t = find_tty(tty_path);
    return t == NULL || t->b == NULL ? 0 : t->b->port;
}
