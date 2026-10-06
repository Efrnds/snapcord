#include "voice/UdpSocket.h"

#include "voice/Rtp.h"

#include <array>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
namespace {
struct WinsockInit
{
    WinsockInit()
    {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WinsockInit() { WSACleanup(); }
};
constexpr uintptr_t InvalidHandle = static_cast<uintptr_t>(INVALID_SOCKET);
void closeHandle(uintptr_t handle)
{
    closesocket(static_cast<SOCKET>(handle));
}
} // namespace
#else
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
namespace {
constexpr int InvalidHandle = -1;
void closeHandle(int handle)
{
    ::close(handle);
}
} // namespace
#endif

UdpSocket::UdpSocket()
    : m_socket(InvalidHandle)
{
#ifdef _WIN32
    static WinsockInit winsock;
#endif
}

UdpSocket::~UdpSocket()
{
    close();
}

bool UdpSocket::connectTo(const std::string& host, uint16_t port)
{
    close();

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0 || !result)
        return false;

    const auto handle = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (handle == static_cast<decltype(handle)>(InvalidHandle)) {
        freeaddrinfo(result);
        return false;
    }
    // connect() on UDP only fixes the peer address, so plain send()/recv() can be used afterwards.
    const bool connected = ::connect(handle, result->ai_addr, static_cast<int>(result->ai_addrlen)) == 0;
    freeaddrinfo(result);
    if (!connected) {
        closeHandle(handle);
        return false;
    }
    m_socket = static_cast<Handle>(handle);
    return true;
}

void UdpSocket::close()
{
    if (m_socket != InvalidHandle) {
        closeHandle(m_socket);
        m_socket = InvalidHandle;
    }
}

bool UdpSocket::isOpen() const
{
    return m_socket != InvalidHandle;
}

bool UdpSocket::send(const uint8_t* data, size_t size)
{
    if (!isOpen())
        return false;
#ifdef _WIN32
    return ::send(static_cast<SOCKET>(m_socket), reinterpret_cast<const char*>(data), static_cast<int>(size), 0)
        == static_cast<int>(size);
#else
    return ::send(m_socket, data, size, 0) == static_cast<ssize_t>(size);
#endif
}

int UdpSocket::receive(uint8_t* buffer, size_t capacity, int timeoutMs)
{
    if (!isOpen())
        return -1;
#ifdef _WIN32
    WSAPOLLFD descriptor{};
    descriptor.fd = static_cast<SOCKET>(m_socket);
    descriptor.events = POLLRDNORM;
    const int ready = WSAPoll(&descriptor, 1, timeoutMs);
    if (ready <= 0)
        return ready == 0 ? 0 : -1;
    const int size = ::recv(static_cast<SOCKET>(m_socket), reinterpret_cast<char*>(buffer), static_cast<int>(capacity), 0);
    // WSAECONNRESET is reported for ICMP "port unreachable" on UDP and is not fatal.
    if (size < 0 && WSAGetLastError() == WSAECONNRESET)
        return 0;
    return size;
#else
    pollfd descriptor{};
    descriptor.fd = m_socket;
    descriptor.events = POLLIN;
    const int ready = ::poll(&descriptor, 1, timeoutMs);
    if (ready <= 0)
        return ready == 0 ? 0 : -1;
    const ssize_t size = ::recv(m_socket, buffer, capacity, 0);
    return static_cast<int>(size);
#endif
}

std::optional<UdpSocket::Endpoint> UdpSocket::discoverPublicEndpoint(uint32_t ssrc, int timeoutMs)
{
    // Request: type 0x1, length 70, SSRC, 64-byte address, 2-byte port (all big endian).
    std::array<uint8_t, 74> request{};
    Rtp::writeU16(request.data(), 0x1);
    Rtp::writeU16(request.data() + 2, 70);
    Rtp::writeU32(request.data() + 4, ssrc);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    // UDP can drop the request, so resend it a few times until the deadline.
    for (int attempt = 0; attempt < 5 && std::chrono::steady_clock::now() < deadline; ++attempt) {
        if (!send(request.data(), request.size()))
            return std::nullopt;
        std::array<uint8_t, 128> response{};
        const auto waitUntil = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(1000));
        while (std::chrono::steady_clock::now() < waitUntil) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                waitUntil - std::chrono::steady_clock::now());
            const int size = receive(response.data(), response.size(), static_cast<int>(remaining.count()));
            if (size < 0)
                return std::nullopt;
            if (size >= 74 && Rtp::readU16(response.data()) == 0x2) {
                Endpoint endpoint;
                const char* address = reinterpret_cast<const char*>(response.data() + 8);
                endpoint.address = std::string(address, strnlen(address, 64));
                endpoint.port = Rtp::readU16(response.data() + 72);
                return endpoint;
            }
        }
    }
    return std::nullopt;
}
