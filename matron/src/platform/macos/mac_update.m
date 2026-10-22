/*
 * mac_update.m: see mac_update.h. manual retain/release (not ARC), as mac_menu.m.
 */

#import <Cocoa/Cocoa.h>
#include <objc/runtime.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "mac_update.h"
#include "open_url.h"
#include "product.h"

#define DEFAULTS_SUITE @"community.verdandi.app"
#define KEY_AUTO @"updateCheckAutomatically"
#define KEY_SKIP @"updateSkipVersion"
// while the app runs, after the check at launch
#define CHECK_EVERY (24 * 60 * 60)

static NSUserDefaults *defaults = nil;
static NSString *current = nil; // the app's version, nil in a build from a checkout
static NSMenuItem *auto_item = nil;
static NSMutableSet *shown = nil; // offered in this run already: Later waits for the next launch
static BOOL checking = NO;

int mac_update_compare(const char *a, const char *b) {
    // leading v, then numbers between dots; anything after a number's digits (-beta) is ignored
    if (*a == 'v' || *a == 'V') {
        a++;
    }
    if (*b == 'v' || *b == 'V') {
        b++;
    }
    while (*a != '\0' || *b != '\0') {
        long x = strtol(a, (char **)&a, 10);
        long y = strtol(b, (char **)&b, 10);
        if (x != y) {
            return x < y ? -1 : 1;
        }
        while (*a != '\0' && *a != '.') {
            a++;
        }
        while (*b != '\0' && *b != '.') {
            b++;
        }
        if (*a == '.') {
            a++;
        }
        if (*b == '.') {
            b++;
        }
    }
    return 0;
}

static BOOL check_automatically(void) {
    return [defaults objectForKey:KEY_AUTO] == nil || [defaults boolForKey:KEY_AUTO];
}

static NSString *app_version(void) {
    const char *app = getenv("VERDANDI_APP");
    if (app == NULL || app[0] == '\0') {
        return nil;
    }
    NSString *plist = [@(app) stringByAppendingPathComponent:@"Contents/Info.plist"];
    NSString *v = [NSDictionary dictionaryWithContentsOfFile:plist][@"CFBundleShortVersionString"];
    return [v isKindOfClass:[NSString class]] && v.length > 0 ? [v copy] : nil;
}

static NSURL *latest_url(void) {
    const char *url = getenv("VERDANDI_UPDATE_URL");
    if (url != NULL && url[0] != '\0') {
        return [NSURL URLWithString:@(url)];
    }
    return [NSURL URLWithString:@"https://api.github.com/repos/" MACOS_PRODUCT_REPO @"/releases/latest"];
}

// the norns window (or whichever of ours is in front): the sheet goes there, so norns keeps
// running and drawing while it is open
static NSWindow *sheet_window(void) {
    NSWindow *w = [NSApp keyWindow] ?: [NSApp mainWindow];
    if (w != nil && w.visible && w.attachedSheet == nil) {
        return w;
    }
    // norns in the background: its window (SDL's), else any of ours
    NSWindow *any = nil;
    for (NSWindow *o in [NSApp windows]) {
        if (!o.visible || o.attachedSheet != nil || [o isKindOfClass:[NSPanel class]]) {
            continue;
        }
        if (strstr(class_getName([o class]), "SDL") != NULL) {
            return o;
        }
        any = any ?: o;
    }
    return any;
}

static void show(NSAlert *alert, void (^done)(NSModalResponse)) {
    NSWindow *w = sheet_window();
    if (w != nil) {
        [alert beginSheetModalForWindow:w completionHandler:done];
    } else {
        done([alert runModal]);
    }
}

