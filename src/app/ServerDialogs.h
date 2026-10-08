#pragma once

#include "core/GuildFolders.h"
#include "core/Session.h"

#include <QDialog>

class ColorSwatch;
class ImageCache;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSlider;
class QStackedWidget;

// "Join a Server": an invite link typed by the user, or one clicked in a message (shown as a preview with an
// "Accept Invite" button).
class JoinServerDialog : public QDialog
{
    Q_OBJECT

public:
    // With a code, the dialog opens on that invite's preview instead of the link field.
    JoinServerDialog(Session* session, ImageCache* images, const QString& code = {}, QWidget* parent = nullptr);

signals:
    // Joined (or already in): the server, or the group conversation when `guildId` is empty.
    void joined(const QString& guildId, const QString& channelId);

private:
    void joinTyped();
    void showPreview(const InviteInfo& invite);
    void acceptInvite(const InviteInfo& invite);
    void showError(const QString& error);

    Session* m_session;
    ImageCache* m_images;
    QStackedWidget* m_pages;
    QLineEdit* m_link;
    QLabel* m_error;
    QPushButton* m_join;
    QLabel* m_previewIcon;
    QLabel* m_previewInviter;
    QLabel* m_previewName;
    QLabel* m_previewCounts;
    QPushButton* m_acceptButton;
    InviteInfo m_invite;
};

// "Invite friends to <server>": sends a new invite link to direct message conversations, or lets the user copy it.
class InviteDialog : public QDialog
{
    Q_OBJECT

public:
    InviteDialog(Session* session, const QString& guildId, const QString& channelId, QWidget* parent = nullptr);

private:
    void fillConversations(const QString& filter);

    Session* m_session;
    QLineEdit* m_search;
    QListWidget* m_conversations;
    QLineEdit* m_link;
    QPushButton* m_copy;
    QLabel* m_hint;
    QString m_code;
    QStringList m_sentTo;
};

// "Folder Settings": the name and color of a server folder.
class FolderSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    // `serverNames` is shown as the placeholder of an unnamed folder.
    FolderSettingsDialog(const GuildFolder& folder, const QString& serverNames, QWidget* parent = nullptr);

    GuildFolder folder() const { return m_folder; }

private:
    void selectColor(int color);

    GuildFolder m_folder;
    QLineEdit* m_name;
    QList<ColorSwatch*> m_swatches;
    ColorSwatch* m_custom;
};

// "Create Channel": a text or voice channel, at the top of the server or inside a category.
class CreateChannelDialog : public QDialog
{
    Q_OBJECT

public:
    CreateChannelDialog(Session* session, const QString& guildId, const QString& categoryId, QWidget* parent = nullptr);

signals:
    void created(const QString& channelId);

private:
    void create();

    Session* m_session;
    QString m_guildId;
    QString m_categoryId;
    QRadioButton* m_text;
    QRadioButton* m_voice;
    QLineEdit* m_name;
    QLabel* m_error;
    QPushButton* m_create;
};

// "Edit Channel": name and the settings of its kind (topic, slowmode and age restriction for text channels;
// bitrate and user limit for voice channels). Categories only have a name.
class ChannelSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    ChannelSettingsDialog(Session* session, const QString& guildId, const QString& channelId, QWidget* parent = nullptr);

private:
    void save();

    Session* m_session;
    QString m_guildId;
    QString m_channelId;
    QLineEdit* m_name;
    QPlainTextEdit* m_topic = nullptr;
    QComboBox* m_slowmode = nullptr;
    QCheckBox* m_nsfw = nullptr;
    QSlider* m_bitrate = nullptr;
    QSlider* m_userLimit = nullptr;
    QLabel* m_error;
    QPushButton* m_save;
};

// Asks before deleting a channel or category, then deletes it. `done` gets the result; without it, errors are
// shown in a message box. Returns false when the user cancelled.
bool confirmDeleteChannel(Session* session, const QString& guildId, const QString& channelId, QWidget* parent,
                          Session::ResultCallback done = {});
