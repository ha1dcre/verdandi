/*
 * device_monitor_macos.cc
 *
 * macOS replacement for device_monitor.cc (udev). three sources, all on the device runloop:
 *  - IOKit serial ports (/dev/cu.*): monome grid/arc, crow, other serial devices. monome
 *    ports go through serialosc_bridge.h: opened directly, or reached through serialosc
 *    when it runs (it reads the port itself)
 *  - IOHIDManager: gamepads, joysticks, multi-axis controllers (keyboard: see sdl_bridge)
 *  - CoreMIDI: entities and lone (app) endpoints, diffed on every setup change
 *
 * like the udev version, nothing is added before dev_monitor_scan(); devices that appear
 * between dev_monitor_init() and the scan are picked up by the scan.
 */

#include <CoreMIDI/CoreMIDI.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <ctype.h>
#include <map>
#include <mutex>
#include <stdio.h>
#include <string.h>
#include <string>

#include "device.h"
#include "device_list.h"
#include "device_monitor.h"
#include "events.h"

#include "platform/macos/device_monitor_macos.h"
#include "platform/macos/device_runloop.h"
#include "platform/macos/serialosc_bridge.h"

namespace {

// matches device_midi_coremidi.inc
const MIDIUniqueID NORNS_VIRTUAL_SRC_UID = 0x6e726e73;
const MIDIUniqueID NORNS_VIRTUAL_DST_UID = 0x6e726e64;

struct serial_dev {
    std::string path;
    device_t type;
    std::string name, vendor, model, serial, interface;
};

// all tables are touched on the runloop thread only, except hids (macos_hid_lookup)
bool live = false;
std::map<uint64_t, serial_dev> serials; // IORegistry entry id -> port
std::mutex hids_lock;
std::map<std::string, IOHIDDeviceRef> hids; // path -> retained device
std::map<std::string, std::string> midis;   // path -> name, as added

IONotificationPortRef notify_port = NULL;
io_iterator_t serial_add_it = 0;
io_iterator_t serial_rm_it = 0;
IOHIDManagerRef hid_mgr = NULL;
CFRunLoopTimerRef serialosc_timer = NULL;

//--- helpers

std::string cf_to_string(CFTypeRef ref) {
    if (ref == NULL || CFGetTypeID(ref) != CFStringGetTypeID()) {
        return "";
    }
    char buf[512];
    if (!CFStringGetCString((CFStringRef)ref, buf, sizeof(buf), kCFStringEncodingUTF8)) {
        return "";
    }
    return buf;
}

std::string search_string(io_object_t service, const char *key) {
    CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    CFTypeRef ref = IORegistryEntrySearchCFProperty(service, kIOServicePlane, k, NULL,
                                                    kIORegistryIterateRecursively | kIORegistryIterateParents);
    CFRelease(k);
    std::string s = cf_to_string(ref);
    if (ref != NULL) {
        CFRelease(ref);
    }
    return s;
}

bool search_int(io_object_t service, const char *key, int *out) {
    CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    CFTypeRef ref = IORegistryEntrySearchCFProperty(service, kIOServicePlane, k, NULL,
                                                    kIORegistryIterateRecursively | kIORegistryIterateParents);
    CFRelease(k);
    bool ok = ref != NULL && CFGetTypeID(ref) == CFNumberGetTypeID() &&
              CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, out);
    if (ref != NULL) {
        CFRelease(ref);
    }
    return ok;
}

// udev's ID_VENDOR / ID_MODEL encoding: trim, whitespace runs -> '_', other odd chars -> '_'
std::string udev_encode(const std::string &in) {
    std::string out;
    bool space = false;
    for (char c : in) {
        if (isspace((unsigned char)c)) {
            space = !out.empty();
            continue;
        }
        if (space) {
            out += '_';
            space = false;
        }
        out += (isalnum((unsigned char)c) || strchr("#+-.:=@_", c)) ? c : '_';
    }
    return out;
}

// device_monitor.cc: is_dev_monome_grid / is_dev_crow, on udev-encoded strings
device_t classify_tty(const std::string &vendor, const std::string &model) {
    if (vendor == "monome" && (model == "grid" || model == "arc" || model.compare(0, 6, "monome") == 0)) {
        return DEV_TYPE_MONOME;
    }
    if (model == "crow:_telephone_line") {
        return DEV_TYPE_CROW;
    }
    return DEV_TYPE_SERIAL;
}

//--- serial

