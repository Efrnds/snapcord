#include "core/GameDetector.h"

#include "core/ClientProperties.h"
#include "core/Log.h"
#include "platform/ProcessList.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

namespace {

constexpr int ScanIntervalMs = 15 * 1000;
constexpr int RetryDownloadMs = 30 * 60 * 1000;
constexpr qint64 ListLifetimeSecs = 3 * 24 * 60 * 60;
constexpr auto ListUrl = "https://discord.com/api/v9/applications/detectable";

QString cachePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/detectable.tsv");
}

// Executables listed for these systems can run here (Windows games run under Proton/Wine on Linux).
bool runsHere(QStringView os)
{
#if defined(Q_OS_WIN)
    return os == u"win32";
#elif defined(Q_OS_MACOS)
    return os == u"darwin";
#else
    return os == u"linux" || os == u"win32";
#endif
}

QString clean(QString text)
{
    return text.replace(u'\t', u' ').replace(u'\n', u' ').replace(u'\r', u' ');
}

// Runs `work` on a thread of its own and hands the result to `done` on the receiver's thread
// (nothing happens if the receiver is gone by then).
template <typename Work, typename Done>
void runInBackground(QObject* receiver, Work work, Done done)
{
    auto result = std::make_shared<std::shared_ptr<GameDetector::Database>>();
    QThread* thread = QThread::create([work = std::move(work), result] { *result = work(); });
    QObject::connect(thread, &QThread::finished, receiver, [result, done = std::move(done)] { done(*result); });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start(QThread::LowPriority);
}

} // namespace

GameDetector::GameDetector(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(ScanIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &GameDetector::scan);
}

void GameDetector::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    if (enabled) {
        loadDatabase();
        return;
    }
    m_timer.stop();
    m_checked.clear();
    m_database.reset(); // a few MB that are only useful while detecting
    setGame(nullptr);
}

void GameDetector::loadDatabase()
{
    if (m_loading)
        return;
    const QFileInfo cache(cachePath());
    if (!cache.exists()) {
        download();
        return;
    }
    const bool stale = cache.lastModified().secsTo(QDateTime::currentDateTime()) > ListLifetimeSecs;
    m_loading = true;
    runInBackground(
        this,
        [path = cache.filePath()] {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? parseCompactList(file.readAll()) : nullptr;
        },
        [this, stale](std::shared_ptr<Database> database) {
            m_loading = false;
            if (!m_enabled)
                return;
            if (database)
                useDatabase(std::move(database));
            if (stale || !m_database)
                download();
        });
}

void GameDetector::download()
{
    if (m_loading)
        return;
    m_loading = true;
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(QLatin1String(ListUrl))};
    request.setHeader(QNetworkRequest::UserAgentHeader, ClientProperties::userAgent());
    request.setRawHeader("X-Super-Properties", ClientProperties::superPropertiesHeader());
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_loading = false;
            qCWarning(lcGateway) << "games: could not download the detectable list:" << reply->errorString();
            // The cached list, if any, keeps working meanwhile.
            QTimer::singleShot(RetryDownloadMs, this, [this] {
                if (m_enabled)
                    download();
            });
            return;
        }
        runInBackground(
            this,
            [json = reply->readAll()] {
                const QByteArray list = compactList(json);
                if (list.isEmpty())
                    return std::shared_ptr<Database>();
                QDir().mkpath(QFileInfo(cachePath()).path());
                QSaveFile file(cachePath());
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(list);
                    file.commit();
                }
                return parseCompactList(list);
            },
            [this](std::shared_ptr<Database> database) {
                m_loading = false;
                if (!database) {
                    qCWarning(lcGateway) << "games: the detectable list could not be read";
                    return;
                }
                qCInfo(lcGateway) << "games: detectable list updated," << database->fileNames.size() << "executables";
                if (m_enabled)
                    useDatabase(std::move(database));
            });
    });
}

void GameDetector::useDatabase(std::shared_ptr<Database> database)
{
    // The cached matches point into the old list.
    m_checked.clear();
    m_database = std::move(database);
    m_timer.start();
    scan();
}

