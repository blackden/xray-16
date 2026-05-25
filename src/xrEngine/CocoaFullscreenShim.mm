// CocoaFullscreenShim — see CocoaFullscreenShim.hpp for design rationale.
//
// Implementation notes:
//   - Singleton: one observer alive at a time. Window rebind tears down the
//     previous observation before installing the new one.
//   - KVO callback re-asserts the mask only if the FullScreenPrimary bit is
//     missing; otherwise we'd ping-pong with our own set call.
//   - Logging via stderr write() avoids xrCore header collisions with
//     Foundation under ObjC++ (same pattern as macos_cocoa_shim.mm). The
//     launcher captures stderr → openxray_ragnar.log via `>> ... 2>&1`.

#include "CocoaFullscreenShim.hpp"

#if defined(XR_PLATFORM_APPLE) || defined(__APPLE__)

#include <unistd.h>
#include <stdio.h>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

static const NSWindowCollectionBehavior kDesiredBits =
    NSWindowCollectionBehaviorFullScreenPrimary;

@interface OpenXRayFullscreenGuard : NSObject
@property (nonatomic, weak) NSWindow *guardedWindow;
@property (nonatomic, assign) BOOL reassertInFlight;
- (void)attachTo:(NSWindow *)win;
- (void)detach;
- (void)forceAssert;
@end

@implementation OpenXRayFullscreenGuard

- (void)attachTo:(NSWindow *)win
{
    if (self.guardedWindow == win)
        return;
    [self detach];
    if (!win)
        return;
    self.guardedWindow = win;
    [win addObserver:self
          forKeyPath:@"collectionBehavior"
             options:(NSKeyValueObservingOptionNew | NSKeyValueObservingOptionOld)
             context:NULL];

    // Seed the desired bits immediately on attach.
    [self forceAssert];

    char buf[160];
    int n = snprintf(buf, sizeof buf,
        "==> [cocoa-guard] attached to NSWindow=%p, initial cb=0x%lx (after seed)\n",
        (__bridge void *)win, (unsigned long)win.collectionBehavior);
    if (n > 0)
        ::write(STDERR_FILENO, buf, (size_t)n);
}

- (void)detach
{
    NSWindow *w = self.guardedWindow;
    if (!w)
        return;
    @try { [w removeObserver:self forKeyPath:@"collectionBehavior"]; }
    @catch (NSException *) { /* not observing — fine */ }
    self.guardedWindow = nil;
}

- (void)forceAssert
{
    NSWindow *w = self.guardedWindow;
    if (!w)
        return;
    NSWindowCollectionBehavior cur = w.collectionBehavior;
    if ((cur & kDesiredBits) == kDesiredBits)
        return;
    self.reassertInFlight = YES;
    w.collectionBehavior = cur | kDesiredBits;
    self.reassertInFlight = NO;
}

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id> *)change
                       context:(void *)context
{
    if (![keyPath isEqualToString:@"collectionBehavior"])
        return;
    if (self.reassertInFlight)
        return; // our own write, ignore

    NSNumber *oldVal = change[NSKeyValueChangeOldKey];
    NSNumber *newVal = change[NSKeyValueChangeNewKey];
    NSWindowCollectionBehavior oldMask = oldVal ? oldVal.unsignedLongValue : 0;
    NSWindowCollectionBehavior newMask = newVal ? newVal.unsignedLongValue : 0;

    // Only fire if SDL actually dropped the FullScreenPrimary bit.
    if ((newMask & kDesiredBits) == kDesiredBits)
        return;

    char buf[200];
    int n = snprintf(buf, sizeof buf,
        "==> [cocoa-guard] SDL stomped collectionBehavior=0x%lx (was=0x%lx, re-asserting +Primary)\n",
        (unsigned long)newMask, (unsigned long)oldMask);
    if (n > 0)
        ::write(STDERR_FILENO, buf, (size_t)n);

    self.reassertInFlight = YES;
    if (NSWindow *w = self.guardedWindow)
        w.collectionBehavior = newMask | kDesiredBits;
    self.reassertInFlight = NO;
}

- (void)dealloc
{
    [self detach];
}

@end

static OpenXRayFullscreenGuard *gGuard = nil;

extern "C" void OpenXRay_InstallCocoaFullscreenGuard(void *nsWindow)
{
    @autoreleasepool
    {
        if (!gGuard)
            gGuard = [[OpenXRayFullscreenGuard alloc] init];

        NSWindow *win = (__bridge NSWindow *)nsWindow;
        [gGuard attachTo:win];
    }
}

extern "C" void OpenXRay_ForceFullscreenPrimary(void)
{
    if (!gGuard)
        return;
    @autoreleasepool
    {
        [gGuard forceAssert];
    }
}

#endif // XR_PLATFORM_APPLE
