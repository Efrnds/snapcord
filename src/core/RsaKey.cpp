#include "core/RsaKey.h"

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <memory>

namespace {

struct ContextDeleter
{
    void operator()(EVP_PKEY_CTX* context) const { EVP_PKEY_CTX_free(context); }
};

} // namespace

RsaKey::RsaKey()
    : m_key(EVP_RSA_gen(2048))
{
}

RsaKey::~RsaKey()
{
    EVP_PKEY_free(m_key);
}

QByteArray RsaKey::publicKeySpki() const
{
    if (!m_key)
        return {};
    const int length = i2d_PUBKEY(m_key, nullptr);
    if (length <= 0)
        return {};
    QByteArray der(length, Qt::Uninitialized);
    auto* cursor = reinterpret_cast<unsigned char*>(der.data());
    i2d_PUBKEY(m_key, &cursor);
    return der;
}

std::optional<QByteArray> RsaKey::decrypt(const QByteArray& ciphertext) const
{
    if (!m_key)
        return std::nullopt;

    std::unique_ptr<EVP_PKEY_CTX, ContextDeleter> context(EVP_PKEY_CTX_new(m_key, nullptr));
    if (!context || EVP_PKEY_decrypt_init(context.get()) <= 0
        || EVP_PKEY_CTX_set_rsa_padding(context.get(), RSA_PKCS1_OAEP_PADDING) <= 0
        || EVP_PKEY_CTX_set_rsa_oaep_md(context.get(), EVP_sha256()) <= 0
        || EVP_PKEY_CTX_set_rsa_mgf1_md(context.get(), EVP_sha256()) <= 0) {
        return std::nullopt;
    }

    const auto* input = reinterpret_cast<const unsigned char*>(ciphertext.constData());
    size_t length = 0;
    if (EVP_PKEY_decrypt(context.get(), nullptr, &length, input, ciphertext.size()) <= 0)
        return std::nullopt;

    QByteArray plaintext(static_cast<qsizetype>(length), Qt::Uninitialized);
    if (EVP_PKEY_decrypt(context.get(), reinterpret_cast<unsigned char*>(plaintext.data()), &length, input,
                         ciphertext.size())
        <= 0) {
        return std::nullopt;
    }
    plaintext.resize(static_cast<qsizetype>(length));
    return plaintext;
}
