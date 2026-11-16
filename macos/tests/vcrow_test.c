/*
 * vcrow_test.c: the virtual crow (lua/core/crow/virtual.lua through vcrow_sim.c) driven by
 * norns' own lua/core/crow.lua, in simulated time (macos/tests/vcrow_test.lua), then once
 * over a real pseudo terminal the way matron's device_crow.cc opens it (vcrow_device.c).
 * headless, run from the repo root:
 *
 * L=../../../deps/prefix; cc -std=c11 -Wall -Wextra -I matron/src/platform/macos -I $L/include \
 *    macos/tests/vcrow_test.c matron/src/platform/macos/vcrow_sim.c \
 *    matron/src/platform/macos/vcrow_device.c $L/lib/liblua.a -o /tmp/vct && /tmp/vct
 */

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "vcrow_device.h"
#include "vcrow_sim.h"

#define FIRMWARE "lua/core/crow/virtual.lua"
#define LIBDIR "lua/lib"

static vcrow_sim_t *sim;
static lua_State *host;
static double sim_now = 0;
static double in_v[2];
static char *rx = NULL;
static size_t rx_len = 0, rx_cap = 0;
static int failures = 0;

static void emit(void *ctx, const char *t, size_t n) {
    (void)ctx;
    if (rx_len + n + 1 > rx_cap) {
        rx_cap = (rx_len + n + 1) * 2;
        rx = (char *)realloc(rx, rx_cap);
    }
    memcpy(rx + rx_len, t, n);
    rx_len += n;
    rx[rx_len] = 0;
}

// what crow sent goes to norns: _norns.crow.event(1, text), and to the test's log
static void deliver(void) {
    if (rx_len == 0) {
        return;
    }
    char *text = strndup(rx, rx_len);
    rx_len = 0;
    lua_getglobal(host, "deliver");
    lua_pushstring(host, text);
    free(text);
    if (lua_pcall(host, 1, 0, 0) != LUA_OK) {
        printf("FAIL deliver: %s\n", lua_tostring(host, -1));
        failures++;
        lua_pop(host, 1);
    }
}

static int l_feed(lua_State *L) {
    size_t n;
    const char *s = luaL_checklstring(L, 1, &n);
    // as the device thread: what came in, then a tick
    vcrow_sim_feed(sim, s, n, sim_now);
    vcrow_sim_tick(sim, sim_now, in_v);
    deliver();
    return 0;
}

static int l_advance(lua_State *L) {
    double dt = luaL_checknumber(L, 1);
    double end = sim_now + dt;
    double next = sim_now;
    int ticks = 0;
    while (sim_now < end - 1e-9) {
        sim_now += 0.001;
        if (sim_now + 1e-9 >= next) {
            double wait = vcrow_sim_tick(sim, sim_now, in_v);
            next = sim_now + wait;
            ticks++;
        }
        deliver();
    }
    lua_pushinteger(L, ticks);
    return 1;
}

static int l_set_input(lua_State *L) {
    int n = (int)luaL_checkinteger(L, 1);
    in_v[n - 1] = luaL_checknumber(L, 2);
    vcrow_sim_tick(sim, sim_now, in_v);
    deliver();
    return 0;
}

static int l_out(lua_State *L) {
    double out[4];
    vcrow_sim_outputs(sim, out);
    lua_pushnumber(L, out[luaL_checkinteger(L, 1) - 1]);
    return 1;
}

static int l_mode(lua_State *L) {
    lua_pushstring(L, vcrow_sim_mode(sim, (int)luaL_checkinteger(L, 1) - 1));
    return 1;
}

static int l_now(lua_State *L) {
    lua_pushnumber(L, sim_now);
    return 1;
}

static int scenario(void) {
    sim = vcrow_sim_new(FIRMWARE, LIBDIR, emit, NULL);
    if (sim == NULL) {
        printf("FAIL: firmware did not load\n");
        return 1;
    }
    host = luaL_newstate();
    luaL_openlibs(host);
    lua_register(host, "sim_feed", l_feed);
    lua_register(host, "advance", l_advance);
    lua_register(host, "set_input", l_set_input);
    lua_register(host, "out", l_out);
    lua_register(host, "mode", l_mode);
    lua_register(host, "now", l_now);
    if (luaL_dofile(host, "macos/tests/vcrow_test.lua") != LUA_OK) {
        printf("FAIL scenario: %s\n", lua_tostring(host, -1));
        failures++;
    } else {
        lua_getglobal(host, "failures");
        failures += (int)lua_tointeger(host, -1);
    }
    lua_close(host);
    vcrow_sim_free(sim);
    return 0;
}

