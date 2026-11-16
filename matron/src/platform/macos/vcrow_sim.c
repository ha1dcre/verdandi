/*
 * vcrow_sim.c: see vcrow_sim.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "vcrow_sim.h"

// the count hook runs every HOOK_STEP instructions; HOOK_LIMIT of them in one call from C
// is a script that does not come back
#define HOOK_STEP 100000
#define HOOK_LIMIT 200

struct vcrow_sim {
    lua_State *L;
    int ref_receive, ref_tick;
    vcrow_emit_fn emit;
    void *ctx;
    int hooks;
    double out[VCROW_OUTPUTS];
    char mode[VCROW_INPUTS][VCROW_LABEL];
};

static vcrow_sim_t *sim_of(lua_State *L) {
    return *(vcrow_sim_t **)lua_getextraspace(L);
}

static void hook(lua_State *L, lua_Debug *ar) {
    (void)ar;
    vcrow_sim_t *s = sim_of(L);
    if (++s->hooks > HOOK_LIMIT) {
        s->hooks = 0;
        luaL_error(L, "virtual crow: the script ran too long and was stopped");
    }
}

static int l_emit(lua_State *L) {
    vcrow_sim_t *s = (vcrow_sim_t *)lua_touserdata(L, lua_upvalueindex(1));
    size_t n;
    const char *t = luaL_checklstring(L, 1, &n);
    if (s->emit != NULL) {
        s->emit(s->ctx, t, n);
    }
    return 0;
}

static int call(vcrow_sim_t *s, int nargs, int nresults) {
    s->hooks = 0;
    if (lua_pcall(s->L, nargs, nresults, 0) != LUA_OK) {
        fprintf(stderr, "vcrow: %s\n", lua_tostring(s->L, -1));
        lua_pop(s->L, 1);
        return -1;
    }
    return 0;
}

vcrow_sim_t *vcrow_sim_new(const char *firmware, const char *lib_dir, vcrow_emit_fn emit, void *ctx) {
    vcrow_sim_t *s = (vcrow_sim_t *)calloc(1, sizeof *s);
    if (s == NULL) {
        return NULL;
    }
    s->emit = emit;
    s->ctx = ctx;
    for (int n = 0; n < VCROW_INPUTS; n++) {
        snprintf(s->mode[n], VCROW_LABEL, "none");
    }
    s->L = luaL_newstate();
    if (s->L == NULL) {
        free(s);
        return NULL;
    }
    lua_State *L = s->L;
    // coroutines made later copy the main thread's extra space and hook
    *(vcrow_sim_t **)lua_getextraspace(L) = s;
    luaL_openlibs(L);
    if (lib_dir != NULL) {
        lua_getglobal(L, "package");
        lua_pushfstring(L, "%s/?.lua", lib_dir);
        lua_setfield(L, -2, "path");
        lua_pop(L, 1);
    }
    lua_sethook(L, hook, LUA_MASKCOUNT, HOOK_STEP);
    if (luaL_loadfile(L, firmware) != LUA_OK) {
        fprintf(stderr, "vcrow: %s\n", lua_tostring(L, -1));
        goto fail;
    }
    lua_pushlightuserdata(L, s);
    lua_pushcclosure(L, l_emit, 1);
    if (call(s, 1, 1) < 0) {
        goto fail;
    }
    if (!lua_istable(L, -1)) {
        fprintf(stderr, "vcrow: %s gives no table\n", firmware);
        goto fail;
    }
    lua_getfield(L, -1, "tick");
    s->ref_tick = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_getfield(L, -1, "receive");
    s->ref_receive = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pop(L, 1);
    return s;

fail:
    lua_close(L);
    free(s);
    return NULL;
}

void vcrow_sim_free(vcrow_sim_t *s) {
    if (s == NULL) {
        return;
    }
    lua_close(s->L);
    free(s);
}

void vcrow_sim_feed(vcrow_sim_t *s, const char *data, size_t len, double now) {
    lua_rawgeti(s->L, LUA_REGISTRYINDEX, s->ref_receive);
    lua_pushlstring(s->L, data, len);
    lua_pushnumber(s->L, now);
    call(s, 2, 0);
}

double vcrow_sim_tick(vcrow_sim_t *s, double now, const double in[VCROW_INPUTS]) {
    lua_State *L = s->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->ref_tick);
    lua_pushnumber(L, now);
    lua_pushnumber(L, in[0]);
    lua_pushnumber(L, in[1]);
    if (call(s, 3, 7) < 0) {
        return 0.01;
    }
    double wait = lua_tonumber(L, -7);
    for (int n = 0; n < VCROW_OUTPUTS; n++) {
        s->out[n] = lua_tonumber(L, -6 + n);
    }
    for (int n = 0; n < VCROW_INPUTS; n++) {
        const char *m = lua_tostring(L, -2 + n);
        snprintf(s->mode[n], VCROW_LABEL, "%s", m != NULL ? m : "");
    }
    lua_pop(L, 7);
    return wait > 0 ? wait : 0.0005;
}

void vcrow_sim_outputs(const vcrow_sim_t *s, double out[VCROW_OUTPUTS]) {
    memcpy(out, s->out, sizeof s->out);
}

const char *vcrow_sim_mode(const vcrow_sim_t *s, int n) {
    return n >= 0 && n < VCROW_INPUTS ? s->mode[n] : "";
}
