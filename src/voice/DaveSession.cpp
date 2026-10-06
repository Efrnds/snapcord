#include "voice/DaveSession.h"

#include "core/Log.h"

#include <dave/dave_interfaces.h>
#include <dave/logger.h>

#include <QDebug>
#include <QtEndian>

namespace dave = discord::dave;

namespace {

constexpr uint64_t NewGroupEpoch = 1;

std::vector<uint8_t> toVector(const QByteArray& data)
{
    return {reinterpret_cast<const uint8_t*>(data.constData()),
            reinterpret_cast<const uint8_t*>(data.constData()) + data.size()};
}

QByteArray toByteArray(const std::vector<uint8_t>& data)
{
    return QByteArray(reinterpret_cast<const char*>(data.data()), static_cast<qsizetype>(data.size()));
}

void logSink(dave::LoggingSeverity severity, const char* file, int line, const std::string& message)
{
    // Only errors: libdave warns on every unencrypted frame in calls without end-to-end encryption.
    if (severity >= dave::LS_ERROR && severity != dave::LS_NONE)
        qWarning().noquote() << "[dave]" << file << line << QString::fromStdString(message);
}

} // namespace

DaveSession::DaveSession(const QString& selfUserId, const QString& channelId, SendJson sendJson, SendBinary sendBinary)
    : m_selfUserId(selfUserId)
    , m_groupId(channelId.toULongLong())
    , m_sendJson(std::move(sendJson))
    , m_sendBinary(std::move(sendBinary))
{
    static const bool logSinkInstalled = [] {
        dave::SetLogSink(logSink);
        return true;
    }();
    Q_UNUSED(logSinkInstalled);

    m_session = dave::mls::CreateSession(nullptr, std::string(), [](const std::string& source, const std::string& reason) {
        qWarning().noquote() << "[dave] MLS failure:" << QString::fromStdString(source) << QString::fromStdString(reason);
    });
    m_encryptor = dave::CreateEncryptor();
}

DaveSession::~DaveSession() = default;

int DaveSession::maxSupportedProtocolVersion()
{
    return dave::MaxSupportedProtocolVersion();
}

void DaveSession::addUser(const QString& userId)
{
    if (userId == m_selfUserId)
        return;
    m_users.insert(userId);
    {
        std::lock_guard lock(m_mediaMutex);
        if (!m_decryptors.count(userId))
            m_decryptors[userId] = dave::CreateDecryptor();
    }
    setupKeyRatchetForUser(userId, m_latestPreparedVersion);
}

void DaveSession::removeUser(const QString& userId)
{
    m_users.remove(userId);
    std::lock_guard lock(m_mediaMutex);
    m_decryptors.erase(userId);
}

void DaveSession::start(int protocolVersion, uint32_t selfSsrc)
{
    {
        std::lock_guard lock(m_mediaMutex);
        m_encryptor->AssignSsrcToCodec(selfSsrc, dave::Codec::Opus);
    }
    handleProtocolInit(protocolVersion);
}

void DaveSession::handleJson(int opcode, const QJsonObject& data)
{
    const int transitionId = data.value(u"transition_id").toInt();
    const int protocolVersion = data.value(u"protocol_version").toInt();
    switch (opcode) {
    case PrepareTransition:
        prepareRatchets(transitionId, protocolVersion);
        sendReadyForTransition(transitionId);
        break;
    case ExecuteTransition:
        executeTransition(transitionId);
        break;
    case PrepareEpoch: {
        const QJsonValue epochValue = data.value(u"epoch");
        const uint64_t epoch = epochValue.isString() ? epochValue.toString().toULongLong()
                                                     : static_cast<uint64_t>(epochValue.toInteger());
        prepareEpoch(epoch, protocolVersion);
        if (epoch == NewGroupEpoch)
            sendKeyPackage();
        break;
    }
    default:
        break;
    }
}

void DaveSession::handleBinary(int opcode, const QByteArray& payload)
{
    switch (opcode) {
    case ExternalSenderPackage:
        m_session->SetExternalSender(toVector(payload));
        break;
    case Proposals: {
        const auto commitWelcome = m_session->ProcessProposals(toVector(payload), recognizedUserIds());
        if (commitWelcome)
            m_sendBinary(CommitWelcome, toByteArray(*commitWelcome));
        break;
    }
    case AnnounceCommitTransition: {
        if (payload.size() < 2)
            return;
        const int transitionId = qFromBigEndian<quint16>(payload.constData());
        const auto result = m_session->ProcessCommit(toVector(payload.mid(2)));
        if (std::holds_alternative<dave::ignored_t>(result))
            return;
        qCInfo(lcVoice) << "DAVE commit for transition" << transitionId
                        << (std::holds_alternative<dave::RosterMap>(result) ? "applied" : "rejected");
        if (std::holds_alternative<dave::RosterMap>(result)) {
            prepareRatchets(transitionId, m_session->GetProtocolVersion());
            sendReadyForTransition(transitionId);
        } else {
            flagInvalidCommitWelcome(transitionId);
            handleProtocolInit(m_session->GetProtocolVersion());
        }
        break;
    }
    case Welcome: {
        if (payload.size() < 2)
            return;
        const int transitionId = qFromBigEndian<quint16>(payload.constData());
        const auto roster = m_session->ProcessWelcome(toVector(payload.mid(2)), recognizedUserIds());
        qCInfo(lcVoice) << "DAVE welcome for transition" << transitionId << (roster ? "accepted" : "rejected");
        if (roster) {
            prepareRatchets(transitionId, m_session->GetProtocolVersion());
            sendReadyForTransition(transitionId);
        } else {
            flagInvalidCommitWelcome(transitionId);
            sendKeyPackage();
        }
        break;
    }
    default:
        break;
    }
}