void serial_add(const serial_dev &s) {
    switch (s.type) {
    case DEV_TYPE_MONOME:
        fprintf(stderr, "dev_monitor: TTY appears to be a monome\n");
        serialosc_tty_added(s.path.c_str(), s.name.c_str(), s.serial.c_str());
        break;
    case DEV_TYPE_CROW:
        fprintf(stderr, "tty is a crow\n");
        dev_list_add(DEV_TYPE_CROW, s.path.c_str(), strdup(s.name.c_str()), NULL);
        break;
    default: {
        fprintf(stderr, "dev_monitor: unmatched TTY device %s at %s\n", s.name.c_str(), s.path.c_str());
        union event_data *ev = event_data_new(EVENT_SERIAL_CONFIG);
        ev->serial_config.vendor = strdup(s.vendor.c_str());
        ev->serial_config.model = strdup(s.model.c_str());
        ev->serial_config.path = strdup(s.path.c_str());
        ev->serial_config.name = strdup(s.name.c_str());
        ev->serial_config.serial = strdup(s.serial.c_str());
        ev->serial_config.interface = strdup(s.interface.c_str());
        event_post(ev);
        break;
    }
    }
}

void serial_matched(void *refcon, io_iterator_t it) {
    (void)refcon;
    io_object_t service;
    while ((service = IOIteratorNext(it)) != 0) {
        int vid = 0, pid = 0, iface = 0;
        CFTypeRef callout = IORegistryEntryCreateCFProperty(service, CFSTR(kIOCalloutDeviceKey), NULL, 0);
        std::string path = cf_to_string(callout);
        if (callout != NULL) {
            CFRelease(callout);
        }
        // only usb serial ports (skips Bluetooth-Incoming-Port, debug-console, ...)
        if (!path.empty() && search_int(service, "idVendor", &vid)) {
            search_int(service, "idProduct", &pid);
            serial_dev s;
            s.path = path;
            s.name = search_string(service, "USB Product Name");
            std::string manufacturer = search_string(service, "USB Vendor Name");
            char hex[8];
            snprintf(hex, sizeof(hex), "%04x", vid);
            s.vendor = manufacturer.empty() ? hex : udev_encode(manufacturer);
            snprintf(hex, sizeof(hex), "%04x", pid);
            s.model = s.name.empty() ? hex : udev_encode(s.name);
            s.serial = search_string(service, "USB Serial Number");
            if (search_int(service, "bInterfaceNumber", &iface)) {
                snprintf(hex, sizeof(hex), "%02x", iface);
                s.interface = hex;
            }
            s.type = classify_tty(s.vendor, s.model);
            uint64_t id = 0;
            IORegistryEntryGetRegistryEntryID(service, &id);
            fprintf(stderr, "dev_monitor: vendor=%s model=%s\n", s.vendor.c_str(), s.model.c_str());
            serials[id] = s;
            if (live) {
                serial_add(s);
            }
        }
        IOObjectRelease(service);
    }
}

void serial_terminated(void *refcon, io_iterator_t it) {
    (void)refcon;
    io_object_t service;
    while ((service = IOIteratorNext(it)) != 0) {
        uint64_t id = 0;
        IORegistryEntryGetRegistryEntryID(service, &id);
        auto found = serials.find(id);
        if (found != serials.end()) {
            if (live && found->second.type == DEV_TYPE_MONOME) {
                serialosc_tty_removed(found->second.path.c_str());
            } else if (live) {
                dev_list_remove(found->second.type, found->second.path.c_str());
            }
            serials.erase(found);
        }
        IOObjectRelease(service);
    }
}

//--- monome devices for serialosc_bridge

void monome_add(const char *path, const char *name) {
    dev_list_add(DEV_TYPE_MONOME, path, strdup(name), NULL);
}

void monome_remove(const char *path) {
    dev_list_remove(DEV_TYPE_MONOME, path);
}

//--- hid

std::string hid_path(IOHIDDeviceRef dev) {
    uint64_t id = 0;
    IORegistryEntryGetRegistryEntryID(IOHIDDeviceGetService(dev), &id);
    char buf[48];
    snprintf(buf, sizeof(buf), "iohid:%llu", (unsigned long long)id);
    return buf;
}

std::string hid_name(IOHIDDeviceRef dev) {
    std::string name = cf_to_string(IOHIDDeviceGetProperty(dev, CFSTR(kIOHIDProductKey)));
    return name.empty() ? "hid device" : name;
}

void hid_matched(void *ctx, IOReturn result, void *sender, IOHIDDeviceRef dev) {
    (void)ctx;
    (void)result;
    (void)sender;
    std::string path = hid_path(dev);
    {
        std::lock_guard<std::mutex> g(hids_lock);
        if (hids.count(path) != 0) {
            return;
        }
        CFRetain(dev);
        hids[path] = dev;
    }
    if (live) {
        dev_list_add(DEV_TYPE_HID, path.c_str(), strdup(hid_name(dev).c_str()), NULL);
    }
}

