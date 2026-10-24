/*
 * mac_menu.m: see mac_menu.h. manual retain/release (not ARC): everything made here
 * lives as long as the app.
 */

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <objc/runtime.h>

#include "app_audio.h"
#include "mac_menu.h"

static void (*item_cb)(int id) = NULL;
static void (*import_cb)(const char *path, int mode) = NULL;
static void (*note_cb)(const char *text) = NULL;
static void (*input_cb)(const char *mode, const char *bundle, const char *name) = NULL;
static int input_now = MAC_INPUT_MIC;

static void note(NSString *text) {
    if (note_cb != NULL) {
        note_cb(text.UTF8String);
    }
}

#define APP_INPUT_MAX 40

@interface NornsMenuTarget : NSObject <NSMenuDelegate>
- (void)act:(NSMenuItem *)item;
- (void)pickApp:(NSMenuItem *)item;
@end

@implementation NornsMenuTarget
- (void)act:(NSMenuItem *)item {
    if (item.tag == MAC_MENU_IMPORT) {
        mac_menu_choose_import();
    } else if (item_cb != NULL) {
        item_cb((int)item.tag);
    }
}

// Devices > Audio Input: the Mac's input, every app, none, or one of the running apps (made
// fresh each time it opens). lua decides (lua/core/menu.lua, _norns.input_chosen) and shows
// it on the MIC button, which says what is checked here
- (void)menuNeedsUpdate:(NSMenu *)menu {
    [menu removeAllItems];
    const char *cur = app_audio_current();
    struct {
        NSString *title;
        const char *mode;
        int input;
    } modes[] = {
        {@"Microphone or Interface", "mic", MAC_INPUT_MIC},
        {@"All Apps (System Audio)", "system", MAC_INPUT_SYSTEM},
        {@"Off", "off", MAC_INPUT_OFF},
    };
    for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++) {
        NSMenuItem *it = [menu addItemWithTitle:modes[i].title action:@selector(pickApp:) keyEquivalent:@""];
        it.target = self;
        it.representedObject = @{@"mode" : @(modes[i].mode)};
        it.state = input_now == modes[i].input ? NSControlStateValueOn : NSControlStateValueOff;
    }
    [menu addItem:[NSMenuItem separatorItem]];
    app_audio_app_t apps[APP_INPUT_MAX];
    int n = app_audio_list(apps, APP_INPUT_MAX);
    for (int i = 0; i < n; i++) {
        NSString *title = [NSString stringWithUTF8String:apps[i].name];
        if (apps[i].playing) {
            title = [title stringByAppendingString:@"  (playing)"];
        }
        NSMenuItem *it = [menu addItemWithTitle:title action:@selector(pickApp:) keyEquivalent:@""];
        it.target = self;
        it.representedObject = @{@"mode" : @"app", @"bundle" : @(apps[i].bundle), @"name" : @(apps[i].name)};
        it.state = input_now == MAC_INPUT_APP && cur != NULL && strcmp(cur, apps[i].bundle) == 0
                       ? NSControlStateValueOn
                       : NSControlStateValueOff;
        NSRunningApplication *ra =
            [NSRunningApplication runningApplicationsWithBundleIdentifier:@(apps[i].bundle)].firstObject;
        if (ra.icon != nil) {
            NSImage *icon = [[ra.icon copy] autorelease];
            icon.size = NSMakeSize(16, 16);
            it.image = icon;
        }
    }
    if (n == 0) {
        NSMenuItem *it = [menu addItemWithTitle:@"no other apps running" action:nil keyEquivalent:@""];
        it.enabled = NO;
    }
}

- (void)pickApp:(NSMenuItem *)item {
    NSDictionary *pick = item.representedObject;
    if (input_cb != NULL && pick != nil) {
        input_cb([pick[@"mode"] UTF8String], [pick[@"bundle"] UTF8String], [pick[@"name"] UTF8String]);
    }
}
@end

