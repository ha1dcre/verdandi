/*
 * vcrow_sim.h
 *
 * the virtual crow's module side: lua/core/crow/virtual.lua (what a crow runs: outputs and
 * asl, inputs and their modes, metro, clock, public) in a lua state of its own. text from
 * norns goes in with vcrow_sim_feed, ^^events and printed text come out through emit, and
 * vcrow_sim_tick moves time on. no i/o and no threads (vcrow_device.h has the pty, and
 * macos/tests/vcrow_test.c runs it on its own).
 *
 * a script that runs too long (an endless loop) is stopped with an error after some 20
 * million lua instructions, so it cannot hang the virtual crow.
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VCROW_INPUTS 2
#define VCROW_OUTPUTS 4
#define VCROW_LABEL 32

typedef struct vcrow_sim vcrow_sim_t;

// text for norns (not 0-terminated)
typedef void (*vcrow_emit_fn)(void *ctx, const char *text, size_t len);

// firmware: path of virtual.lua; lib_dir: norns' lua/lib (for sequins). NULL on failure
extern vcrow_sim_t *vcrow_sim_new(const char *firmware, const char *lib_dir, vcrow_emit_fn emit, void *ctx);
extern void vcrow_sim_free(vcrow_sim_t *s);

// bytes from norns, as they arrive, at time now (as vcrow_sim_tick); a tick should follow
extern void vcrow_sim_feed(vcrow_sim_t *s, const char *data, size_t len, double now);

// the present (seconds, from any start) and the input voltages: runs what is due and
// returns the seconds until it needs to run again
extern double vcrow_sim_tick(vcrow_sim_t *s, double now, const double in[VCROW_INPUTS]);

// after a tick: the output voltages and what each input does ("none", "stream 0.1s", ...)
extern void vcrow_sim_outputs(const vcrow_sim_t *s, double out[VCROW_OUTPUTS]);
extern const char *vcrow_sim_mode(const vcrow_sim_t *s, int n);

#ifdef __cplusplus
}
#endif