// the release notes: GitHub's markdown, without its marks, and not too long for an alert
static NSString *notes(NSString *body) {
    if (![body isKindOfClass:[NSString class]]) {
        return @"";
    }
    NSMutableArray *lines = [NSMutableArray array];
    NSUInteger length = 0;
    for (NSString *raw in [body componentsSeparatedByCharactersInSet:[NSCharacterSet newlineCharacterSet]]) {
        NSString *l = [raw stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        while ([l hasPrefix:@"#"]) {
            l = [[l substringFromIndex:1] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        }
        if ([l hasPrefix:@"- "] || [l hasPrefix:@"* "]) {
            l = [@"• " stringByAppendingString:[l substringFromIndex:2]];
        }
        l = [[l stringByReplacingOccurrencesOfString:@"**" withString:@""] stringByReplacingOccurrencesOfString:@"`"
                                                                                                    withString:@""];
        if (l.length == 0 && (lines.count == 0 || [lines.lastObject length] == 0)) {
            continue;
        }
        if (length + l.length > 600) {
            [lines addObject:@"…"];
            break;
        }
        [lines addObject:l];
        length += l.length;
    }
    while (lines.count > 0 && [lines.lastObject length] == 0) {
        [lines removeLastObject];
    }
    return [lines componentsJoinedByString:@"\n"];
}

static void offer(NSString *version, NSString *page, NSString *body) {
    [shown addObject:version];
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    alert.messageText = [NSString stringWithFormat:@"%@ %@ is out", @MACOS_PRODUCT, version];
    NSString *text = [NSString stringWithFormat:@"You have %@.", current];
    NSString *n = notes(body);
    if (n.length > 0) {
        text = [NSString stringWithFormat:@"%@\n\n%@", text, n];
    }
    alert.informativeText = text;
    [alert addButtonWithTitle:@"Download"];
    [alert addButtonWithTitle:@"Later"];
    [alert addButtonWithTitle:@"Skip This Version"];
    alert.showsSuppressionButton = YES;
    alert.suppressionButton.title = @"Check for updates automatically";
    alert.suppressionButton.state = check_automatically() ? NSControlStateValueOn : NSControlStateValueOff;
    show(alert, ^(NSModalResponse r) {
      [defaults setBool:alert.suppressionButton.state == NSControlStateValueOn forKey:KEY_AUTO];
      auto_item.state = check_automatically() ? NSControlStateValueOn : NSControlStateValueOff;
      if (r == NSAlertFirstButtonReturn) {
          macos_open_url(page.UTF8String);
      } else if (r == NSAlertThirdButtonReturn) {
          [defaults setObject:version forKey:KEY_SKIP];
      }
    });
}

static void say(NSString *title, NSString *text) {
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    alert.messageText = title;
    alert.informativeText = text;
    show(alert, ^(NSModalResponse r) {
      (void)r;
    });
}

// main thread. by_hand: from the menu, which always answers (also: up to date, no network)
// and offers a skipped version again
static void check(BOOL by_hand) {
    if (current == nil) {
        if (by_hand) {
            say(@"No version to compare",
                @"This is a build from a source checkout: it is as new as the checkout. "
                @"The app checks for updates.");
        }
        return;
    }
    if (checking) {
        return;
    }
    NSURL *url = latest_url();
    if (url == nil) {
        return;
    }
    checking = YES;
    // nothing of the user's on the way: no cookies, no cache, no stored credentials
    NSURLSession *session =
        [NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration ephemeralSessionConfiguration]];
    NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:url
                                                       cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                   timeoutInterval:20];
    [req setValue:@"application/vnd.github+json" forHTTPHeaderField:@"Accept"];
    [req setValue:[NSString stringWithFormat:@"%@/%@", @MACOS_PRODUCT, current] forHTTPHeaderField:@"User-Agent"];
    NSURLSessionDataTask *task = [session
        dataTaskWithRequest:req
          completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
            NSInteger status = [response isKindOfClass:[NSHTTPURLResponse class]]
                                   ? ((NSHTTPURLResponse *)response).statusCode
                                   : (response != nil ? 200 : 0); // file:// (tests)
            NSDictionary *release = nil;
            if (error == nil && status == 200 && data != nil) {
                id json = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
                release = [json isKindOfClass:[NSDictionary class]] ? json : nil;
            }
            NSString *tag = release[@"tag_name"];
            NSString *page = release[@"html_url"];
            NSString *body = release[@"body"];
            [tag retain];
            [page retain];
            [body retain];
            NSString *failed = error != nil ? [error.localizedDescription retain] : nil;
            dispatch_async(dispatch_get_main_queue(), ^{
              checking = NO;
              if (![tag isKindOfClass:[NSString class]] || ![page isKindOfClass:[NSString class]] ||
                  ![page hasPrefix:@"https://"]) {
                  fprintf(stderr, "mac_update: no release (%ld%s%s)\n", (long)status, failed ? ": " : "",
                          failed ? failed.UTF8String : "");
                  if (by_hand) {
                      say(@"Could not check for updates",
                          failed ?: @"GitHub did not answer with a release. Try again later.");
                  }
              } else {
                  NSString *version = [tag hasPrefix:@"v"] || [tag hasPrefix:@"V"] ? [tag substringFromIndex:1] : tag;
                  BOOL newer = mac_update_compare(version.UTF8String, current.UTF8String) > 0;
                  fprintf(stderr, "mac_update: latest %s, this %s\n", version.UTF8String, current.UTF8String);
                  if (newer && (by_hand || (![version isEqualToString:[defaults stringForKey:KEY_SKIP]] &&
                                            ![shown containsObject:version]))) {
                      offer(version, page, body);
                  } else if (by_hand) {
                      say(@"You have the newest version",
                          [NSString stringWithFormat:@"%@ %@ is the latest release.", @MACOS_PRODUCT, current]);
                  }
              }
              [tag release];
              [page release];
              [body release];
              [failed release];
            });
          }];
    [task resume];
    [session finishTasksAndInvalidate];
}