static NornsMenuTarget *target = nil;
static NSMenuItem *gif_item = nil;
static NSMenuItem *skin_items[2] = {nil, nil}; // [MAC_SKIN_*]
static int skin_now = MAC_SKIN_DEVICE;

// the look is kept with the app's other settings (mac_update.m)
#define SKIN_DEFAULTS_SUITE @"community.verdandi.app"
#define SKIN_KEY @"skin"

static NSUserDefaults *skin_defaults(void) {
    static NSUserDefaults *d = nil;
    if (d == nil) {
        d = [[NSUserDefaults alloc] initWithSuiteName:SKIN_DEFAULTS_SUITE];
    }
    return d;
}

static void check_skin(void) {
    for (int i = 0; i < 2; i++) {
        skin_items[i].state = i == skin_now ? NSControlStateValueOn : NSControlStateValueOff;
    }
}

static NSMenuItem *add(NSMenu *menu, NSString *title, NSString *key, NSEventModifierFlags mods, int tag) {
    NSMenuItem *item = [menu addItemWithTitle:title action:@selector(act:) keyEquivalent:key];
    item.keyEquivalentModifierMask = mods;
    item.target = target;
    item.tag = tag;
    return item;
}

static void insert(NSMenu *bar, NSMenu *menu, NSInteger at) {
    NSMenuItem *top = [[NSMenuItem alloc] initWithTitle:menu.title action:nil keyEquivalent:@""];
    top.submenu = menu;
    [bar insertItem:top atIndex:at < bar.numberOfItems ? at : bar.numberOfItems];
    [top release];
}

void mac_menu_install(void (*on_item)(int id), void (*on_import)(const char *path, int mode),
                      void (*on_note)(const char *text),
                      void (*on_input)(const char *mode, const char *bundle, const char *name)) {
    item_cb = on_item;
    import_cb = on_import;
    note_cb = on_note;
    input_cb = on_input;
    NSMenu *bar = [NSApp mainMenu];
    if (bar == nil || target != nil) {
        return;
    }
    target = [[NornsMenuTarget alloc] init];
    NSEventModifierFlags cmd = NSEventModifierFlagCommand;
    NSEventModifierFlags shift_cmd = NSEventModifierFlagCommand | NSEventModifierFlagShift;

    NSMenu *file = [[NSMenu alloc] initWithTitle:@"File"];
    add(file, @"Import Audio or Script…", @"o", cmd, MAC_MENU_IMPORT);
    add(file, @"Show dust Folder", @"d", shift_cmd, MAC_MENU_DUST);
    add(file, @"Show Tape Recordings", @"t", shift_cmd, MAC_MENU_TAPE);
    [file addItem:[NSMenuItem separatorItem]];
    add(file, @"Save Screenshot", @"s", shift_cmd, MAC_MENU_SCREENSHOT);
    gif_item = [add(file, @"Record GIF", @"g", shift_cmd, MAC_MENU_GIF) retain];

    // the function keys belong to norns (lua/core/keyboard.lua): no key equivalents here
    NSMenu *devices = [[NSMenu alloc] initWithTitle:@"Devices"];
    add(devices, @"Grid  (F6)", @"", 0, MAC_MENU_GRID);
    add(devices, @"Keyboard  (F7)", @"", 0, MAC_MENU_KEYS);
    add(devices, @"Arc  (F8)", @"", 0, MAC_MENU_ARC);
    if (app_audio_available()) {
        [devices addItem:[NSMenuItem separatorItem]];
        NSMenuItem *from = [devices addItemWithTitle:@"Audio Input" action:nil keyEquivalent:@""];
        NSMenu *apps = [[NSMenu alloc] initWithTitle:@"Audio Input"];
        apps.delegate = target;
        from.submenu = apps;
        [apps release];
    }

    NSMenu *view = [[NSMenu alloc] initWithTitle:@"View"];
    skin_items[MAC_SKIN_DEVICE] = [add(view, @"Device Skin", @"", 0, MAC_MENU_SKIN_DEVICE) retain];
    skin_items[MAC_SKIN_FLAT] = [add(view, @"Dark Flat Skin", @"", 0, MAC_MENU_SKIN_FLAT) retain];
    check_skin();

    // after the app menu, before SDL's Window menu
    insert(bar, file, 1);
    insert(bar, devices, 2);
    insert(bar, view, 3);
    [file release];
    [devices release];
    [view release];
}