void hid_removed(void *ctx, IOReturn result, void *sender, IOHIDDeviceRef dev) {
    (void)ctx;
    (void)result;
    (void)sender;
    std::string path = hid_path(dev);
    if (live) {
        dev_list_remove(DEV_TYPE_HID, path.c_str());
    }
    std::lock_guard<std::mutex> g(hids_lock);
    auto found = hids.find(path);
    if (found != hids.end()) {
        CFRelease(found->second);
        hids.erase(found);
    }
}

CFDictionaryRef hid_match(uint32_t usage) {
    int page = kHIDPage_GenericDesktop;
    int u = (int)usage;
    CFNumberRef p = CFNumberCreate(NULL, kCFNumberIntType, &page);
    CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &u);
    const void *keys[] = {CFSTR(kIOHIDDeviceUsagePageKey), CFSTR(kIOHIDDeviceUsageKey)};
    const void *vals[] = {p, n};
    CFDictionaryRef d =
        CFDictionaryCreate(NULL, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRelease(p);
    CFRelease(n);
    return d;
}

//--- midi

std::string midi_name(MIDIObjectRef obj, CFStringRef prop) {
    CFStringRef s = NULL;
    if (MIDIObjectGetStringProperty(obj, prop, &s) != noErr || s == NULL) {
        return "";
    }
    std::string out = cf_to_string(s);
    CFRelease(s);
    return out;
}

bool midi_offline(MIDIObjectRef obj) {
    SInt32 v = 0;
    return MIDIObjectGetIntegerProperty(obj, kMIDIPropertyOffline, &v) == noErr && v != 0;
}

std::string midi_path(MIDIObjectRef obj) {
    MIDIUniqueID uid = 0;
    MIDIObjectGetIntegerProperty(obj, kMIDIPropertyUniqueID, &uid);
    char buf[32];
    snprintf(buf, sizeof(buf), "coremidi:%d", (int)uid);
    return buf;
}

// a lone endpoint (no entity: another app's virtual port), not our own "norns" port
void midi_scan_endpoint(MIDIEndpointRef ep, std::map<std::string, std::string> &now) {
    MIDIEntityRef entity = 0;
    MIDIEndpointGetEntity(ep, &entity);
    if (entity != 0 || midi_offline(ep)) {
        return;
    }
    MIDIUniqueID uid = 0;
    MIDIObjectGetIntegerProperty(ep, kMIDIPropertyUniqueID, &uid);
    std::string name = midi_name(ep, kMIDIPropertyDisplayName);
    if (uid == NORNS_VIRTUAL_SRC_UID || uid == NORNS_VIRTUAL_DST_UID || name == "norns") {
        return;
    }
    now[midi_path(ep)] = name.empty() ? "midi" : name;
}

void midi_rescan(void) {
    std::map<std::string, std::string> now;
    for (ItemCount i = 0; i < MIDIGetNumberOfDevices(); i++) {
        MIDIDeviceRef dev = MIDIGetDevice(i);
        if (midi_offline(dev)) {
            continue;
        }
        ItemCount n_ent = MIDIDeviceGetNumberOfEntities(dev);
        for (ItemCount e = 0; e < n_ent; e++) {
            MIDIEntityRef ent = MIDIDeviceGetEntity(dev, e);
            if (midi_offline(ent) ||
                (MIDIEntityGetNumberOfSources(ent) == 0 && MIDIEntityGetNumberOfDestinations(ent) == 0)) {
                continue;
            }
            std::string name = n_ent > 1 ? midi_name(ent, kMIDIPropertyDisplayName) : midi_name(dev, kMIDIPropertyName);
            now[midi_path(ent)] = name.empty() ? "midi" : name;
        }
    }
    for (ItemCount i = 0; i < MIDIGetNumberOfSources(); i++) {
        midi_scan_endpoint(MIDIGetSource(i), now);
    }
    for (ItemCount i = 0; i < MIDIGetNumberOfDestinations(); i++) {
        midi_scan_endpoint(MIDIGetDestination(i), now);
    }

    for (auto it = midis.begin(); it != midis.end();) {
        if (now.count(it->first) == 0) {
            fprintf(stderr, "dev_monitor: removing midi device %s\n", it->second.c_str());
            dev_list_remove(DEV_TYPE_MIDI, it->first.c_str());
            it = midis.erase(it);
        } else {
            ++it;
        }
    }
    for (auto &kv : now) {
        if (midis.count(kv.first) == 0) {
            fprintf(stderr, "dev_monitor: adding midi device %s\n", kv.second.c_str());
            midis[kv.first] = kv.second;
            dev_list_add(DEV_TYPE_MIDI, kv.first.c_str(), strdup(kv.second.c_str()), NULL);
        }
    }
}

void midi_setup_changed(void) {
    if (live) {
        midi_rescan();
    }
}

} // namespace

//--- device_monitor.h

