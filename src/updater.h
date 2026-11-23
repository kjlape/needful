#pragma once

// Updates for the macOS app, with Sparkle. Only built when bin/build-macos
// passes CONFIG+=sparkle, which it does when macos/sparkle-public-key exists.
//
// Starts Sparkle, which checks the feed in Info.plist (SUFeedURL) on its own
// schedule and installs only updates signed with the key in SUPublicEDKey,
// and adds Check for Updates… to the app menu. Call it once the window is up.
void startUpdater();
