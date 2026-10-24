/*
 * app_input_test.m: another app's sound arrives in jack through build/macos/app_input
 *
 * cc -fobjc-arc macos/tests/app_input_test.m -I <deps>/include $(pkg-config --cflags --libs jack) \
 *    -framework AudioToolbox -framework Foundation -o /tmp/app_input_test
 * /tmp/app_input_test build/macos/app_input
 *
 * on a jack server of its own (dummy driver, the norns one is not touched) it starts a tone
 * app (this program again, inside a small .app with its own bundle id, playing 440 Hz quietly
 * on the default output), app_input on that bundle id, and measures what comes out of
 * app_input:out_1/2: level and pitch. then the same with --system (every app), and that a
 * process named like one of norns' own (sclang) is left out of it. the first run asks for permission to record other apps'
 * audio (for the terminal it runs in): without it the tap stays silent.
 */

#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>
#include <jack/jack.h>
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>

#define BUNDLE_ID "org.norns-macos.app-input-test-tone"
#define SERVER "norns_app_input_test"
#define TONE_HZ 440.0
#define TONE_AMP 0.03 // -30 dBFS: quiet (it plays on while tapped)

//--- the tone app

static double phase = 0;

static void fill(void *arg, AudioQueueRef q, AudioQueueBufferRef b) {
    (void)arg;
    float *s = b->mAudioData;
    UInt32 frames = b->mAudioDataBytesCapacity / (2 * sizeof(float));
    for (UInt32 i = 0; i < frames; i++) {
        float v = (float)(TONE_AMP * sin(phase));
        s[2 * i] = s[2 * i + 1] = v;
        phase += 2 * M_PI * TONE_HZ / 48000.0;
    }
    b->mAudioDataByteSize = frames * 2 * sizeof(float);
    AudioQueueEnqueueBuffer(q, b, 0, NULL);
}

static int run_tone(void) {
    AudioStreamBasicDescription f = {0};
    f.mSampleRate = 48000;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    f.mChannelsPerFrame = 2;
    f.mBitsPerChannel = 32;
    f.mBytesPerFrame = f.mBytesPerPacket = 8;
    f.mFramesPerPacket = 1;
    AudioQueueRef q;
    if (AudioQueueNewOutput(&f, fill, NULL, NULL, NULL, 0, &q) != noErr) {
        return 1;
    }
    for (int i = 0; i < 3; i++) {
        AudioQueueBufferRef b;
        AudioQueueAllocateBuffer(q, 4096 * 8, &b);
        fill(NULL, q, b);
    }
    AudioQueueStart(q, NULL);
    CFRunLoopRun();
    return 0;
}

//--- the meter

static jack_port_t *in_port[2];
static atomic_int counting = 0;
static double sum_sq[2];
static long frames_seen = 0, crossings = 0;
static float last = 0;

static int process(jack_nframes_t n, void *arg) {
    (void)arg;
    if (!atomic_load(&counting)) {
        return 0;
    }
    for (int c = 0; c < 2; c++) {
        const float *s = jack_port_get_buffer(in_port[c], n);
        for (jack_nframes_t i = 0; i < n; i++) {
            sum_sq[c] += s[i] * s[i];
            if (c == 0) {
                if ((last < 0) != (s[i] < 0)) {
                    crossings++;
                }
                last = s[i];
            }
        }
    }
    frames_seen += n;
    return 0;
}

static int failures = 0;
#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            failures++;                                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

static NSTask *start_logged(NSString *path, NSArray *args, NSDictionary *env, NSString *log) {
    NSTask *t = [NSTask new];
    if (log != nil) {
        [[NSFileManager defaultManager] createFileAtPath:log contents:nil attributes:nil];
        t.standardError = [NSFileHandle fileHandleForWritingAtPath:log];
    }
    t.launchPath = path;
    t.arguments = args;
    NSMutableDictionary *e = [NSProcessInfo.processInfo.environment mutableCopy];
    [e addEntriesFromDictionary:env];
    t.environment = e;
    [t launch];
    return t;
}

static NSTask *start(NSString *path, NSArray *args, NSDictionary *env) {
    return start_logged(path, args, env, nil);
}

static BOOL logged(NSString *log, NSString *what) {
    return [[NSString stringWithContentsOfFile:log encoding:NSUTF8StringEncoding error:NULL] containsString:what];
}

