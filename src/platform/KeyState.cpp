#include "platform/KeyState.h"

#include <QCoreApplication>
#include <Qt>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace KeyState {

bool isSupported()
{
    return true;
}

bool isDown(int nativeKey)
{
    return nativeKey > 0 && (GetAsyncKeyState(nativeKey) & 0x8000) != 0;
}

QString name(int nativeKey)
{
    switch (nativeKey) {
    case VK_LBUTTON:
        return QCoreApplication::translate("KeyState", "Left Mouse Button");
    case VK_RBUTTON:
        return QCoreApplication::translate("KeyState", "Right Mouse Button");
    case VK_MBUTTON:
        return QCoreApplication::translate("KeyState", "Middle Mouse Button");
    case VK_XBUTTON1:
        return QCoreApplication::translate("KeyState", "Mouse Button 4");
    case VK_XBUTTON2:
        return QCoreApplication::translate("KeyState", "Mouse Button 5");
    default:
        break;
    }
    const UINT scanCode = MapVirtualKeyW(static_cast<UINT>(nativeKey), MAPVK_VK_TO_VSC);
    LONG lParam = static_cast<LONG>(scanCode << 16);
    // Navigation keys share scan codes with the numeric keypad; the extended bit tells them apart.
    switch (nativeKey) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_RCONTROL: case VK_RMENU:
        lParam |= 1 << 24;
        break;
    default:
        break;
    }
    wchar_t buffer[64] = {};
    if (GetKeyNameTextW(lParam, buffer, 64) > 0)
        return QString::fromWCharArray(buffer);
    return QCoreApplication::translate("KeyState", "Key %1").arg(nativeKey);
}

int mouseButtonKey(int qtMouseButton)
{
    switch (qtMouseButton) {
    case Qt::MiddleButton:
        return VK_MBUTTON;
    case Qt::BackButton:
        return VK_XBUTTON1;
    case Qt::ForwardButton:
        return VK_XBUTTON2;
    default:
        return 0;
    }
}

} // namespace KeyState

#else

// Global key polling for macOS and Linux arrives with the multi-platform release (Phase 4).
namespace KeyState {

bool isSupported()
{
    return false;
}

bool isDown(int)
{
    return false;
}

QString name(int nativeKey)
{
    return QCoreApplication::translate("KeyState", "Key %1").arg(nativeKey);
}

int mouseButtonKey(int)
{
    return 0;
}

} // namespace KeyState

#endif