void mac_set_app_name(const char *name) {
    [[NSProcessInfo processInfo] setProcessName:@(name)];
}

void mac_menu_set_input(int input) {
    input_now = input;
}

int mac_menu_saved_skin(void) {
    skin_now = [[skin_defaults() stringForKey:SKIN_KEY] isEqualToString:@"flat"] ? MAC_SKIN_FLAT : MAC_SKIN_DEVICE;
    return skin_now;
}

void mac_menu_set_skin(int skin) {
    skin_now = skin == MAC_SKIN_FLAT ? MAC_SKIN_FLAT : MAC_SKIN_DEVICE;
    check_skin();
    [skin_defaults() setObject:skin_now == MAC_SKIN_FLAT ? @"flat" : @"device" forKey:SKIN_KEY];
}

void mac_menu_set_recording(int on) {
    gif_item.title = on ? @"Stop GIF Recording" : @"Record GIF";
}

void mac_menu_choose_import(void) {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = YES;
    panel.allowsMultipleSelection = YES;
    panel.message = @"Audio is linked from dust/audio/mac (it stays where it is); scripts are copied to dust/code.";
    panel.prompt = @"Import";
    NSButton *copy = [NSButton checkboxWithTitle:@"Copy audio into norns instead of linking it" target:nil action:nil];
    copy.state = NSControlStateValueOff;
    panel.accessoryView = copy;
    panel.accessoryViewDisclosed = YES;
    // not modal: the norns window keeps drawing while the panel is open
    [panel beginWithCompletionHandler:^(NSModalResponse result) {
      if (result != NSModalResponseOK || import_cb == NULL) {
          return;
      }
      int mode = copy.state == NSControlStateValueOn ? MAC_IMPORT_COPY : MAC_IMPORT_LINK;
      for (NSURL *url in panel.URLs) {
          if (url.fileURL) {
              import_cb(url.path.fileSystemRepresentation, mode);
          }
      }
    }];
}

int mac_option_held(void) {
    return ([NSEvent modifierFlags] & NSEventModifierFlagOption) != 0;
}

//--- drops beyond the Finder's files
//
// SDL's window takes file URLs (and text). apps that drag something they write only when it
// is dropped (a file promise) or plain audio data get nothing from it: the SDL window class
// gets drop methods of ours that take those and leave the rest to SDL's.

// audio data on the pasteboard: the types norns can play (dust_import.c's list)
static NSArray<NSString *> *audio_data_types(void) {
    static NSArray *types = nil;
    if (types == nil) {
        NSMutableArray *t = [NSMutableArray array];
        for (NSString *ext in @[ @"wav", @"aiff", @"aifc", @"flac", @"ogg", @"mp3" ]) {
            UTType *u = [UTType typeWithFilenameExtension:ext];
            if (u != nil && ![t containsObject:u.identifier]) {
                [t addObject:u.identifier];
            }
        }
        types = [t retain];
    }
    return types;
}

static NSArray<NSString *> *extra_types(void) {
    return [[NSFilePromiseReceiver readableDraggedTypes] arrayByAddingObjectsFromArray:audio_data_types()];
}

