#include "voice/TransportCipher.h"

#include "voice/Rtp.h"

#include <QStringList>

#include <openssl/evp.h>
#include <sodium.h>

#include <memory>

namespace {

constexpr auto AesGcmName = "aead_aes256_gcm_rtpsize";
constexpr auto XChaChaName = "aead_xchacha20_poly1305_rtpsize";

struct CipherContextDeleter
{
    void operator()(EVP_CIPHER_CTX* context) const { EVP_CIPHER_CTX_free(context); }
};
using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, CipherContextDeleter>;

// The 32-bit counter goes in the first bytes of the full nonce; the rest stays zero.
template<size_t N>
std::array<uint8_t, N> expandNonce(const uint8_t* counter)
{
    std::array<uint8_t, N> nonce{};
    std::copy(counter, counter + 4, nonce.begin());
    return nonce;
}

} // namespace

std::optional<TransportCipher::Mode> TransportCipher::choose(const QStringList& offered)
{
    if (offered.contains(QLatin1String(AesGcmName)) && EVP_aes_256_gcm())
        return Mode::AesGcm;
    if (offered.contains(QLatin1String(XChaChaName)))
        return Mode::XChaCha20;
    return std::nullopt;
}

QString TransportCipher::name(Mode mode)
{
    return QLatin1String(mode == Mode::AesGcm ? AesGcmName : XChaChaName);
}

TransportCipher::TransportCipher(Mode mode, const std::array<uint8_t, 32>& key)
    : m_mode(mode)
    , m_key(key)
{
    static const bool sodiumReady = sodium_init() >= 0;
    Q_UNUSED(sodiumReady);
}

bool TransportCipher::seal(const uint8_t* header, size_t headerSize, const uint8_t* payload, size_t payloadSize,
                           std::vector<uint8_t>& packet)
{
    uint8_t counter[NonceSuffixSize];
    Rtp::writeU32(counter, m_nonce.fetch_add(1, std::memory_order_relaxed));

    packet.resize(headerSize + payloadSize + TagSize + NonceSuffixSize);
    std::copy(header, header + headerSize, packet.begin());
    uint8_t* ciphertext = packet.data() + headerSize;

    if (m_mode == Mode::AesGcm) {
        const auto nonce = expandNonce<12>(counter);
        CipherContext context(EVP_CIPHER_CTX_new());
        int length = 0;
        if (!context || EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, m_key.data(), nonce.data()) != 1
            || EVP_EncryptUpdate(context.get(), nullptr, &length, header, static_cast<int>(headerSize)) != 1
            || EVP_EncryptUpdate(context.get(), ciphertext, &length, payload, static_cast<int>(payloadSize)) != 1
            || EVP_EncryptFinal_ex(context.get(), ciphertext + length, &length) != 1
            || EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, TagSize, ciphertext + payloadSize) != 1) {
            return false;
        }
    } else {
        const auto nonce = expandNonce<crypto_aead_xchacha20poly1305_ietf_NPUBBYTES>(counter);
        unsigned long long length = 0;
        if (crypto_aead_xchacha20poly1305_ietf_encrypt(ciphertext, &length, payload, payloadSize, header, headerSize,
                                                       nullptr, nonce.data(), m_key.data())
            != 0) {
            return false;
        }
    }

    std::copy(counter, counter + NonceSuffixSize, packet.end() - NonceSuffixSize);
    return true;
}

bool TransportCipher::open(const uint8_t* packet, size_t size, size_t clearSize, std::vector<uint8_t>& plaintext) const
{
    if (size < clearSize + TagSize + NonceSuffixSize)
        return false;
    const uint8_t* counter = packet + size - NonceSuffixSize;
    const uint8_t* ciphertext = packet + clearSize;
    const size_t ciphertextSize = size - clearSize - NonceSuffixSize; // includes the tag
    const size_t messageSize = ciphertextSize - TagSize;
    plaintext.resize(messageSize);

    if (m_mode == Mode::AesGcm) {
        const auto nonce = expandNonce<12>(counter);
        CipherContext context(EVP_CIPHER_CTX_new());
        int length = 0;
        auto* tag = const_cast<uint8_t*>(ciphertext + messageSize);
        return context && EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, m_key.data(), nonce.data()) == 1
            && EVP_DecryptUpdate(context.get(), nullptr, &length, packet, static_cast<int>(clearSize)) == 1
            && EVP_DecryptUpdate(context.get(), plaintext.data(), &length, ciphertext, static_cast<int>(messageSize)) == 1
            && EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, TagSize, tag) == 1
            && EVP_DecryptFinal_ex(context.get(), plaintext.data() + length, &length) == 1;
    }

    const auto nonce = expandNonce<crypto_aead_xchacha20poly1305_ietf_NPUBBYTES>(counter);
    unsigned long long length = 0;
    return crypto_aead_xchacha20poly1305_ietf_decrypt(plaintext.data(), &length, nullptr, ciphertext, ciphertextSize,
                                                      packet, clearSize, nonce.data(), m_key.data())
        == 0;
}
