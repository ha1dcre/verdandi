/*
 * vcrow_device.h
 *
 * the virtual crow as a serial device: a pseudo terminal with the crow simulator
 * (vcrow_sim.h) on the far side, on a thread of its own. matron opens the pty like a crow on
 * usb (device_crow.cc: ^^i, then lua text), so lua/core/crow.lua and scripts see a crow.
 * the window sets the two input voltages and shows the outputs (vcrow_skin.h).
 */

#pragma once

#include "vcrow_sim.h"

#ifdef __cplusplus
extern "C" {
#endif

// the scope: the last 2 s in bins of 12.5 ms, each the lowest and highest voltage in it
#define VCROW_SCOPE 160
#define VCROW_SCOPE_BIN 0.0125
// scope channels: inputs 1-2, then outputs 1-4
#define VCROW_CHANNELS (VCROW_INPUTS + VCROW_OUTPUTS)

typedef struct {
    double in[VCROW_INPUTS];
    double out[VCROW_OUTPUTS];
    char mode[VCROW_INPUTS][VCROW_LABEL];
    float scope_min[VCROW_CHANNELS][VCROW_SCOPE]; // oldest first
    float scope_max[VCROW_CHANNELS][VCROW_SCOPE];
} vcrow_view_t;

typedef struct vcrow_device vcrow_device_t;

// lua_dir: norns' lua folder (lua/core/crow/virtual.lua, lua/lib). on_change runs on the
// device thread, at most about 30 times a second, while something moves
extern vcrow_device_t *vcrow_open(const char *lua_dir, void (*on_change)(void *ctx), void *ctx);
extern void vcrow_close(vcrow_device_t *d);

// the pty, for dev_list_add(DEV_TYPE_CROW, ...)
extern const char *vcrow_path(const vcrow_device_t *d);

// input n (0-1) at volts (clamped to -5..10)
extern void vcrow_set_input(vcrow_device_t *d, int n, double volts);
extern double vcrow_input(vcrow_device_t *d, int n);

extern void vcrow_view(vcrow_device_t *d, vcrow_view_t *v);

#ifdef __cplusplus
}
#endif