void dev_monitor_init(void) {
    coremidi_client(); // starts the runloop and the MIDI client before anything else
    device_runloop_sync(^{
      CFRunLoopRef rl = CFRunLoopGetCurrent();

      notify_port = IONotificationPortCreate(kIOMainPortDefault);
      CFRunLoopAddSource(rl, IONotificationPortGetRunLoopSource(notify_port), kCFRunLoopDefaultMode);
      CFMutableDictionaryRef match = IOServiceMatching(kIOSerialBSDServiceValue);
      CFDictionarySetValue(match, CFSTR(kIOSerialBSDTypeKey), CFSTR(kIOSerialBSDAllTypes));
      CFRetain(match); // each IOServiceAddMatchingNotification consumes one reference
      IOServiceAddMatchingNotification(notify_port, kIOFirstMatchNotification, match, serial_matched, NULL,
                                       &serial_add_it);
      IOServiceAddMatchingNotification(notify_port, kIOTerminatedNotification, match, serial_terminated, NULL,
                                       &serial_rm_it);
      // draining the iterators arms the notifications
      serial_matched(NULL, serial_add_it);
      serial_terminated(NULL, serial_rm_it);

      hid_mgr = IOHIDManagerCreate(NULL, kIOHIDOptionsTypeNone);
      CFDictionaryRef m[] = {hid_match(kHIDUsage_GD_Joystick), hid_match(kHIDUsage_GD_GamePad),
                             hid_match(kHIDUsage_GD_MultiAxisController)};
      CFArrayRef matches = CFArrayCreate(NULL, (const void **)m, 3, &kCFTypeArrayCallBacks);
      IOHIDManagerSetDeviceMatchingMultiple(hid_mgr, matches);
      CFRelease(matches);
      for (CFDictionaryRef d : m) {
          CFRelease(d);
      }
      IOHIDManagerRegisterDeviceMatchingCallback(hid_mgr, hid_matched, NULL);
      IOHIDManagerRegisterDeviceRemovalCallback(hid_mgr, hid_removed, NULL);
      IOHIDManagerScheduleWithRunLoop(hid_mgr, rl, kCFRunLoopDefaultMode);
      IOReturn r = IOHIDManagerOpen(hid_mgr, kIOHIDOptionsTypeNone);
      if (r != kIOReturnSuccess) {
          fprintf(stderr, "dev_monitor: IOHIDManagerOpen failed (0x%x)\n", r);
      }

      coremidi_set_setup_changed(midi_setup_changed);

      serialosc_ops_t ops = {monome_add, monome_remove};
      serialosc_init(&ops);
      serialosc_timer = CFRunLoopTimerCreateWithHandler(NULL, CFAbsoluteTimeGetCurrent() + 0.1, 0.1, 0, 0,
                                                        ^(CFRunLoopTimerRef t) {
                                                          (void)t;
                                                          if (live) {
                                                              serialosc_tick();
                                                          }
                                                        });
      CFRunLoopAddTimer(rl, serialosc_timer, kCFRunLoopDefaultMode);
    });
}

void dev_monitor_deinit(void) {
    device_runloop_sync(^{
      coremidi_set_setup_changed(NULL);
      if (serialosc_timer != NULL) {
          CFRunLoopTimerInvalidate(serialosc_timer);
          CFRelease(serialosc_timer);
          serialosc_timer = NULL;
      }
      serialosc_deinit();
      if (hid_mgr != NULL) {
          IOHIDManagerClose(hid_mgr, kIOHIDOptionsTypeNone);
          CFRelease(hid_mgr);
          hid_mgr = NULL;
      }
      if (serial_add_it != 0) {
          IOObjectRelease(serial_add_it);
      }
      if (serial_rm_it != 0) {
          IOObjectRelease(serial_rm_it);
      }
      if (notify_port != NULL) {
          IONotificationPortDestroy(notify_port);
          notify_port = NULL;
      }
    });
}

int dev_monitor_scan(void) {
    device_runloop_sync(^{
      live = true;
      // the computer keyboard, through the norns window
      dev_list_add(DEV_TYPE_HID, MACOS_SDL_KEYBOARD_PATH, strdup("keyboard"), NULL);
      for (auto &kv : serials) {
          serial_add(kv.second);
      }
      std::map<std::string, IOHIDDeviceRef> snapshot;
      {
          std::lock_guard<std::mutex> g(hids_lock);
          snapshot = hids;
      }
      for (auto &kv : snapshot) {
          dev_list_add(DEV_TYPE_HID, kv.first.c_str(), strdup(hid_name(kv.second).c_str()), NULL);
      }
      midi_rescan();
    });
    return 0;
}

//--- device_monitor_macos.h

void *macos_hid_lookup(const char *path) {
    if (path == NULL) {
        return NULL;
    }
    std::lock_guard<std::mutex> g(hids_lock);
    auto found = hids.find(path);
    return found == hids.end() ? NULL : (void *)found->second;
}