// <dir>/tone.app/Contents/MacOS/tone = this program, with a bundle id of its own
static NSString *make_tone_app(NSString *dir, const char *self_path) {
    NSString *app = [dir stringByAppendingPathComponent:@"tone.app"];
    NSString *macos = [app stringByAppendingPathComponent:@"Contents/MacOS"];
    [[NSFileManager defaultManager] createDirectoryAtPath:macos withIntermediateDirectories:YES attributes:nil error:NULL];
    NSString *exe = [macos stringByAppendingPathComponent:@"tone"];
    [[NSFileManager defaultManager] removeItemAtPath:exe error:NULL];
    [[NSFileManager defaultManager] copyItemAtPath:@(self_path) toPath:exe error:NULL];
    NSDictionary *plist = @{
        @"CFBundleIdentifier" : @BUNDLE_ID,
        @"CFBundleExecutable" : @"tone",
        @"CFBundleName" : @"tone",
        @"CFBundlePackageType" : @"APPL",
    };
    [plist writeToFile:[app stringByAppendingPathComponent:@"Contents/Info.plist"] atomically:YES];
    return exe;
}

static void measure(double seconds, double rms[2], double *hz);

// until something arrives (a new binary's first tap takes macOS a while), at most 5 s
static void wait_for_sound(void) {
    double rms[2], hz;
    for (int i = 0; i < 25; i++) {
        measure(0.2, rms, &hz);
        if (rms[0] > 1e-3) {
            return;
        }
    }
}

