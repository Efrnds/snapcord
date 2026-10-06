#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace discord::dave {
class IEncryptor;
class IDecryptor;
namespace mls {
class ISession;
}
} // namespace discord::dave

// DAVE end-to-end encryption for one voice call, built on Discord's libdave. Mirrors the reference
// DaveSessionManager from libdave's samples: signaling methods run on the main thread, while
// encrypt()/decrypt() run on the audio and network threads.
class DaveSession
{
public:
    // Voice gateway opcodes used by the DAVE protocol.
    enum Opcode {
        PrepareTransition = 21,
        ExecuteTransition = 22,
        TransitionReady = 23,
        PrepareEpoch = 24,
        ExternalSenderPackage = 25,
        KeyPackage = 26,
        Proposals = 27,
        CommitWelcome = 28,
        AnnounceCommitTransition = 29,
        Welcome = 30,
        InvalidCommitWelcome = 31,
    };

    using SendJson = std::function<void(int opcode, const QJsonObject& data)>;
    using SendBinary = std::function<void(int opcode, const QByteArray& payload)>;

    DaveSession(const QString& selfUserId, const QString& channelId, SendJson sendJson, SendBinary sendBinary);
    ~DaveSession();

    static int maxSupportedProtocolVersion();

    void addUser(const QString& userId);
    void removeUser(const QString& userId);

    // Called with the protocol version from the voice Session Description.
    void start(int protocolVersion, uint32_t selfSsrc);

    void handleJson(int opcode, const QJsonObject& data);
    void handleBinary(int opcode, const QByteArray& payload);

    // Encrypts an Opus frame for sending. Returns false while no key is available yet.
    bool encrypt(uint32_t ssrc, const uint8_t* frame, size_t size, std::vector<uint8_t>& out);
    // Decrypts an Opus frame received from `userId`.
    bool decrypt(const QString& userId, const uint8_t* frame, size_t size, std::vector<uint8_t>& out);

private:
    void handleProtocolInit(int protocolVersion);
    void prepareEpoch(uint64_t epoch, int protocolVersion);
    void prepareRatchets(int transitionId, int protocolVersion);
    void executeTransition(int transitionId);
    void setupKeyRatchetForUser(const QString& userId, int protocolVersion);
    void sendKeyPackage();
    void sendReadyForTransition(int transitionId);
    void flagInvalidCommitWelcome(int transitionId);
    std::set<std::string> recognizedUserIds() const;

    QString m_selfUserId;
    uint64_t m_groupId;
    SendJson m_sendJson;
    SendBinary m_sendBinary;

    std::unique_ptr<discord::dave::mls::ISession> m_session;
    QSet<QString> m_users;
    std::map<int, int> m_pendingTransitions; // transition ID -> protocol version
    int m_latestPreparedVersion = 0;

    // Guards the encryptor/decryptors, which are used from the audio and network threads.
    std::mutex m_mediaMutex;
    std::unique_ptr<discord::dave::IEncryptor> m_encryptor;
    std::map<QString, std::unique_ptr<discord::dave::IDecryptor>> m_decryptors;
};