bool DaveSession::encrypt(uint32_t ssrc, const uint8_t* frame, size_t size, std::vector<uint8_t>& out)
{
    std::lock_guard lock(m_mediaMutex);
    out.resize(m_encryptor->GetMaxCiphertextByteSize(dave::MediaType::Audio, size));
    size_t written = 0;
    const auto result = m_encryptor->Encrypt(dave::MediaType::Audio, ssrc, dave::MakeArrayView(frame, size),
                                             dave::MakeArrayView(out.data(), out.size()), &written);
    if (result != dave::IEncryptor::Success)
        return false;
    out.resize(written);
    return true;
}

bool DaveSession::decrypt(const QString& userId, const uint8_t* frame, size_t size, std::vector<uint8_t>& out)
{
    std::lock_guard lock(m_mediaMutex);
    auto it = m_decryptors.find(userId);
    if (it == m_decryptors.end())
        return false;
    out.resize(it->second->GetMaxPlaintextByteSize(dave::MediaType::Audio, size));
    size_t written = 0;
    const auto result = it->second->Decrypt(dave::MediaType::Audio, dave::MakeArrayView(frame, size),
                                            dave::MakeArrayView(out.data(), out.size()), &written);
    if (result != dave::IDecryptor::Success)
        return false;
    out.resize(written);
    return true;
}

void DaveSession::handleProtocolInit(int protocolVersion)
{
    if (protocolVersion > 0) {
        prepareEpoch(NewGroupEpoch, protocolVersion);
        sendKeyPackage();
    } else {
        prepareRatchets(dave::kInitTransitionId, protocolVersion);
        executeTransition(dave::kInitTransitionId);
    }
}

void DaveSession::prepareEpoch(uint64_t epoch, int protocolVersion)
{
    if (epoch != NewGroupEpoch)
        return;
    std::shared_ptr<::mlspp::SignaturePrivateKey> transientKey;
    m_session->Init(static_cast<dave::ProtocolVersion>(protocolVersion), m_groupId, m_selfUserId.toStdString(),
                    transientKey);
}

void DaveSession::prepareRatchets(int transitionId, int protocolVersion)
{
    for (const QString& userId : std::as_const(m_users))
        setupKeyRatchetForUser(userId, protocolVersion);

    if (transitionId == dave::kInitTransitionId)
        setupKeyRatchetForUser(m_selfUserId, protocolVersion);
    else
        m_pendingTransitions[transitionId] = protocolVersion;

    m_latestPreparedVersion = protocolVersion;
}

void DaveSession::executeTransition(int transitionId)
{
    const auto it = m_pendingTransitions.find(transitionId);
    if (it == m_pendingTransitions.end())
        return;
    const int protocolVersion = it->second;
    m_pendingTransitions.erase(it);
    qCInfo(lcVoice) << "DAVE transition" << transitionId << "executed, protocol version" << protocolVersion;

    if (protocolVersion == dave::kDisabledVersion)
        m_session->Reset();
    setupKeyRatchetForUser(m_selfUserId, protocolVersion);
}

void DaveSession::setupKeyRatchetForUser(const QString& userId, int protocolVersion)
{
    const bool disabled = protocolVersion == dave::kDisabledVersion;
    std::unique_ptr<dave::IKeyRatchet> ratchet = disabled ? nullptr : m_session->GetKeyRatchet(userId.toStdString());

    std::lock_guard lock(m_mediaMutex);
    if (userId == m_selfUserId) {
        m_encryptor->SetPassthroughMode(disabled);
        m_encryptor->SetKeyRatchet(std::move(ratchet));
        return;
    }
    auto it = m_decryptors.find(userId);
    if (it == m_decryptors.end())
        return;
    if (disabled)
        it->second->TransitionToPassthroughMode(true);
    else
        it->second->TransitionToKeyRatchet(std::move(ratchet));
}

void DaveSession::sendKeyPackage()
{
    m_sendBinary(KeyPackage, toByteArray(m_session->GetMarshalledKeyPackage()));
}

void DaveSession::sendReadyForTransition(int transitionId)
{
    if (transitionId != dave::kInitTransitionId)
        m_sendJson(TransitionReady, QJsonObject{{QStringLiteral("transition_id"), transitionId}});
}

void DaveSession::flagInvalidCommitWelcome(int transitionId)
{
    m_sendJson(InvalidCommitWelcome, QJsonObject{{QStringLiteral("transition_id"), transitionId}});
}

std::set<std::string> DaveSession::recognizedUserIds() const
{
    std::set<std::string> ids;
    for (const QString& userId : m_users)
        ids.insert(userId.toStdString());
    ids.insert(m_selfUserId.toStdString());
    return ids;
}