static void check_later(void) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)CHECK_EVERY * NSEC_PER_SEC), dispatch_get_main_queue(),
                   ^{
                     if (check_automatically()) {
                         check(NO);
                     }
                     check_later();
                   });
}

@interface VerdandiUpdateTarget : NSObject
@end

@implementation VerdandiUpdateTarget
- (void)checkNow:(NSMenuItem *)item {
    (void)item;
    check(YES);
}
- (void)toggleAuto:(NSMenuItem *)item {
    [defaults setBool:!check_automatically() forKey:KEY_AUTO];
    item.state = check_automatically() ? NSControlStateValueOn : NSControlStateValueOff;
}
@end

void mac_update_install(void) {
    static VerdandiUpdateTarget *target = nil;
    if (target != nil) {
        return;
    }
    target = [[VerdandiUpdateTarget alloc] init];
    defaults = [[NSUserDefaults alloc] initWithSuiteName:DEFAULTS_SUITE];
    shown = [[NSMutableSet alloc] init];
    current = app_version();

    // the app menu (SDL's): after About
    NSMenu *app = [NSApp mainMenu].itemArray.firstObject.submenu;
    if (app != nil) {
        NSInteger at = app.numberOfItems > 0 ? 1 : 0;
        NSMenuItem *now = [[[NSMenuItem alloc] initWithTitle:@"Check for Updates…"
                                                      action:@selector(checkNow:)
                                               keyEquivalent:@""] autorelease];
        now.target = target;
        auto_item = [[NSMenuItem alloc] initWithTitle:@"Check for Updates Automatically"
                                               action:@selector(toggleAuto:)
                                        keyEquivalent:@""];
        auto_item.target = target;
        auto_item.state = check_automatically() ? NSControlStateValueOn : NSControlStateValueOff;
        [app insertItem:now atIndex:at];
        [app insertItem:auto_item atIndex:at + 1];
        [app insertItem:[NSMenuItem separatorItem] atIndex:at + 2];
    }

    // a few seconds in: the window is up, and the first start has other things to do
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
      if (check_automatically()) {
          check(NO);
      }
    });
    check_later();
}
