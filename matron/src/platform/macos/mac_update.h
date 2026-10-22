/*
 * mac_update.h
 *
 * tells people when there is a newer Verdandi: at launch and once a day after, the latest
 * release on GitHub (MACOS_PRODUCT_REPO) against the app's version. a newer one opens a
 * sheet on the norns window (Download, Later, Skip This Version). the app menu has Check for
 * Updates… and a checkmark for the automatic check.
 *
 * the request is GitHub's public API without an account, cookies or cache; it carries
 * nothing but the app's version (User-Agent).
 *
 * the app's version comes from VERDANDI_APP (the .app the launcher started from, its
 * Info.plist): norns runs from inside the bundle, not as its main executable. without it (a
 * build from a checkout) nothing is checked by itself. VERDANDI_UPDATE_URL replaces the
 * GitHub URL (tests).
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// main thread, after mac_menu_install: the menu items, and the first check
extern void mac_update_install(void);

// version strings ("0.1.2", "v0.2"): <0 when a is older than b, 0 the same, >0 newer
extern int mac_update_compare(const char *a, const char *b);

#ifdef __cplusplus
}
#endif
