/*
 * update_test.m: the update check (matron/src/platform/macos/mac_update.m) in a real AppKit
 * app with a window off the screen (it takes no focus)
 *
 * P=matron/src/platform/macos
 * cc -c $P/mac_update.m $P/open_url.c -I $P && \
 * cc -fobjc-arc macos/tests/update_test.m mac_update.o open_url.o -I $P -framework AppKit \
 *    -o /tmp/update_test && /tmp/update_test [--github]
 *
 * each case runs in a process of its own (the check reads its environment once): an app of
 * version 0.1.1 against a made-up latest release (file://). --github also asks GitHub for
 * the real latest release, as an app of version 0.0.1.
 */

#import <Cocoa/Cocoa.h>
#include <spawn.h>
#include <sys/wait.h>
#include "mac_update.h"

extern char **environ;

static int failures = 0;

static void expect(int ok, const char *what) {
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

static NSString *texts(NSView *v) {
    NSMutableString *s = [NSMutableString string];
    if ([v isKindOfClass:[NSTextField class]]) {
        [s appendFormat:@"%@\n", ((NSTextField *)v).stringValue];
    }
    for (NSView *sub in v.subviews) {
        [s appendString:texts(sub)];
    }
    return s;
}

// child: install, wait for the check, print what the sheet says (or "no sheet")
static int child(void) {
    [NSApplication sharedApplication];
    NSApp.activationPolicy = NSApplicationActivationPolicyAccessory;
    NSMenu *bar = [[NSMenu alloc] init];
    NSMenuItem *top = [[NSMenuItem alloc] init];
    top.submenu = [[NSMenu alloc] initWithTitle:@"App"];
    [top.submenu addItemWithTitle:@"About" action:nil keyEquivalent:@""];
    [bar addItem:top];
    NSApp.mainMenu = bar;
    NSWindow *w = [[NSWindow alloc] initWithContentRect:NSMakeRect(-4000, -4000, 400, 300)
                                              styleMask:NSWindowStyleMaskTitled
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    [w orderBack:nil];
    mac_update_install();
    NSMenu *app = top.submenu;
    printf("menu: %s | %s\n", [app itemAtIndex:1].title.UTF8String, [app itemAtIndex:2].title.UTF8String);
    NSDate *until = [NSDate dateWithTimeIntervalSinceNow:20];
    while (w.attachedSheet == nil && [until timeIntervalSinceNow] > 0) {
        [[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
        if ([until timeIntervalSinceNow] < 12 && getenv("UPDATE_TEST_QUICK")) {
            break; // the check is long done: no sheet will come
        }
    }
    if (w.attachedSheet == nil) {
        printf("no sheet\n");
        return 0;
    }
    printf("sheet: %s\n", [texts(w.attachedSheet.contentView) stringByReplacingOccurrencesOfString:@"\n"
                                                                                       withString:@" / "]
                              .UTF8String);
    [w endSheet:w.attachedSheet returnCode:NSAlertSecondButtonReturn]; // Later
    return 0;
}

static NSString *run(NSString *self_path, NSDictionary *env) {
    NSTask *t = [[NSTask alloc] init];
    t.executableURL = [NSURL fileURLWithPath:self_path];
    t.arguments = @[ @"--child" ];
    NSMutableDictionary *e = [NSMutableDictionary dictionaryWithDictionary:[NSProcessInfo processInfo].environment];
    [e addEntriesFromDictionary:env];
    t.environment = e;
    NSPipe *out = [NSPipe pipe];
    t.standardOutput = out;
    [t launchAndReturnError:NULL];
    NSData *d = [out.fileHandleForReading readDataToEndOfFile];
    [t waitUntilExit];
    return [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding];
}

static NSString *fake_app(NSString *dir, NSString *version) {
    NSString *app = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"V%@.app", version]];
    [[NSFileManager defaultManager] createDirectoryAtPath:[app stringByAppendingPathComponent:@"Contents"]
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:NULL];
    [@{@"CFBundleShortVersionString" : version}
        writeToFile:[app stringByAppendingPathComponent:@"Contents/Info.plist"]
         atomically:YES];
    return app;
}

static NSString *fake_release(NSString *dir, NSString *tag) {
    NSString *f = [dir stringByAppendingPathComponent:[tag stringByAppendingString:@".json"]];
    NSDictionary *r = @{
        @"tag_name" : tag,
        @"html_url" : @"https://github.com/ha1dcre/verdandi/releases/tag/test",
        @"body" : @"## What's new\n\n- **update** notes\n- `second` line\n"
    };
    [[NSJSONSerialization dataWithJSONObject:r options:0 error:NULL] writeToFile:f atomically:YES];
    return [[NSURL fileURLWithPath:f] absoluteString];
}

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc > 1 && strcmp(argv[1], "--child") == 0) {
            return child();
        }
        expect(mac_update_compare("0.1.2", "0.1.1") > 0, "0.1.2 newer than 0.1.1");
        expect(mac_update_compare("v0.2", "0.1.9") > 0, "v0.2 newer than 0.1.9");
        expect(mac_update_compare("0.1.10", "0.1.9") > 0, "0.1.10 newer than 0.1.9");
        expect(mac_update_compare("0.1", "0.1.0") == 0, "0.1 same as 0.1.0");
        expect(mac_update_compare("v0.1.1", "0.1.1") == 0, "v0.1.1 same as 0.1.1");
        expect(mac_update_compare("0.1.1", "0.2") < 0, "0.1.1 older than 0.2");
        expect(mac_update_compare("1.0.0-beta", "0.9") > 0, "1.0.0-beta newer than 0.9");

        // the user's own update settings are put back after
        NSUserDefaults *d = [[NSUserDefaults alloc] initWithSuiteName:@"community.verdandi.app"];
        id saved_auto = [d objectForKey:@"updateCheckAutomatically"];
        id saved_skip = [d objectForKey:@"updateSkipVersion"];
        [d removeObjectForKey:@"updateCheckAutomatically"];
        [d removeObjectForKey:@"updateSkipVersion"];

        NSString *me = [NSString stringWithUTF8String:argv[0]];
        if (![me hasPrefix:@"/"]) {
            me = [[[NSFileManager defaultManager] currentDirectoryPath] stringByAppendingPathComponent:me];
        }
        NSString *dir = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID UUID].UUIDString];
        NSString *app = fake_app(dir, @"0.1.1");
        NSString *out;

        out = run(me, @{@"VERDANDI_APP" : app, @"VERDANDI_UPDATE_URL" : fake_release(dir, @"v0.1.2")});
        printf("%s", out.UTF8String);
        expect([out containsString:@"menu: Check for Updates… | Check for Updates Automatically"], "menu items");
        expect([out containsString:@"Verdandi 0.1.2 is out"], "newer release: sheet");
        expect([out containsString:@"You have 0.1.1."], "says the installed version");
        expect([out containsString:@"• update notes"], "release notes without markdown");
        expect([d boolForKey:@"updateCheckAutomatically"], "automatic check stays on");

        out = run(me, @{
            @"VERDANDI_APP" : app,
            @"VERDANDI_UPDATE_URL" : fake_release(dir, @"v0.1.1"),
            @"UPDATE_TEST_QUICK" : @"1"
        });
        expect([out containsString:@"no sheet"], "same version: no sheet");

        [d setObject:@"0.1.2" forKey:@"updateSkipVersion"];
        out = run(me, @{
            @"VERDANDI_APP" : app,
            @"VERDANDI_UPDATE_URL" : fake_release(dir, @"v0.1.2"),
            @"UPDATE_TEST_QUICK" : @"1"
        });
        expect([out containsString:@"no sheet"], "skipped version: no sheet");
        out = run(me, @{@"VERDANDI_APP" : app, @"VERDANDI_UPDATE_URL" : fake_release(dir, @"v0.1.3")});
        expect([out containsString:@"Verdandi 0.1.3 is out"], "a newer one than the skipped: sheet");
        [d removeObjectForKey:@"updateSkipVersion"];

        [d setBool:NO forKey:@"updateCheckAutomatically"];
        out = run(me, @{
            @"VERDANDI_APP" : app,
            @"VERDANDI_UPDATE_URL" : fake_release(dir, @"v0.1.2"),
            @"UPDATE_TEST_QUICK" : @"1"
        });
        expect([out containsString:@"no sheet"], "automatic check off: no sheet");
        [d removeObjectForKey:@"updateCheckAutomatically"];

        out = run(me, @{@"VERDANDI_UPDATE_URL" : fake_release(dir, @"v0.1.2"), @"UPDATE_TEST_QUICK" : @"1"});
        expect([out containsString:@"no sheet"], "build from a checkout (no app): no check");

        if (argc > 1 && strcmp(argv[1], "--github") == 0) {
            out = run(me, @{@"VERDANDI_APP" : fake_app(dir, @"0.0.1")});
            printf("%s", out.UTF8String);
            expect([out containsString:@"is out"] && [out containsString:@"You have 0.0.1."],
                   "GitHub's latest release: sheet");
        }

        [d removeObjectForKey:@"updateCheckAutomatically"];
        [d removeObjectForKey:@"updateSkipVersion"];
        if (saved_auto != nil) {
            [d setObject:saved_auto forKey:@"updateCheckAutomatically"];
        }
        if (saved_skip != nil) {
            [d setObject:saved_skip forKey:@"updateSkipVersion"];
        }
        [[NSFileManager defaultManager] removeItemAtPath:dir error:NULL];
        printf("%s\n", failures == 0 ? "all ok" : "FAILED");
        return failures != 0;
    }
}
