#pragma once

#include "core/Models.h"

#include <QDialog>

#include <optional>

class ColorSwatch;
class ImageCache;
class ProfileCard;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class Session;

// "Edit Profile": display name, pronouns, avatar, banner color and "About Me", with a live preview.
class ProfileEditDialog : public QDialog
{
    Q_OBJECT

public:
    ProfileEditDialog(Session* session, ImageCache* images, QWidget* parent = nullptr);

private:
    void loadProfile(const UserProfile& profile);
    void updatePreview();
    void chooseAvatar();
    void save();
    void showError(const QString& text);

    Session* m_session;
    ProfileCard* m_preview;
    QLineEdit* m_displayName;
    QLineEdit* m_pronouns;
    QPlainTextEdit* m_bio;
    QLabel* m_bioCounter;
    ColorSwatch* m_bannerColor;
    QPushButton* m_removeAvatar;
    QLabel* m_error;
    QPushButton* m_saveButton;

    UserProfile m_original;
    int m_accentColor = -1;
    QImage m_newAvatar;        // picked but not saved yet
    bool m_avatarRemoved = false;
};

// "Set Custom Status": an emoji, a text and when it clears itself.
class CustomStatusDialog : public QDialog
{
    Q_OBJECT

public:
    CustomStatusDialog(Session* session, ImageCache* images, QWidget* parent = nullptr);

private:
    void save();
    void updateEmojiButton();

    Session* m_session;
    ImageCache* m_images;
    QPushButton* m_emoji;
    QLineEdit* m_text;
    QComboBox* m_clearAfter;
    QString m_emojiName;
};
