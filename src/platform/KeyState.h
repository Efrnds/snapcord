#pragma once

#include <QString>

// Global key state for push-to-talk: works while Snapcord is in the background, without stealing the key
// from other applications. Keys are identified by native codes (virtual-key codes on Windows), which also
// cover the extra mouse buttons commonly used for push-to-talk.
namespace KeyState {

bool isSupported();
bool isDown(int nativeKey);
QString name(int nativeKey);

// Native codes for mouse buttons, which Qt reports as mouse events rather than key events.
int mouseButtonKey(int qtMouseButton);

} // namespace KeyState
