#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class RemoteAuth;
class RestClient;

// Login screen: shows the QR code to scan with the Discord mobile app, or accepts an account token.
class LoginWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LoginWindow(QWidget* parent = nullptr);

signals:
    void loggedIn(const QString& token);

private:
    void showQrCode(const QString& url);
    void showScanned(const QString& username);
    void showError(const QString& message);
    void restart();
    void submitToken();
    void showTokenError(const QString& message);

    RemoteAuth* m_auth;
    QStackedWidget* m_qrStack;
    QLabel* m_qrImage;
    QLabel* m_qrTitle;
    QLabel* m_qrHint;
    QLabel* m_errorLabel;
    QPushButton* m_retryButton;
    QStackedWidget* m_leftStack;
    QLineEdit* m_tokenInput;
    QLabel* m_tokenError;
    QPushButton* m_tokenButton;
    RestClient* m_rest;
};
