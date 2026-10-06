#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// Blocking UDP socket bound to the voice server. Uses native sockets so that the audio thread can send
// and a dedicated thread can receive without going through the Qt event loop.
class UdpSocket
{
public:
    UdpSocket();
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool connectTo(const std::string& host, uint16_t port);
    void close();
    bool isOpen() const;

    bool send(const uint8_t* data, size_t size);
    // Waits up to timeoutMs for a packet. Returns its size, 0 on timeout, or -1 on error.
    int receive(uint8_t* buffer, size_t capacity, int timeoutMs);

    struct Endpoint
    {
        std::string address;
        uint16_t port = 0;
    };
    // Asks the voice server for our public address and port (needed to receive audio behind NAT).
    std::optional<Endpoint> discoverPublicEndpoint(uint32_t ssrc, int timeoutMs);

private:
#ifdef _WIN32
    using Handle = uintptr_t;
#else
    using Handle = int;
#endif
    Handle m_socket;
};