static void measure(double seconds, double rms[2], double *hz) {
    sum_sq[0] = sum_sq[1] = 0;
    frames_seen = crossings = 0;
    atomic_store(&counting, 1);
    usleep((useconds_t)(seconds * 1e6));
    atomic_store(&counting, 0);
    usleep(20000);
    for (int c = 0; c < 2; c++) {
        rms[c] = frames_seen > 0 ? sqrt(sum_sq[c] / frames_seen) : 0;
    }
    *hz = frames_seen > 0 ? crossings / 2.0 / (frames_seen / 48000.0) : 0;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc >= 2 && strcmp(argv[1], "tone") == 0) {
            return run_tone();
        }
        if ([NSBundle.mainBundle.bundleIdentifier isEqualToString:@BUNDLE_ID]) {
            return run_tone();
        }
        if (argc < 2) {
            fprintf(stderr, "usage: app_input_test <path to app_input>\n");
            return 2;
        }
        NSString *dir = [NSTemporaryDirectory() stringByAppendingPathComponent:@"app_input_test"];
        NSString *tone_exe = make_tone_app(dir, argv[0]);
        NSDictionary *env = @{@"JACK_DEFAULT_SERVER" : @SERVER};

        NSTask *jackd = start([NSFileManager.defaultManager isExecutableFileAtPath:@"/opt/homebrew/bin/jackd"] ? @"/opt/homebrew/bin/jackd" : @"/usr/local/bin/jackd", @[ @"-n", @SERVER, @"-d", @"dummy", @"-r", @"48000", @"-p", @"256" ], @{});
        setenv("JACK_DEFAULT_SERVER", SERVER, 1);
        jack_client_t *c = NULL;
        for (int i = 0; i < 50 && c == NULL; i++) {
            usleep(100000);
            jack_status_t st;
            c = jack_client_open("meter", JackNoStartServer, &st);
        }
        if (c == NULL) {
            printf("FAIL: no test jack server\n");
            [jackd terminate];
            return 1;
        }
        in_port[0] = jack_port_register(c, "in_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        in_port[1] = jack_port_register(c, "in_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        jack_set_process_callback(c, process, NULL);
        jack_activate(c);

        // app_input before the app: it waits for it
        NSTask *tap = start(@(argv[1]), @[ @BUNDLE_ID ], env);
        usleep(800000);
        CHECK(tap.running, "app_input waits for an app that is not there yet");
        NSTask *tone = start(tone_exe, @[], @{});
        // the tap follows within half a second; then the first audio
        int connected = 0;
        for (int i = 0; i < 40 && !connected; i++) {
            usleep(100000);
            connected = jack_connect(c, "app_input:out_1", "meter:in_1") == 0 &&
                        jack_connect(c, "app_input:out_2", "meter:in_2") == 0;
        }
        CHECK(connected, "app_input has its jack ports");
        wait_for_sound();
        usleep(300000);
        double rms[2], hz;
        measure(2.0, rms, &hz);
        printf("app input: rms %.4f %.4f (tone %.4f), %.1f Hz\n", rms[0], rms[1], TONE_AMP / sqrt(2), hz);
        CHECK(rms[0] > 0.5 * TONE_AMP / sqrt(2) && rms[0] < 2 * TONE_AMP / sqrt(2),
              "the tone arrives at its level (rms %.4f; 0 = no permission to record other apps)", rms[0]);
        CHECK(fabs(rms[0] - rms[1]) < 0.2 * rms[0] + 1e-6, "both channels");
        CHECK(fabs(hz - TONE_HZ) < 5, "at its pitch: resampled right (%.1f Hz)", hz);

        // the app quits: silence, app_input waits on
        [tone terminate];
        [tone waitUntilExit];
        usleep(1200000);
        measure(1.0, rms, &hz);
        CHECK(rms[0] < 1e-4, "silent once the app is gone (%.5f)", rms[0]);
        CHECK(tap.running, "app_input stays for the app to come back");

        // SIGTERM: it leaves jack
        [tap terminate];
        [tap waitUntilExit];
        CHECK(tap.terminationStatus == 0, "ends cleanly on SIGTERM (%d)", tap.terminationStatus);
        CHECK(jack_port_by_name(c, "app_input:out_1") == NULL, "its ports are gone");

        // --system: every app, but not norns' own processes
        NSString *log = [dir stringByAppendingPathComponent:@"app_input.log"];
        tap = start_logged(@(argv[1]), @[ @"--system" ], env, log);
        tone = start(tone_exe, @[], @{});
        connected = 0;
        for (int i = 0; i < 40 && !connected; i++) {
            usleep(100000);
            connected = jack_connect(c, "app_input:out_1", "meter:in_1") == 0 &&
                        jack_connect(c, "app_input:out_2", "meter:in_2") == 0;
        }
        wait_for_sound();
        usleep(300000);
        measure(2.0, rms, &hz);
        printf("system input: rms %.4f, %.1f Hz\n", rms[0], hz);
        CHECK(rms[0] > 0.5 * TONE_AMP / sqrt(2) && fabs(hz - TONE_HZ) < 5,
              "--system hears the app (rms %.4f, %.1f Hz; other sound playing on the Mac adds to it)", rms[0], hz);
        // the AUv3 takes norns' input (auv3_bridge unplugs everything from crone): no tap, the
        // apps play directly; plugged back in: the tap again
        jack_disconnect(c, "app_input:out_1", "meter:in_1");
        jack_disconnect(c, "app_input:out_2", "meter:in_2");
        usleep(1500000);
        CHECK(logged(log, @"paused"), "unplugged: the tap pauses");
        jack_connect(c, "app_input:out_1", "meter:in_1");
        jack_connect(c, "app_input:out_2", "meter:in_2");
        usleep(1000000);
        CHECK(logged(log, @"resumed"), "plugged back in: it resumes");
        wait_for_sound();
        measure(1.0, rms, &hz);
        CHECK(rms[0] > 0.5 * TONE_AMP / sqrt(2) && fabs(hz - TONE_HZ) < 5, "and the tone arrives again (rms %.4f, %.1f Hz)",
              rms[0], hz);
        [tone terminate];
        [tone waitUntilExit];
        // the same tone from a process named like one of norns' (sclang): left out
        NSString *own_exe = [dir stringByAppendingPathComponent:@"sclang"];
        [[NSFileManager defaultManager] removeItemAtPath:own_exe error:NULL];
        [[NSFileManager defaultManager] copyItemAtPath:@(argv[0]) toPath:own_exe error:NULL];
        NSTask *own = start(own_exe, @[ @"tone" ], @{});
        usleep(2500000);
        measure(1.5, rms, &hz);
        printf("system input, norns' own process playing: rms %.5f\n", rms[0]);
        CHECK(rms[0] < 1e-3, "norns' own processes are left out (rms %.5f)", rms[0]);
        [own terminate];
        [own waitUntilExit];
        [tap terminate];
        [tap waitUntilExit];
        CHECK(tap.terminationStatus == 0, "--system ends cleanly (%d)", tap.terminationStatus);

        jack_client_close(c);
        [jackd terminate];
        [jackd waitUntilExit];
        printf("%s: %d failures\n", failures ? "FAILED" : "app input ok", failures);
        return failures != 0;
    }
}