static int has_files(NSPasteboard *pb) {
    return [pb canReadObjectForClasses:@[ [NSURL class] ] options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
}

static int has_promise(NSPasteboard *pb) {
    return [pb canReadObjectForClasses:@[ [NSFilePromiseReceiver class] ] options:nil];
}

static NSString *audio_data_type(NSPasteboard *pb) {
    return [pb availableTypeFromArray:audio_data_types()];
}

// ours to take: no Finder file (SDL has those), but a promise or audio data
static int ours(id<NSDraggingInfo> info) {
    NSPasteboard *pb = [info draggingPasteboard];
    return !has_files(pb) && (has_promise(pb) || audio_data_type(pb) != nil);
}

// a folder of its own for each drop: the app names the files
static NSURL *drop_folder(void) {
    NSString *dir = [NSTemporaryDirectory() stringByAppendingPathComponent:@"norns-drops"];
    dir = [dir stringByAppendingPathComponent:[NSUUID UUID].UUIDString];
    if (![[NSFileManager defaultManager] createDirectoryAtPath:dir
                                   withIntermediateDirectories:YES
                                                    attributes:nil
                                                         error:NULL]) {
        return nil;
    }
    return [NSURL fileURLWithPath:dir isDirectory:YES];
}

static void take(NSURL *url) {
    char *path = strdup(url.path.fileSystemRepresentation);
    dispatch_async(dispatch_get_main_queue(), ^{
      if (import_cb != NULL) {
          import_cb(path, MAC_IMPORT_TAKE);
      }
      free(path);
    });
}

static BOOL take_drop(id<NSDraggingInfo> info) {
    static NSOperationQueue *queue = nil;
    if (queue == nil) {
        queue = [[NSOperationQueue alloc] init];
        queue.qualityOfService = NSQualityOfServiceUserInitiated;
    }
    NSPasteboard *pb = [info draggingPasteboard];
    NSURL *dir = drop_folder();
    if (dir == nil) {
        return NO;
    }
    NSArray *promises = [pb readObjectsForClasses:@[ [NSFilePromiseReceiver class] ] options:nil];
    if (promises.count > 0) {
        note(promises.count == 1 ? @"getting the file from the app" : @"getting the files from the app");
        for (NSFilePromiseReceiver *r in promises) {
            [r receivePromisedFilesAtDestination:dir
                                         options:@{}
                                  operationQueue:queue
                                          reader:^(NSURL *url, NSError *error) {
                                            if (error != nil) {
                                                fprintf(stderr, "mac_menu: promised file failed: %s\n",
                                                        error.localizedDescription.UTF8String);
                                                note(@"the app did not hand over the file");
                                                return;
                                            }
                                            take(url);
                                          }];
        }
        return YES;
    }
    NSString *type = audio_data_type(pb);
    NSData *data = type != nil ? [pb dataForType:type] : nil;
    NSString *ext = [UTType typeWithIdentifier:type].preferredFilenameExtension;
    if (data == nil || ext == nil) {
        return NO;
    }
    NSURL *file = [dir URLByAppendingPathComponent:[@"dropped audio" stringByAppendingPathExtension:ext]];
    if (![data writeToURL:file atomically:NO]) {
        return NO;
    }
    take(file);
    return YES;
}

// SDL's own methods, called for everything that is not ours
static IMP sdl_entered = NULL, sdl_updated = NULL, sdl_perform = NULL, sdl_register = NULL;
static Class adopted = Nil;

static NSDragOperation drop_entered(id self, SEL cmd, id<NSDraggingInfo> info) {
    if (ours(info)) {
        return NSDragOperationCopy;
    }
    IMP sdl = cmd == @selector(draggingUpdated:) ? sdl_updated : sdl_entered;
    return ((NSDragOperation(*)(id, SEL, id))sdl)(self, cmd, info);
}

static BOOL drop_perform(id self, SEL cmd, id<NSDraggingInfo> info) {
    if (ours(info)) {
        return take_drop(info);
    }
    BOOL ok = ((BOOL(*)(id, SEL, id))sdl_perform)(self, cmd, info);
    if (!has_files([info draggingPasteboard])) {
        // nothing norns can use: say so, and what the app offered (norns.log)
        fprintf(stderr, "mac_menu: drop without a file, types: %s\n",
                [[[info draggingPasteboard].types componentsJoinedByString:@", "] UTF8String]);
        note(@"nothing to import: the app dragged no audio file");
    }
    return ok;
}

// SDL registers its types again (drop events on and off): ours stay with them
static void drop_register(id self, SEL cmd, NSArray<NSString *> *types) {
    NSMutableArray *all = [NSMutableArray arrayWithArray:types];
    for (NSString *t in extra_types()) {
        if (![all containsObject:t]) {
            [all addObject:t];
        }
    }
    ((void (*)(id, SEL, id))sdl_register)(self, cmd, all);
}

// our method in place of SDL's (or the one it inherits); the old one is returned
static IMP replace(Class c, SEL sel, IMP ours_imp) {
    Method m = class_getInstanceMethod(c, sel);
    if (m == NULL) {
        return NULL;
    }
    IMP old = method_getImplementation(m);
    class_replaceMethod(c, sel, ours_imp, method_getTypeEncoding(m));
    return old;
}

static void adopt_class(Class c) {
    // SDL's drop handling is what this builds on: without it, leave the class alone
    if (class_getInstanceMethod(c, @selector(draggingEntered:)) == NULL ||
        class_getInstanceMethod(c, @selector(performDragOperation:)) == NULL) {
        return;
    }
    sdl_entered = replace(c, @selector(draggingEntered:), (IMP)drop_entered);
    // without its own, AppKit keeps what draggingEntered said
    if (class_getInstanceMethod(c, @selector(draggingUpdated:)) != NULL) {
        sdl_updated = replace(c, @selector(draggingUpdated:), (IMP)drop_entered);
    } else {
        sdl_updated = sdl_entered;
    }
    sdl_perform = replace(c, @selector(performDragOperation:), (IMP)drop_perform);
    sdl_register = replace(c, @selector(registerForDraggedTypes:), (IMP)drop_register);
    adopted = c;
}

void mac_drop_adopt_windows(void) {
    for (NSWindow *w in [NSApp windows]) {
        // the class it claims (KVO hides its own subclass)
        Class c = [w class];
        if (strstr(class_getName(c), "SDL") == NULL) {
            continue;
        }
        if (adopted == Nil) {
            adopt_class(c);
        }
        if (c != adopted) {
            continue;
        }
        // registered before we were there: SDL's types (files, text) once more, through
        // drop_register
        [w registerForDraggedTypes:@[ NSPasteboardTypeFileURL, NSPasteboardTypeString ]];
    }
}

//--- a script's file choice (lua's fileselect) in the Finder's open panel

static NSOpenPanel *choose_panel = nil;
static void (*chosen_cb)(const char *path) = NULL;

void mac_menu_choose_file(const char *dir, const char *dust, void (*done)(const char *path)) {
    NSString *d = dir != NULL && dir[0] != '\0' ? @(dir) : nil;
    // dust itself says nothing about where the file might be: the panel's own last folder then.
    // a folder linked from dust/audio/mac: where it really is
    BOOL anywhere = d == nil || [d.stringByStandardizingPath isEqualToString:@(dust).stringByStandardizingPath];
    NSURL *url = anywhere ? nil : [NSURL fileURLWithPath:d.stringByResolvingSymlinksInPath isDirectory:YES];
    if (choose_panel != nil) {
        if (url != nil) {
            choose_panel.directoryURL = url;
        }
        return;
    }
    chosen_cb = done;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.message = @"Choose a file for the script (audio from elsewhere is linked into dust/audio/mac), "
                    @"or Cancel for norns' own browser.";
    panel.prompt = @"Choose";
    if (url != nil) {
        panel.directoryURL = url;
    }
    choose_panel = [panel retain];
    // not modal: norns keeps running, and its own browser stays usable behind it
    [panel beginWithCompletionHandler:^(NSModalResponse result) {
      NSOpenPanel *p = choose_panel;
      choose_panel = nil;
      if (result == NSModalResponseOK && p.URL.fileURL && chosen_cb != NULL) {
          chosen_cb(p.URL.path.fileSystemRepresentation);
      }
      [p release];
    }];
}

void mac_menu_choose_file_close(void) {
    if (choose_panel != nil) {
        [choose_panel cancel:nil];
    }
}
