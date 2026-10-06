#include "platform/CredentialStore.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

// macOS Keychain: a generic password item for service "Snapcord", account "token".
namespace {

CFStringRef toCFString(const char* text)
{
    return CFStringCreateWithCString(kCFAllocatorDefault, text, kCFStringEncodingUTF8);
}

// Base query identifying the token item. The caller releases it.
CFMutableDictionaryRef baseQuery()
{
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                             &kCFTypeDictionaryValueCallBacks);
    CFStringRef service = toCFString("Snapcord");
    CFStringRef account = toCFString("token");
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service);
    CFDictionarySetValue(query, kSecAttrAccount, account);
    CFRelease(service);
    CFRelease(account);
    return query;
}

} // namespace

namespace CredentialStore {

bool isPersistent()
{
    return true;
}

QString loadToken()
{
    CFMutableDictionaryRef query = baseQuery();
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);
    if (status != errSecSuccess || !result)
        return {};
    const auto data = static_cast<CFDataRef>(result);
    const QString token = QString::fromUtf8(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                                            static_cast<qsizetype>(CFDataGetLength(data)));
    CFRelease(result);
    return token;
}

bool saveToken(const QString& token)
{
    clearToken();
    const QByteArray bytes = token.toUtf8();
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(bytes.constData()), bytes.size());
    CFMutableDictionaryRef item = baseQuery();
    CFDictionarySetValue(item, kSecValueData, data);
    // Readable only while the Mac is unlocked, and never synced to other devices through iCloud.
    CFDictionarySetValue(item, kSecAttrAccessible, kSecAttrAccessibleWhenUnlockedThisDeviceOnly);
    const OSStatus status = SecItemAdd(item, nullptr);
    CFRelease(item);
    CFRelease(data);
    return status == errSecSuccess;
}

void clearToken()
{
    CFMutableDictionaryRef query = baseQuery();
    SecItemDelete(query);
    CFRelease(query);
}

} // namespace CredentialStore
