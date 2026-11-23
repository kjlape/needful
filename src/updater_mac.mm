// Sparkle for the macOS app. See updater.h and docs/building-for-macos.md.

#import <AppKit/AppKit.h>
#import <Sparkle/Sparkle.h>

#include <QCoreApplication>

#include "updater.h"

static SPUStandardUpdaterController *controller;

void startUpdater() {
    if (controller)
        return;
    controller = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES
                                                               updaterDelegate:nil
                                                            userDriverDelegate:nil];

    // Qt builds the app menu (About, Hide, Quit) itself; the item goes right
    // after its first entry, where Mac apps keep it.
    NSMenu *appMenu = NSApp.mainMenu.itemArray.firstObject.submenu;
    if (!appMenu)
        return;
    const QString title = QCoreApplication::translate("Updater", "Check for Updates…");
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title.toNSString()
                                                  action:@selector(checkForUpdates:)
                                           keyEquivalent:@""];
    item.target = controller;
    [appMenu insertItem:item atIndex:MIN(1, appMenu.numberOfItems)];
}
