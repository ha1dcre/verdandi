/*
 * vgrid_device.h
 *
 * the virtual grid and arc as serial devices: a pseudo terminal that speaks the device side
 * of the mext protocol (vgrid_proto.h). libmonome opens it like a usb grid or arc, so
 * matron's monome code and the scripts see a varibright grid of the chosen size (64, 128
 * or 256), or an arc with 4 rings.
 *
 * libmonome on macOS takes the serial number from the device path (after the first
 * '-'), so the pty is reached through a link <private tmp dir>/grid-m<digits> (arc-m...).
 */

#pragma once

#include <stdint.h>

#include "vgrid_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vgrid_device vgrid_device_t;

// on_change runs on the device thread whenever the leds changed
extern vgrid_device_t *vgrid_open(const char *id, int cols, int rows, void (*on_change)(void *ctx), void *ctx);
extern vgrid_device_t *vgrid_open_arc(const char *id, int rings, void (*on_change)(void *ctx), void *ctx);
// a grid (rings 0) or arc (rings > 0) that libmonome reports with this serial (it must read
// as a mext one, "m<digits>"); used for hardware reached through serialosc (serialosc_bridge.h)
extern vgrid_device_t *vgrid_open_serial(const char *id, const char *serial, int cols, int rows, int rings,
                                         void (*on_change)(void *ctx), void *ctx);
extern void vgrid_close(vgrid_device_t *g);

// the path to hand to libmonome / dev_list_add
extern const char *vgrid_path(const vgrid_device_t *g);

// once libmonome has the device open, the link and its directory can go (the path stays
// valid as a name for dev_list_remove); vgrid_close does it too
extern void vgrid_unlink(vgrid_device_t *g);

// a key went down (1) or up (0)
extern void vgrid_key(vgrid_device_t *g, int x, int y, int down);

// arc: ring n turned by delta (any size; sent in pieces), its push button down (1) or up (0)
extern void vgrid_enc(vgrid_device_t *g, int n, int delta);
extern void vgrid_enc_key(vgrid_device_t *g, int n, int down);

// the current leds (0-15, [y][x]) and intensity
extern void vgrid_levels(vgrid_device_t *g, uint8_t level[VGRID_MAX][VGRID_MAX], int *intensity);
// an arc's leds (0-15) and intensity
extern void vgrid_rings(vgrid_device_t *g, uint8_t ring[VARC_RINGS][VARC_LEDS], int *intensity);

#ifdef __cplusplus
}
#endif
