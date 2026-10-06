#pragma once

#include <QByteArray>

#include <optional>

typedef struct evp_pkey_st EVP_PKEY;

// 2048-bit RSA key pair used by the QR code login handshake (RSA-OAEP with SHA-256).
class RsaKey
{
public:
    RsaKey();
    ~RsaKey();

    RsaKey(const RsaKey&) = delete;
    RsaKey& operator=(const RsaKey&) = delete;

    bool isValid() const { return m_key != nullptr; }

    // DER-encoded SubjectPublicKeyInfo of the public key.
    QByteArray publicKeySpki() const;

    std::optional<QByteArray> decrypt(const QByteArray& ciphertext) const;

private:
    EVP_PKEY* m_key;
};
