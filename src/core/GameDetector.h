#pragma once

#include <QHash>
#include <QJsonObject>
#include <QMultiHash>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <memory>

class QNetworkAccessManager;

// Finds the game the user is playing, like the official client: running processes are matched against
// Discord's list of detectable applications (downloaded once and cached on disk for a few days).
class GameDetector : public QObject
{
    Q_OBJECT

public:
    struct Game
    {
        QString applicationId;
        QString name;
        QString arguments; // must appear in the command line (games that share an executable)
    };
    struct Database
    {
        QHash<QString, Game> byPath;           // "game.exe" or the end of a path, "_retail_/wow.exe"
        QMultiHash<QString, Game> byArguments; // by file name, for entries that also need arguments
        QSet<QString> fileNames;               // every file name above, to skip unrelated processes quickly
    };

    explicit GameDetector(QObject* parent = nullptr);

    void setEnabled(bool enabled);

    // The detectable list as compact "path \t arguments \t id \t name" lines, with only this system's games.
    static QByteArray compactList(const QByteArray& json);
    static std::shared_ptr<Database> parseCompactList(const QByteArray& list);
    // The game whose executable is at `path` (lowercase, '/' separators), or null.
    static const Game* findByPath(const Database& database, const QString& path);

signals:
    // The game being played, as a presence activity; empty when none.
    void activityChanged(const QJsonObject& activity);

private:
    void loadDatabase();
    void download();
    void useDatabase(std::shared_ptr<Database> database);
    void scan();
    const Game* match(qint64 pid, const QString& name) const;
    void setGame(const Game* game);

    QNetworkAccessManager* m_network = nullptr;
    std::shared_ptr<const Database> m_database;
    QTimer m_timer;
    bool m_enabled = false;
    bool m_loading = false;
    // Process ID -> its game (null = not a game), so each process is only looked at once.
    QHash<qint64, const Game*> m_checked;
    QString m_currentId;
};