//--- over a pty, as matron opens a crow

static double mono(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// read what comes within secs into buf
static size_t read_for(int fd, char *buf, size_t cap, double secs) {
    size_t have = 0;
    double end = mono() + secs;
    while (mono() < end && have < cap - 1) {
        ssize_t n = read(fd, buf + have, cap - 1 - have);
        if (n > 0) {
            have += (size_t)n;
        } else {
            usleep(1000);
        }
    }
    buf[have] = 0;
    return have;
}

static int changes = 0;
static void on_change(void *ctx) {
    (void)ctx;
    __atomic_add_fetch(&changes, 1, __ATOMIC_RELAXED);
}

#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

static void pty_test(void) {
    vcrow_device_t *d = vcrow_open("lua", on_change, NULL);
    CHECK(d != NULL, "pty: vcrow_open");
    if (d == NULL) {
        return;
    }
    // as dev_crow_init: raw, 0.5 s timeout reads, ^^i with a 0 after it
    int fd = open(vcrow_path(d), O_RDWR | O_NOCTTY | O_SYNC);
    CHECK(fd >= 0, "pty: open %s", vcrow_path(d));
    struct termios t;
    memset(&t, 0, sizeof t);
    t.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 5;
    tcflush(fd, TCIFLUSH);
    tcsetattr(fd, TCSANOW, &t);
    char buf[4096];

    if (write(fd, "^^i\n\0", 5) != 5) {
        CHECK(0, "pty: write");
    }
    read_for(fd, buf, sizeof buf, 0.2);
    CHECK(strstr(buf, "^^identity(") != NULL, "pty: identity, got '%s'", buf);
    printf("pty: identity: %s", buf);
    // norns' first words after the add (lua/core/crow.lua) and a few settings
    const char *say = "crow.reset()\noutput[1].volts=3.5\noutput[2].slew=0.2\noutput[2].volts=-4\n"
                      "input[1].stream=function(...)_c.tell(\"A\",quote(...))end\ninput[1].mode(\"stream\",0.05)\n";
    if (write(fd, say, strlen(say)) != (ssize_t)strlen(say)) {
        CHECK(0, "pty: write");
    }
    vcrow_set_input(d, 0, 2.25);
    read_for(fd, buf, sizeof buf, 0.4);
    vcrow_view_t v;
    vcrow_view(d, &v);
    CHECK(fabs(v.out[0] - 3.5) < 1e-6, "pty: out 1 is %g", v.out[0]);
    CHECK(fabs(v.out[1] + 4) < 1e-6, "pty: out 2 slewed to %g", v.out[1]);
    CHECK(strstr(buf, "^^A(2.25)") != NULL, "pty: stream of input 1: '%.80s'", buf);
    CHECK(strcmp(v.mode[0], "stream 0.05s") == 0, "pty: mode label '%s'", v.mode[0]);
    CHECK(fabs(v.in[0] - 2.25) < 1e-9, "pty: input 1 is %g", v.in[0]);
    int nstream = 0;
    for (const char *p = buf; (p = strstr(p, "^^A(")) != NULL; p++) {
        nstream++;
    }
    CHECK(nstream >= 6 && nstream <= 9, "pty: %d stream events in 0.4 s at 0.05 s", nstream);
    // the scope keeps the last 2 s: output 1 went to 3.5
    CHECK(v.scope_max[2][VCROW_SCOPE - 1] > 3.4, "pty: scope of out 1 ends at %g", v.scope_max[2][VCROW_SCOPE - 1]);
    CHECK(__atomic_load_n(&changes, __ATOMIC_RELAXED) > 0, "pty: on_change called");
    // an endless loop is stopped, the crow answers again afterwards
    const char *loop = "while true do end\n";
    if (write(fd, loop, strlen(loop)) != (ssize_t)strlen(loop)) {
        CHECK(0, "pty: write");
    }
    read_for(fd, buf, sizeof buf, 1.0);
    CHECK(strstr(buf, "ran too long") != NULL, "pty: endless loop stopped: '%.120s'", buf);
    if (write(fd, "^^v\n", 4) != 4) {
        CHECK(0, "pty: write");
    }
    read_for(fd, buf, sizeof buf, 0.2);
    CHECK(strstr(buf, "^^version(") != NULL, "pty: answers after the loop: '%.80s'", buf);
    close(fd);
    vcrow_close(d);
    printf("pty: done\n");
}

int main(void) {
    scenario();
    pty_test();
    if (failures == 0) {
        printf("vcrow_test: all passed\n");
        return 0;
    }
    printf("vcrow_test: %d failed\n", failures);
    return 1;
}
