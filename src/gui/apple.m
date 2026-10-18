/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The menu's side of the iOS and macOS apps (docs/APPLE.md): the folders, the game the menu plays, and the windows
   SDL's give dist/apple/Frontend.swift to present its pickers and share sheet over */
#include "gui/apple.h"
#include "gui/i18n.h"
#include <SDL3/SDL.h>
#include <TargetConditionals.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#import <Foundation/Foundation.h>

/* dist/apple/Frontend.swift. window is the SDL window's UIWindow or NSWindow; done gets the path picked, NULL for none */
extern bool snx_apple_pick(void* window, bool folder, void (*done)(void* data, const char* path), void* data);
extern bool snx_apple_share(void* window, const char* path);

void gui_apple_start(void) {
#if TARGET_OS_IPHONE
    @autoreleasepool {
        NSFileManager* files = NSFileManager.defaultManager;
        NSURL* documents = [files URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
        /* the user copies prepared games there, through the Files app or the Finder */
        [files createDirectoryAtURL:[documents URLByAppendingPathComponent:@"games" isDirectory:YES]
            withIntermediateDirectories:YES
                             attributes:nil
                                  error:NULL];
        if (documents) chdir(documents.fileSystemRepresentation);
    }
#endif
}

/* the window AppKit or UIKit has under the SDL one */
static void* native_window(SDL_Window* window) {
    SDL_PropertiesID properties = window ? SDL_GetWindowProperties(window) : 0;
#if TARGET_OS_IPHONE
    return properties ? SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL) : NULL;
#else
    return properties ? SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL) : NULL;
#endif
}

static SDL_DialogFileCallback pick_done;
static void* pick_data;
/* the picker's answer, as an SDL dialog gives it: a list of the one path, empty when none was picked */
static void picked(void* data, const char* path) {
    const char* list[2] = {path, NULL};
    SDL_DialogFileCallback done = pick_done;
    (void)data;
    pick_done = NULL;
    if (done) done(pick_data, list, -1);
}
int gui_apple_pick(SDL_Window* window, int folder, SDL_DialogFileCallback done, void* data, char* message,
                   size_t size) {
    void* native = native_window(window);
    pick_done = done;
    pick_data = data;
    if (native && snx_apple_pick(native, folder != 0, picked, NULL)) return 1;
    pick_done = NULL;
    snprintf(message, size, "%s",
             native ? snx_tr("The document picker did not open.") : snx_tr("No window to show the picker over."));
    return 0;
}

int gui_apple_share(SDL_Window* window, const char* path, char* message, size_t size) {
    void* native = native_window(window);
    if (native && snx_apple_share(native, path)) return 1;
    snprintf(message, size, snx_tr("The log could not be shown: it is at %s"), path);
    return 0;
}

static char** played;
static int played_count;
void gui_apple_play(const char* const* argv) {
    int n = 0, i;
    while (argv[n]) ++n;
    for (i = 0; i < played_count; ++i) free(played[i]);
    free(played);
    played_count = 0;
    played = (char**)calloc((size_t)n + 1, sizeof(*played));
    if (!played) return;
    for (i = 0; i < n; ++i) played[i] = strdup(argv[i]);
    played_count = n;
}
char** gui_apple_played(int* argc) {
    *argc = played_count;
    return played_count ? played : NULL;
}