QByteArray GameDetector::compactList(const QByteArray& json)
{
    const QJsonArray applications = QJsonDocument::fromJson(json).array();
    QByteArray list;
    for (const QJsonValue& value : applications) {
        const QJsonObject application = value.toObject();
        const QString id = application.value(u"id").toString();
        const QString name = clean(application.value(u"name").toString());
        if (id.isEmpty() || name.isEmpty())
            continue;
        for (const QJsonValue& executableValue : application.value(u"executables").toArray()) {
            const QJsonObject executable = executableValue.toObject();
            // Launchers are not the game itself.
            if (executable.value(u"is_launcher").toBool() || !runsHere(executable.value(u"os").toString()))
                continue;
            const QString path = clean(executable.value(u"name").toString().toLower());
            if (path.isEmpty())
                continue;
            const QString arguments = clean(executable.value(u"arguments").toString().toLower());
            list += QStringList{path, arguments, id, name}.join(u'\t').toUtf8() + '\n';
        }
    }
    return list;
}

std::shared_ptr<GameDetector::Database> GameDetector::parseCompactList(const QByteArray& list)
{
    auto database = std::make_shared<Database>();
    for (const QByteArray& line : list.split('\n')) {
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() != 4)
            continue;
        QString path = QString::fromUtf8(fields[0]);
        const Game game{QString::fromUtf8(fields[2]), QString::fromUtf8(fields[3]), QString::fromUtf8(fields[1])};
        // ">name" means the file name must match exactly, together with the arguments.
        const bool exact = path.startsWith(u'>');
        if (exact)
            path.remove(0, 1);
        const QString fileName = path.mid(path.lastIndexOf(u'/') + 1);
        if (fileName.isEmpty())
            continue;
        database->fileNames.insert(fileName);
        if (exact)
            database->byArguments.insert(fileName, game);
        else if (!database->byPath.contains(path))
            database->byPath.insert(path, game);
    }
    if (database->fileNames.isEmpty())
        return nullptr;
    return database;
}

void GameDetector::scan()
{
    if (!m_database)
        return;
    QHash<qint64, const Game*> checked;
    const Game* found = nullptr;
    const Game* current = nullptr;
    for (const ProcessList::Process& process : ProcessList::list()) {
        if (!m_database->fileNames.contains(process.name))
            continue;
        const auto it = m_checked.constFind(process.pid);
        const Game* game = it != m_checked.cend() ? it.value() : match(process.pid, process.name);
        checked.insert(process.pid, game);
        if (!game)
            continue;
        if (!found)
            found = game;
        if (game->applicationId == m_currentId)
            current = game;
    }
    m_checked = std::move(checked);
    // Keep showing the same game while it runs, even if another one was opened later.
    setGame(current ? current : found);
}

const GameDetector::Game* GameDetector::match(qint64 pid, const QString& name) const
{
    const auto [first, last] = m_database->byArguments.equal_range(name);
    if (first != last) {
        const QString commandLine = ProcessList::commandLine(pid);
        for (auto it = first; it != last; ++it) {
            if (it->arguments.isEmpty() || commandLine.contains(it->arguments))
                return &*it;
        }
    }
    const QString path = ProcessList::path(pid);
    return findByPath(*m_database, path.isEmpty() ? name : path);
}

const GameDetector::Game* GameDetector::findByPath(const Database& database, const QString& path)
{
    // Entries can include the end of the path ("_retail_/wow.exe"): try every ending of the process path.
    qsizetype end = path.size();
    while (end > 0) {
        const qsizetype slash = path.lastIndexOf(u'/', end - 1);
        const auto it = database.byPath.constFind(path.mid(slash + 1));
        if (it != database.byPath.cend())
            return &*it;
        if (slash <= 0)
            break;
        end = slash;
    }
    return nullptr;
}

void GameDetector::setGame(const Game* game)
{
    const QString id = game ? game->applicationId : QString();
    if (id == m_currentId)
        return;
    m_currentId = id;
    if (!game) {
        qCInfo(lcGateway) << "games: no game running";
        emit activityChanged({});
        return;
    }
    qCInfo(lcGateway) << "games: playing" << game->name;
    emit activityChanged(QJsonObject{
        {QStringLiteral("type"), 0},
        {QStringLiteral("name"), game->name},
        {QStringLiteral("application_id"), game->applicationId},
        {QStringLiteral("timestamps"), QJsonObject{{QStringLiteral("start"), QDateTime::currentMSecsSinceEpoch()}}},
    });
}
