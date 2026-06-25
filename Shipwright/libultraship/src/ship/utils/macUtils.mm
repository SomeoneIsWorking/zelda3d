// macUtils.mm
#ifdef __APPLE__
#import "ship/utils/macUtils.h"
// SDL3-MIGRATION: SDL_syswm.h / SDL_SysWMinfo / SDL_GetWindowWMInfo were removed in SDL3.
// The native cocoa NSWindow is now fetched via window properties. (Mac-only: not built on Linux.)
#import <SDL3/SDL.h>
#import <Cocoa/Cocoa.h>

static NSWindow *GetCocoaNSWindow(SDL_Window *window) {
    return (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                       SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
}

//Just a simple function to toggle the native macOS fullscreen.
void toggleNativeMacOSFullscreen(SDL_Window *window) {
    NSWindow *nswindow = GetCocoaNSWindow(window);
    if (nswindow) {
        [nswindow toggleFullScreen:nil];
    }
}

//Just a simple function to check if we are in native macOS fullscreen mode. Needed to avoid the game from crashing
//when going from native to SDL fullscreening modes or getting other forms of breakage.
bool isNativeMacOSFullscreenActive(SDL_Window *window) {
    NSWindow *nswindow = GetCocoaNSWindow(window);
    if (nswindow) {
        return (([nswindow styleMask] & NSWindowStyleMaskFullScreen) == NSWindowStyleMaskFullScreen);
    }
    return false;
}
#endif