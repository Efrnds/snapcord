#pragma once

#include <QString>
#include <QStringList>

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

// Transport encryption between this client and Discord's voice server ("rtpsize" AEAD modes).
// Packet layout: clear RTP header | ciphertext | 16-byte tag | 4-byte big-endian nonce counter.
class TransportCipher
{
public:
    enum class Mode { AesGcm, XChaCha20 };
    static constexpr size_t TagSize = 16;
    static constexpr size_t NonceSuffixSize = 4;

    // Picks the preferred mode among those offered by the server.
    static std::optional<Mode> choose(const QStringList& offered);
    static QString name(Mode mode);

    TransportCipher(Mode mode, const std::array<uint8_t, 32>& key);

    // Encrypts `payload` and appends tag and nonce. `header` must be `headerSize` clear bytes. Thread-safe.
    bool seal(const uint8_t* header, size_t headerSize, const uint8_t* payload, size_t payloadSize,
              std::vector<uint8_t>& packet);

    // Decrypts a received packet whose first `clearSize` bytes are the clear header. Thread-safe.
    bool open(const uint8_t* packet, size_t size, size_t clearSize, std::vector<uint8_t>& plaintext) const;

private:
    Mode m_mode;
    std::array<uint8_t, 32> m_key;
    std::atomic<uint32_t> m_nonce{0};
};
