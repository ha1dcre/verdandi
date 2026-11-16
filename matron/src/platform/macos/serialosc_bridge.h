/*
 * serialosc_bridge.h
 *
 * monome grids and arcs when serialosc runs on the Mac. serialosc (the monome service Max,
 * Ableton and others use) opens every grid's serial port itself; a second program reading the
 * same port gets only part of the bytes, so keys go missing or land on the wrong pads. while
 * serialosc is up, matron does not open the port: each device is reached through serialosc
 * (OSC on 127.0.0.1) via a pseudo terminal that speaks mext (vgrid_device.h), so libmonome
 * and matron's monome code see the same device as over usb (name, serial, size).
 *
 * the usb port stays the truth for whether a device is plugged in (the IOKit monitor reports
 * it); serialosc only decides the route:
 *  - serialosc not running (its port, 12002, unbound): the port is opened at once, as before
 *  - serialosc lists the device: through serialosc
 *  - serialosc running but the device not listed within 2 s: the port, as before
 *  - serialosc starts later: moved over once it lists the device (checked every 2 s)
 *  - serialosc quits: back to the port once it has not answered for 5 s
 * VERDANDI_SERIALOSC_PORT overrides 12002 (tests).
 *
 * every call, and the ops, run on one thread (the device runloop).
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // a monome device for matron at path (dev_list_add); name is copied
    void (*add)(const char *path, const char *name);
    void (*remove)(const char *path);
} serialosc_ops_t;

extern void serialosc_init(const serialosc_ops_t *ops);
extern void serialosc_deinit(void);

// a monome usb serial port (/dev/cu.*) appeared / went away; name: its usb product name,
// usb_serial: its usb serial number (may be empty)
extern void serialosc_tty_added(const char *path, const char *name, const char *usb_serial);
extern void serialosc_tty_removed(const char *path);

// call regularly (every 100 ms); does nothing while no monome port is plugged in
extern void serialosc_tick(void);

// how a port is routed: 0 undecided, 1 opened directly, 2 through serialosc
extern int serialosc_route(const char *tty_path);
// the path handed to ops.add for a port routed through serialosc ("" otherwise)
extern const char *serialosc_bridge_path(const char *tty_path);
// the udp port serialosc sends this device's keys to (0 if not routed through serialosc)
extern int serialosc_bridge_port(const char *tty_path);

#ifdef __cplusplus
}
#endif
